#pragma once
#include "bitstream.h"
#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <queue>
#include <memory>
#include <functional>

static constexpr int SYMBOL_COUNT = 256; // all possible byte symbols

struct HuffmanCode {
    uint16_t code = 0;   // bits of the code
    uint8_t length = 0;  // length of the code in bits
};

struct DecodeEntry {
    uint8_t symbol;
    uint8_t length;
};

class Huffman {
public:
    // Compress input using canonical Huffman coding
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input);

    // Decompress input using canonical Huffman coding with lookup tables
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);

private:
    // Build Huffman tree and return code lengths
    static std::array<uint8_t, SYMBOL_COUNT> build_code_lengths(const std::array<uint32_t, SYMBOL_COUNT>& freq);

    // Convert code lengths to canonical Huffman codes
    static std::array<HuffmanCode, SYMBOL_COUNT> build_canonical_codes(const std::array<uint8_t, SYMBOL_COUNT>& lengths);

    // Build fast decode table for lookup-based decoding
    static std::vector<DecodeEntry> build_decode_table(const std::array<HuffmanCode, SYMBOL_COUNT>& table, int max_len);
};