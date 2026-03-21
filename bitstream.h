#pragma once
#include <cstdint>
#include <vector>
#include <cassert>
#include <array>
#include <stdexcept>

static uint32_t bitMask32(const uint8_t count)
{
    return (count >= 32) ? 0xFFFFFFFFu : ((1u << count) - 1u);
}

class BitWriter
{
    std::vector<uint8_t> buffer;
    uint64_t bitBuffer = 0;
    int bitsInBuffer = 0;
    size_t totalBits = 0;

    void drainBytes()
    {
        while (bitsInBuffer >= 8)
        {
            bitsInBuffer -= 8;
            buffer.push_back(static_cast<uint8_t>(bitBuffer >> bitsInBuffer));
        }
        bitBuffer &= (bitsInBuffer > 0) ? ((1ULL << bitsInBuffer) - 1) : 0;
    }

public:
    BitWriter() = default;

    void writeBits(const uint32_t bits, const uint8_t count)
    {
        assert(count <= 32);
        bitBuffer = (bitBuffer << count) | (static_cast<uint64_t>(bits) & bitMask32(count));
        bitsInBuffer += count;
        totalBits += count;

        // Defer flushing until we have at least 4 bytes worth of bits.
        // This reduces branch overhead and push_back calls.
        if (bitsInBuffer >= 32)
        {
            drainBytes();
        }
    }

    void flush()
    {
        drainBytes();
        if (bitsInBuffer > 0)
        {
            buffer.push_back(static_cast<uint8_t>(bitBuffer << (8 - bitsInBuffer)));
            bitBuffer = 0;
            bitsInBuffer = 0;
        }
    }

    [[nodiscard]] const std::vector<uint8_t>& getBuffer() const { return buffer; }
    [[nodiscard]] size_t getTotalBits() const { return totalBits; }
};

class BitReader
{
    const uint8_t* data = nullptr;
    size_t byteCount = 0;
    size_t bytePos = 0;
    uint32_t bitBuffer = 0;
    int bitsInBuffer = 0;
    size_t totalBits = 0;
    size_t bitsRead = 0;

public:
    BitReader() = default;

    BitReader(const uint8_t* d, const size_t size, const size_t totalBits_)
        : data(d), byteCount(size), totalBits(totalBits_)
    {
    }

    void refill(const uint8_t needed)
    {
        // Loads bytes from the source until bitsInBuffer >= needed.
        // needed <= 32, so at most 4 iterations. Zero-pads past end of source.
        for (int fill = bitsInBuffer; fill < needed; fill += 8)
        {
            const uint8_t next = (bytePos < byteCount) ? data[bytePos++] : 0;
            bitBuffer = (bitBuffer << 8) | next;
            bitsInBuffer += 8;
        }
    }

    uint32_t peekBits(const uint8_t n)
    {
        assert(n <= 32);
        refill(n);
        return (bitBuffer >> (bitsInBuffer - n)) & bitMask32(n);
    }

    void consumeBits(const uint8_t n)
    {
        assert(n <= 32);
        refill(n);
        bitsInBuffer -= n;
        bitBuffer &= bitMask32(static_cast<uint8_t>(bitsInBuffer));
        bitsRead += n;
    }

    [[nodiscard]] size_t getTotalBitsRead() const { return bitsRead; }
};

// Interleaved bitstream support for parallel decode
// Distributes symbols round-robin across 3 internal bitstreams
class InterleavedBitWriter
{
    std::array<BitWriter, 3> writers;
    int nextStream = 0;

public:
    void writeBits(const uint32_t bits, const uint8_t count)
    {
        writers[nextStream].writeBits(bits, count);
        nextStream = (nextStream + 1) % 3;
    }

    // Flush all streams and return serialized: [size0][size1][size2][bits0|bits1|bits2]
    std::vector<uint8_t> getInterleaved()
    {
        for (auto& w : writers) w.flush();

        std::vector<uint8_t> result;
        std::array<uint32_t, 3> sizes{};

        std::array<std::vector<uint8_t>, 3> buffers;

        for (int i = 0; i < 3; ++i)
        {
            buffers[i] = writers[i].getBuffer();
            sizes[i] = static_cast<uint32_t>(buffers[i].size());
        }

        // Write sizes (LE32)
        for (int i = 0; i < 3; ++i)
        {
            result.push_back(static_cast<uint8_t>(sizes[i]));
            result.push_back(static_cast<uint8_t>(sizes[i] >> 8));
            result.push_back(static_cast<uint8_t>(sizes[i] >> 16));
            result.push_back(static_cast<uint8_t>(sizes[i] >> 24));
        }

        // Write concatenated streams
        for (int i = 0; i < 3; ++i)
        {
            result.insert(result.end(), buffers[i].begin(), buffers[i].end());
        }

        return result;
    }

