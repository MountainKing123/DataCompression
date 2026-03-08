#pragma once
#include <vector>
#include <cstdint>

namespace compression
{
    using Byte = std::uint8_t;

    struct CompressOptions
    {
        size_t chunkSize = 128 * 1024;   // 128 KiB default chunk size

        // LZ77 options
        uint32_t minMatchLength = 3;
        uint32_t maxMatchLength = 65535;
        uint32_t maxDistance    = 131072;

        size_t numThreads = 0;           // 0 = auto-detect
    };

    // Compress arbitrary data into a single byte stream.
    // Automatically splits input into chunks, compresses each with
    // LZ77+Huffman multi-stream, and serializes the result.
    std::vector<Byte> compress(const std::vector<Byte>& input,
                               const CompressOptions& opts = {});

    // Decompress data produced by compress().
    std::vector<Byte> decompress(const std::vector<Byte>& compressed);

} // namespace compression
