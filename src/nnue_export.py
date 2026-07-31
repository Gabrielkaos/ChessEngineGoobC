#!/usr/bin/env python3
"""
NNUE Weight Export Script for GOOB Chess Engine

This script exports trained PyTorch weights to the binary format expected by nnue.h.
The format matches Stockfish-style NNUE with HalfKP features.

File Format (all little-endian):
    char[4]   magic = "NUE4"
    int32     version = 4
    int32     input_size (49152 for HalfKP)
    int32     l1_size (256)
    int32     l2_size (32)
    int32     l3_size (32)
    float32   output_scale
    int32     qa_scale (64)
    int32     qb_scale (64)
    int32     qc_scale (64)
    int32     qd_scale (64)
    int16[l1_size * input_size]  fc1.weight (quantized)
    int32[l1_size]               fc1.bias (quantized)
    int8[l2_size * l1_size]      fc2.weight (quantized)
    int32[l2_size]               fc2.bias (quantized)
    int8[l3_size * l2_size]      fc3.weight (quantized)
    int32[l3_size]               fc3.bias (quantized)
    int8[1 * l3_size]            fc4.weight (quantized)
    int32[1]                     fc4.bias (quantized)
"""

import struct
import sys
import os

def export_random_weights(output_path, 
                          input_size=49152, 
                          l1_size=256, 
                          l2_size=32, 
                          l3_size=32,
                          qa_scale=64,
                          qb_scale=64,
                          qc_scale=64,
                          qd_scale=64,
                          output_scale=400.0):
    """
    Generate and export random weights for testing.
    In production, you would load trained weights from PyTorch.
    """
    import numpy as np
    
    print(f"Generating random NNUE weights...")
    print(f"  Architecture: {input_size} -> {l1_size} -> {l2_size} -> {l3_size} -> 1")
    print(f"  Quantization: QA={qa_scale}, QB={qb_scale}, QC={qc_scale}, QD={qd_scale}")
    
    # Initialize weights with small random values (Xavier-like initialization)
    np.random.seed(42)  # For reproducibility
    
    # L1 weights: initialized smaller for stability
    l1_w = np.random.randn(l1_size, input_size).astype(np.float32) * 0.01
    l1_b = np.zeros(l1_size, dtype=np.float32)
    
    # L2 weights
    l2_w = np.random.randn(l2_size, l1_size).astype(np.float32) * np.sqrt(2.0 / l1_size)
    l2_b = np.zeros(l2_size, dtype=np.float32)
    
    # L3 weights
    l3_w = np.random.randn(l3_size, l2_size).astype(np.float32) * np.sqrt(2.0 / l2_size)
    l3_b = np.zeros(l3_size, dtype=np.float32)
    
    # Output weights
    out_w = np.random.randn(1, l3_size).astype(np.float32) * np.sqrt(2.0 / l3_size)
    out_b = np.array([0.0], dtype=np.float32)
    
    # Quantize weights
    # L1: int16 quantization
    l1_w_q = np.clip(np.round(l1_w * qa_scale), -32768, 32767).astype(np.int16)
    l1_b_q = np.clip(np.round(l1_b * qa_scale), -2147483648, 2147483647).astype(np.int32)
    
    # L2: int8 quantization (scale includes 127 for clipped ReLU output)
    l2_w_q = np.clip(np.round(l2_w * qb_scale), -128, 127).astype(np.int8)
    l2_b_q = np.clip(np.round(l2_b * 127 * qb_scale), -2147483648, 2147483647).astype(np.int32)
    
    # L3: int8 quantization
    l3_w_q = np.clip(np.round(l3_w * qc_scale), -128, 127).astype(np.int8)
    l3_b_q = np.clip(np.round(l3_b * 127 * qc_scale), -2147483648, 2147483647).astype(np.int32)
    
    # Output: int8 quantization
    out_w_q = np.clip(np.round(out_w * qd_scale), -128, 127).astype(np.int8)
    out_b_q = np.clip(np.round(out_b * 127 * qd_scale), -2147483648, 2147483647).astype(np.int32)
    
    # Write binary file
    with open(output_path, 'wb') as f:
        # Header
        f.write(b'NUE4')  # Magic number (little-endian: '4', 'E', 'U', 'N')
        f.write(struct.pack('<i', 4))  # Version
        f.write(struct.pack('<i', input_size))
        f.write(struct.pack('<i', l1_size))
        f.write(struct.pack('<i', l2_size))
        f.write(struct.pack('<i', l3_size))
        f.write(struct.pack('<f', output_scale))
        f.write(struct.pack('<i', qa_scale))
        f.write(struct.pack('<i', qb_scale))
        f.write(struct.pack('<i', qc_scale))
        f.write(struct.pack('<i', qd_scale))
        
        # Weights (in order matching nnue.h expectations)
        # Note: L1 weights are written neuron-major, will be transposed on load
        f.write(l1_w_q.tobytes())  # [l1_size][input_size]
        f.write(l1_b_q.tobytes())  # [l1_size]
        f.write(l2_w_q.tobytes())  # [l2_size][l1_size]
        f.write(l2_b_q.tobytes())  # [l2_size]
        f.write(l3_w_q.tobytes())  # [l3_size][l2_size]
        f.write(l3_b_q.tobytes())  # [l3_size]
        f.write(out_w_q.tobytes()) # [1][l3_size]
        f.write(out_b_q.tobytes()) # [1]
    
    file_size = os.path.getsize(output_path)
    print(f"\nExported to: {output_path}")
    print(f"File size: {file_size:,} bytes ({file_size / 1024 / 1024:.2f} MB)")
    
    return True


def verify_weights(path):
    """Verify a weight file can be read correctly."""
    with open(path, 'rb') as f:
        magic = f.read(4)
        if magic != b'NUE4':
            print(f"ERROR: Invalid magic number: {magic}")
            return False
        
        version = struct.unpack('<i', f.read(4))[0]
        input_size = struct.unpack('<i', f.read(4))[0]
        l1_size = struct.unpack('<i', f.read(4))[0]
        l2_size = struct.unpack('<i', f.read(4))[0]
        l3_size = struct.unpack('<i', f.read(4))[0]
        output_scale = struct.unpack('<f', f.read(4))[0]
        qa_scale = struct.unpack('<i', f.read(4))[0]
        qb_scale = struct.unpack('<i', f.read(4))[0]
        qc_scale = struct.unpack('<i', f.read(4))[0]
        qd_scale = struct.unpack('<i', f.read(4))[0]
        
        print(f"Verified: {path}")
        print(f"  Version: {version}")
        print(f"  Architecture: {input_size} -> {l1_size} -> {l2_size} -> {l3_size} -> 1")
        print(f"  Output scale: {output_scale}")
        print(f"  Quantization: QA={qa_scale}, QB={qb_scale}, QC={qc_scale}, QD={qd_scale}")
        
        return True


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python nnue_export.py <output_file> [--verify]")
        print("\nExample:")
        print("  python nnue_export.py goob.nnue           # Generate random weights")
        print("  python nnue_export.py goob.nnue --verify  # Verify existing file")
        sys.exit(1)
    
    output_file = sys.argv[1]
    
    if len(sys.argv) > 2 and sys.argv[2] == "--verify":
        success = verify_weights(output_file)
    else:
        success = export_random_weights(output_file)
    
    sys.exit(0 if success else 1)
