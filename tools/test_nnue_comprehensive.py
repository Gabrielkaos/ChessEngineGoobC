import subprocess
import numpy as np
import torch
import torch.nn as nn

# 1. Load PyTorch model with Schoenemann weights
with open("src/weights/quantised.bin", "rb") as f:
    feat_w_raw = np.frombuffer(f.read(768 * 1024 * 2), dtype=np.int16).reshape(768, 1024)
    feat_b_raw = np.frombuffer(f.read(1024 * 2), dtype=np.int16)
    out_w_raw = np.frombuffer(f.read(8 * 2048 * 2), dtype=np.int16).reshape(8, 2048)
    out_b_raw = np.frombuffer(f.read(8 * 2), dtype=np.int16)

QA = 255
QB = 64
scale = 400

PIECE_MAP = {
    'P': (0, 0), 'N': (0, 1), 'B': (0, 2), 'R': (0, 3), 'Q': (0, 4), 'K': (0, 5),
    'p': (1, 0), 'n': (1, 1), 'b': (1, 2), 'r': (1, 3), 'q': (1, 4), 'k': (1, 5)
}

def parse_fen(fen):
    parts = fen.split()
    placement = parts[0]
    stm = 0 if parts[1] == 'w' else 1
    
    pieces = []
    rank = 7
    file = 0
    for ch in placement:
        if ch == '/':
            rank -= 1
            file = 0
        elif ch.isdigit():
            file += int(ch)
        else:
            color, pt = PIECE_MAP[ch]
            sq = rank * 8 + file
            pieces.append((color, pt, sq))
            file += 1
            
    return pieces, stm

