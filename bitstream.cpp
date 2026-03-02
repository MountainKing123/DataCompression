#include "bitstream.h"

#include <cassert>

#pragma region BitWriter
void BitWriter::write_bits(const uint32_t bits, const int count)
{
    // Append bits to the bit_buffer_
    // We store them in the least significant bits
    bit_buffer_ |= static_cast<uint64_t>(bits) << bit_count_;
    bit_count_ += count;

    while (bit_count_ >= 8)
    {
        // Take the least significant byte from bit_buffer_ and store it
        data_.push_back(static_cast<uint8_t>(bit_buffer_ & 0xFF));

        // Shift out the byte we just wrote
        bit_buffer_ >>= 8;

        // Reduce count by 8
        bit_count_ -= 8;
    }
}

void BitWriter::flush()
{
    if (bit_count_ > 0)
    {
        // Remaining bits (less than 8) are written as a full byte
        data_.push_back(static_cast<uint8_t>(bit_buffer_));
        bit_buffer_ = 0;
        bit_count_ = 0;
    }
}
#pragma endregion BitWriter

#pragma region BitReader
BitReader::BitReader(const uint8_t* data, size_t size)
    : ptr_(data), end_(data + size)
{
}

uint32_t BitReader::read_bits(const int count)
{
    // Ensure bit_buffer_ has at least `count` bits
    while (bit_count_ < count)
    {
        assert(ptr_ < end_ && "Reading past end of buffer");

        // Pull in the next byte into the buffer
        bit_buffer_ |= static_cast<uint64_t>(*ptr_++) << bit_count_;
        bit_count_ += 8;
    }

    // Extract the requested bits from the least significant bits
    const uint32_t result = static_cast<uint32_t>(bit_buffer_ & ((1ull << count) - 1));

    // Remove the bits we just consumed
    bit_buffer_ >>= count;
    bit_count_ -= count;

    return result;
}
#pragma endregion BitReader
