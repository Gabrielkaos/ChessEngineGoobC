# Modern NNUE Implementation for GOOB

This directory now contains a modern, Stockfish-style NNUE implementation with the following features:

## Architecture

**HalfKP (King Position + Piece placement relative to King)**
- Input: 49152 features (64 king squares × 12 piece types × 64 from-squares)
- L1 (Accumulator): 256 neurons, int16, clipped ReLU
- L2: 32 neurons, int8, clipped ReLU
- L3: 32 neurons, int8, clipped ReLU
- Output: 1 neuron (raw evaluation in centipawns)

## Key Features

### 1. Efficiently Updatable Architecture (EUA)
Only 2-4 features change per move (piece movement), making incremental updates extremely fast compared to full recomputation.

### 2. Dual Accumulator Buffering
Maintains separate accumulators for White and Black perspectives, avoiding expensive feature mirroring during search.

### 3. SIMD Optimization
- **AVX2**: 32 lanes per iteration using `_mm256_maddubs_epi16` + `_mm256_madd_epi16`
- **AVX-512 VNNI**: 64 lanes per iteration using `_mm512_dpbusd_epi32`
- Runtime CPU feature detection with fallback to scalar code

### 4. Full Quantization
All layers use integer arithmetic except for a single float multiplication at the final output:
- L1 weights: int16
- L2/L3/Output weights: int8
- Activations: int8 [0, 127] after clipped ReLU

## Files

### `nnue.h` / `nnue.c`
Main implementation header with inline implementation. Include in your project:

```c
#define NNUE_IMPLEMENTATION
#include "nnue.h"
```

### `nnue_export.py`
Python script to export trained PyTorch weights to binary format:

```bash
# Generate test weights (random initialization)
python3 nnue_export.py goob.nnue

# Verify existing weight file
python3 nnue_export.py goob.nnue --verify
```

## API Reference

### Initialization
```c
// Load weights from file (call once at startup)
int nnue_init(const char *filepath);

// Free allocated memory
void nnue_free(void);

// Check if NNUE is ready
int nnue_is_ready(void);
```

### Incremental Updates
```c
// Add piece at square (called when placing pieces)
void nnue_add_piece(S_BOARD *pos, int piece, int sq);

// Remove piece from square (called when capturing)
void nnue_remove_piece(S_BOARD *pos, int piece, int sq);

// Move piece (more efficient than remove+add)
void nnue_move_piece(S_BOARD *pos, int piece, int from, int to);

// Full refresh (call after FEN parsing or new game)
void nnue_refresh(S_BOARD *pos);
```

### Evaluation
```c
// Evaluate position, returns centipawns from side-to-move POV
int nnue_evaluate(const S_BOARD *pos);
```

## Integration Example

```c
// In your engine initialization
#include "nnue.h"

S_BOARD board;
board.nnue_state = calloc(1, sizeof(NNUE_State));  // Allocate NNUE state

if (!nnue_init("goob.nnue")) {
    fprintf(stderr, "Failed to load NNUE weights\n");
    exit(1);
}

nnue_refresh(&board);  // Initialize accumulators

// In make_move()
nnue_move_piece(&board, piece, from_sq, to_sq);
if (captured_piece) {
    nnue_remove_piece(&board, captured_piece, to_sq);
}

// In search, when evaluating
if (nnue_is_ready()) {
    int eval = nnue_evaluate(&board);
    // Use eval in search...
}

// Cleanup
nnue_free();
free(board.nnue_state);
```

## Performance Notes

1. **Accumulator Layout**: L1 weights are stored feature-major (transposed from disk format) for contiguous memory access during incremental updates.

2. **Cache Efficiency**: HalfKP encoding ensures only ~16 features are active at any time, and moving a piece only requires updating 2-4 feature vectors.

3. **SIMD Dispatch**: The implementation automatically selects the best available instruction set at runtime:
   - AVX-512 VNNI (best)
   - AVX2 (good)
   - Scalar (fallback)

4. **Memory Usage**: 
   - Weight file: ~24 MB (for 49152×256×32×32 architecture)
   - Runtime memory: ~2 KB per accumulator (256 × int16 × 2 sides)

## Training Your Own Network

To train a production-quality network:

1. Collect positions from self-play games
2. Train using PyTorch/TensorFlow with HalfKP feature encoding
3. Export weights using a modified `nnue_export.py` that loads your trained model instead of random weights

The current `nnue_export.py` generates random weights for testing. Replace the weight generation section with your trained model loading code.

## Comparison with Existing nnue_loader.h

| Feature | nnue_loader.h (existing) | nnue.h (new) |
|---------|-------------------------|--------------|
| Architecture | Simple FC (768→L1→L2→L3→1) | HalfKP (49152→256→32→32→1) |
| Perspective | Absolute (White-fixed) | Dual (both sides buffered) |
| SIMD | AVX2 (fc2/fc3/fc4 only) | AVX2 + AVX-512 VNNI (all layers) |
| Update Method | Incremental accumulator | Incremental EUA features |
| Feature Encoding | 768 (12×64) | 49152 (64×12×64) |
| Active Features | All 768 | ~16 per side |

The HalfKP architecture used here is the same as Stockfish's NNUE, providing stronger evaluation with similar computational cost due to sparse feature activation.