    [[nodiscard]] size_t getTotalBits() const
    {
        size_t total = 0;
        for (const auto& w : writers) total += w.getTotalBits();
        return total;
    }
};

class InterleavedBitReader
{
    std::array<BitReader, 3> readers;
    int nextStream = 0;

public:
    // Parse interleaved format: [size0 LE32][size1 LE32][size2 LE32][bits0|bits1|bits2]
    InterleavedBitReader(const uint8_t* data, const size_t dataSize)
    {
        if (dataSize < 12) throw std::runtime_error("Interleaved stream too small");

        uint32_t sizes[3];
        for (int i = 0; i < 3; ++i)
        {
            sizes[i] = static_cast<uint32_t>(data[i * 4])
                | (static_cast<uint32_t>(data[i * 4 + 1]) << 8)
                | (static_cast<uint32_t>(data[i * 4 + 2]) << 16)
                | (static_cast<uint32_t>(data[i * 4 + 3]) << 24);
        }

        size_t pos = 12;
        for (int i = 0; i < 3; ++i)
        {
            if (pos + sizes[i] > dataSize)
                throw std::runtime_error("Interleaved stream size mismatch");

            readers[i] = BitReader(data + pos, sizes[i], sizes[i] * 8ull);
            pos += sizes[i];
        }
    }

    // Peek bits from the current stream (does NOT advance to next stream)
    uint32_t peekBits(const uint8_t n)
    {
        return readers[nextStream].peekBits(n);
    }

    // Consume bits from the current stream AND advance to the next stream.
    // This must be called after peekBits to complete one symbol decode.
    void consumeBits(const uint8_t n)
    {
        readers[nextStream].consumeBits(n);
        nextStream = (nextStream + 1) % 3;
    }
};

// Bit writer that packs bits LSB-first into bytes, producing output that
// can be read from the end of a buffer by ReverseBitReader.
// Bits within each byte are filled from LSB toward MSB.
// The output bytes are in reverse order: byte[0] holds the LAST bits written.
class ReverseBitWriter
{
    std::vector<uint8_t> buffer;
    uint64_t bitBuffer = 0;
    int bitsInBuffer = 0;
    size_t totalBits = 0;

    void drainBytes()
    {
        while (bitsInBuffer >= 8)
        {
            buffer.push_back(static_cast<uint8_t>(bitBuffer & 0xFF));
            bitBuffer >>= 8;
            bitsInBuffer -= 8;
        }
    }

public:
    ReverseBitWriter() = default;

    void writeBits(const uint32_t bits, const uint8_t count)
    {
        assert(count <= 32);
        bitBuffer |= (static_cast<uint64_t>(bits) & bitMask32(count)) << bitsInBuffer;
        bitsInBuffer += count;
        totalBits += count;
        if (bitsInBuffer >= 32) drainBytes();
    }

    void flush()
    {
        drainBytes();
        if (bitsInBuffer > 0)
        {
            buffer.push_back(static_cast<uint8_t>(bitBuffer & 0xFF));
            bitBuffer = 0;
            bitsInBuffer = 0;
        }
    }

    // Returns bytes in reversed order (last-written bits in byte[0]).
    // The caller must reverse this to get the final on-wire layout.
    [[nodiscard]] const std::vector<uint8_t>& getBuffer() const { return buffer; }
    [[nodiscard]] size_t getTotalBits() const { return totalBits; }
};

// Bit reader that reads from the END of a byte buffer, consuming bytes
// backward and bits LSB-first within each byte. Mirrors ReverseBitWriter.
class ReverseBitReader
{
    const uint8_t* data = nullptr;
    size_t byteCount = 0;
    size_t bytePos = 0; // next byte to consume (counting from the end)
    uint32_t bitBuffer = 0;
    int bitsInBuffer = 0;

public:
    void refill(const uint8_t needed)
    {
        // Loads bytes from the end of the source until bitsInBuffer >= needed.
        // needed <= 32, so at most 4 iterations. Zero-pads past start of source.
        for (int fill = bitsInBuffer; fill < needed; fill += 8)
        {
            const uint8_t next = (bytePos < byteCount)
                                     ? data[byteCount - 1 - bytePos++]
                                     : 0;
            bitBuffer |= (static_cast<uint32_t>(next) << bitsInBuffer);
            bitsInBuffer += 8;
        }
    }

    ReverseBitReader() = default;

    ReverseBitReader(const uint8_t* d, const size_t size)
        : data(d), byteCount(size)
    {
    }

    uint32_t peekBits(const uint8_t n)
    {
        assert(n <= 32);
        refill(n);
        return bitBuffer & bitMask32(n);
    }

    void consumeBits(const uint8_t n)
    {
        assert(n <= 32);
        refill(n);
        bitBuffer >>= n;
        bitsInBuffer -= n;
    }
};

