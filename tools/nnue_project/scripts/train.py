"""
Train the NNUE model on the binary cache produced by prepare_data.py.

Example:
    python train.py --train ../data/train1.bin --val ../data/val1.bin \
        --epochs 20 --batch-size 8192 --lr 1e-3 --resume
"""

import argparse
import os
import time

import torch
import torch.nn as nn
from torch.utils.data import DataLoader

from model import NNUE
from nnue_dataset import NNUEDataset, nnue_collate


def _to_device(features, device):
    white_idx, black_idx, offsets, stm = features
    return (
        white_idx.to(device),
        black_idx.to(device),
        offsets.to(device),
        stm.to(device),
    )


def evaluate(model, loader, device):
    model.eval()
    total_loss = 0.0
    n = 0
    loss_fn = nn.MSELoss(reduction="sum")
    with torch.no_grad():
        for features, y in loader:
            white_idx, black_idx, offsets, stm = _to_device(features, device)
            y = y.to(device)
            pred = torch.sigmoid(model(white_idx, offsets, black_idx, offsets, stm))
            total_loss += loss_fn(pred, y).item()
            n += y.size(0)
    model.train()
    return total_loss / n


def save_checkpoint(path, model, optimizer, scheduler, epoch, best_val):
    """Saves the complete state to resume training later."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    checkpoint = {
        "epoch": epoch,
        "model_state_dict": model.state_dict(),
        "optimizer_state_dict": optimizer.state_dict(),
        "scheduler_state_dict": scheduler.state_dict(),
        "best_val": best_val,
    }
    torch.save(checkpoint, path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", default="../data/train1.bin")
    ap.add_argument("--val", default="../data/val1.bin")
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch-size", type=int, default=8192)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--out-best", default="../checkpoints/nnue.pt", help="Path to save best model")
    ap.add_argument("--out-last", default="../checkpoints/last.pt", help="Path to save last model checkpoint")
    ap.add_argument(
        "--resume",
        nargs="?",
        const="../checkpoints/last.pt",
        default=None,
        help="Path to checkpoint to resume training from (defaults to ../checkpoints/last.pt if flag is passed alone)",
    )
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Using device: {device}")

    train_ds = NNUEDataset(args.train)
    val_ds = NNUEDataset(args.val)
    print(f"Train positions: {len(train_ds)}  Val positions: {len(val_ds)}")

    train_loader = DataLoader(
        train_ds,
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=args.workers,
        pin_memory=(device.type == "cuda"),
        drop_last=True,
        collate_fn=nnue_collate,
        persistent_workers=(args.workers > 0),
    )
    val_loader = DataLoader(
        val_ds,
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.workers,
        collate_fn=nnue_collate,
        persistent_workers=(args.workers > 0),
    )

    model = NNUE().to(device)
    print(f"model layers size{model.l1_size},{model.l2_size},{model.l3_size}")
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    sched = torch.optim.lr_scheduler.StepLR(
        opt, step_size=max(1, args.epochs // 4), gamma=0.3
    )
    loss_fn = nn.MSELoss()

    start_epoch = 1
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
            print(
                f"--> Resumed successfully at Epoch {start_epoch} (Best Val MSE: {best_val:.6f})"
            )
        else:
            print(f"Warning: Checkpoint '{args.resume}' not found. Starting from scratch.")

    for epoch in range(start_epoch, args.epochs + 1):
        t0 = time.time()
        running = 0.0
        seen = 0

        total_batches = len(train_loader)

        for batch_idx, (features, y) in enumerate(train_loader, start=1):
            white_idx, black_idx, offsets, stm = _to_device(features, device)
            y = y.to(device)

            opt.zero_grad()

            pred = torch.sigmoid(model(white_idx, offsets, black_idx, offsets, stm))
            loss = loss_fn(pred, y)

            loss.backward()
            opt.step()

            running += loss.item() * y.size(0)
            seen += y.size(0)

            # Print every 100 batches (or on the last batch)
            if batch_idx % 100 == 0 or batch_idx == total_batches:
                elapsed = time.time() - t0
                avg_loss = running / seen

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

        val_loss = evaluate(model, val_loader, device)
        dt = time.time() - t0

        print(
            f"Epoch {epoch:3d} complete | "
            f"Train MSE {running/seen:.6f} | "
            f"Val MSE {val_loss:.6f} | "
            f"Time {dt:.1f}s"
        )

        # Save last state after every epoch
        save_checkpoint(args.out_last, model, opt, sched, epoch, best_val)

        # Save best state if validation improved
        if val_loss < best_val:
            best_val = val_loss
            save_checkpoint(args.out_best, model, opt, sched, epoch, best_val)
            print(f"  -> saved new best checkpoint to {args.out_best}")

    print("Done. Best val MSE:", best_val)


if __name__ == "__main__":
    main()