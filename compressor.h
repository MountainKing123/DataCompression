#pragma once
#include <vector>
#include <cstdint>

namespace compression
{
    using Byte = std::uint8_t;

    struct CompressedBlock
    {
        std::vector<Byte> data;
    };

    class BlockCompressor
    {

    public:
        CompressedBlock compress(const std::vector<Byte>& input);
        std::vector<Byte> decompress(const CompressedBlock& block);
    };
} // namespace compression
