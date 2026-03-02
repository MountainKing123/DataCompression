#pragma once
#include <cstdint>
#include <vector>
#include <cassert>
#include <cstddef>

class BitWriter {
    std::vector<uint8_t> buffer;
    uint32_t bitBuffer = 0;
    int bitsInBuffer = 0;
    size_t totalBits = 0;

public:
    BitWriter() = default;

    void writeBits(uint32_t bits, uint8_t count) {
        assert(count <= 32);
        bitBuffer = (bitBuffer << count) | (bits & ((1u << count) - 1));
        bitsInBuffer += count;
        totalBits += count;

        while (bitsInBuffer >= 8) {
            bitsInBuffer -= 8;
            buffer.push_back(uint8_t(bitBuffer >> bitsInBuffer));
            bitBuffer &= (1u << bitsInBuffer) - 1;
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
        int bitsLeft = (int)(totalBits - bitsRead);
        if (n > bitsLeft) n = bitsLeft;

        while (bitsInBuffer < n && bytePos < byteCount) {
            bitBuffer = (bitBuffer << 8) | data[bytePos++];
            bitsInBuffer += 8;
        }

        return (bitBuffer >> (bitsInBuffer - n)) & ((1u << n) - 1);
    }

    void consumeBits(uint8_t n) {
        int bitsLeft = (int)(totalBits - bitsRead);
        if (n > bitsLeft) n = bitsLeft;

        bitsInBuffer -= n;
        bitBuffer &= (1u << bitsInBuffer) - 1;
        bitsRead += n;
    }

    size_t getTotalBitsRead() const { return bitsRead; }
};