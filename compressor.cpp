#include "compressor.h"
#include <cstring>

namespace compression
{
    CompressedBlock BlockCompressor::compress(const std::vector<Byte>& input)
    {
        CompressedBlock block;

        // Block format:
        // [4 bytes original_size][raw bytes]

        uint32_t size = static_cast<uint32_t>(input.size());

        block.data.resize(4 + size);
        std::memcpy(block.data.data(), &size, 4);
        std::memcpy(block.data.data() + 4, input.data(), size);

        return block;
    }

    std::vector<Byte> BlockCompressor::decompress(const CompressedBlock& block)
    {
        uint32_t size = 0;
        std::memcpy(&size, block.data.data(), 4);

        std::vector<Byte> output(size);
        std::memcpy(output.data(), block.data.data() + 4, size);

        return output;
    }
} // namespace compression
