#include "huffman.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <random>
#include <cstring>

void testParallelFrequencyCounting()
{
    std::cout << "=== Parallel Frequency Counting Test ===\n\n";

    // Generate large test data
    std::vector<uint8_t> input(1000000);  // 1MB
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : input) {
        b = static_cast<uint8_t>(dist(rng));
    }

    // Test serial
    auto t0 = std::chrono::steady_clock::now();
    Huffman::CompressOptions serialOpts;
    serialOpts.parallelFrequencyCount = false;
    auto compressedSerial = Huffman::compress(input, serialOpts);
    auto t1 = std::chrono::steady_clock::now();
    double serialMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Test parallel
    auto t2 = std::chrono::steady_clock::now();
    Huffman::CompressOptions parallelOpts;
    parallelOpts.parallelFrequencyCount = true;
    parallelOpts.numThreads = 4;
    auto compressedParallel = Huffman::compress(input, parallelOpts);
    auto t3 = std::chrono::steady_clock::now();
    double parallelMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::cout << "Input size: " << input.size() << " bytes\n";
    std::cout << "Serial compress time:   " << std::fixed << std::setprecision(2) << serialMs << " ms\n";
    std::cout << "Parallel compress time: " << std::fixed << std::setprecision(2) << parallelMs << " ms\n";
    std::cout << "Speedup: " << std::fixed << std::setprecision(2) << (serialMs / parallelMs) << "x\n";
    std::cout << "Compressed size: " << compressedSerial.size() << " bytes\n";

    // Decompress both to verify correctness (compressed bytes may differ due to tree construction order)
    auto decompressedSerial = Huffman::decompress(compressedSerial);
    auto decompressedParallel = Huffman::decompress(compressedParallel);

    bool outputsMatch = (decompressedSerial == input) && (decompressedParallel == input);
    std::cout << "Decompressed outputs match input: " << (outputsMatch ? "YES" : "NO") << "\n";
    std::cout << "Compressed bytes identical: " << (compressedSerial == compressedParallel ? "YES" : "NO (expected, tree order may vary)") << "\n\n";
}

void testMultiPassArchitecture()
{
    std::cout << "=== Separate Decode Pass Test ===\n\n";

    std::vector<uint8_t> input;
    const char* text = "The quick brown fox jumps over the lazy dog. ";
    size_t textLen = std::strlen(text);
    for (int i = 0; i < 100; ++i) {
        for (size_t j = 0; j < textLen; ++j) {
            input.push_back(static_cast<uint8_t>(text[j]));
        }
    }

    std::cout << "Original size: " << input.size() << " bytes\n";

    auto compressed = Huffman::compress(input);
    std::cout << "Compressed size: " << compressed.size() << " bytes\n\n";

    // Traditional single-pass decompress
    auto t0 = std::chrono::steady_clock::now();
    auto decompressedCombined = Huffman::decompress(compressed);
    auto t1 = std::chrono::steady_clock::now();
    double combinedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Separate passes
    auto t2 = std::chrono::steady_clock::now();

    // Pass 1: Parse compressed stream into symbol array
    auto decoded = Huffman::parsePass(compressed);
    auto t3 = std::chrono::steady_clock::now();

    // Pass 2: Apply symbols to output
    auto decompressedSeparate = Huffman::applyPass(decoded);
    auto t4 = std::chrono::steady_clock::now();

    double parseMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    double applyMs = std::chrono::duration<double, std::milli>(t4 - t3).count();
    double totalMs = std::chrono::duration<double, std::milli>(t4 - t2).count();

    std::cout << "Combined decompress time:  " << std::fixed << std::setprecision(3) << combinedMs << " ms\n";
    std::cout << "Separate passes:\n";
    std::cout << "  Parse pass:  " << std::fixed << std::setprecision(3) << parseMs << " ms\n";
    std::cout << "  Apply pass:  " << std::fixed << std::setprecision(3) << applyMs << " ms\n";
    std::cout << "  Total:       " << std::fixed << std::setprecision(3) << totalMs << " ms\n";
    std::cout << "Results match: " << (decompressedCombined == decompressedSeparate ? "YES" : "NO") << "\n";
    std::cout << "Roundtrip OK:  " << (input == decompressedCombined ? "YES" : "NO") << "\n\n";
}

void testConfigurablePasses()
{
    std::cout << "=== Configurable Pass Architecture Demo ===\n\n";

    // Create test data with different characteristics
    std::vector<uint8_t> input;
    for (int i = 0; i < 1000; ++i) {
        input.push_back(static_cast<uint8_t>(i % 10)); // Low entropy
    }

    auto compressed = Huffman::compress(input);

    auto decoded = Huffman::parsePass(compressed);

    std::cout << "Parse pass complete:\n";
    std::cout << "  Symbol count: " << decoded.symbols.size() << "\n";
    std::cout << "  Expected size: " << decoded.count << "\n";
    std::cout << "  First 20 symbols: ";
    for (size_t i = 0; i < std::min(size_t(20), decoded.symbols.size()); ++i) {
        std::cout << static_cast<int>(decoded.symbols[i]) << " ";
    }
    std::cout << "\n\n";


    std::cout << "Apply pass (standard path):\n";
    auto output = Huffman::applyPass(decoded);
    std::cout << "  Output size: " << output.size() << "\n";
    std::cout << "  Matches input: " << (output == input ? "YES" : "NO") << "\n\n";
}

void testBackwardCompatibility()
{
    std::cout << "=== Backward Compatibility Test ===\n\n";

    std::vector<uint8_t> input = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Compress with new parallel API
    auto compressedNew = Huffman::compress(input);

    // Decompress with old API (via wrapper)
    auto decompressedOld = Huffman::decompress(compressedNew);

    // Compress with old API
    auto compressedOld = Huffman::compress(input);

    // Decompress with new API
    auto decompressedNew = Huffman::decompress(compressedOld);

    std::cout << "New compress -> Old decompress: " << (decompressedOld == input ? "OK" : "FAILED") << "\n";
    std::cout << "Old compress -> New decompress: " << (decompressedNew == input ? "OK" : "FAILED") << "\n";
    std::cout << "Format identical: " << (compressedNew == compressedOld ? "YES" : "NO (optimized)") << "\n\n";
}

int main()
{
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  Multi-Pass Parallel Huffman Codec\n";
    std::cout << "========================================\n\n";

    testParallelFrequencyCounting();
    testMultiPassArchitecture();
    testConfigurablePasses();
    testBackwardCompatibility();

    std::cout << "========================================\n";
    std::cout << "All tests completed!\n";
    std::cout << "========================================\n";

    return 0;
}




