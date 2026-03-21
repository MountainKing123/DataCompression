#include "lz_huffman.h"
#include "compressor.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <iomanip>
#include <string>
#include <random>

static const char* streamTypeName(const LZHuffman::StreamType t)
{
    return t == LZHuffman::StreamType::Huffman ? "Huffman" : "raw";
}

int main()
{
    std::cout << "=== lz.huffman Token-Based Multi-Stream Test ===\n\n";

    // Test 1: Repeating text pattern
    std::cout << "Test 1: Repeating text pattern\n";
    {
        std::string text = "The quick brown fox jumps over the lazy dog. ";
        std::vector<uint8_t> input;
        for (int i = 0; i < 25; ++i)
            input.insert(input.end(), text.begin(), text.end());

        const auto c = LZHuffman::compressChunk(input);
        const auto d = LZHuffman::decompressChunk(c);

        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Token stream:      " << c.tokenStream.size()
                  << " bytes (" << streamTypeName(c.tokenHeader.type) << ")\n";
        std::cout << "  Literal stream:    " << c.literalStream.size()
                  << " bytes (" << streamTypeName(c.literalHeader.type) << ")\n";
        std::cout << "  DistPacked stream: " << c.distPackedStream.size()
                  << " bytes (" << streamTypeName(c.distPackedHeader.type) << ")\n";
        std::cout << "  LenOverflow stream:  " << c.lenOverflowStream.size()
                  << " bytes (" << streamTypeName(c.lenOverflowHeader.type) << ")\n";
        std::cout << "  ExtraBits stream:  " << c.extraBitsStream.size() << " bytes\n";
        std::cout << "  LenOverflowExtra stream:  " << c.lenOverflowExtraStream.size() << " bytes\n";
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

        const auto c = LZHuffman::compressChunk(input);
        const auto d = LZHuffman::decompressChunk(c);

        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Tokens: " << c.tokenCount << "\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 3: Small chunk (all literals, no matches)
    std::cout << "Test 3: Small chunk (all literals)\n";
    {
        std::vector<uint8_t> input{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
        const auto c = LZHuffman::compressChunk(input);
        const auto d = LZHuffman::decompressChunk(c);
        std::cout << "  Tokens: " << c.tokenCount << ", Literals: " << c.literalByteCount << "\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 4: Empty input
    std::cout << "Test 4: Empty input\n";
    {
        std::vector<uint8_t> input;
        const auto c = LZHuffman::compressChunk(input);
        const auto d = LZHuffman::decompressChunk(c);
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

        const auto c = LZHuffman::compressChunk(input);
        const auto d = LZHuffman::decompressChunk(c);
        std::cout << "  Input: " << input.size() << " bytes, Tokens: " << c.tokenCount << "\n";
        std::cout << "  LenOverflow bytes: " << c.lenOverflowStream.size() << "\n";
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

        const auto c = LZHuffman::compressChunk(input);
        const auto d = LZHuffman::decompressChunk(c);
        std::cout << "  Input: " << input.size() << " bytes, Tokens: " << c.tokenCount << "\n";
        std::cout << "  LenOverflow count: " << c.lenOverflowCount << "\n";
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

        const auto c = LZHuffman::compressChunk(input);
        const auto d = LZHuffman::decompressChunk(c);
        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  DistPacked count: " << c.distPackedCount << " (fewer = more recent offsets)\n";
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

        const auto c = LZHuffman::compressChunk(input);
        const auto d = LZHuffman::decompressChunk(c);

        size_t totalCompressed = c.tokenStream.size() + c.literalStream.size() +
            c.distPackedStream.size() + c.lenOverflowStream.size() +
            c.extraBitsStream.size() + c.lenOverflowExtraStream.size();
        double ratio = static_cast<double>(input.size()) / static_cast<double>(totalCompressed);

        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Compressed streams: " << totalCompressed << " bytes\n";
        std::cout << "  Ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 9: Interleaved mode roundtrip (repeating text)
    std::cout << "Test 9: Interleaved mode - repeating text\n";
    {
        std::string text = "The quick brown fox jumps over the lazy dog. ";
        std::vector<uint8_t> input;
        for (int i = 0; i < 25; ++i)
            input.insert(input.end(), text.begin(), text.end());

        LZHuffman::CompressOptions opts;
        opts.useInterleaved = true;
        const auto c = LZHuffman::compressChunk(input, opts);
        const auto d = LZHuffman::decompressChunk(c);
        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Token interleaved:   " << (c.tokenHeader.interleaved ? "yes" : "no") << "\n";
        std::cout << "  Literal interleaved: " << (c.literalHeader.interleaved ? "yes" : "no") << "\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 10: Interleaved mode roundtrip (large mixed data)
    std::cout << "Test 10: Interleaved mode - large data (64 KiB)\n";
    {
        std::mt19937 rng(99);
        std::vector<uint8_t> input(65536);
        for (auto& b : input)
            b = (rng() % 100 < 70) ? 0xAA : static_cast<uint8_t>(rng() & 0xFF);

        LZHuffman::CompressOptions opts;
        opts.useInterleaved = true;
        const auto c = LZHuffman::compressChunk(input, opts);
        const auto d = LZHuffman::decompressChunk(c);

        size_t totalCompressed = c.tokenStream.size() + c.literalStream.size() +
            c.distPackedStream.size() + c.lenOverflowStream.size() +
            c.extraBitsStream.size() + c.lenOverflowExtraStream.size();
        double ratio = static_cast<double>(input.size()) / static_cast<double>(totalCompressed);

        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Compressed streams: " << totalCompressed << " bytes\n";
        std::cout << "  Ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";
        std::cout << "  Roundtrip: " << (d == input ? "PASS" : "FAIL") << "\n\n";
        assert(d == input);
    }

    // Test 11: Interleaved vs sequential produce same decompressed output
    std::cout << "Test 11: Interleaved vs sequential - identical output\n";
    {
        std::mt19937 rng(77);
        std::vector<uint8_t> input(32768);
        for (auto& b : input)
            b = (rng() % 100 < 80) ? static_cast<uint8_t>(rng() % 4) : static_cast<uint8_t>(rng() & 0xFF);

        LZHuffman::CompressOptions seqOpts;
        seqOpts.useInterleaved = false;
        const auto cSeq = LZHuffman::compressChunk(input, seqOpts);
        const auto dSeq = LZHuffman::decompressChunk(cSeq);

        LZHuffman::CompressOptions intOpts;
        intOpts.useInterleaved = true;
        const auto cInt = LZHuffman::compressChunk(input, intOpts);
        const auto dInt = LZHuffman::decompressChunk(cInt);

        bool match = (dSeq == dInt) && (dSeq == input);
        std::cout << "  Sequential decompressed == Interleaved decompressed: "
                  << (match ? "PASS" : "FAIL") << "\n\n";
        assert(match);
    }

    // Test 12: Interleaved mode via compressor API (full serialize/deserialize)
    std::cout << "Test 12: Interleaved mode via compressor API\n";
    {
        std::string text = "ABCDEFGHIJ";
        std::vector<uint8_t> input;
        for (int i = 0; i < 500; ++i)
            input.insert(input.end(), text.begin(), text.end());

        compression::CompressOptions opts;
        opts.useInterleaved = true;
        const auto compressed = compression::compress(input, opts);
        const auto decompressed = compression::decompress(compressed);

        double ratio = static_cast<double>(input.size()) / static_cast<double>(compressed.size());
        std::cout << "  Input: " << input.size() << " bytes\n";
        std::cout << "  Compressed: " << compressed.size() << " bytes\n";
        std::cout << "  Ratio: " << std::fixed << std::setprecision(2) << ratio << ":1\n";
        std::cout << "  Roundtrip: " << (decompressed == input ? "PASS" : "FAIL") << "\n\n";
        assert(decompressed == input);
    }

    std::cout << "=== All token-based multi-stream tests passed! ===\n";
    return 0;
}


