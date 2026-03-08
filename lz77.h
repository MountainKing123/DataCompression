#pragma once
#include <cstdint>
#include <vector>
#include <array>

// LZ77 compression with chunk-local matching and recent-offsets cache
class LZ77
{
public:
    static constexpr int RecentOffsetCount = 3;
    static constexpr uint32_t RecentOffsetInit = 8;

    struct Match
    {
        uint32_t distance = 0;
        uint32_t length = 0;
        bool isValid() const { return length >= 3; }
    };

    struct CompressOptions
    {
        uint32_t minMatchLength = 3;
        uint32_t maxMatchLength = 65535;
        uint32_t maxDistance    = 131072;
        size_t hashTableSize    = 65536;
        size_t maxChainLength   = 128;
    };

    struct EncodedSymbol
    {
        enum Type { Literal, Match } type;
        uint8_t literal = 0;
        uint32_t distance = 0;
        uint32_t length = 0;
        bool isRecentOffset = false;
        uint8_t recentIndex = 0;   // 0, 1, or 2
    };

    struct IntermediateStream
    {
        std::vector<EncodedSymbol> symbols;
        size_t uncompressedSize = 0;
    };

    static Match findBestMatch(const std::vector<uint8_t>& chunk,
                               size_t position,
                               const CompressOptions& opts);

    static IntermediateStream encodeChunk(const std::vector<uint8_t>& chunk,
                                          const CompressOptions& opts = {});

    static std::vector<uint8_t> decodeStream(const IntermediateStream& stream);

    // Recent-offset cache operations (also used by LZ77Huffman for decode)
    static void promoteRecent(std::array<uint32_t, RecentOffsetCount>& recent, int index);
    static void insertRecent(std::array<uint32_t, RecentOffsetCount>& recent, uint32_t distance);

private:
    static constexpr uint32_t HASH_SEED = 0x9e3779b1;

    static inline uint32_t hash4(const uint8_t* data);

    static inline void updateHashChain(std::vector<int>& hashTable,
                                       std::vector<int>& hashChain,
                                       uint32_t hash,
                                       int position);
};

