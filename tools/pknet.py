#!/usr/bin/env python3
"""
pknet.py - trainer / exporter for GOOB's Ethereal-style PK network.

This reproduces the architecture used by Ethereal's PKNetwork so the engine's
C loader (src/pknet_loader.c) can evaluate it:

    Architecture : [224 inputs] -> [32 hidden] -> [2 outputs (MG, EG)]
    Inputs       : pawn + king placement only (0/1 indicators)
    Hidden       : linear (no activation) -- inputs are 0/1
    Output gate  : hidden neuron contributes only if hidden >= 0 (ReLU gate)
    Outputs      : MG score and EG score in centipawns (white POV), tapered

Input index (must match pkIndex() in pknet_loader.c exactly):

    idx = 112 * colour + (48 if KING) + sq - (8 if PAWN)

    white pawn : 0..47      white king : 48..111
    black pawn : 112..159   black king : 160..223

Square convention: sq = rank*8 + file, a1 = 0, h8 = 63 (same as the engine).

Binary format (src/pknet_loader.c reads this):

    uint32 magic = 0x32324B50   ("PK22")
    float  scale
    float  w1[224][32]          row-major: for idx in 0..223: for i in 0..31
    float  b1[32]
    float  w2[2][32]            row-major: for o in 0..1: for j in 0..31
    float  b2[2]

Usage
-----
    # Train from labelled data "FEN;mg;eg" (cp, white POV) or "FEN;result"
    python3 pknet.py train data.txt --epochs 20 --lr 1e-3 --out pknet.bin

    # Emit a random net (handy to test the C loader before training)
    python3 pknet.py random pknet.bin

    # Round-trip check that the exported binary decodes identically
    python3 pknet.py verify pknet.bin
"""

import argparse
import math
import os
import struct
import sys
import time

import numpy as np
try:
    import torch
    import torch.nn as nn
    from torch.utils.data import Dataset, DataLoader
    HAS_TORCH = True
except ImportError:
    HAS_TORCH = False

# ---- architecture constants (keep in sync with pknet_loader.c) -------------
INPUT = 224
H1 = 32
OUT = 2
MAGIC = 0x32324B50

WHITE, BLACK = 0, 1
PAWN, KING = 1, 6


def pk_index(colour, piece, sq):
    idx = 112 * colour
    if piece == KING:
        idx += 48
    if piece == PAWN:
        sq -= 8
    return idx + sq


PHASE_TOTAL = 24  # 4*Q(4) not present here -- see weights below

def fen_to_phase(fen):
    placement = fen.strip().split()[0]
    q = r = m = 0   # m = knights + bishops combined, matches C's single term
    for ch in placement:
        u = ch.upper()
        if u == "Q": q += 1
        elif u == "R": r += 1
        elif u in ("N", "B"): m += 1

    phase = 24 - 4 * q - 2 * r - 1 * m
    phase = max(phase, 0)
    return (phase * 256 + 12) // 24


# ---- FEN parsing (built-in, no python-chess dependency) --------------------
def fen_to_features(fen):
    """Return a (INPUT,) float32 array of 0/1 indicators for the PK placement."""
    x = np.zeros(INPUT, dtype=np.float32)
    placement = fen.strip().split()[0]
    ranks = placement.split("/")  # ranks[0] is rank 8
    for r, rank_str in enumerate(ranks):
        rank = 7 - r  # rank 8 -> index 7
        file = 0
        for ch in rank_str:
            if ch.isdigit():
                file += int(ch)
            else:
                sq = rank * 8 + file
                if ch == "P":
                    x[pk_index(WHITE, PAWN, sq)] = 1.0
                elif ch == "p":
                    x[pk_index(BLACK, PAWN, sq)] = 1.0
                elif ch == "K":
                    x[pk_index(WHITE, KING, sq)] = 1.0
                elif ch == "k":
                    x[pk_index(BLACK, KING, sq)] = 1.0
                file += 1
    return x


