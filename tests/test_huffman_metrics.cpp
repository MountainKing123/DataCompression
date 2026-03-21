#include "huffman.h"
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>
#include <cassert>
#include <cmath>
#include <array>
#include <chrono>

// Calculate Shannon entropy of the data (0-8, where 8 = maximum entropy for bytes)
double calculateEntropy(const std::vector<uint8_t>& data)
{
    std::array<int, 256> freq = {};
    for (uint8_t byte : data) {
        freq[byte]++;
    }

    double entropy = 0.0;
    for (int count : freq) {
        if (count > 0) {
            double p = static_cast<double>(count) / data.size();
            entropy -= p * std::log2(p);
        }
    }
    return entropy;
}

// Count distinct bytes in data
int countDistinctBytes(const std::vector<uint8_t>& data)
{
    std::array<bool, 256> seen = {};
    int count = 0;
    for (uint8_t byte : data) {
        if (!seen[byte]) {
            seen[byte] = true;
            count++;
        }
    }
    return count;
}

struct CompressionMetrics {
    size_t originalSize;
    size_t compressedSize;
    double compressionRatio;      // original / compressed (e.g., 3.5:1)
    double percentCompressed;     // compressed / original * 100
    double bitsPerByte;          // (compressed * 8) / original
    double entropy;              // Shannon entropy (0-8)
    int distinctBytes;           // number of unique byte values
    double compressMs;           // compression wall time
    double decompressMs;         // decompression wall time
    bool roundtripOK;
};

CompressionMetrics analyzeCompression(const std::string& name, const std::vector<uint8_t>& input)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto compressed = Huffman::compress(input);
    const auto t1 = std::chrono::steady_clock::now();
    auto decompressed = Huffman::decompress(compressed);
    const auto t2 = std::chrono::steady_clock::now();

    CompressionMetrics metrics;
    metrics.originalSize = input.size();
    metrics.compressedSize = compressed.size();
    metrics.compressionRatio = static_cast<double>(input.size()) / static_cast<double>(compressed.size());
    metrics.percentCompressed = (100.0 * compressed.size()) / input.size();
    metrics.bitsPerByte = (compressed.size() * 8.0) / input.size();
    metrics.entropy = calculateEntropy(input);
    metrics.distinctBytes = countDistinctBytes(input);
    metrics.compressMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    metrics.decompressMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    metrics.roundtripOK = (input == decompressed);

    assert(metrics.roundtripOK);
    return metrics;
}

void printMetrics(const std::string& name, const CompressionMetrics& m)
{
    std::cout << "\n" << std::string(75, '-') << "\n";
    std::cout << "Test: " << name << "\n";
    std::cout << std::string(75, '-') << "\n";

    std::cout << std::left;
    std::cout << "Original Size:          " << std::right << std::setw(10) << m.originalSize << " bytes\n";
    std::cout << std::left << "Compressed Size:        " << std::right << std::setw(10) << m.compressedSize << " bytes\n";
    std::cout << std::left << "\nCompression Ratio:      " << std::right << std::setw(10) << std::fixed << std::setprecision(2) << m.compressionRatio << " :1\n";
    std::cout << std::left << "Space Used:             " << std::right << std::setw(10) << std::fixed << std::setprecision(2) << m.percentCompressed << " %\n";
    std::cout << std::left << "Bits Per Byte:          " << std::right << std::setw(10) << std::fixed << std::setprecision(3) << m.bitsPerByte << " bits\n";
    std::cout << std::left << "\nData Entropy:           " << std::right << std::setw(10) << std::fixed << std::setprecision(3) << m.entropy << " bits (max 8)\n";
    std::cout << std::left << "Distinct Bytes:         " << std::right << std::setw(10) << m.distinctBytes << " / 256\n";
    std::cout << std::left << "Theoretical Min Bits:   " << std::right << std::setw(10) << std::fixed << std::setprecision(3) << m.entropy << " bits/byte\n";

    // Efficiency: how close we got to theoretical minimum
    if (m.entropy > 0.01) {
        double efficiency = (m.entropy / m.bitsPerByte) * 100.0;
        std::cout << std::left << "Compression Efficiency: " << std::right << std::setw(10) << std::fixed << std::setprecision(1) << efficiency << " %\n";
    }
    std::cout << std::left << "Compress Time:          " << std::right << std::setw(10) << std::fixed << std::setprecision(3) << m.compressMs << " ms\n";
    std::cout << std::left << "Decompress Time:        " << std::right << std::setw(10) << std::fixed << std::setprecision(3) << m.decompressMs << " ms\n";

    std::cout << std::left << "Roundtrip Validation:   " << std::right << std::setw(10) << (m.roundtripOK ? "OK" : "FAILED") << "\n";
}

void printSummaryTable()
{
    std::cout << "\n" << std::string(150, '=') << "\n";
    std::cout << std::left << std::setw(34) << "Test Name"
              << std::setw(12) << "| Original"
              << std::setw(12) << "| Compressed"
              << std::setw(10) << "| Ratio"
              << std::setw(10) << "| Entropy"
              << std::setw(10) << "| C(ms)"
              << std::setw(10) << "| D(ms)"
              << std::setw(10) << "| Distinct"
              << "\n";
    std::cout << std::string(140, '-') << "\n";
}