// Bidirectional extra-bits writer: even-indexed items write forward (MSB-first),
// odd-indexed items write backward (LSB-first from end of buffer).
// Output layout: [fwdSize LE16][fwdBytes][bwdBytes]
// The decoder reads fwd from the front and bwd from the end of the combined buffer.
class BidirectionalBitWriter
{
    BitWriter fwd;
    ReverseBitWriter bwd;
    int index = 0;

public:
    BidirectionalBitWriter() = default;

    void writeBits(const uint32_t bits, const uint8_t count)
    {
        if (index & 1)
            bwd.writeBits(bits, count);
        else
            fwd.writeBits(bits, count);
        ++index;
    }

    // Returns mode: 0 = empty, 1 = fwd-only, 2 = bidirectional
    [[nodiscard]] uint8_t getMode() const
    {
        const bool hasFwd = (fwd.getTotalBits() > 0);
        const bool hasBwd = (bwd.getTotalBits() > 0);
        if (!hasFwd && !hasBwd) return 0;
        if (!hasBwd) return 1;
        return 2;
    }

    // Flush and return the serialized extra bits stream.
    // Mode 0 (empty): returns empty.
    // Mode 1 (fwd-only): returns raw forward bytes (no header).
    // Mode 2 (bidirectional): returns [fwdSize LE16][fwdBytes][bwdBytes].
    //   bwdBytes are reversed from ReverseBitWriter output so that the
    //   last byte in the buffer holds the first-written backward bits —
    //   the decoder reads bytes from the end to recover them in order.
    std::vector<uint8_t> finish()
    {
        fwd.flush();
        bwd.flush();

        const auto& fwdBuf = fwd.getBuffer();
        const auto& bwdRaw = bwd.getBuffer(); // reversed order from writer

        if (fwdBuf.empty() && bwdRaw.empty())
            return {};

        if (bwdRaw.empty())
            return fwdBuf; // mode 1: raw forward bytes, no header

        // Mode 2: bidirectional
        // ReverseBitWriter produces bytes with last-written bits in byte[0].
        // We reverse so that byte[last] holds last-written bits --
        // the decoder's ReverseBitReader reads from byte[last] backward.
        std::vector<uint8_t> bwdBuf(bwdRaw.rbegin(), bwdRaw.rend());

        std::vector<uint8_t> result;
        result.reserve(2 + fwdBuf.size() + bwdBuf.size());

        // LE16 split marker: fwd byte count (max 65535, sufficient for 128 KiB chunks)
        auto fwdSize = static_cast<uint16_t>(fwdBuf.size());
        result.push_back(static_cast<uint8_t>(fwdSize));
        result.push_back(static_cast<uint8_t>(fwdSize >> 8));

        result.insert(result.end(), fwdBuf.begin(), fwdBuf.end());
        result.insert(result.end(), bwdBuf.begin(), bwdBuf.end());
        return result;
    }

    [[nodiscard]] size_t getTotalBits() const
    {
        return fwd.getTotalBits() + bwd.getTotalBits();
    }
};

// Bidirectional extra-bits reader: reads even-indexed items forward from
// the front of the buffer, odd-indexed items backward from the end.
// Two independent cursors enable two parallel dependency chains.
class BidirectionalBitReader
{
    BitReader fwdReader;
    ReverseBitReader bwdReader;
    uint8_t mode = 0;
    int index = 0;

public:
    BidirectionalBitReader() = default;

    // mode: 0 = empty, 1 = fwd-only, 2 = bidirectional
    BidirectionalBitReader(const uint8_t mode_, const uint8_t* data, const size_t dataSize)
        : mode(mode_)
    {
        if (mode_ == 0 || dataSize == 0) return;

        if (mode_ == 1)
        {
            fwdReader = BitReader(data, dataSize, dataSize * 8ull);
        }
        else
        {
            // Bidirectional: [fwdSize LE16][fwdBytes][bwdBytes]
            if (dataSize < 2) return;

            uint16_t fwdSize = static_cast<uint16_t>(data[0])
                | (static_cast<uint16_t>(data[1]) << 8);

            const uint8_t* fwdData = data + 2;
            size_t bwdSize = dataSize - 2 - fwdSize;
            const uint8_t* bwdData = data + 2 + fwdSize;

            fwdReader = BitReader(fwdData, fwdSize, fwdSize * 8ull);
            bwdReader = ReverseBitReader(bwdData, bwdSize);
        }
    }

    uint32_t peekBits(const uint8_t n)
    {
        if (mode == 0) return 0;
        if (mode == 1) return fwdReader.peekBits(n);
        return (index & 1) ? bwdReader.peekBits(n) : fwdReader.peekBits(n);
    }

    void consumeBits(const uint8_t n)
    {
        if (mode == 0) return;
        if (mode == 1)
        {
            fwdReader.consumeBits(n);
            ++index;
            return;
        }
        if (index & 1)
            bwdReader.consumeBits(n);
        else
            fwdReader.consumeBits(n);
        ++index;
    }
};
