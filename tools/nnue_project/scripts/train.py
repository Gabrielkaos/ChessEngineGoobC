"""
Train the NNUE model on the binary cache produced by prepare_data.py.

Example:
    python train.py --train ../data/train1.bin --val ../data/val1.bin \
        --epochs 20 --batch-size 8192 --lr 1e-3 --resume

A file larger than RAM: pre-shuffle it once (clear_cache.py --shuffle), then
    python train.py --train ../data/clean/train.bin --val ../data/clean/val.bin \
        --shuffle-batches --resume

Fine-tune an existing network on new data (weights only, fresh optimizer):
    python train.py --train ../data/train2.bin --val ../data/val2.bin \
        --init ../checkpoints/nnue.pt --lr 3e-4 --epochs 8 \
        --out-best ../checkpoints/nnue_ft.pt --out-last ../checkpoints/last_ft.pt

Round 2 data experiments (fixed sample budget, per-step cosine, last checkpoint,
extra validation sets and a learning-curve CSV):
    python train.py --train mix.bin --val ../data/val.bin --val-extra val_gen2.bin \
        --init ../checkpoints/exp_datagen_d1_50m/nnue.pt --total-samples 320000000 \
        --lr-schedule step-cosine --select-by last --val-every 2000 ...
"""

import argparse
import functools
import os
import shutil
import time

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Sampler

from model import NNUE
from nnue_dataset import NNUEDataset, nnue_collate


def _to_device(features, device):
    white_idx, black_idx, offsets, stm, buckets = features
    nb = device.type == "cuda"  # overlap host->device copies with compute (needs pinned memory)
    return (
        white_idx.to(device, non_blocking=nb),
        black_idx.to(device, non_blocking=nb),
        offsets.to(device, non_blocking=nb),
        stm.to(device, non_blocking=nb),
        buckets.to(device, non_blocking=nb),
    )


@torch.no_grad()
def clip_weights(model, limit):
    """Keep every effective parameter inside [-limit, limit], as bullet does (1.98).

    The engine depends on this: its fast SCReLU kernel computes w * v in
    int16 with v <= 255, so quantized output weights (w * 64) must stay
    within +/-128, and the int16 accumulator (w * 255, summed over up to 32
    pieces plus the bias) must not overflow. For factorized king buckets,
    the exported weight is ft_shared + ft_buckets[b], so ft_buckets[b] is
    clamped to [-limit - ft_shared, limit - ft_shared]."""
    if getattr(model, "factorized", False):
        model.ft_shared.clamp_(-limit, limit)
        model.ft_buckets.copy_(
            torch.clamp(model.ft_buckets, -limit - model.ft_shared, limit - model.ft_shared)
        )
    else:
        model.ft.weight.clamp_(-limit, limit)
    for p in (model.ft_bias, model.output_weights, model.output_biases):
        p.clamp_(-limit, limit)


class ResumableBatchSampler(Sampler):
    """Yields one index array per batch (drop_last), in an order that is
    reproducible from (seed, epoch), so a mid-epoch checkpoint resumes at the
    exact batch it stopped at instead of replaying the start of the epoch.

    Batches are numpy slices of a single permutation: no per-position Python
    objects, so memory and CPU stay flat even for hundreds of millions of
    positions (a Python list of 300M indices alone would need ~11 GB).

    Modes:
      shuffle          a fresh permutation of all positions every epoch
      shuffle_batches  fixed contiguous batches, visited in a fresh order every
                       epoch; for a file pre-shuffled on disk (clear_cache.py
                       --shuffle) that does not fit in RAM, since every batch
                       is then a single sequential read
      neither          file order
    """

    def __init__(self, n, batch_size, shuffle, seed=0, shuffle_batches=False):
        self.n = n
        self.batch_size = batch_size
        self.shuffle = shuffle
        self.shuffle_batches = shuffle_batches
        self.seed = seed
        self.num_batches = n // batch_size
        self.epoch = 1
        self.start_batch = 0

    def set_epoch(self, epoch, start_batch=0):
        self.epoch = epoch
        self.start_batch = start_batch

    def _generator(self):
        g = torch.Generator()
        g.manual_seed(self.seed * 1_000_003 + self.epoch)
        return g

    def __iter__(self):
        B = self.batch_size
        if self.shuffle_batches:
            order = torch.randperm(self.num_batches, generator=self._generator()).numpy()
            for b in order[self.start_batch:]:
                yield np.arange(b * B, (b + 1) * B)
            return
        perm = None
        if self.shuffle:
            # int32 halves the memory; torch draws the same permutation either way.
            dtype = torch.int32 if self.n < 2**31 else torch.int64
            perm = torch.randperm(self.n, generator=self._generator(), dtype=dtype).numpy()
        for b in range(self.start_batch, self.num_batches):
            yield perm[b * B:(b + 1) * B] if perm is not None else np.arange(b * B, (b + 1) * B)

    def __len__(self):
        return max(0, self.num_batches - self.start_batch)


