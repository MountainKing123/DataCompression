#include "huffman.h"
#include "bitstream.h"
#include <cstring>
#include <algorithm>
#include <cassert>

void Huffman::buildFrequencies(const std::vector<uint8_t>& input,
                               std::array<uint32_t, MaxSymbols>& freq)
{
    freq.fill(0);
    for (uint8_t b : input)
        freq[b]++;
}

void Huffman::buildCodeLengths(const std::array<uint32_t, MaxSymbols>& freq,
                               std::array<uint8_t, MaxSymbols>& lengths)
{
    struct Node {
        uint32_t frequency;
        int16_t left;
        int16_t right;
        int16_t parent;
        int16_t symbol;
    };

    std::array<Node, MaxSymbols * 2> nodes{};
    int16_t nodeCount = 0;
    std::array<int16_t, MaxSymbols> active{};
    int16_t activeCount = 0;
    lengths.fill(0);

    // Initialize leaves
    for (int16_t i = 0; i < MaxSymbols; ++i)
    {
        if (freq[i] > 0)
        {
            nodes[nodeCount] = { freq[i], -1, -1, -1, i };
            active[activeCount++] = nodeCount++;
        }
    }

    if (activeCount == 0) return;
    if (activeCount == 1) { lengths[nodes[active[0]].symbol] = 1; return; }

    // Build Huffman tree
    while (activeCount > 1)
    {
        std::sort(active.begin(), active.begin() + activeCount,
                  [&](int16_t a, int16_t b) { return nodes[a].frequency < nodes[b].frequency; });

        int16_t a = active[0];
        int16_t b = active[1];

        nodes[nodeCount] = {
            nodes[a].frequency + nodes[b].frequency,
            a,
            b,
            -1,
            -1
        };

        nodes[a].parent = nodes[b].parent = nodeCount;

        // Update active list
        active[0] = active[activeCount - 1];
        active[1] = nodeCount;
        activeCount--;
        nodeCount++;
    }

    // Assign code lengths
    for (int16_t i = 0; i < nodeCount; ++i)
    {
        if (nodes[i].symbol >= 0)
        {
            int depth = 0;
            int16_t current = i;
            while (nodes[current].parent != -1) { depth++; current = nodes[current].parent; }
            if (depth > MaxCodeLength) depth = MaxCodeLength;
            lengths[nodes[i].symbol] = static_cast<uint8_t>(depth);
        }
    }
}

void Huffman::buildCanonicalCodes(const std::array<uint8_t, MaxSymbols>& lengths,
                                  std::array<HuffmanCode, MaxSymbols>& codes)
{
    codes.fill({0,0});
    std::array<int, MaxCodeLength + 1> blCount{};
    for (int i = 0; i < MaxSymbols; ++i) if (lengths[i]) blCount[lengths[i]]++;

    std::array<uint16_t, MaxCodeLength + 1> nextCode{};
    uint16_t code = 0;
    for (int bits = 1; bits <= MaxCodeLength; ++bits)
    {
        code = (code + blCount[bits-1]) << 1;
        nextCode[bits] = code;
    }

    for (int i = 0; i < MaxSymbols; ++i)
    {
        uint8_t len = lengths[i];
        if (len)
        {
            codes[i] = { nextCode[len], len };
            nextCode[len]++;
        }
    }
}

void Huffman::buildDecodeTable(const std::array<HuffmanCode, MaxSymbols>& codes,
                               std::array<int16_t, DecodeTableSize>& table)
{
    table.fill(-1);

    for (int symbol = 0; symbol < MaxSymbols; ++symbol)
    {
        const HuffmanCode& c = codes[symbol];
        if (!c.length) continue;

        // replicate code into all entries for lookup
        int shift = MaxCodeLength - c.length;
        int start = c.bits << shift;
        int end = (c.bits + 1) << shift;

        for (int i = start; i < end; ++i)
            table[i] = symbol;
    }
}

// ----------------------------
// Compress / Decompress
// ----------------------------

