#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cassert>

class Huffman
{
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);

private:
    static constexpr int MaxSymbols = 256;
    static constexpr int MaxCodeLength = 11;
    static constexpr int DecodeTableSize = 1 << MaxCodeLength;

    struct HuffmanCode
    {
        uint16_t bits = 0;
        uint8_t length = 0;
    };

    static void buildFrequencies(const std::vector<uint8_t>& input,
        std::array<uint32_t, MaxSymbols>& freq);

    static void buildCodeLengths(const std::array<uint32_t, MaxSymbols>& freq,
        std::array<uint8_t, MaxSymbols>& lengths);

    static void buildCanonicalCodes(const std::array<uint8_t, MaxSymbols>& lengths,
        std::array<HuffmanCode, MaxSymbols>& codes);

    static void buildDecodeTable(const std::array<HuffmanCode, MaxSymbols>& codes,
        std::array<int16_t, DecodeTableSize>& table);
};