def evaluate(model, loader, device):
    model.eval()
    total_loss = torch.zeros((), device=device)
    n = 0
    loss_fn = nn.MSELoss(reduction="sum")
    with torch.no_grad():
        for features, y in loader:
            white_idx, black_idx, offsets, stm, buckets = _to_device(features, device)
            y = y.to(device, non_blocking=device.type == "cuda")
            pred = torch.sigmoid(model(white_idx, black_idx, offsets, stm, buckets))
            total_loss += loss_fn(pred, y)
            n += y.size(0)
    model.train()
    return total_loss.item() / max(1, n)


def has_results(path, probe=200_000):
    """True if any of the first `probe` records carries a game result."""
    ds = NNUEDataset(path)
    k = min(ds.n, probe)
    if k == 0:
        return False
    mm = np.memmap(path, dtype=np.uint8, mode="r", shape=(ds.n, 68))
    return bool(np.any((np.asarray(mm[:k, 67]) >> 1) & 3))


class ValSet:
    """A validation file with a blended-target loader and, for files that hold
    game results, a pure-eval loader (comparable across --wdl-lambda)."""

    def __init__(self, path, args, device, limit=0):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        ds = NNUEDataset(path)
        if limit and limit < len(ds):
            ds = torch.utils.data.Subset(ds, range(limit))
        self.n = len(ds)

        def loader(lmb):
            return DataLoader(
                ds,
                batch_size=args.batch_size,
                shuffle=False,
                num_workers=0,
                collate_fn=functools.partial(
                    nnue_collate, wdl_lambda=lmb, mirror=args.mirror,
                    king_buckets=args.king_buckets, flip_augment=False,
                ),
            )

        self.blended = loader(args.wdl_lambda)
        self.pure = loader(1.0) if has_results(path) and args.wdl_lambda != 1.0 else None

    def columns(self):
        return [f"{self.name}_mse"] + ([f"{self.name}_eval_mse"] if self.pure is not None else [])

    def measure(self, model, device):
        out = [evaluate(model, self.blended, device)]
        if self.pure is not None:
            out.append(evaluate(model, self.pure, device))
        return out


