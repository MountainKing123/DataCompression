#include "compressor.h"
#include "../bench_tracker.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <string>
#include <sstream>
#include <cmath>
#include <random>
#include <cassert>
#include <numeric>

static BenchmarkResult runBench(const std::string& name,
                                const std::vector<uint8_t>& input,
                                const int iterations = 5)
{
    BenchmarkResult r;
    r.name = name;
    r.uncompressedSize = input.size();

    compression::CompressOptions opts;

    // Warm up
    auto compressed = compression::compress(input, opts);

    // Benchmark compress
    double bestCompress = 1e18;
    for (int i = 0; i < iterations; ++i)
    {
        auto t0 = std::chrono::steady_clock::now();
        compressed = compression::compress(input, opts);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms < bestCompress) bestCompress = ms;
    }
    r.compressionTime_ms = bestCompress;
    r.compressedSize = compressed.size();

    // Benchmark decompress
    double bestDecompress = 1e18;
    std::vector<uint8_t> output;
    for (int i = 0; i < iterations; ++i)
    {
        auto t0 = std::chrono::steady_clock::now();
        output = compression::decompress(compressed);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms < bestDecompress) bestDecompress = ms;
    }
    r.decompressionTime_ms = bestDecompress;
    r.roundtrip = (output == input);

    return r;
}

static std::vector<uint8_t> makeRepeatingText(const size_t targetSize)
{
    const std::string sentence = "The quick brown fox jumps over the lazy dog. ";
    std::vector<uint8_t> data;
    data.reserve(targetSize);
    while (data.size() < targetSize)
    {
        for (char c : sentence)
        {
            if (data.size() >= targetSize) break;
            data.push_back(static_cast<uint8_t>(c));
        }
    }
    return data;
}

static std::vector<uint8_t> makeSkewed(const size_t targetSize)
{
    std::mt19937 rng(42);
    std::vector<uint8_t> data(targetSize);
    for (auto& b : data)
    {
        b = (rng() % 100 < 90) ? 0xAA : static_cast<uint8_t>(rng() & 0xFF);
    }
    return data;
}

static std::vector<uint8_t> makeBinaryPattern(const size_t targetSize)
{
    std::vector<uint8_t> data(targetSize);
    // Simulate structured binary data: repeating 256-byte blocks with small variations
    std::mt19937 rng(123);
    std::vector<uint8_t> block(256);
    std::iota(block.begin(), block.end(), 0);
    for (size_t i = 0; i < targetSize; ++i)
    {
        data[i] = block[i % 256];
        if (rng() % 20 == 0)
            data[i] ^= 1; // occasional bit flip
    }
    return data;
}

static std::vector<uint8_t> makeRandom(const size_t targetSize)
{
    std::mt19937 rng(99);
    std::vector<uint8_t> data(targetSize);
    for (auto& b : data)
        b = static_cast<uint8_t>(rng() & 0xFF);
    return data;
}

// Simulated structured log lines: "2026-03-15 12:34:56 [INFO ] RequestHandler: processed 1234 bytes in 5ms\n"
// High structural repetition in keys/timestamps, varying numeric values.
static std::vector<uint8_t> makeLogText(const size_t targetSize)
{
    const char* levels[] = {"INFO ", "WARN ", "ERROR", "DEBUG"};
    const char* modules[] = {"RequestHandler", "AuthService", "DBConnector", "CacheLayer", "Scheduler"};
    std::mt19937 rng(77);
    std::vector<uint8_t> data;
    data.reserve(targetSize);
    int seq = 0;
    while (data.size() < targetSize)
    {
        char line[128];
        int sec  = static_cast<int>(rng() % 60);
        int min  = static_cast<int>(rng() % 60);
        int hour = static_cast<int>(rng() % 24);
        int bytes = static_cast<int>(rng() % 65536);
        int ms    = static_cast<int>(rng() % 500);
        const char* level  = levels[rng() % 4];
        const char* module = modules[rng() % 5];
        int n = std::snprintf(line, sizeof(line),
            "2026-03-15 %02d:%02d:%02d [%s] %s: processed %d bytes in %dms\n",
            hour, min, sec, level, module, bytes, ms);
        if (n <= 0) break;
        for (int i = 0; i < n && data.size() < targetSize; ++i)
            data.push_back(static_cast<uint8_t>(line[i]));
        ++seq;
    }
    return data;
}