def py_eval_integer(fen):
    pieces, stm = parse_fen(fen)
    w_acc = feat_b_raw.astype(np.int32).copy()
    b_acc = feat_b_raw.astype(np.int32).copy()
    for color, ptype, sq in pieces:
        w_idx = color * 384 + ptype * 64 + sq
        b_idx = (color ^ 1) * 384 + ptype * 64 + (sq ^ 56)
        w_acc += feat_w_raw[w_idx]
        b_acc += feat_w_raw[b_idx]

    us = w_acc if stm == 0 else b_acc
    them = b_acc if stm == 0 else w_acc

    us_clamped = np.clip(us, 0, QA)
    them_clamped = np.clip(them, 0, QA)
    us_act = us_clamped * us_clamped
    them_act = them_clamped * them_clamped

    act = np.concatenate([us_act, them_act])
    num_pieces = len(pieces)
    bucket = min(max((num_pieces - 2) // 4, 0), 7)

    val = int(np.sum(act * out_w_raw[bucket]))
    val = int(val / QA)
    val += int(out_b_raw[bucket])
    val *= scale
    val = int(val / (QA * QB))
    return val

class SchoenemannNNUE(nn.Module):
    def __init__(self):
        super().__init__()
        self.ft = nn.EmbeddingBag(768, 1024, mode='sum')
        self.ft_bias = nn.Parameter(torch.zeros(1024))
        self.out_w = nn.Parameter(torch.zeros(8, 2048))
        self.out_b = nn.Parameter(torch.zeros(8))

    def forward(self, w_idx, b_idx, offsets, stm, buckets):
        acc_w = self.ft(w_idx, offsets) + self.ft_bias
        acc_b = self.ft(b_idx, offsets) + self.ft_bias
        is_b = stm.bool().unsqueeze(1)
        us = torch.where(is_b, acc_b, acc_w)
        them = torch.where(is_b, acc_w, acc_b)
        us_act = torch.clamp(us, 0.0, 1.0) ** 2
        them_act = torch.clamp(them, 0.0, 1.0) ** 2
        x = torch.cat([us_act, them_act], dim=1)
        out = (x * self.out_w[buckets]).sum(dim=1) + self.out_b[buckets]
        return out

py_model = SchoenemannNNUE()
with torch.no_grad():
    py_model.ft.weight.copy_(torch.from_numpy(feat_w_raw.astype(np.float32) / QA))
    py_model.ft_bias.copy_(torch.from_numpy(feat_b_raw.astype(np.float32) / QA))
    py_model.out_w.copy_(torch.from_numpy(out_w_raw.astype(np.float32) / QB))
    py_model.out_b.copy_(torch.from_numpy(out_b_raw.astype(np.float32) / (QA * QB)))

def py_eval_float(fen):
    pieces, stm = parse_fen(fen)
    w_feat = [c * 384 + pt * 64 + sq for c, pt, sq in pieces]
    b_feat = [(c ^ 1) * 384 + pt * 64 + (sq ^ 56) for c, pt, sq in pieces]

    w_idx = torch.tensor(w_feat, dtype=torch.long)
    b_idx = torch.tensor(b_feat, dtype=torch.long)
    offsets = torch.tensor([0], dtype=torch.long)
    stm_t = torch.tensor([stm], dtype=torch.long)
    bucket = min(max((len(pieces) - 2) // 4, 0), 7)
    buckets_t = torch.tensor([bucket], dtype=torch.long)

    with torch.no_grad():
        logit = py_model(w_idx, b_idx, offsets, stm_t, buckets_t)
        cp = logit.item() * scale
    return cp

def goob_eval(fen):
    proc = subprocess.Popen(
        ["./src/bin/linux/GOOB-2.2-BETA-native"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    cmds = f"uci\nsetoption name UseNNUE value true\nposition fen {fen}\neval\nquit\n"
    out, _ = proc.communicate(input=cmds, timeout=5)
    for line in out.splitlines():
        if line.startswith("Eval:"):
            return int(line.split(":")[1].strip())
    raise RuntimeError(f"Could not parse eval from GOOB output: {out}")

def schoenemann_raw_eval(fen):
    proc = subprocess.Popen(
        ["./Schoenemann-0.5.0/src/null"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    cmds = f"position fen {fen}\neval\n"
    try:
        out, _ = proc.communicate(input=cmds, timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, _ = proc.communicate()
    for line in out.splitlines():
        if "The raw eval is:" in line:
            return int(line.split("The raw eval is:")[1].strip())
    raise RuntimeError(f"Could not parse eval from Schoenemann: {out}")

test_fens = [
    # Startpos (32 pieces, bucket 7)
    ("Startpos (32 pieces, bucket 7)", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
    # Startpos Black to move
    ("Startpos BTM", "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"),
    # Middlegame with 28 pieces (bucket 6)
    ("Italian Game (28 pieces, bucket 6)", "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 4 5"),
    # Complex Sicilian (26 pieces, bucket 6)
    ("Sicilian Najdorf (26 pieces, bucket 6)", "r1bqkb1r/1p3ppp/p1np1n2/4p3/3NP3/2N1BP2/PPP3PP/R2QKB1R w KQkq - 0 8"),
    # French Defense (24 pieces, bucket 5)
    ("French Winawer (24 pieces, bucket 5)", "rnbqk2r/ppp2ppp/4pn2/3p4/1bPP4/2N1P3/PP3PPP/R1BQKBNR w KQkq - 2 5"),
    # Kings Indian (22 pieces, bucket 5)
    ("KID Mar del Plata (22 pieces, bucket 5)", "r1bq1rk1/ppp2pbp/2np1np1/4p3/2PPP3/2N1BP2/PP2N1PP/R2QKB1R b KQ - 1 7"),
    # Queenless middlegame (20 pieces, bucket 4)
    ("Queenless Middlegame (20 pieces, bucket 4)", "r1b2rk1/pp3ppp/2n1pn2/8/8/1PN1PN2/P4PPP/R3KB1R w KQ - 1 12"),
    # Heavy piece endgame (16 pieces, bucket 3)
    ("Rook + Bishop Endgame (16 pieces, bucket 3)", "4r1k1/5ppp/8/8/8/2B5/5PPP/4R1K1 w - - 0 25"),
    # Minor piece endgame (12 pieces, bucket 2)
    ("Knight + Pawn Endgame (12 pieces, bucket 2)", "8/5pk1/4p1p1/8/8/4N1P1/5PK1/8 w - - 0 35"),
    # Double Rook endgame (10 pieces, bucket 2)
    ("Double Rook Endgame (10 pieces, bucket 2)", "8/2r2pk1/p3p1p1/1p6/1P6/P3R1P1/5PK1/2r5 w - - 0 40"),
    # 8-piece endgame (bucket 1)
    ("Single Rook Endgame (8 pieces, bucket 1)", "8/5pk1/4p1p1/1r6/8/6P1/4RPK1/8 w - - 0 45"),
    # 6-piece endgame (bucket 1)
    ("Pawn + Rook Endgame (6 pieces, bucket 1)", "8/5k2/4p1p1/8/8/6P1/4RPK1/8 w - - 0 50"),
    # 5-piece endgame (bucket 0)
    ("KRP vs KR (5 pieces, bucket 0)", "8/8/4k1p1/8/8/5K2/4R3/8 b - - 0 55"),
    # 4-piece endgame (bucket 0)
    ("KR vs KR (4 pieces, bucket 0)", "8/8/4k3/8/8/5K2/4R3/7r b - - 0 56"),
    # 3-piece endgame (bucket 0)
    ("KQ vs K (3 pieces, bucket 0)", "8/8/4k3/8/8/5K2/4Q3/8 b - - 0 57"),
    # 2-piece endgame (bucket 0)
    ("K vs K (2 pieces, bucket 0)", "8/8/4k3/8/8/5K2/8/8 w - - 0 58"),
    # Tactically unbalanced: Promotion threat
    ("Promotion Threat", "8/1P6/8/8/8/8/5k2/4K3 w - - 0 1"),
    # En Passant available
    ("En Passant FEN", "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3"),
    # Castling available both sides
    ("Castling Open", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"),
]

print(f"{'Position Description':<40} | {'GOOB C':>8} | {'Schoen C':>8} | {'Py Int':>8} | {'Py Float':>8} | {'Diff':>5}")
print("-" * 88)

all_passed = True
for name, fen in test_fens:
    g_val = goob_eval(fen)
    s_val = schoenemann_raw_eval(fen)
    p_int = py_eval_integer(fen)
    p_flt = py_eval_float(fen)
    
    match = (g_val == s_val == p_int)
    diff = abs(g_val - s_val) + abs(g_val - p_int)
    status = "OK" if match else "FAIL"
    if not match:
        all_passed = False
    print(f"{name:<40} | {g_val:>8} | {s_val:>8} | {p_int:>8} | {p_flt:>8.2f} | {status:>5}")

if all_passed:
    print("\nALL POSITION TESTS PASSED BIT-FOR-BIT ACROSS GOOB C, SCHOENEMANN C, AND PYTHON!")
else:
    print("\nSOME TESTS FAILED!")
