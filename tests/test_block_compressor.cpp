#include "compressor.h"
#include <iostream>
#include <vector>
#include <random>
#include <iomanip>
#include <cassert>

using namespace compression;

int main()
{
    std::cout << "=== Compressor API Test ===\n\n";

    // Test 1: Custom chunk size
    std::cout << "Test 1: Repeating text (32 KiB chunks)\n";
    {
        CompressOptions opts;
        opts.chunkSize = 32 * 1024;

        std::vector<Byte> input;
        const char* pattern = "The quick brown fox jumps over the lazy dog. ";
        for (int i = 0; i < 2000; ++i)
            for (const char* p = pattern; *p; ++p)
                input.push_back(static_cast<Byte>(*p));

        auto compressed = compress(input, opts);
        auto decompressed = decompress(compressed);

        std::cout << "  Original size:     " << input.size() << " bytes\n";
        std::cout << "  Compressed size:   " << compressed.size() << " bytes\n";
        double ratio = static_cast<double>(input.size()) / static_cast<double>(compressed.size());
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";

        bool match = (input == decompressed);
        std::cout << "  Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 2: Highly skewed data
    std::cout << "Test 2: Highly skewed data (90% one byte)\n";
    {
        CompressOptions opts;
        opts.chunkSize = 64 * 1024;

        std::vector<Byte> input(100 * 1024);
        std::mt19937 rng(11111); // NOLINT(cert-msc51-cpp) - constant seed for reproducible tests
        std::uniform_int_distribution<int> dist(0, 99);

        for (auto& b : input)
            b = (dist(rng) < 90) ? 0xAA : static_cast<Byte>(dist(rng) % 256);

        auto compressed = compress(input, opts);
        auto decompressed = decompress(compressed);

        std::cout << "  Original size:     " << input.size() << " bytes\n";
        std::cout << "  Compressed size:   " << compressed.size() << " bytes\n";
        double ratio = static_cast<double>(input.size()) / static_cast<double>(compressed.size());
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";

        bool match = (input == decompressed);
        std::cout << "  Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 3: Default options
    std::cout << "Test 3: Default options (sequential pattern)\n";
    {
        std::vector<Byte> input;
        for (int i = 0; i < 50000; ++i)
            input.push_back(static_cast<Byte>(i % 256));

        auto compressed = compress(input);
        auto decompressed = decompress(compressed);

        std::cout << "  Original size:     " << input.size() << " bytes\n";
        std::cout << "  Compressed size:   " << compressed.size() << " bytes\n";
        double ratio = static_cast<double>(input.size()) / static_cast<double>(compressed.size());
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";

        bool match = (input == decompressed);
        std::cout << "  Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 4: Low entropy (4 distinct bytes)
    std::cout << "Test 4: Low entropy (4 distinct bytes)\n";
    {
        std::vector<Byte> input(75 * 1024);
        std::mt19937 rng(22222); // NOLINT(cert-msc51-cpp) - constant seed for reproducible tests
        std::uniform_int_distribution<int> dist(0, 3);
        uint8_t bytes[] = {0x00, 0x55, 0xAA, 0xFF};

        for (auto& b : input)
            b = bytes[dist(rng)];

        auto compressed = compress(input);
        auto decompressed = decompress(compressed);

        std::cout << "  Original size:     " << input.size() << " bytes\n";
        std::cout << "  Compressed size:   " << compressed.size() << " bytes\n";
        double ratio = static_cast<double>(input.size()) / static_cast<double>(compressed.size());
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";

        bool match = (input == decompressed);
        std::cout << "  Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 5: Multi-chunk (512 KiB, 128 KiB chunks)
    std::cout << "Test 5: Multi-chunk (512 KiB, 128 KiB chunks)\n";
    {
        std::vector<Byte> input;
        const char* text = "ABCDEFGHIJKLMNOP";
        for (size_t i = 0; i < 512 * 1024; ++i)
            input.push_back(static_cast<Byte>(text[i % 16]));

        auto compressed = compress(input);
        auto decompressed = decompress(compressed);

        std::cout << "  Original size:     " << input.size() << " bytes\n";
        std::cout << "  Compressed size:   " << compressed.size() << " bytes\n";
        double ratio = static_cast<double>(input.size()) / static_cast<double>(compressed.size());
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";

        bool match = (input == decompressed);
        std::cout << "  Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 6: Empty input
    std::cout << "Test 6: Empty input\n";
    {
        std::vector<Byte> input;
        auto compressed = compress(input);
        auto decompressed = decompress(compressed);

        std::cout << "  Compressed size: " << compressed.size() << " bytes\n";
        bool match = (decompressed.empty());
        std::cout << "  Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    std::cout << "=== All compressor API tests passed! ===\n";
    return 0;
}

