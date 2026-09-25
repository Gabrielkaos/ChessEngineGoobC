"""
Train the NNUE model on the binary cache produced by prepare_data.py.

Example:
    python train.py --train ../data/train1.bin --val ../data/val1.bin \
        --epochs 20 --batch-size 8192 --lr 1e-3 --resume

A file larger than RAM: pre-shuffle it once (clean_cache.py --shuffle), then
    python train.py --train ../data/clean/train.bin --val ../data/clean/val.bin \
        --shuffle-batches --resume

Fine-tune an existing network on new data (weights only, fresh optimizer):
    python train.py --train ../data/train2.bin --val ../data/val2.bin \
        --init ../checkpoints/nnue.pt --lr 3e-4 --epochs 8 \
        --out-best ../checkpoints/nnue_ft.pt --out-last ../checkpoints/last_ft.pt
"""

import argparse
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
    """Keep every parameter inside [-limit, limit], as bullet does (1.98).

    The engine depends on this: its fast SCReLU kernel computes w * v in
    int16 with v <= 255, so quantized output weights (w * 64) must stay
    within +/-128, and the int16 accumulator (w * 255, summed over up to 32
    pieces plus the bias) must not overflow. Without clipping, nothing in
    training stops weights from drifting past those limits."""
    for p in (model.ft.weight, model.ft_bias, model.output_weights, model.output_biases):
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
                       epoch; for a file pre-shuffled on disk (clean_cache.py
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
    }
    if batch is not None:
        checkpoint["batch"] = batch
    if total_batches is not None:
        checkpoint["total_batches"] = total_batches
    if sampler_state is not None:
        checkpoint["sampler"] = sampler_state

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
             "(overrides --shuffle). Use with a file pre-shuffled by clean_cache.py --shuffle "
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
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Using device: {device}")

    train_ds = NNUEDataset(args.train)
    val_ds = NNUEDataset(args.val)
    print(f"Train positions: {len(train_ds)}  Val positions: {len(val_ds)}")

    total_batches = len(train_ds) // args.batch_size
    if total_batches == 0:
        raise SystemExit("Training set is smaller than one batch.")

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
        collate_fn=nnue_collate,
        persistent_workers=(args.workers > 0),
    )
    val_loader = DataLoader(
        val_ds,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.workers,
        pin_memory=(device.type == "cuda"),
        collate_fn=nnue_collate,
        persistent_workers=(args.workers > 0),
    )

    model = NNUE().to(device)
    print(f"Model architecture: {model.num_features} -> ({model.l1_size}x2) -> 1x{model.num_buckets}")
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(
        opt, T_max=args.epochs, eta_min=1e-5
    )
    loss_fn = nn.MSELoss()

    start_epoch = 1
    start_batch = 0
    best_val = float("inf")

    # Resume training state if requested and checkpoint exists
    if args.resume:
        if os.path.isfile(args.resume):
            print(f"--> Resuming from checkpoint: {args.resume}")
            checkpoint = torch.load(args.resume, map_location=device)
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
            where = f" at batch {start_batch}/{total_batches}" if start_batch else ""
            print(
                f"--> Resumed successfully at Epoch {start_epoch}{where} (Best Val MSE: {best_val:.6f})"
            )
        else:
            print(f"Warning: Checkpoint '{args.resume}' not found. Starting from scratch.")
    elif args.init:
        checkpoint = torch.load(args.init, map_location=device)
        model.load_state_dict(checkpoint.get("model_state_dict", checkpoint))
        print(f"--> Initialized weights from {args.init} (fresh optimizer and schedule)")

    if args.clip > 0:
        clip_weights(model, args.clip)  # an older, unclipped net is brought into range once here

    if start_epoch > args.epochs:
        print(
            f"--> Checkpoint has already reached Epoch {start_epoch - 1} of {args.epochs}. "
            f"Increase --epochs to train further."
        )
        return

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

                running += loss.detach() * y.size(0)
                seen += y.size(0)
                progress = (epoch, batch_idx)

                # Periodic mid-epoch save
                if args.save_interval > 0 and batch_idx % args.save_interval == 0:
                    save_checkpoint(
                        args.out_last, model, opt, sched, epoch - 1, best_val,
                        batch=batch_idx, total_batches=total_batches, sampler_state=sampler_state,
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

            print()  # Move to next line after epoch finishes

            sched.step()
            finalized_epoch = epoch

            val_loss = evaluate(model, val_loader, device)
            dt = time.time() - t0
            train_mse = running.item() / max(1, seen)

            print(
                f"Epoch {epoch:3d} complete | "
                f"Train MSE {train_mse:.6f} | "
                f"Val MSE {val_loss:.6f} | "
                f"Time {dt:.1f}s"
            )

            is_best = val_loss < best_val
            if is_best:
                best_val = val_loss

            # Save last state after every completed epoch
            save_checkpoint(
                args.out_last, model, opt, sched, epoch, best_val,
                batch=total_batches, total_batches=total_batches, sampler_state=sampler_state,
            )

            # Save best state if validation improved
            if is_best:
                save_checkpoint(
                    args.out_best, model, opt, sched, epoch, best_val,
                    batch=total_batches, total_batches=total_batches, sampler_state=sampler_state,
                )
                print(f"  -> saved new best checkpoint to {args.out_best}")

        print("Done. Best val MSE:", best_val)

    except KeyboardInterrupt:
        print("\n\n[!] Training paused by user (KeyboardInterrupt).")
        print("--> Saving emergency checkpoint...")
        p_epoch, p_batches = progress
        if finalized_epoch >= p_epoch:
            saved_epoch, saved_batch = finalized_epoch, total_batches
        elif p_batches >= total_batches:
            sched.step()             # every batch of this epoch ran: finish the epoch
            finalized_epoch = p_epoch
            saved_epoch, saved_batch = p_epoch, total_batches
        else:
            saved_epoch, saved_batch = p_epoch - 1, p_batches
        save_checkpoint(
            args.out_last, model, opt, sched, saved_epoch, best_val,
            batch=saved_batch, total_batches=total_batches, sampler_state=sampler_state,
        )
        print(f"--> Emergency checkpoint successfully saved to: {args.out_last}")
        print("--> Resume anytime by running with: --resume\n")


if __name__ == "__main__":
    main()