#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <thread>

// Huffman entropy codec with chunk-based compression and multi-pass decompression
class Huffman
{
public:
    struct CompressOptions {
        bool parallelFrequencyCount = true;
        size_t numThreads = std::thread::hardware_concurrency();
        size_t parallelThreshold = 10000;
        size_t chunkSize = 128 * 1024;
    };

    struct DecompressOptions {
        bool separatePasses = false;
    };

    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input,
                                         const CompressOptions& opts);
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input);

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input,
                                           const DecompressOptions& opts);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);

    struct ChunkHeader {
        uint32_t uncompressedSize = 0;
        uint32_t compressedSize = 0;
        uint32_t checksum = 0;
    };

    struct DecodedStream {
        std::vector<uint8_t> symbols;
        size_t count;
    };

    struct EntropyHeader {
        std::array<uint8_t, 256> codeLengths{};
        uint32_t uncompressedSize = 0;
    };

    static DecodedStream parsePass(const std::vector<uint8_t>& compressed);
    static std::vector<uint8_t> applyPass(const DecodedStream& decoded);

    static std::vector<uint8_t> compressChunked(const std::vector<uint8_t>& input,
                                                 const CompressOptions& opts);
    static std::vector<uint8_t> compressChunked(const std::vector<uint8_t>& input);

    static std::vector<uint8_t> decompressChunked(const std::vector<uint8_t>& input);

    static std::vector<uint8_t> compressChunk(const std::vector<uint8_t>& chunkData,
                                              const CompressOptions& opts);

    static std::vector<uint8_t> decompressChunk(const std::vector<uint8_t>& compressedChunk);

    // --- Shared building blocks (used by LZHuffman as well) ---

    static constexpr int MaxSymbols = 256;
    static constexpr int MaxCodeLength = 15;
    static constexpr int PrimaryDecodeBits = 10;
    static constexpr int PrimaryDecodeSize = 1 << PrimaryDecodeBits;

    struct HuffmanCode
    {
        uint16_t bits = 0;
        uint8_t length = 0;
    };

    struct PrimaryDecodeEntry
    {
        int16_t symbol = -1;
        uint8_t length = 0;
        uint16_t secondaryOffset = 0;
        uint8_t secondaryBits = 0;
    };

    static void buildFrequenciesParallel(const std::vector<uint8_t>& input,
                                        std::array<uint32_t, MaxSymbols>& freq,
                                        size_t numThreads);

    static void buildFrequenciesSerial(const std::vector<uint8_t>& input,
                                      std::array<uint32_t, MaxSymbols>& freq);

    static void buildCodeLengths(const std::array<uint32_t, MaxSymbols>& freq,
                                std::array<uint8_t, MaxSymbols>& lengths);

    static void buildCanonicalCodes(const std::array<uint8_t, MaxSymbols>& lengths,
                                    std::array<HuffmanCode, MaxSymbols>& codes);

    static void buildDecodeTable(const std::array<HuffmanCode, MaxSymbols>& codes,
                                std::array<PrimaryDecodeEntry, PrimaryDecodeSize>& primaryTable,
                                std::vector<uint16_t>& secondaryTable);

};
