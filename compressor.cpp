#include "compressor.h"
#include "lz_huffman.h"

namespace
{
    void writeLE32(std::vector<uint8_t>& out, const uint32_t v)
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
                           const LZHuffman::StreamHeader& header,
                           const std::vector<uint8_t>& streamData)
    {
        auto typeAndFlags = static_cast<uint8_t>(header.type);
        if (header.interleaved) typeAndFlags |= 0x80;
        out.push_back(typeAndFlags);

        if (header.type == LZHuffman::StreamType::Huffman ||
            header.type == LZHuffman::StreamType::RLEHuffman)
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

            if (header.type == LZHuffman::StreamType::RLEHuffman)
                writeLE32(out, header.rleEncodedSize);
        }

        writeLE32(out, static_cast<uint32_t>(streamData.size()));
        out.insert(out.end(), streamData.begin(), streamData.end());
    }

    bool readStreamHeader(const uint8_t* p, const size_t totalSize, size_t& pos,
                          LZHuffman::StreamHeader& header,
                          std::vector<uint8_t>& streamData)
    {
        if (pos >= totalSize) return false;
        uint8_t typeAndFlags = p[pos++];
        header.interleaved = (typeAndFlags & 0x80) != 0;  // Bit 7 = interleaved flag
        header.type = static_cast<LZHuffman::StreamType>(typeAndFlags & 0x0F);

        header.codeLengths.fill(0);
        header.rleEncodedSize = 0;
        if (header.type == LZHuffman::StreamType::Huffman ||
            header.type == LZHuffman::StreamType::RLEHuffman)
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

            if (header.type == LZHuffman::StreamType::RLEHuffman)
            {
                if (pos + 4 > totalSize) return false;
                header.rleEncodedSize = readLE32(p + pos); pos += 4;
            }
        }

        if (pos + 4 > totalSize) return false;
        uint32_t streamSize = readLE32(p + pos); pos += 4;
        if (pos + streamSize > totalSize) return false;
        streamData.assign(p + pos, p + pos + streamSize);
        pos += streamSize;
        return true;
    }

    bool readRawStream(const uint8_t* p, const size_t totalSize, size_t& pos,
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
    LZHuffman::CompressOptions chunkOpts;
    chunkOpts.minMatchLength = opts.minMatchLength;
    chunkOpts.maxMatchLength = opts.maxMatchLength;
    chunkOpts.maxDistance     = opts.maxDistance;
    chunkOpts.useInterleaved = opts.useInterleaved;
    chunkOpts.useRLE         = opts.useRLE;

    std::vector<LZHuffman::CompressedChunk> chunks;
    for (size_t off = 0; off < input.size(); off += opts.chunkSize)
    {
        const size_t len = std::min(opts.chunkSize, input.size() - off);
        std::vector<uint8_t> chunkData(input.begin() + static_cast<ptrdiff_t>(off),
                                       input.begin() + static_cast<ptrdiff_t>(off + len));
        chunks.push_back(LZHuffman::compressChunk(chunkData, chunkOpts));
    }

    if (chunks.empty())
        chunks.push_back(LZHuffman::compressChunk({}, chunkOpts));

    std::vector<Byte> out;
    out.reserve(input.size());

    out.push_back('L'); out.push_back('Z');
    out.push_back('H'); out.push_back('8');
    writeLE32(out, static_cast<uint32_t>(input.size()));
    writeLE32(out, static_cast<uint32_t>(chunks.size()));

    for (const auto& c : chunks)
    {
        writeLE32(out, c.uncompressedSize);
        writeLE32(out, c.tokenCount);
        writeLE32(out, c.literalByteCount);
        writeLE32(out, c.distPackedCount);
        writeLE32(out, c.lrl8Count);
        writeLE32(out, c.extraBitCount);
        writeLE32(out, c.lrl8ExtraCount);

        uint8_t flags = 0;
        if (c.literalSubMode) flags |= 0x01;
        out.push_back(flags);

        writeStreamHeader(out, c.tokenHeader, c.tokenStream);
        writeStreamHeader(out, c.literalHeader, c.literalStream);
        writeStreamHeader(out, c.distPackedHeader, c.distPackedStream);
        writeStreamHeader(out, c.lrl8Header, c.lrl8Stream);

        // Extra bits: encode mode in top 2 bits of size field
        const auto extraSize = static_cast<uint32_t>(c.extraBitsStream.size());
        const uint32_t packedSize = extraSize | (static_cast<uint32_t>(c.extraBitsMode) << 30);
        writeLE32(out, packedSize);
        out.insert(out.end(), c.extraBitsStream.begin(), c.extraBitsStream.end());

        writeLE32(out, static_cast<uint32_t>(c.lrl8ExtraStream.size()));
        out.insert(out.end(), c.lrl8ExtraStream.begin(), c.lrl8ExtraStream.end());
    }

    return out;
}

std::vector<Byte> decompress(const std::vector<Byte>& compressed)
{
    if (compressed.size() < 12) return {};

    const uint8_t* p = compressed.data();

    if (p[0] != 'L' || p[1] != 'Z' || p[2] != 'H' || p[3] != '8')
        return {};

    const uint32_t totalSize = readLE32(p + 4);
    const uint32_t chunkCount = readLE32(p + 8);
    size_t pos = 12;

    std::vector<Byte> output;
    output.reserve(totalSize);

    for (uint32_t ci = 0; ci < chunkCount; ++ci)
    {
        if (pos + 29 > compressed.size()) return {};

        LZHuffman::CompressedChunk chunk;
        chunk.uncompressedSize = readLE32(p + pos); pos += 4;
        chunk.tokenCount       = readLE32(p + pos); pos += 4;
        chunk.literalByteCount = readLE32(p + pos); pos += 4;
        chunk.distPackedCount  = readLE32(p + pos); pos += 4;
        chunk.lrl8Count        = readLE32(p + pos); pos += 4;
        chunk.extraBitCount    = readLE32(p + pos); pos += 4;
        chunk.lrl8ExtraCount   = readLE32(p + pos); pos += 4;

        uint8_t flags = p[pos++];
        chunk.literalSubMode = (flags & 0x01) != 0;

        if (!readStreamHeader(p, compressed.size(), pos, chunk.tokenHeader, chunk.tokenStream))
            return {};
        if (!readStreamHeader(p, compressed.size(), pos, chunk.literalHeader, chunk.literalStream))
            return {};
        if (!readStreamHeader(p, compressed.size(), pos, chunk.distPackedHeader, chunk.distPackedStream))
            return {};
        if (!readStreamHeader(p, compressed.size(), pos, chunk.lrl8Header, chunk.lrl8Stream))
            return {};

        // Extra bits: extract mode from top 2 bits of size field
        if (pos + 4 > compressed.size()) return {};
        uint32_t packedSize = readLE32(p + pos); pos += 4;
        chunk.extraBitsMode = static_cast<uint8_t>(packedSize >> 30);
        const uint32_t extraSize = packedSize & 0x3FFFFFFF;
        if (pos + extraSize > compressed.size()) return {};
        chunk.extraBitsStream.assign(p + pos, p + pos + extraSize);
        pos += extraSize;
        if (!readRawStream(p, compressed.size(), pos, chunk.lrl8ExtraStream))
            return {};

        const auto decoded = LZHuffman::decompressChunk(chunk);
        output.insert(output.end(), decoded.begin(), decoded.end());
    }

    return output;
}

} // namespace compression

