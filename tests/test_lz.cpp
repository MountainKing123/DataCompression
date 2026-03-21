#include "lz.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <iomanip>

void printSymbols(const LZ::IntermediateStream& stream, const size_t maxShow = 20)
{
    std::cout << "Symbols (" << stream.symbols.size() << " total):\n";
    for (size_t i = 0; i < std::min(maxShow, stream.symbols.size()); ++i)
    {
        const auto& sym = stream.symbols[i];
        if (sym.type == LZ::EncodedSymbol::Literal)
        {
            std::cout << "  [" << i << "] Literal: " << std::hex << (int)sym.literal << std::dec << "\n";
        }
        else
        {
            std::cout << "  [" << i << "] Match: distance=" << sym.distance
                     << ", length=" << sym.length << "\n";
        }
    }
    if (stream.symbols.size() > maxShow)
    {
        std::cout << "  ... " << (stream.symbols.size() - maxShow) << " more symbols\n";
    }
}

int main()
{
    std::cout << "=== LZ77 Compression Test ===\n\n";

    // Test 1: Simple repeating pattern
    std::cout << "Test 1: Repeating pattern\n";
    {
        std::vector<uint8_t> input{0xAA, 0xBB, 0xCC, 0xAA, 0xBB, 0xCC, 0xAA, 0xBB, 0xCC};

        std::cout << "Input: ";
        for (auto b : input) std::cout << std::hex << (int)b << " ";
        std::cout << std::dec << "\n";

        LZ::CompressOptions opts;
        auto stream = LZ::encodeChunk(input, opts);

        std::cout << "Uncompressed size: " << stream.uncompressedSize << "\n";
        printSymbols(stream);

        auto decoded = LZ::decodeStream(stream);
        bool match = (input == decoded);
        std::cout << "Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 2: Text pattern
    std::cout << "Test 2: Text pattern\n";
    {
        std::string text = "The quick brown fox jumps over the lazy dog. ";
        std::vector<uint8_t> input(text.begin(), text.end());

        // Repeat pattern 3 times
        std::vector<uint8_t> chunk = input;
        chunk.insert(chunk.end(), input.begin(), input.end());
        chunk.insert(chunk.end(), input.begin(), input.end());

        std::cout << "Input: " << text << " (repeated 3x, " << chunk.size() << " bytes)\n";

        LZ::CompressOptions opts;
        auto stream = LZ::encodeChunk(chunk, opts);

        std::cout << "Symbol count: " << stream.symbols.size() << "\n";
        std::cout << "Symbols breakdown:\n";

        size_t literals = 0, matches = 0;
        for (const auto& sym : stream.symbols)
        {
            if (sym.type == LZ::EncodedSymbol::Literal) literals++;
            else matches++;
        }
        std::cout << "  Literals: " << literals << "\n";
        std::cout << "  Matches:  " << matches << "\n";

        auto decoded = LZ::decodeStream(stream);
        bool match = (chunk == decoded);
        std::cout << "Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 3: No repeats (worst case)
    std::cout << "Test 3: No repeating patterns (random-like)\n";
    {
        std::vector<uint8_t> input{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        std::cout << "Input: ";
        for (auto b : input) std::cout << std::hex << (int)b << " ";
        std::cout << std::dec << "\n";

        LZ::CompressOptions opts;
        auto stream = LZ::encodeChunk(input, opts);

        std::cout << "Symbol count: " << stream.symbols.size() << "\n";
        std::cout << "(Should be all literals, no matches)\n";

        auto decoded = LZ::decodeStream(stream);
        bool match = (input == decoded);
        std::cout << "Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 4: Longer repeating sequence
    std::cout << "Test 4: Longer repeating sequence\n";
    {
        std::vector<uint8_t> input;
        std::string pattern = "abcdefghij";

        // Create 10 repetitions
        for (int i = 0; i < 10; ++i)
        {
            input.insert(input.end(), pattern.begin(), pattern.end());
        }

        std::cout << "Input: " << pattern << " (repeated 10x, " << input.size() << " bytes)\n";

        LZ::CompressOptions opts;
        auto stream = LZ::encodeChunk(input, opts);

        size_t literals = 0, matches = 0;
        uint32_t totalMatchLen = 0;
        for (const auto& sym : stream.symbols)
        {
            if (sym.type == LZ::EncodedSymbol::Literal) literals++;
            else
            {
                matches++;
                totalMatchLen += sym.length;
            }
        }

        std::cout << "Symbols: " << stream.symbols.size() << "\n";
        std::cout << "  Literals: " << literals << "\n";
        std::cout << "  Matches:  " << matches << " (total " << totalMatchLen << " bytes)\n";
        std::cout << "Compression: " << input.size() << " ? " << stream.symbols.size()
                 << " symbols\n";

        auto decoded = LZ::decodeStream(stream);
        bool match = (input == decoded);
        std::cout << "Result: " << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    std::cout << "=== All LZ77 tests passed! ===\n";
    return 0;
}