# ---- network forward (must match pknet_eval in pknet_loader.c) ------------
def forward(x, phase, W1, b1, W2, b2, scale=1.0):
    h = b1 + W1 @ x                       # (H1,)
    gate = (h > 0).astype(np.float32)
    out = b2 + (gate * h) @ W2.T          # (OUT,) = [mg, eg]
    interp = (out[0] * (256 - phase) + out[1] * phase) / 256.0
    return interp * scale


# ---- binary export / import -----------------------------------------------
def export_bin(path, W1, b1, W2, b2, scale=1.0):
    with open(path, "wb") as f:
        f.write(struct.pack("<I", MAGIC))
        f.write(struct.pack("<f", scale))
        # w1: C array pk_w1[INPUT][H1] is row-major => for idx, for i
        for idx in range(INPUT):
            for i in range(H1):
                f.write(struct.pack("<f", float(W1[i, idx])))
        for i in range(H1):
            f.write(struct.pack("<f", float(b1[i])))
        # w2: C array pk_w2[OUT][H1] is row-major => for o, for j
        for o in range(OUT):
            for j in range(H1):
                f.write(struct.pack("<f", float(W2[o, j])))
        for o in range(OUT):
            f.write(struct.pack("<f", float(b2[o])))


def import_bin(path):
    with open(path, "rb") as f:
        magic = struct.unpack("<I", f.read(4))[0]
        if magic != MAGIC:
            raise ValueError("bad magic: not a PK22 net")
        scale = struct.unpack("<f", f.read(4))[0]
        w1 = np.zeros((H1, INPUT), dtype=np.float32)
        for idx in range(INPUT):
            for i in range(H1):
                w1[i, idx] = struct.unpack("<f", f.read(4))[0]
        b1 = np.array([struct.unpack("<f", f.read(4))[0] for _ in range(H1)],
                      dtype=np.float32)
        w2 = np.zeros((OUT, H1), dtype=np.float32)
        for o in range(OUT):
            for j in range(H1):
                w2[o, j] = struct.unpack("<f", f.read(4))[0]
        b2 = np.array([struct.unpack("<f", f.read(4))[0] for _ in range(OUT)],
                      dtype=np.float32)
    return w1, b1, w2, b2, scale


# ---- data loading (PyTorch) -------------------------------------------------
if HAS_TORCH:
    class FENDataset(Dataset):
        def __init__(self, path, from_result, scale):
            self.lines = []
            self.from_result = from_result
            self.scale = scale
            with open(path) as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    self.lines.append(line)

        def __len__(self):
            return len(self.lines)

        def __getitem__(self, idx):
            line = self.lines[idx]
            parts = line.split(";")
            fen = parts[0].strip()
            x = fen_to_features(fen)
            phase = fen_to_phase(fen)
            
            if len(parts) >= 3:
                mg = float(parts[1]); eg = float(parts[2])
                target = (mg * (256 - phase) + eg * phase) / 256.0
            elif len(parts) == 2:
                if self.from_result:
                    r = min(max(float(parts[1]), 1e-3), 1 - 1e-3)
                    target = 400.0 * math.log10(r / (1.0 - r))
                else:
                    target = float(parts[1])
            else:
                target = 0.0

            target = target / self.scale
            return (torch.from_numpy(x), 
                    torch.tensor(phase, dtype=torch.float32), 
                    torch.tensor(target, dtype=torch.float32))

    class PKNetModel(nn.Module):
        def __init__(self):
            super().__init__()
            self.fc1 = nn.Linear(INPUT, H1)
            self.fc2 = nn.Linear(H1, OUT)

        def forward(self, x, phase):
            h = self.fc1(x)
            gate = (h > 0).float()
            out = self.fc2(gate * h)
            wmg = (256.0 - phase) / 256.0
            weg = phase / 256.0
            interp = out[:, 0] * wmg + out[:, 1] * weg
            return interp