// Simulated JSON records: repeated key names, varying numeric/string values.
// Models REST API response payloads or config files.
static std::vector<uint8_t> makeJsonLike(const size_t targetSize)
{
    std::mt19937 rng(55);
    std::vector<uint8_t> data;
    data.reserve(targetSize);
    while (data.size() < targetSize)
    {
        char record[256];
        int id    = static_cast<int>(rng() % 100000);
        int age   = static_cast<int>(20 + rng() % 60);
        int score = static_cast<int>(rng() % 1000);
        const char* status = (rng() % 2) ? "active" : "inactive";
        int n = std::snprintf(record, sizeof(record),
            "{\"id\":%d,\"age\":%d,\"score\":%d,\"status\":\"%s\",\"region\":\"eu-west-1\"}\n",
            id, age, score, status);
        if (n <= 0) break;
        for (int i = 0; i < n && data.size() < targetSize; ++i)
            data.push_back(static_cast<uint8_t>(record[i]));
    }
    return data;
}

// Simulated x86-64 binary: repeated instruction preambles (push/pop/mov/call/ret)
// with varying operand bytes. Models executable sections.
static std::vector<uint8_t> makeBinaryExecutable(const size_t targetSize)
{
    std::mt19937 rng(33);
    std::vector<uint8_t> data;
    data.reserve(targetSize);
    // Common x86-64 instruction byte patterns
    const std::vector<std::vector<uint8_t>> templates = {
        {0x55, 0x48, 0x89, 0xE5},               // push rbp; mov rbp, rsp
        {0x48, 0x83, 0xEC, 0x00},               // sub rsp, imm8
        {0xE8, 0x00, 0x00, 0x00, 0x00},         // call rel32
        {0x48, 0x8B, 0x45, 0x00},               // mov rax, [rbp+disp8]
        {0x48, 0x89, 0x45, 0x00},               // mov [rbp+disp8], rax
        {0x5D, 0xC3},                           // pop rbp; ret
        {0x0F, 0x1F, 0x44, 0x00, 0x00},        // nop DWORD [rax+rax*1+0]
        {0x48, 0x31, 0xC0},                     // xor rax, rax
        {0x41, 0x57, 0x41, 0x56, 0x41, 0x55},  // push r15; push r14; push r13
    };
    while (data.size() < targetSize)
    {
        auto& tmpl = templates[rng() % templates.size()];
        for (size_t i = 0; i < tmpl.size() && data.size() < targetSize; ++i)
        {
            // Randomize operand bytes (last byte of multi-byte instructions)
            if (i == tmpl.size() - 1 && tmpl.size() > 2)
                data.push_back(static_cast<uint8_t>(rng() & 0xFF));
            else
                data.push_back(tmpl[i]);
        }
    }
    return data;
}

// Sorted delta-coded integers (columnar/database style).
// Each value is 4 bytes LE, values incrementing with small random deltas.
// Models time-series or sorted index data � highly compressible.
static std::vector<uint8_t> makeSortedIntegers(const size_t targetSize)
{
    std::mt19937 rng(11);
    std::vector<uint8_t> data;
    data.reserve(targetSize);
    uint32_t val = 1000000;
    while (data.size() + 4 <= targetSize)
    {
        val += static_cast<uint32_t>(1 + rng() % 16); // small delta
        data.push_back(static_cast<uint8_t>(val));
        data.push_back(static_cast<uint8_t>(val >> 8));
        data.push_back(static_cast<uint8_t>(val >> 16));
        data.push_back(static_cast<uint8_t>(val >> 24));
    }
    // Pad to exact size
    while (data.size() < targetSize)
        data.push_back(0);
    return data;
}

