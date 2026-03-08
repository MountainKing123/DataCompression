#pragma once
#include <cstdint>
#include <vector>
#include <cassert>
#include <cstddef>

namespace {
inline uint32_t bitMask32(uint8_t count)
{
    return (count >= 32) ? 0xFFFFFFFFu : ((1u << count) - 1u);
}
}

class BitWriter {
    std::vector<uint8_t> buffer;
    uint32_t bitBuffer = 0;
    int bitsInBuffer = 0;
    size_t totalBits = 0;

public:
    BitWriter() = default;

    void writeBits(uint32_t bits, uint8_t count) {
        assert(count <= 32);
        bitBuffer = (bitBuffer << count) | (bits & bitMask32(count));
        bitsInBuffer += count;
        totalBits += count;

        while (bitsInBuffer >= 8) {
            bitsInBuffer -= 8;
            buffer.push_back(uint8_t(bitBuffer >> bitsInBuffer));
            bitBuffer &= bitMask32(static_cast<uint8_t>(bitsInBuffer));
        }
    }

    void flush() {
        if (bitsInBuffer > 0) {
            buffer.push_back(uint8_t(bitBuffer << (8 - bitsInBuffer)));
            bitBuffer = 0;
            bitsInBuffer = 0;
        }
    }

    const std::vector<uint8_t>& getBuffer() const { return buffer; }
    size_t getTotalBits() const { return totalBits; }
};

class BitReader {
    const uint8_t* data;
    size_t byteCount;
    size_t bytePos = 0;
    uint32_t bitBuffer = 0;
    int bitsInBuffer = 0;
    size_t totalBits = 0;
    size_t bitsRead = 0;

public:
    BitReader(const uint8_t* d, size_t size, size_t totalBits_)
        : data(d), byteCount(size), totalBits(totalBits_) {}

    uint32_t peekBits(uint8_t n) {
        assert(n <= 32);

        // Keep fixed-width peek semantics by zero-padding if the source is exhausted.
        while (bitsInBuffer < n) {
            uint8_t next = (bytePos < byteCount) ? data[bytePos++] : 0;
            bitBuffer = (bitBuffer << 8) | next;
            bitsInBuffer += 8;
        }

        return (bitBuffer >> (bitsInBuffer - n)) & bitMask32(n);
    }

    void consumeBits(uint8_t n) {
        assert(n <= 32);
        while (bitsInBuffer < n) {
            uint8_t next = (bytePos < byteCount) ? data[bytePos++] : 0;
            bitBuffer = (bitBuffer << 8) | next;
            bitsInBuffer += 8;
        }

        bitsInBuffer -= n;
        bitBuffer &= bitMask32(static_cast<uint8_t>(bitsInBuffer));
        bitsRead += n;
    }

    size_t getTotalBitsRead() const { return bitsRead; }
};