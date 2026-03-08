#include "lz77_huffman.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <iomanip>
#include <string>
#include <random>

static const char* streamTypeName(LZ77Huffman::StreamType t)
{
    return t == LZ77Huffman::StreamType::Huffman ? "Huffman" : "raw";
}

int main()
{
    std::cout << "=== LZ77+Huffman Token-Based Multi-Stream Test ===\n\n";

    // Test 1: Repeating text pattern
    std::cout << "Test 1: Repeating text pattern\n";
    {
        std::string text = "The quick brown fox jumps over the lazy dog. ";
        std::vector<uint8_t> input;
        for (int i = 0; i < 25; ++i)
            input.insert(input.end(), text.begin(), text.end());

        auto c = LZ77Huffman::compressChunk(input);
        auto d = LZ77Huffman::decompressChunk(c);

        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Token stream:      " << c.tokenStream.size()
                  << " bytes (" << streamTypeName(c.tokenHeader.type) << ")\n";
        std::cout << "  Literal stream:    " << c.literalStream.size()
                  << " bytes (" << streamTypeName(c.literalHeader.type) << ")\n";
        std::cout << "  DistClass stream:  " << c.distClassStream.size()
                  << " bytes (" << streamTypeName(c.distClassHeader.type) << ")\n";
        std::cout << "  LenClass stream:   " << c.lenClassStream.size()
                  << " bytes (" << streamTypeName(c.lenClassHeader.type) << ")\n";
        std::cout << "  ExtraBits stream:  " << c.extraBitsStream.size() << " bytes\n";
        std::cout << "  Overflow stream:   " << c.overflowStream.size() << " bytes\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 2: Mixed repeating and unique data
    std::cout << "Test 2: Mixed repeating and unique\n";
    {
        std::vector<uint8_t> input;
        const std::string pattern = "ABCDEFGH";
        for (int i = 0; i < 100; ++i)
            input.insert(input.end(), pattern.begin(), pattern.end());
        for (int i = 0; i < 256; ++i)
            input.push_back(static_cast<uint8_t>(i));

        auto c = LZ77Huffman::compressChunk(input);
        auto d = LZ77Huffman::decompressChunk(c);

        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Tokens: " << c.tokenCount << "\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 3: Small chunk (all literals, no matches)
    std::cout << "Test 3: Small chunk (all literals)\n";
    {
        std::vector<uint8_t> input{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        auto c = LZ77Huffman::compressChunk(input);
        auto d = LZ77Huffman::decompressChunk(c);
        std::cout << "  Tokens: " << c.tokenCount << ", Literals: " << c.literalByteCount << "\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 4: Empty input
    std::cout << "Test 4: Empty input\n";
    {
        std::vector<uint8_t> input;
        auto c = LZ77Huffman::compressChunk(input);
        auto d = LZ77Huffman::decompressChunk(c);
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 5: Long literal run (overflow litlen)
    std::cout << "Test 5: Long literal run (overflow litlen)\n";
    {
        std::vector<uint8_t> input;
        for (int i = 0; i < 300; ++i)
            input.push_back(static_cast<uint8_t>(i % 256));
        // Append repeated pattern to also exercise match tokens
        std::string pattern = "ABCDEFGHIJ";
        for (int i = 0; i < 50; ++i)
            input.insert(input.end(), pattern.begin(), pattern.end());

        auto c = LZ77Huffman::compressChunk(input);
        auto d = LZ77Huffman::decompressChunk(c);
        std::cout << "  Input: " << input.size() << " bytes, Tokens: " << c.tokenCount << "\n";
        std::cout << "  Overflow bytes: " << c.overflowStream.size() << "\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 6: Long match (overflow matchlen)
    std::cout << "Test 6: Long match (overflow matchlen)\n";
    {
        // 20 unique bytes then 5000 copies = very long match
        std::vector<uint8_t> input;
        for (int i = 0; i < 20; ++i)
            input.push_back(static_cast<uint8_t>(i));
        for (int i = 0; i < 5000; ++i)
            input.push_back(input[i % 20]);

        auto c = LZ77Huffman::compressChunk(input);
        auto d = LZ77Huffman::decompressChunk(c);
        std::cout << "  Input: " << input.size() << " bytes, Tokens: " << c.tokenCount << "\n";
        std::cout << "  LenClass overflow: " << c.lenClassCount << "\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 7: Recent offset usage
    std::cout << "Test 7: Recent offsets\n";
    {
        // Pattern that heavily uses recent offsets
        std::vector<uint8_t> input;
        for (int i = 0; i < 200; ++i)
        {
            input.push_back(0xAA);
            input.push_back(0xBB);
            input.push_back(0xCC);
        }

        auto c = LZ77Huffman::compressChunk(input);
        auto d = LZ77Huffman::decompressChunk(c);
        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  DistClass count: " << c.distClassCount << " (fewer = more recent offsets)\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 8: Large data roundtrip
    std::cout << "Test 8: Large data (64 KiB)\n";
    {
        std::mt19937 rng(42);
        std::vector<uint8_t> input(65536);
        for (auto& b : input)
            b = (rng() % 100 < 70) ? 0xAA : static_cast<uint8_t>(rng() & 0xFF);

        auto c = LZ77Huffman::compressChunk(input);
        auto d = LZ77Huffman::decompressChunk(c);

        size_t totalCompressed = c.tokenStream.size() + c.literalStream.size() +
            c.distClassStream.size() + c.lenClassStream.size() +
            c.extraBitsStream.size() + c.overflowStream.size();
        double ratio = static_cast<double>(input.size()) / static_cast<double>(totalCompressed);

        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Compressed streams: " << totalCompressed << " bytes\n";
        std::cout << "  Ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    std::cout << "=== All token-based multi-stream tests passed! ===\n";
    return 0;
}