int main()
{
    // Helper to format size with appropriate units
    auto formatSize = [](const size_t bytes) -> std::string {
        if (bytes >= 1024 * 1024) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / (1024 * 1024)) << " MiB";
            return oss.str();
        } else if (bytes >= 1024) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / 1024) << " KiB";
            return oss.str();
        } else {
            return std::to_string(bytes) + " B";
        }
    };

    std::cout << "=== lz.huffman Multi-Stream Benchmark ===\n\n";

    const size_t sizes[] = {32768, 131072, 1048576, 4194304, 16777216};
    const char* sizeLabels[] = {"32 KiB", "128 KiB", "1 MiB", "4 MiB", "16 MiB"};
    const int iters[] = {20, 10, 5, 3, 1};

    struct DataGen
    {
        const char* name;
        std::vector<uint8_t> (*fn)(size_t);
    };

    DataGen generators[] = {
        {"Repeating text", makeRepeatingText},
        {"Skewed (90% one byte)", makeSkewed},
        {"Binary pattern", makeBinaryPattern},
        {"Random (incompressible)", makeRandom},
        {"Log text", makeLogText},
        {"JSON-like records", makeJsonLike},
        {"Simulated x86 binary", makeBinaryExecutable},
        {"Sorted integers", makeSortedIntegers},
    };

    // Header
    std::cout << std::left
              << std::setw(32) << "Data Type"
              << std::right
              << std::setw(14) << "Uncompressed"
              << std::setw(14) << "Compressed"
              << std::setw(10) << "Ratio"
              << std::setw(12) << "Enc ms"
              << std::setw(12) << "Dec ms"
              << std::setw(12) << "Enc MB/s"
              << std::setw(12) << "Dec MB/s"
              << "  OK"
              << "\n";
    std::cout << std::string(144, '-') << "\n";

    bool allPass = true;
    std::vector<BenchmarkResult> results;

    for (size_t gi = 0; gi < 8; ++gi)
    {
        for (size_t si = 0; si < 5; ++si)
        {
            auto data = generators[gi].fn(sizes[si]);
            std::string name = std::string(generators[gi].name) + " " + sizeLabels[si];
            auto r = runBench(name, data, iters[si]);
            results.push_back(r);

            std::string dataTypeLabel = name;

            std::cout << std::left << std::setw(32) << dataTypeLabel
                      << std::right << std::fixed
                      << std::setw(14) << formatSize(r.uncompressedSize)
                      << std::setw(14) << formatSize(r.compressedSize)
                      << std::setprecision(2) << std::setw(10) << r.getRatio() << ":1"
                      << std::setprecision(2) << std::setw(12) << r.compressionTime_ms
                      << std::setprecision(2) << std::setw(12) << r.decompressionTime_ms
                      << std::setprecision(1) << std::setw(12) << r.getCompressSpeed_MB_s()
                      << std::setprecision(1) << std::setw(12) << r.getDecompressSpeed_MB_s()
                      << "  " << (r.roundtrip ? "PASS" : "FAIL")
                      << "\n";

            if (!r.roundtrip) {
                std::cout << "FAIL: " << name << "\n";
                std::cout << "  Uncompressed size: " << r.uncompressedSize << "\n";
                std::cout << "  Compressed size:   " << r.compressedSize << "\n";
                std::cout << "  Compression ratio: " << r.getRatio() << ":1\n";
                std::cout << "  Compression time:  " << r.compressionTime_ms << " ms\n";
                std::cout << "  Decompression time:" << r.decompressionTime_ms << " ms\n";
                std::cout << std::flush;
            }
            assert(r.roundtrip);
        }
    }

    // Save current results to benchmark_results directory
    BenchmarkTracker tracker;

    // Resolve path relative to project source root (set by CMake)
#ifdef PROJECT_SOURCE_DIR
    std::string benchDir = std::string(PROJECT_SOURCE_DIR) + "/benchmark_results";
#else
    std::string benchDir = "benchmark_results";
#endif
    std::string currentFile = benchDir + "/current.csv";
    std::string baselineFile = benchDir + "/baseline.csv";
    std::string deltaFile = benchDir + "/delta_report.txt";

    tracker.saveResults(currentFile, results);
    std::cout << "\nBenchmark results saved to " << currentFile << "\n";

    // Try to load previous baseline and show delta
    auto previous = tracker.loadResults(baselineFile);
    if (!previous.empty())
    {
        tracker.printDeltaReport(results, previous);
        tracker.saveDeltaReport(deltaFile, results, previous);
        std::cout << "Delta report saved to " << deltaFile << "\n";
    }
    else
    {
        std::cout << "No baseline found (" << baselineFile << ").\n";
        std::cout << "To create baseline from current results:\n";
        std::cout << "  cd benchmark_results && copy current.csv baseline.csv\n";
    }

    tracker.printSummary(results);

    std::cout << "\n=== " << (allPass ? "All benchmarks passed!" : "FAILURES DETECTED") << " ===\n";


    return allPass ? 0 : 1;
}