# ---- data loading (NumPy Fallback) ----------------------------------------
def load_data(path, from_result=False):
    X, P, Y = [], [], []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(";")
            fen = parts[0].strip()
            x = fen_to_features(fen)
            phase = fen_to_phase(fen)
            if len(parts) >= 3:
                mg = float(parts[1]); eg = float(parts[2])
                target = (mg * (256 - phase) + eg * phase) / 256.0
            elif len(parts) == 2:
                if from_result:
                    r = min(max(float(parts[1]), 1e-3), 1 - 1e-3)
                    target = 400.0 * math.log10(r / (1.0 - r))
                else:
                    target = float(parts[1])
            else:
                continue
            X.append(x)
            P.append(phase)
            Y.append(target)
    return (np.array(X, dtype=np.float32),
            np.array(P, dtype=np.float32),
            np.array(Y, dtype=np.float32))

# ---- training --------------------------------------------------------------
def train(data_path, out_path, epochs, lr, batch, from_result, scale=1.0, resume_path=None):
    if HAS_TORCH:
        dataset = FENDataset(data_path, from_result, scale)
        n = len(dataset)
        print(f"[pknet] loaded {n} positions (PyTorch)", file=sys.stderr)
        
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        print(f"[pknet] using device: {device}", file=sys.stderr)

        loader = DataLoader(dataset, batch_size=batch, shuffle=True, num_workers=4)
        model = PKNetModel().to(device)
        
        # Initialize exactly as before
        if resume_path:
            w1, b1, w2, b2, _ = import_bin(resume_path)
            model.fc1.weight.data.copy_(torch.from_numpy(w1))
            model.fc1.bias.data.copy_(torch.from_numpy(b1))
            model.fc2.weight.data.copy_(torch.from_numpy(w2))
            model.fc2.bias.data.copy_(torch.from_numpy(b2))
            print(f"[pknet] resumed from {resume_path}", file=sys.stderr)
        else:
            nn.init.normal_(model.fc1.weight, mean=0.0, std=0.1)
            nn.init.zeros_(model.fc1.bias)
            nn.init.normal_(model.fc2.weight, mean=0.0, std=0.1)
            nn.init.zeros_(model.fc2.bias)

        optimizer = torch.optim.Adam(model.parameters(), lr=lr)
        criterion = nn.MSELoss()

        out_dir = os.path.dirname(out_path)
        best_path = os.path.join(out_dir, "pknet_best.bin") if out_dir else "pknet_best.bin"
        last_path = os.path.join(out_dir, "pknet_last.bin") if out_dir else "pknet_last.bin"
        best_loss = float('inf')

        for epoch in range(epochs):
            model.train()
            total_loss = 0.0
            num_batches = len(loader)
            start_time = time.time()
            samples_processed = 0
            for b_idx, (xb, pb, yb) in enumerate(loader):
                xb, pb, yb = xb.to(device), pb.to(device), yb.to(device)
                optimizer.zero_grad()
                interp = model(xb, pb)
                loss = criterion(interp, yb)
                loss.backward()
                optimizer.step()
                batch_sz = xb.size(0)
                total_loss += loss.item() * batch_sz
                samples_processed += batch_sz

                if (b_idx + 1) % 10 == 0 or (b_idx + 1) == num_batches:
                    elapsed = time.time() - start_time
                    batches_left = num_batches - (b_idx + 1)
                    eta = (elapsed / (b_idx + 1)) * batches_left
                    print(f"\r[pknet] epoch {epoch+1}/{epochs}  batch {b_idx+1}/{num_batches}  loss={total_loss/samples_processed:.4f}  eta={eta:.1f}s", end="", file=sys.stderr, flush=True)

            epoch_loss = total_loss / n
            print(f"\r[pknet] epoch {epoch+1}/{epochs}  loss={epoch_loss:.4f}{' '*30}", file=sys.stderr, flush=True)

            model.eval()
            W1 = model.fc1.weight.detach().cpu().numpy()
            b1 = model.fc1.bias.detach().cpu().numpy()
            W2 = model.fc2.weight.detach().cpu().numpy()
            b2 = model.fc2.bias.detach().cpu().numpy()
            
            export_bin(last_path, W1, b1, W2, b2, scale)
            if epoch_loss < best_loss:
                best_loss = epoch_loss
                export_bin(best_path, W1, b1, W2, b2, scale)
        
    else:
        # Fallback NumPy train
        X, P, Y = load_data(data_path, from_result)
        if scale != 1.0:
            Y = Y / scale
        n = len(X)
        print(f"[pknet] loaded {n} positions (NumPy)", file=sys.stderr)

        rng = np.random.default_rng(1234)
        if resume_path:
            W1, b1, W2, b2, _ = import_bin(resume_path)
            print(f"[pknet] resumed from {resume_path} (NumPy)", file=sys.stderr)
        else:
            W1 = rng.normal(0, 0.1, (H1, INPUT)).astype(np.float32)
            b1 = np.zeros(H1, dtype=np.float32)
            W2 = rng.normal(0, 0.1, (OUT, H1)).astype(np.float32)
            b2 = np.zeros(OUT, dtype=np.float32)

        mW1 = np.zeros_like(W1); vW1 = np.zeros_like(W1)
        mb1 = np.zeros_like(b1);  vb1 = np.zeros_like(b1)
        mW2 = np.zeros_like(W2); vW2 = np.zeros_like(W2)
        mb2 = np.zeros_like(b2);  vb2 = np.zeros_like(b2)
        b1v = 1e-8
        beta1, beta2 = 0.9, 0.999
        step = 0

        out_dir = os.path.dirname(out_path)
        best_path = os.path.join(out_dir, "pknet_best.bin") if out_dir else "pknet_best.bin"
        last_path = os.path.join(out_dir, "pknet_last.bin") if out_dir else "pknet_last.bin"
        best_loss = float('inf')

        for epoch in range(epochs):
            perm = rng.permutation(n)
            total_loss = 0.0
            num_batches = (n + batch - 1) // batch
            start_time = time.time()
            samples_processed = 0
            b_idx = 0
            for start in range(0, n, batch):
                step += 1
                idx = perm[start:start + batch]
                xb = X[idx]; pb = P[idx]; yb = Y[idx]

                h = b1 + xb @ W1.T
                gate = (h > 0).astype(np.float32)
                out = b2 + (gate * h) @ W2.T

                wmg = (256.0 - pb) / 256.0
                weg = pb / 256.0
                interp = out[:, 0] * wmg + out[:, 1] * weg

                loss = np.mean((interp - yb) ** 2)
                batch_sz = len(idx)
                total_loss += loss * batch_sz
                samples_processed += batch_sz

                dinterp = 2.0 * (interp - yb) / xb.shape[0]
                dout = np.stack([dinterp * wmg, dinterp * weg], axis=1)

                dg = (gate * h)
                dW2 = dout.T @ dg
                db2 = dout.sum(0)
                dh = gate * (dout @ W2)
                dW1 = dh.T @ xb
                db1 = dh.sum(0)

                for (p, dp, m, v) in ((W1, dW1, mW1, vW1), (b1, db1, mb1, vb1),
                                     (W2, dW2, mW2, vW2), (b2, db2, mb2, vb2)):
                    m *= beta1; m += (1 - beta1) * dp
                    v *= beta2; v += (1 - beta2) * (dp ** 2)
                    mhat = m / (1 - beta1 ** step)
                    vhat = v / (1 - beta2 ** step)
                    p -= lr * mhat / (np.sqrt(vhat) + b1v)

                b_idx += 1
                if b_idx % 10 == 0 or b_idx == num_batches:
                    elapsed = time.time() - start_time
                    batches_left = num_batches - b_idx
                    eta = (elapsed / b_idx) * batches_left
                    print(f"\r[pknet] epoch {epoch+1}/{epochs}  batch {b_idx}/{num_batches}  loss={total_loss/samples_processed:.4f}  eta={eta:.1f}s", end="", file=sys.stderr, flush=True)

            epoch_loss = total_loss / n
            print(f"\r[pknet] epoch {epoch+1}/{epochs}  loss={epoch_loss:.4f}{' '*30}", file=sys.stderr, flush=True)
            
            export_bin(last_path, W1, b1, W2, b2, scale)
            if epoch_loss < best_loss:
                best_loss = epoch_loss
                export_bin(best_path, W1, b1, W2, b2, scale)

    export_bin(out_path, W1, b1, W2, b2, scale)
    print(f"[pknet] wrote {out_path}", file=sys.stderr)


