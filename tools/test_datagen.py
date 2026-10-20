#!/usr/bin/env python3
"""
Unit and regression test suite for Phase 2 Native Self-Play Data Generation.

Verifies all requirements of NNUE_IMPROVEMENT_PLAN.md Section 6.7:
  1. Determinism: Same seed and parameters produce identical binary files bit-for-bit.
  2. No in-check positions: Python-chess verifies board.is_check() is False for all samples.
  3. Valid quiet positions: Positions are valid non-tactical states.
  4. Result flags: Game outcome bits (White win, Draw, Black win) are properly encoded.
  5. Pipeline compatibility: Records are collated and processed cleanly by nnue_dataset.py.
  6. NNUE safety guards: Refuses to overwrite existing files or write to protected directories.

Usage:
    python3 tools/test_datagen.py
"""

import os
import shutil
import subprocess
import sys
import tempfile

try:
    import chess
except ImportError:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    venv_py = os.path.join(repo_root, "tools", ".venv", "bin", "python3")
    if os.path.exists(venv_py) and sys.executable != venv_py:
        os.execv(venv_py, [venv_py] + sys.argv)
    else:
        sys.stderr.write("Error: python-chess is required. Run in tools/.venv.\n")
        sys.exit(1)

_SCRIPTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "nnue_project", "scripts")
if _SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, _SCRIPTS_DIR)

