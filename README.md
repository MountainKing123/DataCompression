# Data Compression - LZ77 + Huffman Token-Based Codec

A C++ implementation of LZ77 compression combined with Huffman entropy coding. Uses token-based encoding with per-stream Huffman tables, log2-class distance/length encoding, and a 3-slot recent-offsets cache. Chunk-based processing enables parallel decompression.

## Architecture

The codec processes data in 128 KiB chunks. Each chunk is independently compressed through a multi-stage pipeline:

1. **LZ77 Match Finding** - Hash-chain match search with 3-slot recent-offsets cache
2. **Token Packing** - Literal runs and matches encoded as token bytes (litlen + matchlen + offset mode)
3. **Stream Separation** - Token, literal, distance-class, length-class, extra-bits, and overflow streams
4. **Per-Stream Huffman** - Each stream gets independent entropy tables with raw fallback
5. **Wire Serialization** - LZH4 format with sparse/dense code-length headers

Token byte layout:
```
Bits 0-1: literal run length (0-2 inline; 3 = overflow varint)
Bits 2-5: match length field (0-14 = length 3-17; 15 = overflow via length class)
Bits 6-7: offset mode (0 = new distance; 1/2/3 = recent offset 0/1/2)
```

Distances use DEFLATE-style log2 classes (36 classes covering 1-262144). Extra bits for distance/length values are stored in a separate raw bitstream, bypassing Huffman.

## Files

### Core Implementation
| File | Purpose |
|------|---------|
| `huffman.h/cpp` | Huffman codec: frequency analysis, canonical codes, two-level decode tables |
| `lz77.h/cpp` | LZ77 encoder with hash-chain match finding and 3-slot recent-offsets cache |
| `lz77_huffman.h/cpp` | Token-based stream separation, per-stream Huffman, log2-class encoding |
| `bitstream.h/cpp` | Bit-level reading/writing primitives |
| `compressor.h/cpp` | High-level block compression API (LZH4 wire format) |

### Test Suite
| File | Purpose |
|------|---------|
| `tests/test_benchmark.cpp` | Performance benchmarking across data patterns and sizes |
| `tests/test_block_compressor.cpp` | Block compression API roundtrip tests |
| `tests/test_lz77_huffman_multistream.cpp` | Token-based multi-stream tests (overflow, recent offsets, edge cases) |
| `tests/test_lz77_huffman_layers.cpp` | LZ77 and Huffman as separate layers |
| `tests/test_lz77.cpp` | LZ77-only encoding validation |
| `tests/test_huffman_metrics.cpp` | Huffman entropy analysis and metrics across data patterns |
| `tests/test_huffman_edge_cases.cpp` | Huffman edge cases (single byte, uniform, alternating, sequential) |
| `tests/test_huffman_parallel.cpp` | Huffman parallel frequency counting |

## Build & Run

Prerequisites: CMake 3.15+, C++23 compiler (MSVC, GCC, Clang)

```bash
cd DataCompression
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release

# Run main demo
./Release/DataCompression.exe

# Run benchmark suite
./Release/TestBenchmark.exe
```

## Benchmark Results (Release Build)

```
=== LZ77+Huffman Multi-Stream Benchmark ===

Data Type                         Uncompressed    Compressed     Ratio      Enc ms      Dec ms    Enc MB/s    Dec MB/s  OK
------------------------------------------------------------------------------------------------------------------------------------------------
Repeating text 32 KiB                 32.0 KiB         156 B    210.05:1        0.18        0.06       172.7       515.7  PASS
Repeating text 128 KiB               128.0 KiB         173 B    757.64:1        2.33        0.22        53.7       572.6  PASS
Repeating text 1 MiB                   1.0 MiB       1.3 KiB    806.60:1       19.08        1.42        52.4       703.6  PASS
Repeating text 4 MiB                   4.0 MiB       5.0 KiB    812.22:1       77.63        5.99        51.5       668.1  PASS
Repeating text 16 MiB                 16.0 MiB      20.1 KiB    813.64:1      316.44       24.05        50.6       665.3  PASS
Skewed (90% one byte) 32 KiB          32.0 KiB       8.0 KiB      4.02:1        1.08        0.16        28.9       195.8  PASS
Skewed (90% one byte) 128 KiB        128.0 KiB      29.9 KiB      4.28:1        4.38        0.54        28.5       232.8  PASS
Skewed (90% one byte) 1 MiB            1.0 MiB     237.0 KiB      4.32:1       34.42        4.16        29.1       240.2  PASS
Skewed (90% one byte) 4 MiB            4.0 MiB     946.3 KiB      4.33:1      142.59       15.51        28.1       257.8  PASS
Skewed (90% one byte) 16 MiB          16.0 MiB       3.7 MiB      4.33:1      572.83       66.97        27.9       238.9  PASS
Binary pattern 32 KiB                 32.0 KiB       4.7 KiB      6.75:1        0.57        0.10        54.8       327.9  PASS
Binary pattern 128 KiB               128.0 KiB      15.0 KiB      8.53:1        2.63        0.29        47.5       424.0  PASS
Binary pattern 1 MiB                   1.0 MiB     121.0 KiB      8.46:1       21.93        2.74        45.6       364.4  PASS
Binary pattern 4 MiB                   4.0 MiB     484.7 KiB      8.45:1       88.42       11.78        45.2       339.5  PASS
Binary pattern 16 MiB                 16.0 MiB       1.9 MiB      8.45:1      365.23       46.08        43.8       347.2  PASS
Random (incompressible) 32 KiB        32.0 KiB      32.1 KiB      1.00:1        0.63        0.04        49.4       870.5  PASS
Random (incompressible) 128 KiB      128.0 KiB     128.1 KiB      1.00:1        4.35        0.15        28.7       835.6  PASS
Random (incompressible) 1 MiB          1.0 MiB       1.0 MiB      1.00:1       35.33        1.43        28.3       700.8  PASS
Random (incompressible) 4 MiB          4.0 MiB       4.0 MiB      1.00:1      144.15        5.84        27.7       685.3  PASS
Random (incompressible) 16 MiB        16.0 MiB      16.0 MiB      1.00:1      590.02       24.98        27.1       640.4  PASS

=== All benchmarks passed! ===
```

### Performance Summary

| Data Type | Ratio | Encode MB/s | Decode MB/s |
|-----------|-------|-------------|-------------|
| Repeating text | 210-814:1 | 51-173 | 516-704 |
| Skewed (90% one byte) | 4.0-4.3:1 | 28-29 | 196-258 |
| Binary pattern | 6.8-8.5:1 | 44-55 | 328-424 |
| Random (incompressible) | 1.00:1 | 27-49 | 640-871 |

Key observations:
- Random data achieves 1.00:1 ratio (no expansion) with raw-stream fallback
- Random data decodes at 640-870 MB/s by skipping Huffman entirely
- Decompression is consistently 3-10x faster than compression
- Per-stream tables and log2-class encoding improve ratio on match-heavy data

## License

(See LICENSE file)