def save_checkpoint(
    path,
    model,
    optimizer,
    scheduler,
    epoch,
    best_val,
    batch=None,
    total_batches=None,
    sampler_state=None,
    global_step=None,
):
    """Saves the complete state to resume training later using an atomic write."""
    abs_path = os.path.abspath(path)
    os.makedirs(os.path.dirname(abs_path), exist_ok=True)
    checkpoint = {
        "epoch": epoch,
        "model_state_dict": model.state_dict(),
        "optimizer_state_dict": optimizer.state_dict(),
        "scheduler_state_dict": scheduler.state_dict(),
        "best_val": best_val,
        "arch": getattr(model, "arch", {"mirror": False, "king_buckets": 1}),
    }
    if batch is not None:
        checkpoint["batch"] = batch
    if total_batches is not None:
        checkpoint["total_batches"] = total_batches
    if sampler_state is not None:
        checkpoint["sampler"] = sampler_state
    if global_step is not None:
        checkpoint["global_step"] = global_step

    tmp_path = f"{abs_path}.tmp"
    torch.save(checkpoint, tmp_path)

    # Keep previous checkpoint as .bak as an extra fallback
    if os.path.exists(abs_path):
        bak_path = f"{abs_path}.bak"
        try:
            shutil.copyfile(abs_path, bak_path)
        except Exception:
            pass

    os.replace(tmp_path, abs_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", default="../data/train1.bin")
    ap.add_argument("--val", default="../data/val1.bin")
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch-size", type=int, default=8192)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument(
        "--shuffle",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Shuffle train data each epoch (default: on; --no-shuffle for file order). "
             "The data cache is in stream order, so unshuffled batches are highly correlated.",
    )
    ap.add_argument(
        "--shuffle-batches",
        action="store_true",
        help="Shuffle the order of fixed, contiguous batches instead of single positions "
             "(overrides --shuffle). Use with a file pre-shuffled by clear_cache.py --shuffle "
             "that is larger than RAM: each batch becomes one sequential read.",
    )
    ap.add_argument("--seed", type=int, default=0, help="Seed for the per-epoch shuffle")
    ap.add_argument(
        "--clip",
        type=float,
        default=1.98,
        help="Clamp all weights to [-clip, clip] after every step (default 1.98, as bullet; "
             "required for the engine's fast int16 kernels). 0 disables.",
    )
    ap.add_argument(
        "--save-interval",
        type=int,
        default=2000,
        help="Save checkpoint every N batches within an epoch (default: 2000, 0 to disable)",
    )
    ap.add_argument("--out-best", default="../checkpoints/nnue.pt", help="Path to save best model")
    ap.add_argument("--out-last", default="../checkpoints/last.pt", help="Path to save last model checkpoint")
    ap.add_argument(
        "--resume",
        nargs="?",
        const="../checkpoints/last.pt",
        default=None,
        help="Path to checkpoint to resume training from (defaults to ../checkpoints/last.pt if flag is passed alone)",
    )
    ap.add_argument(
        "--init",
        default=None,
        help="Start from the weights of this checkpoint (optimizer, scheduler and epoch start fresh). "
             "Use this to fine-tune an existing network on new data. Ignored with --resume.",
    )
    ap.add_argument(
        "--init-from-unbucketed",
        default=None,
        help="Warm-start a factorized bucketed model (king_buckets > 1) from a 1-bucket checkpoint: "
             "copies ft.weight -> ft_shared, zeros ft_buckets, and copies ft_bias, output_weights, output_biases.",
    )
    ap.add_argument(
        "--wdl-lambda",
        type=float,
        default=0.75,
        help="Weight on eval sigmoid vs WDL game result in [0, 1] (default: 0.75; 1.0 = pure eval)",
    )
    ap.add_argument(
        "--king-buckets",
        type=int,
        default=1,
        choices=[1, 4],
        help="Number of perspective king buckets (default: 1; use 4 for Phase 4 factorized king buckets)",
    )
    ap.add_argument(
        "--optimizer",
        choices=["adam", "adamw"],
        default="adam",
        help="Optimizer (default: adam; use adamw with --bucket-decay for factorized king buckets)",
    )
    ap.add_argument(
        "--weight-decay",
        type=float,
        default=0.0,
        help="Weight decay for shared/output parameters (default: 0.0)",
    )
    ap.add_argument(
        "--bucket-decay",
        type=float,
        default=0.01,
        help="Weight decay applied to ft_buckets when --optimizer adamw is used (default: 0.01)",
    )
    arch_group = ap.add_mutually_exclusive_group()
    arch_group.add_argument(
        "--mirror",
        action="store_true",
        help="Horizontal king mirroring: flip perspective squares horizontally when own king is on files e..h",
    )
    arch_group.add_argument(
        "--flip-augment",
        action="store_true",
        help="Random horizontal flip augmentation (p=0.5 per sample, both perspectives) without mirroring",
    )
    ap.add_argument(
        "--val-extra",
        action="append",
        default=[],
        help="Extra validation file, evaluated after every epoch next to --val (repeatable). "
             "Files with game results also report MSE against the pure-eval target.",
    )
    ap.add_argument(
        "--val-every",
        type=int,
        default=0,
        help="Also evaluate every validation set every N batches and append a row to a "
             "learning-curve CSV beside --out-last (<out-last stem>_curve.csv). 0 disables.",
    )
    ap.add_argument(
        "--curve-val-max",
        type=int,
        default=200_000,
        help="Records per validation set used for the --val-every curve points (default 200k).",
    )
    ap.add_argument(
        "--select-by",
        choices=["val", "last"],
        default="val",
        help="Which checkpoint --out-best holds: lowest --val MSE (default) or simply the last epoch.",
    )
    ap.add_argument(
        "--total-samples",
        type=int,
        default=0,
        help="Stop after total_samples // batch_size optimizer steps, whatever the file size "
             "(overrides --epochs). 0 = train --epochs full epochs.",
    )
    ap.add_argument(
        "--lr-schedule",
        choices=["epoch-cosine", "step-cosine"],
        default="epoch-cosine",
        help="epoch-cosine: cosine over --epochs, stepped once per epoch (default). "
             "step-cosine: cosine over every optimizer step of the run, stepped per batch.",
    )
    ap.add_argument("--lr-min", type=float, default=1e-5, help="Final LR of the cosine schedule (default 1e-5)")
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Using device: {device}")

    train_ds = NNUEDataset(args.train)
    val_ds = NNUEDataset(args.val)
    print(f"Train positions: {len(train_ds)}  Val positions: {len(val_ds)}")

    total_batches = len(train_ds) // args.batch_size
    if total_batches == 0:
        raise SystemExit("Training set is smaller than one batch.")
    if args.total_samples > 0:
        total_steps = args.total_samples // args.batch_size
        args.epochs = -(-total_steps // total_batches)
        print(f"Sample budget: {total_steps * args.batch_size:,} samples = {total_steps:,} steps "
              f"= {total_steps / total_batches:.2f} epochs of this file")
    else:
        total_steps = args.epochs * total_batches

    train_collate = functools.partial(
        nnue_collate,
        wdl_lambda=args.wdl_lambda,
        mirror=args.mirror,
        king_buckets=args.king_buckets,
        flip_augment=args.flip_augment,
    )
    val_collate = functools.partial(
        nnue_collate,
        wdl_lambda=args.wdl_lambda,
        mirror=args.mirror,
        king_buckets=args.king_buckets,
        flip_augment=False,
    )

    train_sampler = ResumableBatchSampler(
        len(train_ds), args.batch_size, args.shuffle, args.seed, args.shuffle_batches
    )
    sampler_state = {"shuffle": args.shuffle, "seed": args.seed, "batch_size": args.batch_size}
    if args.shuffle_batches:
        sampler_state["shuffle_batches"] = True
    train_loader = DataLoader(
        train_ds,
        batch_sampler=train_sampler,
        num_workers=args.workers,
        pin_memory=(device.type == "cuda"),
        collate_fn=train_collate,
        persistent_workers=(args.workers > 0),
    )
    val_loader = DataLoader(
        val_ds,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.workers,
        pin_memory=(device.type == "cuda"),
        collate_fn=val_collate,
        persistent_workers=(args.workers > 0),
    )

    model = NNUE(mirror=args.mirror, king_buckets=args.king_buckets).to(device)
    print(
        f"Model architecture: {model.num_features}x{model.king_buckets} -> ({model.l1_size}x2) -> 1x{model.num_buckets} "
        f"| arch={model.arch}"
        + (" [flip_augment=True]" if args.flip_augment else "")
    )
    if args.optimizer == "adamw":
        if model.factorized:
            other_params = [model.ft_shared, model.ft_bias, model.output_weights, model.output_biases]
            opt = torch.optim.AdamW(
                [
                    {"params": [model.ft_buckets], "weight_decay": args.bucket_decay},
                    {"params": other_params, "weight_decay": args.weight_decay},
                ],
                lr=args.lr,
            )
        else:
            opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    else:
        opt = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    step_sched = args.lr_schedule == "step-cosine"
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(
        opt, T_max=total_steps if step_sched else args.epochs, eta_min=args.lr_min
    )
    loss_fn = nn.MSELoss()

    start_epoch = 1
    start_batch = 0
    best_val = float("inf")
    global_step = 0

    # Resume training state if requested and checkpoint exists
    if args.resume:
        if os.path.isfile(args.resume):
            print(f"--> Resuming from checkpoint: {args.resume}")
            checkpoint = torch.load(args.resume, map_location=device)
            ckpt_arch = checkpoint.get("arch", {"mirror": False, "king_buckets": 1})
            if (
                bool(ckpt_arch.get("mirror", False)) != bool(model.arch["mirror"])
                or int(ckpt_arch.get("king_buckets", 1)) != int(model.arch["king_buckets"])
            ):
                raise SystemExit(
                    f"Refusing to --resume '{args.resume}': checkpoint arch {ckpt_arch} "
                    f"does not match requested arch {model.arch}."
                )
            model.load_state_dict(checkpoint["model_state_dict"])
            opt.load_state_dict(checkpoint["optimizer_state_dict"])
            sched.load_state_dict(checkpoint["scheduler_state_dict"])
            start_epoch = checkpoint["epoch"] + 1
            best_val = checkpoint.get("best_val", float("inf"))
            saved_batch = checkpoint.get("batch")
            saved_total = checkpoint.get("total_batches")
            if saved_batch is not None and saved_total is not None and 0 < saved_batch < saved_total:
                same_order = checkpoint.get("sampler", sampler_state) == sampler_state
                if saved_total == total_batches and same_order:
                    start_batch = saved_batch
                else:
                    print("    (data, batch size or shuffle settings changed: restarting that epoch from batch 0)")
            global_step = checkpoint.get(
                "global_step", (start_epoch - 1) * total_batches + start_batch
            )
            where = f" at batch {start_batch}/{total_batches}" if start_batch else ""
            print(
                f"--> Resumed successfully at Epoch {start_epoch}{where} (Best Val MSE: {best_val:.6f})"
            )
        else:
            print(f"Warning: Checkpoint '{args.resume}' not found. Starting from scratch.")
    elif args.init_from_unbucketed:
        if not model.factorized:
            raise SystemExit("--init-from-unbucketed requires --king-buckets > 1 (factorized model).")
        checkpoint = torch.load(args.init_from_unbucketed, map_location=device)
        sd = checkpoint.get("model_state_dict", checkpoint) if isinstance(checkpoint, dict) else checkpoint
        with torch.no_grad():
            model.ft_shared.copy_(sd["ft.weight"])
            model.ft_buckets.zero_()
            model.ft_bias.copy_(sd["ft_bias"])
            model.output_weights.copy_(sd["output_weights"])
            model.output_biases.copy_(sd["output_biases"])
        print(
            f"--> Warm-started factorized {model.king_buckets}-bucket model from 1-bucket checkpoint "
            f"{args.init_from_unbucketed} (ft_shared=ft.weight, ft_buckets=0)"
        )
    elif args.init:
        checkpoint = torch.load(args.init, map_location=device)
        model.load_state_dict(checkpoint.get("model_state_dict", checkpoint))
        print(f"--> Initialized weights from {args.init} (fresh optimizer and schedule)")

    if args.clip > 0:
        clip_weights(model, args.clip)  # an older, unclipped net is brought into range once here

    if args.total_samples > 0 and global_step >= total_steps:
        print(f"--> Checkpoint has already reached the sample budget ({global_step} steps).")
        return
    if start_epoch > args.epochs:
        print(
            f"--> Checkpoint has already reached Epoch {start_epoch - 1} of {args.epochs}. "
            f"Increase --epochs to train further."
        )
        return

    val_extra = [ValSet(p, args, device) for p in args.val_extra]
    for vs in val_extra:
        print(f"Extra val: {vs.path} ({vs.n} positions{', + pure-eval MSE' if vs.pure is not None else ''})")
    curve_path = None
    if args.val_every > 0:
        curve_sets = [ValSet(args.val, args, device, args.curve_val_max)] + [
            ValSet(p, args, device, args.curve_val_max) for p in args.val_extra
        ]
        curve_path = os.path.splitext(args.out_last)[0] + "_curve.csv"
        if not os.path.exists(curve_path):
            with open(curve_path, "w") as f:
                cols = ["step", "samples", "epoch", "lr", "train_loss"]
                for vs in curve_sets:
                    cols += vs.columns()
                f.write(",".join(cols) + "\n")
        print(f"Learning curve every {args.val_every} batches -> {curve_path}")
    curve_loss = torch.zeros((), device=device)
    curve_seen = 0

    # Progress is stored as one tuple so each update is a single assignment:
    # a Ctrl-C can land anywhere and the handler still sees a consistent state.
    progress = (start_epoch, start_batch)   # (epoch, batches finished in it)
    finalized_epoch = start_epoch - 1       # last epoch whose scheduler step ran

    try:
        for epoch in range(start_epoch, args.epochs + 1):
            first = start_batch if epoch == start_epoch else 0
            train_sampler.set_epoch(epoch, first)

            t0 = time.time()
            running = torch.zeros((), device=device)   # summed on-device: no per-batch GPU sync
            seen = 0

            for batch_idx, (features, y) in enumerate(train_loader, start=first + 1):
                white_idx, black_idx, offsets, stm, buckets = _to_device(features, device)
                y = y.to(device, non_blocking=device.type == "cuda")

                opt.zero_grad(set_to_none=True)

                pred = torch.sigmoid(model(white_idx, black_idx, offsets, stm, buckets))
                loss = loss_fn(pred, y)

                loss.backward()
                opt.step()
                if args.clip > 0:
                    clip_weights(model, args.clip)

                if step_sched:
                    sched.step()
                global_step += 1

                running += loss.detach() * y.size(0)
                seen += y.size(0)
                progress = (epoch, batch_idx)

                if curve_path is not None:
                    curve_loss += loss.detach() * y.size(0)
                    curve_seen += y.size(0)
                    if global_step % args.val_every == 0:
                        row = [global_step, global_step * args.batch_size, epoch,
                               opt.param_groups[0]["lr"], curve_loss.item() / max(1, curve_seen)]
                        for vs in curve_sets:
                            row += vs.measure(model, device)
                        with open(curve_path, "a") as f:
                            f.write(",".join(f"{v:.8g}" if isinstance(v, float) else str(v) for v in row) + "\n")
                        curve_loss.zero_()
                        curve_seen = 0

                # Periodic mid-epoch save
                if args.save_interval > 0 and batch_idx % args.save_interval == 0:
                    save_checkpoint(
                        args.out_last, model, opt, sched, epoch - 1, best_val,
                        batch=batch_idx, total_batches=total_batches, sampler_state=sampler_state,
                        global_step=global_step,
                    )

                # Print every 100 batches (or on the last batch)
                if batch_idx % 100 == 0 or batch_idx == total_batches:
                    elapsed = time.time() - t0
                    avg_loss = running.item() / seen

                    print(
                        f"\rEpoch {epoch}/{args.epochs} | "
                        f"Batch {batch_idx}/{total_batches} "
                        f"({100 * batch_idx / total_batches:.1f}%) | "
                        f"Loss {avg_loss:.6f} | "
                        f"Elapsed {elapsed:.1f}s",
                        end="",
                        flush=True,
                    )

                if args.total_samples > 0 and global_step >= total_steps:
                    break

            print()  # Move to next line after epoch finishes

            if not step_sched:
                sched.step()
            finalized_epoch = epoch

            val_loss = evaluate(model, val_loader, device)
            dt = time.time() - t0
            train_mse = running.item() / max(1, seen)

            extra = ""
            for vs in val_extra:
                m = vs.measure(model, device)
                extra += f" | {vs.name} {m[0]:.6f}" + (f" (eval {m[1]:.6f})" if len(m) > 1 else "")
            print(
                f"Epoch {epoch:3d} complete | "
                f"Train MSE {train_mse:.6f} | "
                f"Val MSE {val_loss:.6f}{extra} | "
                f"Time {dt:.1f}s"
            )

            is_best = val_loss < best_val
            if is_best:
                best_val = val_loss
            if args.select_by == "last":
                is_best = True

            # Save last state after every completed epoch
            save_checkpoint(
                args.out_last, model, opt, sched, epoch, best_val,
                batch=total_batches, total_batches=total_batches, sampler_state=sampler_state,
                global_step=global_step,
            )

            # Save best state if validation improved (or every epoch with --select-by last)
            if is_best:
                save_checkpoint(
                    args.out_best, model, opt, sched, epoch, best_val,
                    batch=total_batches, total_batches=total_batches, sampler_state=sampler_state,
                    global_step=global_step,
                )
                print(f"  -> saved {'last' if args.select_by == 'last' else 'new best'} checkpoint to {args.out_best}")

            if args.total_samples > 0 and global_step >= total_steps:
                break

        print("Done. Best val MSE:", best_val)

    except KeyboardInterrupt:
        print("\n\n[!] Training paused by user (KeyboardInterrupt).")
        print("--> Saving emergency checkpoint...")
        p_epoch, p_batches = progress
        if finalized_epoch >= p_epoch:
            saved_epoch, saved_batch = finalized_epoch, total_batches
        elif p_batches >= total_batches:
            if not step_sched:
                sched.step()         # every batch of this epoch ran: finish the epoch
            finalized_epoch = p_epoch
            saved_epoch, saved_batch = p_epoch, total_batches
        else:
            saved_epoch, saved_batch = p_epoch - 1, p_batches
        save_checkpoint(
            args.out_last, model, opt, sched, saved_epoch, best_val,
            batch=saved_batch, total_batches=total_batches, sampler_state=sampler_state,
            global_step=global_step,
        )
        print(f"--> Emergency checkpoint successfully saved to: {args.out_last}")
        print("--> Resume anytime by running with: --resume\n")


if __name__ == "__main__":
    main()