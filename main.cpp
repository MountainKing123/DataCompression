#include "huffman.h"
#include "bitstream.h"

#include <iostream>
#include <vector>
#include <random>
#include <iomanip>
#include <cassert>

void printHex(const std::vector<uint8_t>& data)
{
    for (size_t i = 0; i < data.size(); ++i)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    if (data.size() % 16 != 0) std::cout << "\n";
}

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

    // Print info
    std::cout << "Original size:   " << input.size() << " bytes\n";
    std::cout << "Original data:\n";
    printHex(input);

    std::cout << "Compressed size: " << compressed.size() << " bytes\n";
    std::cout << "Compressed data:\n";
    printHex(compressed);

    std::cout << "Decompressed size: " << decompressed.size() << " bytes\n";
    std::cout << "Decompressed data:\n";
    printHex(decompressed);

    std::cout << std::endl; //Flush for debugging.

    // Validate roundtrip
    assert(input == decompressed);
    std::cout << "Roundtrip OK!\n";

    return 0;
}