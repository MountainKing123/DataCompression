#include "compressor.h"

#include <iostream>
#include <vector>
#include <random>
#include <iomanip>
#include <cassert>

void printHex(const std::vector<uint8_t>& data, const size_t maxBytes = 256)
{
    size_t limit = std::min(data.size(), maxBytes);
    for (size_t i = 0; i < limit; ++i)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(data[i]) << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    if (limit % 16 != 0) std::cout << "\n";
    if (data.size() > maxBytes)
        std::cout << "... (" << (data.size() - maxBytes) << " more bytes)\n";
    std::cout << std::dec;
}

int main()
{
    // Generate test data: repeating text pattern
    std::string sentence = "The quick brown fox jumps over the lazy dog. ";
    std::vector<uint8_t> input;
    for (int i = 0; i < 500; ++i)
        for (char c : sentence)
            input.push_back(static_cast<uint8_t>(c));

    // Compress
    compression::CompressOptions opts;
    opts.chunkSize = 32 * 1024;
    const auto compressed = compression::compress(input, opts);

    // Decompress
    const auto decompressed = compression::decompress(compressed);

    // Print info
    std::cout << "=== LZ77+Huffman Token-Based Compression ===\n\n";
    std::cout << "Original size:     " << input.size() << " bytes\n";
    std::cout << "Compressed size:   " << compressed.size() << " bytes\n";
    double ratio = static_cast<double>(input.size()) / static_cast<double>(compressed.size());
    std::cout << "Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n\n";

    std::cout << "Compressed data (first 64 bytes):\n";
    printHex(compressed, 64);

    bool match = (input == decompressed);
    std::cout << "\nVerification:      " << (match ? "PASS" : "FAIL") << "\n";

    assert(match);
    return 0;
}
