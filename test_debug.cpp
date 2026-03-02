#include "huffman.h"
#include "bitstream.h"

#include <iostream>
#include <vector>
#include <random>
#include <iomanip>

int main()
{
    // Generate random input
    std::vector<uint8_t> input(100);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : input)
        b = static_cast<uint8_t>(dist(rng));

    // Compress
    std::vector<uint8_t> compressed = Huffman::compress(input);

    // Decompress
    std::vector<uint8_t> decompressed = Huffman::decompress(compressed);

    // Compare
    if (input == decompressed) {
        std::cout << "SUCCESS: Roundtrip OK!\n";
        std::cout << "Original size: " << input.size() << " bytes\n";
        std::cout << "Compressed size: " << compressed.size() << " bytes\n";
        return 0;
    } else {
        std::cout << "FAILURE: Decompressed data doesn't match!\n";
        std::cout << "First difference at byte ";
        for (size_t i = 0; i < std::max(input.size(), decompressed.size()); ++i) {
            if (i >= input.size() || i >= decompressed.size() || input[i] != decompressed[i]) {
                std::cout << i << "\n";
                std::cout << "Expected: 0x" << std::hex << (int)input[i] << "\n";
                std::cout << "Got:      0x" << std::hex << (int)decompressed[i] << "\n";
                return 1;
            }
        }
    }
    return 0;
}
