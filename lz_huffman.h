#pragma once
#include "lz.h"
#include "huffman.h"
#include <vector>
#include <cstdint>
#include <array>

// Multi-stream LZ+Huffman compression with token-based encoding.
// Each chunk produces independent bitstreams with per-stream entropy tables.
//
// Token byte layout:
//   Bits 0-1: litlen  (0-2 = literal count; 3 = escape to lrl8 stream)
//   Bits 2-5: matchlen field (0-14 = matchlen 3-17; 15 = escape to lrl8 stream)
//   Bits 6-7: offset mode (0 = new offset; 1/2/3 = recent offset 0/1/2)
//
// Overflow (lrl8) stream - matches Kraken's lrl8 approach:
//   Both litlen overflow and matchlen overflow write into the same byte stream.
//   Values 0-254 store the overflow value directly (1 byte).
//   Value 255 signals escape: the actual value follows as a LE32 in lrl8ExtraStream.
//
// Offset encoding: packed byte absorbs log2 class + 4 mantissa bits.
//   Packed byte = high nibble (extra-bit count) | low nibble (4 mantissa bits)
//   Remaining extra bits stored in a separate bitstream.
class LZHuffman
{
public:
    enum class StreamType : uint8_t
    {
        Raw        = 0,
        Huffman    = 1,
        RLEHuffman = 2,   // RLE pre-pass then Huffman on the control stream
    };

    struct StreamHeader
    {
        StreamType type = StreamType::Raw;
        bool interleaved = false;
        uint32_t rleEncodedSize = 0;  // for RLEHuffman: size of RLE-encoded stream before Huffman
        std::array<uint8_t, 256> codeLengths{};
    };

    struct CompressOptions
    {
        uint32_t minMatchLength;
        uint32_t maxMatchLength;
        uint32_t maxDistance;
        bool useInterleaved;
        bool useRLE;

        CompressOptions()
            : minMatchLength(3)
            , maxMatchLength(65535)
            , maxDistance(131072)
            , useInterleaved(false)
            , useRLE(true)
        {}
    };

    struct CompressedChunk
    {
        StreamHeader tokenHeader;
        StreamHeader literalHeader;
        StreamHeader distPackedHeader;
        StreamHeader lrl8Header;            // unified litlen+matchlen overflow stream

        std::vector<uint8_t> tokenStream;
        std::vector<uint8_t> literalStream;
        std::vector<uint8_t> distPackedStream;  // packed offset bytes (entropy-coded)
        std::vector<uint8_t> lrl8Stream;         // unified overflow: 0-254 direct, 255 = escape
        std::vector<uint8_t> extraBitsStream;    // extra bits for packed offsets (raw, no mode byte)
        std::vector<uint8_t> lrl8ExtraStream;    // LE32 values for lrl8 escapes (255)
        uint8_t extraBitsMode = 0;               // 0=empty, 1=fwd-only, 2=bidirectional

        uint32_t uncompressedSize = 0;
        bool literalSubMode = false;  // true = literals stored as deltas

        uint32_t tokenCount       = 0;
        uint32_t literalByteCount = 0;
        uint32_t distPackedCount  = 0;
        uint32_t lrl8Count        = 0;   // number of bytes in lrl8Stream
        uint32_t extraBitCount    = 0;
        uint32_t lrl8ExtraCount   = 0;  // number of LE32 escapes
    };

    // Packed offset encoding: absorbs 4 mantissa bits into the entropy-coded byte.
    //   For distance >= 1:
    //     n = BSR(distance)  (position of highest set bit)
    //     if n < 4: packed = distance - 1  (values 0..14, no extra bits)
    //     else:     packed = ((n - 3) << 4) | ((distance >> (n - 4)) & 0x0F)
    //               extra bits = n - 4 bits of (distance & mask)
    static uint8_t packOffset(uint32_t distance, uint32_t& extraBits, uint8_t& extraBitCount);
    static uint32_t unpackOffset(uint8_t packed, uint32_t extraBits);

    static CompressedChunk compressChunk(const std::vector<uint8_t>& input,
                                         const CompressOptions& opts = {});

    static std::vector<uint8_t> decompressChunk(const CompressedChunk& chunk);

private:
    // Token bit-field constants
    static constexpr uint8_t LitLenEscape    = 3;
    static constexpr uint8_t MatchLenEscape  = 15;
    static constexpr uint8_t MatchLenBase    = 3;
    // Inline matchlen range: 3..17 (fields 0..14). Overflow starts at 18.
    static constexpr uint8_t MatchLenInlineMax = 17;  // MatchLenBase + 14

    static uint8_t makeToken(const uint8_t litlen, const uint8_t matchlenField, const uint8_t offsetMode)
    {
        return static_cast<uint8_t>((offsetMode << 6) | (matchlenField << 2) | litlen);
    }

    struct TokenizedStreams
    {
        std::vector<uint8_t> tokens;
        std::vector<uint8_t> literals;
        std::vector<uint8_t> subLiterals;   // delta-coded: literal - prediction
        std::vector<uint8_t> distPacked;
        std::vector<uint8_t> lrl8;
        std::vector<uint8_t> lrl8Extra;
        std::vector<uint8_t> extraBitsData;
        uint32_t extraBitCount = 0;
        uint8_t extraBitsMode = 0;   // 0=empty, 1=fwd-only, 2=bidirectional
    };

    static TokenizedStreams tokenize(const LZ::IntermediateStream& intermediate,
                                     const std::vector<uint8_t>& input);

    // Write a value into the lrl8 stream. Values 0-254 stored directly.
    // Values >= 255 write 255 as escape byte, then store actual value as LE32.
    static void writeLrl8(TokenizedStreams& ts, uint32_t value);

    static std::vector<uint8_t> encodeStream(const std::vector<uint8_t>& input,
                                             const std::array<Huffman::HuffmanCode, 256>& codes,
                                             bool interleaved = false);

    static std::vector<uint8_t> rleEncode(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> rleDecode(const std::vector<uint8_t>& encoded, uint32_t expectedSize);

    static std::vector<uint8_t> decodeStream(
        const std::vector<uint8_t>& encoded,
        uint32_t expectedBytes,
        const std::array<Huffman::PrimaryDecodeEntry, Huffman::PrimaryDecodeSize>& primaryTable,
        const std::vector<uint16_t>& secondaryTable,
        bool interleaved = false);
};

