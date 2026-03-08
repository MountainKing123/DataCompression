#include "compressor.h"
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

struct BenchResult
{
    std::string name;
    size_t inputSize = 0;
    size_t compressedSize = 0;
    double compressMs = 0;
    double decompressMs = 0;
    bool roundtrip = false;
};

static BenchResult runBench(const std::string& name,
                            const std::vector<uint8_t>& input,
                            int iterations = 5)
{
    BenchResult r;
    r.name = name;
    r.inputSize = input.size();

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
    r.compressMs = bestCompress;
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
    r.decompressMs = bestDecompress;
    r.roundtrip = (output == input);

    return r;
}

static std::vector<uint8_t> makeRepeatingText(size_t targetSize)
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

static std::vector<uint8_t> makeSkewed(size_t targetSize)
{
    std::mt19937 rng(42);
    std::vector<uint8_t> data(targetSize);
    for (auto& b : data)
    {
        b = (rng() % 100 < 90) ? 0xAA : static_cast<uint8_t>(rng() & 0xFF);
    }
    return data;
}

static std::vector<uint8_t> makeBinaryPattern(size_t targetSize)
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

static std::vector<uint8_t> makeRandom(size_t targetSize)
{
    std::mt19937 rng(99);
    std::vector<uint8_t> data(targetSize);
    for (auto& b : data)
        b = static_cast<uint8_t>(rng() & 0xFF);
    return data;
}

int main()
{
    // Helper to format size with appropriate units
    auto formatSize = [](size_t bytes) -> std::string {
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

    std::cout << "=== LZ77+Huffman Multi-Stream Benchmark ===\n\n";

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

    for (size_t gi = 0; gi < 4; ++gi)
    {
        for (size_t si = 0; si < 5; ++si)
        {
            auto data = generators[gi].fn(sizes[si]);
            auto r = runBench(generators[gi].name, data, iters[si]);

            double ratio = static_cast<double>(r.inputSize) / static_cast<double>(r.compressedSize);
            double cMBs = (static_cast<double>(r.inputSize) / (1024.0 * 1024.0)) / (r.compressMs / 1000.0);
            double dMBs = (static_cast<double>(r.inputSize) / (1024.0 * 1024.0)) / (r.decompressMs / 1000.0);

            std::string dataTypeLabel = std::string(generators[gi].name) + " " + sizeLabels[si];

            std::cout << std::left << std::setw(32) << dataTypeLabel
                      << std::right << std::fixed
                      << std::setw(14) << formatSize(r.inputSize)
                      << std::setw(14) << formatSize(r.compressedSize)
                      << std::setprecision(2) << std::setw(10) << ratio << ":1"
                      << std::setprecision(2) << std::setw(12) << r.compressMs
                      << std::setprecision(2) << std::setw(12) << r.decompressMs
                      << std::setprecision(1) << std::setw(12) << cMBs
                      << std::setprecision(1) << std::setw(12) << dMBs
                      << "  " << (r.roundtrip ? "PASS" : "FAIL")
                      << "\n";

            if (!r.roundtrip) allPass = false;
            assert(r.roundtrip);
        }
    }

    std::cout << "\n=== " << (allPass ? "All benchmarks passed!" : "FAILURES DETECTED") << " ===\n";


    return allPass ? 0 : 1;
}






