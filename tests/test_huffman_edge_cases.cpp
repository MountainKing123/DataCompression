#include "huffman.h"
#include <iostream>
#include <vector>
#include <iomanip>
#include <cassert>
#include <chrono>

int main()
{
    // Test 1: Edge case - Single byte
    std::cout << "=== TEST 1: Single byte ===\n";
    std::vector<uint8_t> input1 = {0x42};
    const auto t1c0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> compressed1 = Huffman::compress(input1);
    const auto t1c1 = std::chrono::steady_clock::now();
    std::vector<uint8_t> decompressed1 = Huffman::decompress(compressed1);
    const auto t1d1 = std::chrono::steady_clock::now();
    assert(input1 == decompressed1);
    std::cout << "Original: " << input1.size() << " bytes, Compressed: " << compressed1.size() << " bytes\n";
    std::cout << "Time C/D: " << std::fixed << std::setprecision(3)
              << std::chrono::duration<double, std::milli>(t1c1 - t1c0).count() << " / "
              << std::chrono::duration<double, std::milli>(t1d1 - t1c1).count() << " ms\n";
    std::cout << "Roundtrip: OK\n\n";

    // Test 2: Edge case - All same byte
    std::cout << "=== TEST 2: All same byte ===\n";
    std::vector<uint8_t> input2(1000, 0xFF);
    const auto t2c0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> compressed2 = Huffman::compress(input2);
    const auto t2c1 = std::chrono::steady_clock::now();
    std::vector<uint8_t> decompressed2 = Huffman::decompress(compressed2);
    const auto t2d1 = std::chrono::steady_clock::now();
    assert(input2 == decompressed2);
    std::cout << "Original: " << input2.size() << " bytes, Compressed: " << compressed2.size() << " bytes\n";
    std::cout << "Ratio: " << std::fixed << std::setprecision(2) << (100.0 * compressed2.size() / input2.size()) << "%\n";
    std::cout << "Time C/D: " << std::fixed << std::setprecision(3)
              << std::chrono::duration<double, std::milli>(t2c1 - t2c0).count() << " / "
              << std::chrono::duration<double, std::milli>(t2d1 - t2c1).count() << " ms\n";
    std::cout << "Roundtrip: OK\n\n";

    // Test 3: Edge case - Two alternating bytes
    std::cout << "=== TEST 3: Two alternating bytes ===\n";
    std::vector<uint8_t> input3;
    for (int i = 0; i < 500; ++i) {
        input3.push_back(0xAA);
        input3.push_back(0x55);
    }
    const auto t3c0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> compressed3 = Huffman::compress(input3);
    const auto t3c1 = std::chrono::steady_clock::now();
    std::vector<uint8_t> decompressed3 = Huffman::decompress(compressed3);
    const auto t3d1 = std::chrono::steady_clock::now();
    assert(input3 == decompressed3);
    std::cout << "Original: " << input3.size() << " bytes, Compressed: " << compressed3.size() << " bytes\n";
    std::cout << "Ratio: " << std::fixed << std::setprecision(2) << (100.0 * compressed3.size() / input3.size()) << "%\n";
    std::cout << "Time C/D: " << std::fixed << std::setprecision(3)
              << std::chrono::duration<double, std::milli>(t3c1 - t3c0).count() << " / "
              << std::chrono::duration<double, std::milli>(t3d1 - t3c1).count() << " ms\n";
    std::cout << "Roundtrip: OK\n\n";

    // Test 4: Edge case - Empty data should still work
    std::cout << "=== TEST 4: Minimal sequential data ===\n";
    std::vector<uint8_t> input4 = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    const auto t4c0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> compressed4 = Huffman::compress(input4);
    const auto t4c1 = std::chrono::steady_clock::now();
    std::vector<uint8_t> decompressed4 = Huffman::decompress(compressed4);
    const auto t4d1 = std::chrono::steady_clock::now();
    assert(input4 == decompressed4);
    std::cout << "Original: " << input4.size() << " bytes, Compressed: " << compressed4.size() << " bytes\n";
    std::cout << "Time C/D: " << std::fixed << std::setprecision(3)
              << std::chrono::duration<double, std::milli>(t4c1 - t4c0).count() << " / "
              << std::chrono::duration<double, std::milli>(t4d1 - t4c1).count() << " ms\n";
    std::cout << "Roundtrip: OK\n\n";

    std::cout << "All edge case tests passed!\n";
    return 0;
}
