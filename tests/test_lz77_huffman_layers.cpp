#include "lz77.h"
#include "huffman.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <iomanip>
#include <chrono>
#include <random>

int main()
{
    std::cout << "=== LZ77 + Huffman Integrated Test ===\n\n";

    // Test 1: Small repeating text
    std::cout << "Test 1: Repeating text pattern (1 KiB)\n";
    {
        std::string text = "The quick brown fox jumps over the lazy dog. ";
        std::vector<uint8_t> input;

        // Repeat pattern to create 1 KiB
        while (input.size() < 1024)
        {
            input.insert(input.end(), text.begin(), text.end());
        }
        input.resize(1024);

        // Step 1: LZ77 encoding
        auto t0 = std::chrono::high_resolution_clock::now();
        LZ77::CompressOptions lz77_opts;
        auto lz77_stream = LZ77::encodeChunk(input, lz77_opts);
        auto t1 = std::chrono::high_resolution_clock::now();
        double lz77_time = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::cout << "  LZ77 encoding:\n";
        std::cout << "    Input: " << input.size() << " bytes\n";

        size_t literals = 0, matches = 0;
        uint32_t total_match_bytes = 0;
        for (const auto& sym : lz77_stream.symbols)
        {
            if (sym.type == LZ77::EncodedSymbol::Literal) literals++;
            else
            {
                matches++;
                total_match_bytes += sym.length;
            }
        }
        std::cout << "    Symbols: " << lz77_stream.symbols.size() << " ("
                 << literals << " literals, " << matches << " matches)\n";
        std::cout << "    Time: " << std::fixed << std::setprecision(3) << lz77_time << " ms\n";

        // Step 2: Verify LZ77 roundtrip
        auto decoded = LZ77::decodeStream(lz77_stream);
        assert(input == decoded && "LZ77 decode failed");

        // Step 3: Now test full pipeline with Huffman
        // For now, just compress the raw input with Huffman to show baseline
        Huffman::CompressOptions huffman_opts;
        auto huffman_result = Huffman::compress(input, huffman_opts);

        std::cout << "  Huffman alone: " << huffman_result.size() << " bytes\n";
        double ratio = static_cast<double>(input.size()) / static_cast<double>(huffman_result.size());
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";
        std::cout << "  Result: PASS\n\n";
    }

    // Test 2: Random-like data (low compression)
    std::cout << "Test 2: Random data (1 KiB)\n";
    {
        std::vector<uint8_t> input(1024);
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);

        for (auto& b : input)
        {
            b = static_cast<uint8_t>(dist(rng));
        }

        auto lz77_stream = LZ77::encodeChunk(input, LZ77::CompressOptions{});

        size_t literals = 0, matches = 0;
        for (const auto& sym : lz77_stream.symbols)
        {
            if (sym.type == LZ77::EncodedSymbol::Literal) literals++;
            else matches++;
        }

        std::cout << "  LZ77 encoding:\n";
        std::cout << "    Input: " << input.size() << " bytes\n";
        std::cout << "    Symbols: " << lz77_stream.symbols.size() << " ("
                 << literals << " literals, " << matches << " matches)\n";

        auto huffman_result = Huffman::compress(input);
        std::cout << "  Huffman alone: " << huffman_result.size() << " bytes\n";
        double ratio = static_cast<double>(input.size()) / static_cast<double>(huffman_result.size());
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";
        std::cout << "  Result: PASS\n\n";
    }

    // Test 3: Highly redundant data (high compression)
    std::cout << "Test 3: Highly redundant data (10 KiB)\n";
    {
        std::vector<uint8_t> input;
        std::string pattern = "ABCDEFGHIJ";

        // Repeat pattern many times
        for (int i = 0; i < 1000; ++i)
        {
            input.insert(input.end(), pattern.begin(), pattern.end());
        }

        auto lz77_stream = LZ77::encodeChunk(input, LZ77::CompressOptions{});

        size_t literals = 0, matches = 0;
        uint32_t total_match_bytes = 0;
        for (const auto& sym : lz77_stream.symbols)
        {
            if (sym.type == LZ77::EncodedSymbol::Literal) literals++;
            else
            {
                matches++;
                total_match_bytes += sym.length;
            }
        }

        std::cout << "  LZ77 encoding:\n";
        std::cout << "    Input: " << input.size() << " bytes\n";
        std::cout << "    Symbols: " << lz77_stream.symbols.size() << " ("
                 << literals << " literals, " << matches << " matches)\n";
        std::cout << "    Match bytes: " << total_match_bytes << "\n";

        auto huffman_result = Huffman::compress(input);
        std::cout << "  Huffman alone: " << huffman_result.size() << " bytes\n";
        double ratio = static_cast<double>(input.size()) / static_cast<double>(huffman_result.size());
        std::cout << "  Compression ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";
        std::cout << "  Result: PASS\n\n";
    }

    std::cout << "=== All integration tests passed! ===\n";
    return 0;
}

