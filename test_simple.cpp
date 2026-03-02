#include "huffman.h"
#include "bitstream.h"
#include <iostream>
#include <vector>
#include <random>

int main()
{
    // Generate random input
    std::vector<uint8_t> input(100);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : input)
        b = static_cast<uint8_t>(dist(rng));

    std::cout << "Input last 10 bytes: ";
    for (size_t i = input.size() - 10; i < input.size(); ++i) {
        std::cout << std::hex << (int)input[i] << " ";
    }
    std::cout << "\n";

    // Compress
    std::vector<uint8_t> compressed = Huffman::compress(input);
    std::cout << "Compressed size: " << compressed.size() << " bytes\n";

    // Decompress
    std::vector<uint8_t> decompressed = Huffman::decompress(compressed);

    std::cout << "Decompressed last 10 bytes: ";
    for (size_t i = decompressed.size() - 10; i < decompressed.size(); ++i) {
        std::cout << std::hex << (int)decompressed[i] << " ";
    }
    std::cout << "\n";

    // Check sizes
    std::cout << std::dec << "Input size: " << input.size() << ", Decompressed size: " << decompressed.size() << "\n";

    // Find first difference
    for (size_t i = 0; i < std::max(input.size(), decompressed.size()); ++i) {
        if (i >= input.size() || i >= decompressed.size() || input[i] != decompressed[i]) {
            std::cout << "First difference at byte " << i << ": expected 0x"
                      << std::hex << (int)input[i] << ", got 0x" << (int)decompressed[i] << "\n";
            break;
        }
    }

    return (input == decompressed) ? 0 : 1;
}

