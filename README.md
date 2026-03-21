# Data Compression — LZ + Huffman Token-Based Codec

A C++ implementation of an LZ + Huffman token-based codec.
Uses packed offset encoding, per-stream entropy tables, literal subtraction, an optional
RLE pre-pass, and a bidirectional extra-bits stream. Chunk-based processing enables
parallel compression and decompression.

## Architecture

Data is processed in independent 128 KiB chunks through a multi-stage pipeline:

1. **LZ Match Finding** — Hash-chain search with 3-slot MRU recent-offsets cache
2. **Tokenization** — Literal runs and matches packed into token bytes
3. **Stream Separation** — Six independent byte streams per chunk
4. **Entropy Selection** — Each stream independently tries Raw / Huffman / RLE+Huffman
5. **Wire Serialization** — `LZH8` format with sparse or dense code-length headers

### Token Byte Layout

```
Bits 0-1: litlen       (0-2 inline literals; 3 = escape to lenOverflow stream)
Bits 2-5: matchlen     (0-14 = matchlen 3-17; 15 = escape to lenOverflow stream)
Bits 6-7: offset mode  (0 = new distance; 1/2/3 = recent-offset slot 0/1/2)
```

### Offset Encoding

Each distance is packed into a single entropy-coded byte that absorbs the log2 class
and 4 mantissa bits, minimising the raw extra-bits residual:

```
n = BSR(distance)                        // position of highest set bit
if n < 4:  packed = distance - 1        // values 0-14, zero extra bits
else:      packed = ((n-3) << 4) | ((distance >> (n-4)) & 0x0F)
           extra bits = (n-4) raw bits of (distance & mask)
```

Extra bits are stored in a **bidirectional** raw bitstream: even-indexed offsets are
written forward and odd-indexed offsets are written backward into the same buffer,
giving the decoder two independent dependency chains.

### Length Overflow Stream

Both litlen ≥ 3 and matchlen ≥ 18 overflow into a **single shared byte stream**.
Values 0–254 are stored directly (1 byte). Value 255 is an escape that signals a
full LE32 value in a separate `lenOverflowExtra` stream.

### Literal Encoding

Per chunk, the encoder builds both a raw literal array and a delta-coded array
(`literal − output[pos − lastOffset]`). It selects whichever has lower Shannon
entropy. The chosen mode is stored in the per-chunk flags byte.

### Entropy Pipeline (per stream)

Each of the four entropy-coded streams independently tries:
1. **Raw** — memcpy, used when all other modes expand
2. **Huffman** — canonical Huffman with two-level decode table
3. **RLE + Huffman** — RLE pre-pass (runs of 3+ bytes with `0xFF` escape protocol),
   followed by Huffman on the compressed control stream

The smallest output wins. Stream type is stored in the wire header.

---

## Wire Format — `LZH8`

### File Header (12 bytes)
```
[4]  Magic          "LZH8"
[4]  TotalSize      LE32 — original uncompressed size
[4]  ChunkCount     LE32 — number of chunks
```

### Per-Chunk Header (29 bytes fixed + variable streams)
```
[4]  UncompressedSize      LE32
[4]  TokenCount            LE32
[4]  LiteralByteCount      LE32
[4]  DistPackedCount       LE32
[4]  LenOverflowCount      LE32
[4]  ExtraBitCount         LE32
[4]  LenOverflowExtraCount LE32
[1]  Flags                 bit 0 = literalSubMode
```

Followed by four **entropy-coded streams** (token, literal, distPacked, lenOverflow), each
with a stream header:
```
[1]  TypeAndFlags   bits 0-3 = StreamType (0=Raw, 1=Huffman, 2=RLEHuffman)
                    bit  7   = interleaved flag (reserved)
--- if Huffman or RLEHuffman: ---
[1]  CodeLengthMode  0 = dense (256 bytes follow), 1 = sparse
--- sparse: ---
[1]  SymbolCount-1
[N*2] (symbol, codeLength) pairs
--- if RLEHuffman: ---
[4]  RleEncodedSize  LE32
--- always: ---
[4]  StreamSize      LE32
[N]  StreamBytes
```