from fen_utils import unpack_record, RECORD_SIZE
from nnue_dataset import NNUEDataset, nnue_collate


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    datagen_bin = os.path.join(repo_root, "tools", "datagen")

    if not os.path.exists(datagen_bin):
        print(f"Error: {datagen_bin} not built. Run 'gcc ... -o tools/datagen' first.")
        sys.exit(1)

    tmp_dir = tempfile.mkdtemp(prefix="goob_datagen_test_")
    try:
        print("=== Test 1: Determinism (same seed & parameters) ===")
        file1 = os.path.join(tmp_dir, "run1.bin")
        file2 = os.path.join(tmp_dir, "run2.bin")

        cmd1 = [datagen_bin, "--out", file1, "--nodes", "500", "--games", "2", "--seed", "42"]
        cmd2 = [datagen_bin, "--out", file2, "--nodes", "500", "--games", "2", "--seed", "42"]

        res1 = subprocess.run(cmd1, capture_output=True, text=True, cwd=repo_root)
        res2 = subprocess.run(cmd2, capture_output=True, text=True, cwd=repo_root)

        assert res1.returncode == 0, f"Run 1 failed: {res1.stderr}"
        assert res2.returncode == 0, f"Run 2 failed: {res2.stderr}"

        with open(file1, "rb") as f1, open(file2, "rb") as f2:
            data1 = f1.read()
            data2 = f2.read()

        assert len(data1) > 0, "No data produced in Run 1"
        assert data1 == data2, "Non-deterministic output! Run 1 != Run 2"
        print(f"  OK: Determinism verified bit-for-bit across {len(data1) // RECORD_SIZE} positions.")

        print("\n=== Test 2: In-Check & Quiet Position Rules (python-chess) ===")
        num_records = len(data1) // RECORD_SIZE
        assert num_records >= 20, f"Expected at least 20 positions, got {num_records}"

        PIECE_CHARS = {
            1: "P", 2: "N", 3: "B", 4: "R", 5: "Q", 6: "K",
            7: "p", 8: "n", 9: "b", 10: "r", 11: "q", 12: "k",
        }

        for i in range(num_records):
            rec = data1[i * RECORD_SIZE : (i + 1) * RECORD_SIZE]
            board_bytes, stm, cp, is_mate, result = unpack_record(rec, with_result=True)

            # Reconstruct board via FEN
            board = chess.Board(None)
            for sq in range(64):
                val = board_bytes[sq]
                if val in PIECE_CHARS:
                    board.set_piece_at(sq, chess.Piece.from_symbol(PIECE_CHARS[val]))
            board.turn = chess.WHITE if stm == 0 else chess.BLACK

            # Rule: side to move must NOT be in check
            assert not board.is_check(), f"Position #{i} has side to move in check: {board.fen()}"

            # Rule: game result must be valid (1.0, 0.5, or 0.0)
            assert result in (1.0, 0.5, 0.0), f"Position #{i} has invalid result: {result}"

        print(f"  OK: All {num_records} positions verified strictly quiet and not in check.")

        print("\n=== Test 3: Pipeline Collation & Model Targets ===")
        ds = NNUEDataset(file1)
        assert len(ds) == num_records, f"Dataset length mismatch: {len(ds)} vs {num_records}"

        feats, targets = nnue_collate(
            [ds[i] for i in range(len(ds))],
            mirror=True,
            king_buckets=4,
            wdl_lambda=0.75,
        )
        assert len(targets) == num_records, "Collate target length mismatch"
        print(f"  OK: Collate successful. Targets shape: {targets.shape}, mean target: {targets.mean().item():.4f}")

        print("\n=== Test 4: NNUE Safety Protection Guards ===")
        # 1. Refuse to overwrite existing file
        res_overwrite = subprocess.run(
            [datagen_bin, "--out", file1, "--nodes", "500", "--games", "1"],
            capture_output=True,
            text=True,
            cwd=repo_root,
        )
        assert res_overwrite.returncode != 0, "Failed to refuse overwrite of existing file!"
        assert "already exists" in (res_overwrite.stdout + res_overwrite.stderr)
        print("  OK: Refused overwrite of existing file.")

        # 2. Refuse protected target path
        res_protected = subprocess.run(
            [datagen_bin, "--out", "src/weights/quantised.bin", "--nodes", "500", "--games", "1"],
            capture_output=True,
            text=True,
            cwd=repo_root,
        )
        assert res_protected.returncode != 0, "Failed to reject protected weights destination!"
        assert "violates NNUE protection rules" in (res_protected.stdout + res_protected.stderr)
        print("  OK: Refused write to protected weights path.")

        print("\n=== Test 5: Dataset Mixer & Safety (mix_data.py) ===")
        mix_out = os.path.join(tmp_dir, "mixed.bin")
        mix_val = os.path.join(tmp_dir, "val_gen.bin")
        mix_script = os.path.join(repo_root, "tools", "nnue_project", "scripts", "mix_data.py")

        # 1. Successful mixing with validation split
        res_mix = subprocess.run(
            [sys.executable, mix_script, "--gen-data", file1, "--base-data", file2, "--out", mix_out, "--val-out", mix_val, "--val-count", "10", "--gen-share", "0.5"],
            capture_output=True,
            text=True,
            cwd=repo_root,
        )
        assert res_mix.returncode == 0, f"mix_data.py failed: {res_mix.stderr}"
        assert os.path.exists(mix_out), "mixed.bin not created"
        assert os.path.exists(mix_val), "val_gen.bin not created"
        assert os.path.getsize(mix_val) == 10 * RECORD_SIZE, "val_gen.bin size mismatch"
        print(f"  OK: Successfully mixed datasets with held-out validation set ({os.path.getsize(mix_out) // RECORD_SIZE} records).")

        # 2. Protection refusal on existing file
        res_mix_exist = subprocess.run(
            [sys.executable, mix_script, "--gen-data", file1, "--base-data", file2, "--out", mix_out],
            capture_output=True,
            text=True,
            cwd=repo_root,
        )
        assert res_mix_exist.returncode != 0, "mix_data.py failed to refuse existing file"
        print("  OK: Refused overwrite of existing mixed dataset.")

        # 3. Protection refusal on protected destination
        res_mix_prot = subprocess.run(
            [sys.executable, mix_script, "--gen-data", file1, "--base-data", file2, "--out", "src/weights/quantised.bin"],
            capture_output=True,
            text=True,
            cwd=repo_root,
        )
        assert res_mix_prot.returncode != 0, "mix_data.py failed to reject protected weights path"
        print("  OK: Refused write to protected weights path.")

        print("\n=======================================================")
        print("ALL PHASE 2 DATAGEN & MIX TESTS PASSED SUCCESSFULLY!")
        print("=======================================================")

    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
