#include "huffman.h"
#include <iostream>
#include <vector>
#include <random>
#include <cassert>

int main() {
    // Generate random input
    std::vector<uint8_t> input(10000);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);
    for(auto& b : input)
        b = static_cast<uint8_t>(dist(rng));

    // Compress
    auto compressed = Huffman::compress(input);

    // Decompress
    auto decompressed = Huffman::decompress(compressed);

    // Validate roundtrip
    assert(input == decompressed);

    std::cout << "Original size:   " << input.size() << " bytes\n";
    std::cout << "Compressed size: " << compressed.size() << " bytes\n";
    std::cout << "Roundtrip OK!\n";

    return 0;
}