Followed by two **raw streams**:
```
ExtraBits stream:
[4]  PackedSize      LE32 — top 2 bits = extraBitsMode (0=empty, 1=fwd-only, 2=bidirectional)
                            low 30 bits = byte count
[N]  Bytes           — bidirectional layout: [fwdBytes][bwdBytes]

LenOverflowExtra stream:
[4]  StreamSize      LE32
[N]  Bytes           — LE32 escape values for lenOverflow overflow
```

---

## Files

### Core Implementation
| File | Purpose |
|------|---------|
| `huffman.h/cpp` | Huffman codec: frequency analysis, canonical codes, two-level decode tables |
| `lz.h/cpp` | LZ encoder: hash-chain match finding, 3-slot recent-offsets cache |
| `lz_huffman.h/cpp` | Tokenization, stream separation, entropy selection, packed offset encoding |
| `bitstream.h` | Bit-level I/O: forward/reverse/bidirectional bit readers and writers |
| `compressor.h/cpp` | High-level block API: `LZH8` wire format serialization |
| `diag_overhead.cpp` | Diagnostic tool: per-stream size breakdown on synthetic inputs |

### Test Suite
| File | Purpose |
|------|---------|
| `tests/test_benchmark.cpp` | Performance benchmarks across data patterns and sizes |
| `tests/test_block_compressor.cpp` | Block API roundtrip tests |
| `tests/test_lz_huffman_multistream.cpp` | Multi-stream edge cases (overflow, recent offsets) |
| `tests/test_lz_huffman_layers.cpp` | LZ and Huffman as independent layers |
| `tests/test_lz.cpp` | LZ-only encoding validation |
| `tests/test_huffman_metrics.cpp` | Huffman entropy analysis across data patterns |
| `tests/test_huffman_edge_cases.cpp` | Edge cases: single byte, uniform, alternating, sequential |
| `tests/test_huffman_parallel.cpp` | Parallel frequency counting |

---

## Build & Run

**Prerequisites:** CMake 3.15+, C++23 compiler (MSVC, GCC, Clang)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run main demo
./build/Release/DataCompression.exe

# Run full benchmark suite
./build/Release/TestBenchmark.exe
```

---

## Benchmark Results (Release Build)

Results from `benchmark_results/current.csv` — 40 test cases across 8 data patterns.

| Data Type | Size | Compressed | Ratio | Enc MB/s | Dec MB/s |
|-----------|------|-----------|-------|----------|----------|
| Repeating text | 32 KiB – 16 MiB | 176 B – 21 KiB | **186–771:1** | 224–630 | 530–679 |
| Skewed (90% one byte) | 32 KiB – 16 MiB | 8.2 – 3795 KiB | **3.9–4.3:1** | 38–42 | 153–193 |
| Binary pattern | 32 KiB – 16 MiB | 4.3 – 1924 KiB | **7.5–8.6:1** | 70–81 | 262–303 |
| Random (incompressible) | 32 KiB – 16 MiB | ≈ input | **1.00:1** | 22–37 | 381–391 |
| Log text | 32 KiB – 16 MiB | 6.4 – 2912 KiB | **5.0–5.6:1** | 68–73 | 212–259 |
| JSON-like records | 32 KiB – 16 MiB | 4.7 – 2100 KiB | **6.8–7.8:1** | 75–81 | 265–309 |
| Simulated x86 binary | 32 KiB – 16 MiB | 16.7 – 7987 KiB | **1.9–2.1:1** | 30–32 | 97–110 |
| Sorted integers | 32 KiB – 16 MiB | 6.5 – 3225 KiB | **4.9–5.1:1** | 34–51 | 158–170 |

All 40 benchmarks pass roundtrip verification.

### Key Observations

- **Random data** achieves exactly 1.00:1 (no expansion) via raw-stream fallback; decodes
  at ~385 MB/s by skipping Huffman entirely
- **Repeating text** achieves 186–771:1 due to RLE pre-pass on all streams, not just literals
- **Literal subtraction** is the primary driver for sorted integers (5:1 vs ~3:1 without it)
- **Packed offset encoding** eliminates most raw extra-bit overhead; 4 mantissa bits per
  offset move into the entropy-coded distPacked stream
- Decompression is consistently **3–10× faster** than compression

---

## License

See [LICENSE](LICENSE).