void printSummaryRow(const std::string& name, const CompressionMetrics& m)
{
    std::cout << std::left << std::setw(34) << name;
    std::cout << std::setw(12) << ("| " + std::to_string(m.originalSize));
    std::cout << std::setw(12) << ("| " + std::to_string(m.compressedSize));
    std::cout << std::setw(10) << ("| " + std::to_string(m.compressionRatio).substr(0, 4) + ":1");
    std::cout << std::setw(10) << ("| " + std::to_string(m.entropy).substr(0, 4));
    std::cout << std::setw(10) << ("| " + std::to_string(m.compressMs).substr(0, 5));
    std::cout << std::setw(10) << ("| " + std::to_string(m.decompressMs).substr(0, 5));
    std::cout << std::setw(10) << ("| " + std::to_string(m.distinctBytes));
    std::cout << "\n";
}

int main()
{
    std::cout << "=== Huffman Compression Analysis with Metrics ===\n\n";
    std::cout << "Demonstrates compression performance and data entropy analysis\n";
    std::cout << std::string(130, '=') << "\n\n";

    std::vector<std::pair<std::string, CompressionMetrics>> results;

    // Test 1: Single byte (highly redundant)
    std::vector<uint8_t> t1(50000, 0x42);
    results.push_back({"Single byte repeated (50K)", analyzeCompression("Single byte repeated (50K)", t1)});

    // Test 2: Two bytes alternating
    std::vector<uint8_t> t2;
    for (int i = 0; i < 25000; ++i) {
        t2.push_back(0xAA);
        t2.push_back(0x55);
    }
    results.push_back({"Two bytes alternating (25K)", analyzeCompression("Two bytes alternating (25K)", t2)});

    // Test 3: Four distinct values
    std::vector<uint8_t> t3;
    for (int i = 0; i < 10000; ++i) {
        t3.push_back(static_cast<uint8_t>(i % 4));
    }
    results.push_back({"Four distinct bytes (10K)", analyzeCompression("Four distinct bytes (10K)", t3)});

    // Test 4: English text
    std::vector<uint8_t> t4;
    std::string english = "The quick brown fox jumps over the lazy dog. ";
    for (int i = 0; i < 300; ++i) {
        for (char c : english) t4.push_back(static_cast<uint8_t>(c));
    }
    results.push_back({"English text repeated (300x)", analyzeCompression("English text repeated (300x)", t4)});

    // Test 5: 16 distinct values
    std::vector<uint8_t> t5;
    for (int i = 0; i < 10000; ++i) {
        t5.push_back(static_cast<uint8_t>((i / 100) % 16));
    }
    results.push_back({"16 distinct bytes (10K)", analyzeCompression("16 distinct bytes (10K)", t5)});

    // Test 6: Uniform distribution
    std::vector<uint8_t> t6;
    for (int i = 0; i < 10000; ++i) {
        t6.push_back(static_cast<uint8_t>(i % 256));
    }
    results.push_back({"Uniform distribution (10K)", analyzeCompression("Uniform distribution (10K)", t6)});

    // Test 7: Pseudo-random
    std::vector<uint8_t> t7;
    for (int i = 0; i < 1000; ++i) {
        t7.push_back(static_cast<uint8_t>(i * 37 % 256));
    }
    results.push_back({"Pseudo-random bytes (1K)", analyzeCompression("Pseudo-random bytes (1K)", t7)});

    // Test 8: Edge case - single byte
    std::vector<uint8_t> t8 = {0x42};
    results.push_back({"Single byte", analyzeCompression("Single byte", t8)});

    // Test 9: Two different bytes
    std::vector<uint8_t> t9 = {0x00, 0xFF};
    results.push_back({"Two different bytes", analyzeCompression("Two different bytes", t9)});

    // Print detailed analysis for each test
    std::cout << "\n" << std::string(130, '=') << "\n";
    std::cout << "DETAILED ANALYSIS PER TEST\n";
    std::cout << std::string(130, '=') << "\n";

    for (const auto& [name, metrics] : results) {
        printMetrics(name, metrics);
    }

    // Print summary table
    std::cout << "\n\n";
    printSummaryTable();
    for (const auto& [name, metrics] : results) {
        printSummaryRow(name, metrics);
    }
    std::cout << std::string(130, '=') << "\n";

    // Print insights
    std::cout << "\nKEY INSIGHTS:\n";
    std::cout << "- Entropy measures data randomness (0-8 bits). Lower entropy = easier to compress\n";
    std::cout << "- Compression efficiency shows how close we got to the theoretical minimum\n";
    std::cout << "- Bits per byte shows actual compressed size in bits per original byte\n";
    std::cout << "- Distinct bytes affect compression: few unique bytes = better compression\n";
    std::cout << "\nAll tests passed! Compression works correctly across all data patterns.\n";

    return 0;
}