std::vector<uint8_t> Huffman::compress(const std::vector<uint8_t>& input)
{
    std::array<uint32_t, MaxSymbols> freq{};
    std::array<uint8_t, MaxSymbols> lengths{};
    std::array<HuffmanCode, MaxSymbols> codes{};
    buildFrequencies(input, freq);
    buildCodeLengths(freq, lengths);
    buildCanonicalCodes(lengths, codes);

    BitWriter writer;

    // Header: code lengths
    for (uint8_t l : lengths) writer.writeBits(l, 8);

    // Original size (big endian)
    uint32_t size = static_cast<uint32_t>(input.size());
    writer.writeBits((size >> 24) & 0xFF, 8);
    writer.writeBits((size >> 16) & 0xFF, 8);
    writer.writeBits((size >> 8) & 0xFF, 8);
    writer.writeBits(size & 0xFF, 8);

    for (uint8_t b : input)
    {
        const HuffmanCode& c = codes[b];
        writer.writeBits(c.bits, c.length);
    }

    // Store total bits written (before padding)
    size_t totalBits = writer.getTotalBits();
    writer.flush();

    // Append total bits as 4 bytes at the end (big endian)
    auto& buffer = const_cast<std::vector<uint8_t>&>(writer.getBuffer());
    buffer.push_back(static_cast<uint8_t>((totalBits >> 24) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((totalBits >> 16) & 0xFF));
    buffer.push_back(static_cast<uint8_t>((totalBits >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>(totalBits & 0xFF));

    return buffer;
}

std::vector<uint8_t> Huffman::decompress(const std::vector<uint8_t>& input)
{
    std::array<HuffmanCode, MaxSymbols> codes{};
    std::array<uint8_t, MaxSymbols> lengths{};
    std::array<int16_t, DecodeTableSize> decodeTable{};

    // Read total bits from the last 4 bytes
    if (input.size() < 4) return std::vector<uint8_t>();

    size_t totalBits = 0;
    totalBits |= (static_cast<size_t>(input[input.size() - 4]) << 24);
    totalBits |= (static_cast<size_t>(input[input.size() - 3]) << 16);
    totalBits |= (static_cast<size_t>(input[input.size() - 2]) << 8);
    totalBits |= (static_cast<size_t>(input[input.size() - 1]));

    // Create reader with data excluding the last 4 bytes
    BitReader reader(input.data(), input.size() - 4, totalBits);

    // Read header
    for (int i = 0; i < MaxSymbols; ++i)
        lengths[i] = static_cast<uint8_t>(reader.peekBits(8)), reader.consumeBits(8);

    buildCanonicalCodes(lengths, codes);
    buildDecodeTable(codes, decodeTable);

    // Read original size
    uint32_t size = 0;
    for (int i = 0; i < 4; ++i)
    {
        uint8_t byte = static_cast<uint8_t>(reader.peekBits(8));
        size = (size << 8) | byte;
        reader.consumeBits(8);
    }

    std::vector<uint8_t> output;
    output.reserve(size);

    while (output.size() < size)
    {
        // Check how many valid bits we have left
        int bitsLeft = static_cast<int>(totalBits) - static_cast<int>(reader.getTotalBitsRead());
        if (bitsLeft <= 0) break;

        // Only peek as many bits as we actually have (don't peek padding)
        // Clamp to a reasonable max for decoding (most codes are much shorter than MaxCodeLength)
        int bitsToLookup = std::min(bitsLeft, MaxCodeLength);

        int code = reader.peekBits(bitsToLookup);

        // Shift the code to align it as if we peeked MaxCodeLength bits
        // This is necessary because the decode table expects MaxCodeLength-bit aligned codes
        if (bitsToLookup < MaxCodeLength) {
            code <<= (MaxCodeLength - bitsToLookup);
        }

        int symbol = decodeTable[code];

        if (symbol < 0 || symbol >= MaxSymbols) {
            // Invalid symbol
            break;
        }

        output.push_back(static_cast<uint8_t>(symbol));
        reader.consumeBits(codes[symbol].length);
    }

    return output;
}