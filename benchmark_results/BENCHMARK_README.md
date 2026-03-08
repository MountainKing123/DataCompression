# Benchmark Results Directory

This directory tracks the latest benchmark results for performance regression detection.

## Files

- `current.csv` - Latest benchmark run results
- `baseline.csv` - Previous stable baseline for comparison
- `delta_report.txt` - Detailed per-test comparison showing % changes

## Creating a Baseline

After a successful benchmark run:

```bash
cd benchmark_results
cp current.csv baseline.csv
```

The next benchmark run will automatically compare against this baseline and generate a delta report.

