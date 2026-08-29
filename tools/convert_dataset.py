import sys
import time
import multiprocessing as mp
from evaluation import evalFen

def process_line(line):
    line = line.strip()
    if not line:
        return None
    
    parts = line.split(';')
    fen = parts[0]
    
    try:
        mg, eg = evalFen(fen)
        return f"{fen};{mg};{eg}"
    except Exception:
        return None

def main(input_file, output_file, threads):
    print(f"Converting '{input_file}' to '{output_file}' using {threads} threads...")
    start_time = time.time()
    
    # Read all lines into memory (250MB is fine for ~4M lines)
    print("Loading file into memory...")
    with open(input_file, 'r') as f:
        lines = f.readlines()
        
    total_lines = len(lines)
    print(f"Loaded {total_lines} positions. Starting evaluation...")

    written = 0
    with mp.Pool(processes=threads) as pool:
        # imap gives us results as they complete, keeping memory usage low for outputs
        results = pool.imap(process_line, lines, chunksize=5000)
        
        with open(output_file, 'w') as out_f:
            for i, res in enumerate(results):
                if res is not None:
                    out_f.write(res + "\n")
                    written += 1
                
                # Print progress every 100,000 lines
                if (i + 1) % 100000 == 0:
                    elapsed = time.time() - start_time
                    rate = (i + 1) / elapsed
                    percent = ((i + 1) / total_lines) * 100
                    print(f"Processed {i + 1}/{total_lines} ({percent:.1f}%) | Speed: {rate:.0f} pos/sec")

    total_time = time.time() - start_time
    print(f"\nDone! Wrote {written} positions to '{output_file}' in {total_time:.1f} seconds.")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python3 convert_dataset.py <input.epd> <output.epd> [threads]")
        sys.exit(1)
        
    in_file = sys.argv[1]
    out_file = sys.argv[2]
    threads = int(sys.argv[3]) if len(sys.argv) > 3 else mp.cpu_count()
    
    main(in_file, out_file, threads)
