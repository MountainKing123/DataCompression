# Benchmark Tracking System

A slim framework for tracking compression codec performance across iterations and detecting performance regressions.

## Overview

The framework consists of:
- **bench_tracker.h** - Core tracking library with CSV save/load and delta reporting
- **test_benchmark.cpp** - Main benchmark suite that uses the tracker
- **CSV Output** - Persistent storage of benchmark results for comparison

## Usage

### Running Benchmarks

```bash
cd build/Release
./TestBenchmark.exe
```

This creates `bench_results_current.csv` with the latest results.

### Creating a Baseline

After a successful benchmark run, save the current results as the baseline:

```bash
cp bench_results_current.csv bench_results_baseline.csv
```

### Comparing Against Baseline

On the next run, the framework automatically compares current results against `bench_results_baseline.csv` and displays a delta report showing:
- **Ratio delta** - Compression ratio improvement/regression (%)
- **Enc MB/s delta** - Encoding speed change (%)
- **Dec MB/s delta** - Decoding speed change (%)

## CSV Format

Both `bench_results_current.csv` and `bench_results_baseline.csv` use this format:

```
Name,Uncompressed,Compressed,Ratio,CompressTime_ms,DecompressTime_ms,CompressSpeed_MB_s,DecompressSpeed_MB_s,Roundtrip
```

Example:
```
Repeating text 32 KiB,32768,156,210.05,0.184,0.062,172.7,515.7,PASS
```

## API

### BenchmarkResult

Struct containing a single benchmark result with computed properties:
- `getRatio()` - Uncompressed / Compressed
- `getCompressSpeed_MB_s()` - Encoding throughput
- `getDecompressSpeed_MB_s()` - Decoding throughput

### BenchmarkTracker

Main class for managing benchmark data:

```cpp
BenchmarkTracker tracker;

// Save results
tracker.saveResults("bench_results_current.csv", results);

// Load previous results
auto baseline = tracker.loadResults("bench_results_baseline.csv");

// Show delta comparison
tracker.printDeltaReport(results, baseline);

// Print summary stats
tracker.printSummary(results);
```

## Workflow

1. **Establish baseline:**
   ```bash
   ./TestBenchmark.exe
   cp bench_results_current.csv bench_results_baseline.csv
   ```

2. **Make code changes** (e.g., Phase 3 optimizations)

3. **Run benchmark again:**
   ```bash
   ./TestBenchmark.exe
   ```
   
   Automatically shows delta report comparing to baseline.

4. **Review results:**
   - Look for regressions (negative deltas)
   - Verify improvements from optimizations
   - Monitor average speed across all test cases

5. **Update baseline (if happy with results):**
   ```bash
   cp bench_results_current.csv bench_results_baseline.csv
   ```

## Test Coverage

The benchmark suite covers:

- **Repeating text** - Best-case compression (5 sizes: 32 KiB to 16 MiB)
- **Skewed distribution** - 90% one byte (5 sizes)
- **Binary pattern** - Structured data with variations (5 sizes)
- **Random data** - Incompressible baseline (5 sizes)

**Total: 20 test cases** covering realistic and stress-test scenarios.

## Integration with Optimization Plan

After each phase (1, 2, 3, etc.), run the benchmark suite to:
1. Verify correctness (all roundtrips pass)
2. Measure compression ratio improvement
3. Detect encoding/decoding speed regressions
4. Document performance characteristics

The CSV format enables historical tracking and trend analysis across multiple commits.