# ---- subcommands ----------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="GOOB PK-net trainer")
    sub = ap.add_subparsers(dest="cmd", required=True)

    t = sub.add_parser("train", help="train from FEN;mg;eg (or FEN;result)")
    t.add_argument("data")
    t.add_argument("--out", default="pknet.bin")
    t.add_argument("--epochs", type=int, default=20)
    t.add_argument("--lr", type=float, default=1e-3)
    t.add_argument("--batch", type=int, default=64)
    t.add_argument("--from-result", action="store_true",
                   help="treat single ';result' column as win%% -> cp")
    t.add_argument("--scale", type=float, default=1.0)
    t.add_argument("--resume", help="path to a .bin file to resume training from")

    r = sub.add_parser("random", help="emit a random net (loader smoke test)")
    r.add_argument("out")
    r.add_argument("--scale", type=float, default=1.0)

    v = sub.add_parser("verify", help="round-trip check an exported net")
    v.add_argument("path")

    e = sub.add_parser("from-ethereal",
                       help="convert Ethereal's pknet_*.net text into PK22 binary")
    e.add_argument("net", help="Ethereal .net file (text)")
    e.add_argument("out")
    e.add_argument("--scale", type=float, default=1.0)

    args = ap.parse_args()

    if args.cmd == "train":
        train(args.data, args.out, args.epochs, args.lr, args.batch,
              args.from_result, args.scale, args.resume)
    elif args.cmd == "random":
        rng = np.random.default_rng(0)
        W1 = rng.normal(0, 0.1, (H1, INPUT)).astype(np.float32)
        b1 = rng.normal(0, 0.01, H1).astype(np.float32)
        W2 = rng.normal(0, 0.1, (OUT, H1)).astype(np.float32)
        b2 = rng.normal(0, 0.01, OUT).astype(np.float32)
        export_bin(args.out, W1, b1, W2, b2, args.scale)
        print(f"[pknet] wrote random net {args.out}", file=sys.stderr)
    elif args.cmd == "from-ethereal":
        # Ethereal's .net is text: one row per line, first token is a row
        # index (skipped by Ethereal's parser), then weights, then a bias.
        # Lines 0..31 = input rows (224 weights), 32..33 = output rows (32).
        rows = []
        with open(args.net) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                # The .net is a C string-array: each line is wrapped in
                # quotes with a trailing comma, e.g.  "224 0.1 ... -3.2",
                # so strip those characters before tokenizing.
                toks = line.replace('"', ' ').replace(',', ' ').split()
                rows.append([float(t) for t in toks[1:]])  # drop index token
        assert len(rows) == H1 + OUT, f"expected {H1+OUT} rows, got {len(rows)}"

        W1 = np.zeros((H1, INPUT), dtype=np.float32)
        b1 = np.zeros(H1, dtype=np.float32)
        for i in range(H1):
            vals = rows[i]
            assert len(vals) == INPUT + 1, f"input row {i}: expected {INPUT+1} values"
            for j in range(INPUT):
                W1[i, j] = vals[j]
            b1[i] = vals[INPUT]

        W2 = np.zeros((OUT, H1), dtype=np.float32)
        b2 = np.zeros(OUT, dtype=np.float32)
        for o in range(OUT):
            vals = rows[H1 + o]
            assert len(vals) == H1 + 1, f"output row {o}: expected {H1+1} values"
            for j in range(H1):
                W2[o, j] = vals[j]
            b2[o] = vals[H1]

        export_bin(args.out, W1, b1, W2, b2, args.scale)
        print(f"[pknet] converted Ethereal net -> {args.out} "
              f"(scale={args.scale})", file=sys.stderr)

    elif args.cmd == "verify":
        W1, b1, W2, b2, scale = import_bin(args.path)
        print(f"[pknet] OK magic=PK22 scale={scale} "
              f"|w1|={np.linalg.norm(W1):.3f} |w2|={np.linalg.norm(W2):.3f}",
              file=sys.stderr)


if __name__ == "__main__":
    main()
