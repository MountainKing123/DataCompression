#include "compressor.h"
#include "lz77_huffman.h"
#include <cstring>

// Wire format (v4 - LZH4):
//   [4] magic "LZH4"
//   [4] total uncompressed size (LE32)
//   [4] chunk count (LE32)
//   For each chunk:
//     [4] uncompressed size (LE32)
//     [4] tokenCount (LE32)
//     [4] literalByteCount (LE32)
//     [4] distClassCount (LE32)
//     [4] lenClassCount (LE32)
//     [4] extraBitCount (LE32)
//     [4] overflowCount (LE32)
//     Per stream (token, literal, distClass, lenClass):
//       StreamHeader (type + optional code lengths + data)
//     [4] extraBits stream size (LE32)
//     [extraBits bytes]
//     [4] overflow stream size (LE32)
//     [overflow bytes]

namespace
{
    void writeLE32(std::vector<uint8_t>& out, uint32_t v)
    {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 24));
    }

    uint32_t readLE32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0])
             | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16)
             | (static_cast<uint32_t>(p[3]) << 24);
    }

    void writeStreamHeader(std::vector<uint8_t>& out,
                           const LZ77Huffman::StreamHeader& header,
                           const std::vector<uint8_t>& streamData)
    {
        out.push_back(static_cast<uint8_t>(header.type));

        if (header.type == LZ77Huffman::StreamType::Huffman)
        {
            int usedSymbols = 0;
            for (uint8_t len : header.codeLengths)
                if (len != 0) usedSymbols++;

            const size_t sparseBytes = (usedSymbols == 0)
                ? 0 : (1 + static_cast<size_t>(usedSymbols) * 2);
            constexpr size_t denseBytes = 256;

            if (usedSymbols > 0 && sparseBytes < denseBytes)
            {
                out.push_back(1);
                out.push_back(static_cast<uint8_t>(usedSymbols - 1));
                for (int i = 0; i < 256; ++i)
                {
                    if (header.codeLengths[i] != 0)
                    {
                        out.push_back(static_cast<uint8_t>(i));
                        out.push_back(header.codeLengths[i]);
                    }
                }
            }
            else
            {
                out.push_back(0);
                out.insert(out.end(), header.codeLengths.begin(), header.codeLengths.end());
            }
        }

        writeLE32(out, static_cast<uint32_t>(streamData.size()));
        out.insert(out.end(), streamData.begin(), streamData.end());
    }

    bool readStreamHeader(const uint8_t* p, size_t totalSize, size_t& pos,
                          LZ77Huffman::StreamHeader& header,
                          std::vector<uint8_t>& streamData)
    {
        if (pos >= totalSize) return false;
        header.type = static_cast<LZ77Huffman::StreamType>(p[pos++]);

        header.codeLengths.fill(0);
        if (header.type == LZ77Huffman::StreamType::Huffman)
        {
            if (pos >= totalSize) return false;
            uint8_t codeLengthMode = p[pos++];

            if (codeLengthMode == 0)
            {
                if (pos + 256 > totalSize) return false;
                std::copy(p + pos, p + pos + 256, header.codeLengths.begin());
                pos += 256;
            }
            else
            {
                if (pos >= totalSize) return false;
                int symbolCount = static_cast<int>(p[pos++]) + 1;
                if (pos + static_cast<size_t>(symbolCount) * 2 > totalSize) return false;
                for (int i = 0; i < symbolCount; ++i)
                {
                    uint8_t symbol = p[pos++];
                    uint8_t length = p[pos++];
                    header.codeLengths[symbol] = length;
                }
            }
        }

        if (pos + 4 > totalSize) return false;
        uint32_t streamSize = readLE32(p + pos); pos += 4;
        if (pos + streamSize > totalSize) return false;
        streamData.assign(p + pos, p + pos + streamSize);
        pos += streamSize;
        return true;
    }

    bool readRawStream(const uint8_t* p, size_t totalSize, size_t& pos,
                       std::vector<uint8_t>& streamData)
    {
        if (pos + 4 > totalSize) return false;
        uint32_t streamSize = readLE32(p + pos); pos += 4;
        if (pos + streamSize > totalSize) return false;
        streamData.assign(p + pos, p + pos + streamSize);
        pos += streamSize;
        return true;
    }
}

