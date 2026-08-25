#!/usr/bin/env python3
"""
Downloads positions from Hugging Face (Maxlegrec/ChessFENS) 
and converts them into the FEN;result format for tuning.

Usage:
    python3 download_hf_dataset.py [num_positions] [out_file]

Example (downloads 2 million positions to dataset.epd):
    python3 download_hf_dataset.py 2000000 dataset.epd
"""

import sys
import os

try:
    from datasets import load_dataset
except ImportError:
    print("Error: The 'datasets' library is missing.")
    print("Please install it by running: pip install datasets")
    sys.exit(1)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 download_hf_dataset.py [num_positions] [out_file]")
        print("Example: python3 download_hf_dataset.py 1000000 dataset.epd")
        sys.exit(1)
        
    try:
        num_positions = int(sys.argv[1])
    except ValueError:
        print("Error: [num_positions] must be an integer.")
        sys.exit(1)
        
    out_file = sys.argv[2] if len(sys.argv) > 2 else "dataset.epd"
    
    print(f"Streaming dataset Maxlegrec/ChessFENS from Hugging Face...")
    print(f"Target: {num_positions} positions")
    print(f"Output file: {out_file}")
    
    # We use streaming=True so we don't have to download all 700+ million rows at once.
    dataset = load_dataset("Maxlegrec/ChessFENS", split="train", streaming=True)
    
    count = 0
    mode = "a" if os.path.exists(out_file) else "w"
    
    with open(out_file, mode) as f:
        # Check if we need to repair missing newline when appending
        if mode == "a" and os.path.getsize(out_file) > 0:
            with open(out_file, "rb") as chk:
                chk.seek(-1, os.SEEK_END)
                if chk.read(1) != b"\n":
                    f.write("\n")
                    f.flush()

        for row in dataset:
            if count >= num_positions:
                break
                
            fen = row.get("fen")
            game_winner = row.get("game_winner")
            
            if not fen or game_winner is None:
                continue
                
            # Game Winner format: [white_win, draw, black_win]
            # [1, 0, 0] -> White win (1.0)
            # [0, 1, 0] -> Draw (0.5)
            # [0, 0, 1] -> Black win (0.0)
            
            if game_winner[0] == 1:
                result = "1.0"
            elif game_winner[1] == 1:
                result = "0.5"
            elif game_winner[2] == 1:
                result = "0.0"
            else:
                continue # Skip malformed rows
                
            f.write(f"{fen};{result}\n")
            count += 1
            
            if count % 100000 == 0:
                print(f"Downloaded {count} / {num_positions} positions...")
                f.flush()
                
    print(f"Successfully wrote {count} positions to {out_file}")

if __name__ == "__main__":
    main()
