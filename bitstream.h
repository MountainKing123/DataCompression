#pragma once
#include <vector>
#include <cstdint>

class BitWriter
{
public:
    using Byte = uint8_t;

    // Write `count` least-significant bits of `bits` to the stream.
    // Example: write_bits(0b101, 3) will write three bits: 1,0,1
    void write_bits(uint32_t bits, int count);

    // Flush any remaining bits to the output buffer (pad to 8 bits)
    void flush();

    // Access the underlying byte buffer
    const std::vector<Byte>& data() const { return data_; }

private:
    std::vector<Byte> data_; // Output buffer storing full bytes
    uint64_t bit_buffer_ = 0; // Temporary buffer for bits before we have 8
    int bit_count_ = 0; // Number of valid bits in bit_buffer_
};

// BitReader allows reading arbitrary bits from a byte-aligned buffer.
class BitReader
{
public:
    // Construct with pointer to data + size
    BitReader(const uint8_t* data, size_t size);

    // Read `count` bits from the stream, returns them in the least significant bits of a uint32_t
    uint32_t read_bits(int count);

    const uint8_t* ptr_; // Current read pointer
    const uint8_t* end_; // End of buffer

private:
    uint64_t bit_buffer_ = 0; // Temporary buffer storing bits before consuming
    int bit_count_ = 0; // Number of valid bits in bit_buffer_
};