namespace compression
{

std::vector<Byte> compress(const std::vector<Byte>& input,
                           const CompressOptions& opts)
{
    LZ77Huffman::CompressOptions chunkOpts;
    chunkOpts.minMatchLength = opts.minMatchLength;
    chunkOpts.maxMatchLength = opts.maxMatchLength;
    chunkOpts.maxDistance     = opts.maxDistance;

    std::vector<LZ77Huffman::CompressedChunk> chunks;
    for (size_t off = 0; off < input.size(); off += opts.chunkSize)
    {
        size_t len = std::min(opts.chunkSize, input.size() - off);
        std::vector<uint8_t> chunkData(input.begin() + static_cast<ptrdiff_t>(off),
                                       input.begin() + static_cast<ptrdiff_t>(off + len));
        chunks.push_back(LZ77Huffman::compressChunk(chunkData, chunkOpts));
    }

    if (chunks.empty())
        chunks.push_back(LZ77Huffman::compressChunk({}, chunkOpts));

    std::vector<Byte> out;
    out.reserve(input.size());

    out.push_back('L'); out.push_back('Z');
    out.push_back('H'); out.push_back('4');
    writeLE32(out, static_cast<uint32_t>(input.size()));
    writeLE32(out, static_cast<uint32_t>(chunks.size()));

    for (const auto& c : chunks)
    {
        writeLE32(out, c.uncompressedSize);
        writeLE32(out, c.tokenCount);
        writeLE32(out, c.literalByteCount);
        writeLE32(out, c.distClassCount);
        writeLE32(out, c.lenClassCount);
        writeLE32(out, c.extraBitCount);
        writeLE32(out, c.overflowCount);

        writeStreamHeader(out, c.tokenHeader, c.tokenStream);
        writeStreamHeader(out, c.literalHeader, c.literalStream);
        writeStreamHeader(out, c.distClassHeader, c.distClassStream);
        writeStreamHeader(out, c.lenClassHeader, c.lenClassStream);

        writeLE32(out, static_cast<uint32_t>(c.extraBitsStream.size()));
        out.insert(out.end(), c.extraBitsStream.begin(), c.extraBitsStream.end());

        writeLE32(out, static_cast<uint32_t>(c.overflowStream.size()));
        out.insert(out.end(), c.overflowStream.begin(), c.overflowStream.end());
    }

    return out;
}

std::vector<Byte> decompress(const std::vector<Byte>& compressed)
{
    if (compressed.size() < 12) return {};

    const uint8_t* p = compressed.data();

    if (p[0] != 'L' || p[1] != 'Z' || p[2] != 'H' || p[3] != '4')
        return {};

    const uint32_t totalSize = readLE32(p + 4);
    const uint32_t chunkCount = readLE32(p + 8);
    size_t pos = 12;

    std::vector<Byte> output;
    output.reserve(totalSize);

    for (uint32_t ci = 0; ci < chunkCount; ++ci)
    {
        if (pos + 28 > compressed.size()) return {};

        LZ77Huffman::CompressedChunk chunk;
        chunk.uncompressedSize = readLE32(p + pos); pos += 4;
        chunk.tokenCount       = readLE32(p + pos); pos += 4;
        chunk.literalByteCount = readLE32(p + pos); pos += 4;
        chunk.distClassCount   = readLE32(p + pos); pos += 4;
        chunk.lenClassCount    = readLE32(p + pos); pos += 4;
        chunk.extraBitCount    = readLE32(p + pos); pos += 4;
        chunk.overflowCount    = readLE32(p + pos); pos += 4;

        if (!readStreamHeader(p, compressed.size(), pos, chunk.tokenHeader, chunk.tokenStream))
            return {};
        if (!readStreamHeader(p, compressed.size(), pos, chunk.literalHeader, chunk.literalStream))
            return {};
        if (!readStreamHeader(p, compressed.size(), pos, chunk.distClassHeader, chunk.distClassStream))
            return {};
        if (!readStreamHeader(p, compressed.size(), pos, chunk.lenClassHeader, chunk.lenClassStream))
            return {};

        if (!readRawStream(p, compressed.size(), pos, chunk.extraBitsStream))
            return {};
        if (!readRawStream(p, compressed.size(), pos, chunk.overflowStream))
            return {};

        auto decoded = LZ77Huffman::decompressChunk(chunk);
        output.insert(output.end(), decoded.begin(), decoded.end());
    }

    return output;
}

} // namespace compression
