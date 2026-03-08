#pragma once
#include "lz77.h"
#include "huffman.h"
#include <vector>
#include <cstdint>
#include <array>

// Multi-stream LZ77+Huffman compression with token-based encoding.
// Each chunk produces independent bitstreams with per-stream entropy tables.
//
// Token byte layout:
//   Bits 0-1: litlen  (0-2 = literal count; 3 = escape to overflow)
//   Bits 2-5: matchlen field (0-14 = matchlen 3-17; 15 = escape to overflow)
//   Bits 6-7: offset mode (0 = new offset; 1/2/3 = recent offset 0/1/2)
class LZ77Huffman
{
public:
    enum class StreamType : uint8_t
    {
        Raw     = 0,
        Huffman = 1,
    };

    struct StreamHeader
    {
        StreamType type = StreamType::Raw;
        std::array<uint8_t, 256> codeLengths{};
    };

    struct CompressOptions
    {
        uint32_t minMatchLength = 3;
        uint32_t maxMatchLength = 65535;
        uint32_t maxDistance    = 131072;
    };

    struct CompressedChunk
    {
        StreamHeader tokenHeader;
        StreamHeader literalHeader;
        StreamHeader distClassHeader;
        StreamHeader lenClassHeader;

        std::vector<uint8_t> tokenStream;      // packed token bytes
        std::vector<uint8_t> literalStream;     // literal bytes
        std::vector<uint8_t> distClassStream;   // distance class codes
        std::vector<uint8_t> lenClassStream;    // length class codes (overflow only)
        std::vector<uint8_t> extraBitsStream;   // extra bits for dist+len classes
        std::vector<uint8_t> overflowStream;    // overflow varint for litlen >= 3, matchlen >= 18

        uint32_t uncompressedSize = 0;

        uint32_t tokenCount      = 0;
        uint32_t literalByteCount = 0;
        uint32_t distClassCount   = 0;
        uint32_t lenClassCount    = 0;
        uint32_t extraBitCount    = 0;
        uint32_t overflowCount    = 0;
    };

    // Distance class tables (DEFLATE-inspired, extended to 131072)
    static constexpr int MaxDistClasses = 36;
    static constexpr int MaxLenClasses  = 29;

    struct ClassEntry
    {
        uint32_t base;
        uint8_t  extraBits;
    };

    static const ClassEntry distClassTable[MaxDistClasses];
    static const ClassEntry lenClassTable[MaxLenClasses];

    static uint8_t distanceToClass(uint32_t distance);
    static uint8_t lengthToClass(uint32_t length);

    static CompressedChunk compressChunk(const std::vector<uint8_t>& input,
                                         const CompressOptions& opts = {});

    static std::vector<uint8_t> decompressChunk(const CompressedChunk& chunk);

private:
    // Token bit-field constants
    static constexpr uint8_t LitLenBits   = 2;
    static constexpr uint8_t LitLenEscape = 3;
    static constexpr uint8_t MatchLenBits = 4;
    static constexpr uint8_t MatchLenEscape = 15;
    static constexpr uint8_t MatchLenBase = 3;
    static constexpr uint8_t OffsetModeBits = 2;

    static uint8_t makeToken(uint8_t litlen, uint8_t matchlenField, uint8_t offsetMode)
    {
        return static_cast<uint8_t>((offsetMode << 6) | (matchlenField << 2) | litlen);
    }

    struct TokenizedStreams
    {
        std::vector<uint8_t> tokens;
        std::vector<uint8_t> literals;
        std::vector<uint8_t> distClasses;
        std::vector<uint8_t> lenClasses;     // only for overflow matchlen
        std::vector<uint8_t> extraBitsData;
        uint32_t extraBitCount = 0;
        std::vector<uint8_t> overflow;       // varint overflow for litlen/matchlen
    };

    static TokenizedStreams tokenize(const LZ77::IntermediateStream& intermediate);

    static void encodeOverflowVarInt(std::vector<uint8_t>& out, uint32_t value);
    static uint32_t decodeOverflowVarInt(const std::vector<uint8_t>& data, size_t& pos);

    static std::vector<uint8_t> encodeStream(const std::vector<uint8_t>& input,
                                             const std::array<Huffman::HuffmanCode, 256>& codes);

    static std::vector<uint8_t> decodeStream(
        const std::vector<uint8_t>& encoded,
        uint32_t expectedBytes,
        const std::array<Huffman::PrimaryDecodeEntry, Huffman::PrimaryDecodeSize>& primaryTable,
        const std::vector<uint16_t>& secondaryTable);
};
