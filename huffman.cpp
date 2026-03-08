#include "huffman.h"
#include "bitstream.h"
#include <algorithm>
#include <queue>

namespace
{
    constexpr uint8_t kMagic0 = 'H';
    constexpr uint8_t kMagic1 = 'F';
    constexpr uint8_t kFormatVersion = 1;
    constexpr uint8_t kModeChunked = 2;
    constexpr uint8_t kModeDense = 0;
    constexpr uint8_t kModeSparse = 1;

    void writeByte(BitWriter& writer, uint8_t v) { writer.writeBits(v, 8); }

    void writeU32BE(BitWriter& writer, uint32_t v)
    {
        writeByte(writer, static_cast<uint8_t>((v >> 24) & 0xFF));
        writeByte(writer, static_cast<uint8_t>((v >> 16) & 0xFF));
        writeByte(writer, static_cast<uint8_t>((v >> 8) & 0xFF));
        writeByte(writer, static_cast<uint8_t>(v & 0xFF));
    }

    uint8_t readByte(BitReader& reader)
    {
        auto v = static_cast<uint8_t>(reader.peekBits(8));
        reader.consumeBits(8);
        return v;
    }

    uint32_t readU32BE(BitReader& reader)
    {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            v = (v << 8) | readByte(reader);
        }
        return v;
    }

    uint16_t packSecondaryEntry(uint8_t symbol, uint8_t length)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(symbol) << 8) | length);
    }
}

// ============================================================================
// PARALLEL FREQUENCY COUNTING
// ============================================================================

void Huffman::buildFrequenciesSerial(const std::vector<uint8_t>& input,
                                     std::array<uint32_t, MaxSymbols>& freq)
{
    freq.fill(0);
    for (uint8_t b : input)
    {
        freq[b]++;
    }
}

void Huffman::buildFrequenciesParallel(const std::vector<uint8_t>& input,
                                       std::array<uint32_t, MaxSymbols>& freq,
                                       size_t numThreads)
{
    if (numThreads <= 1)
    {
        return buildFrequenciesSerial(input, freq);
    }

    std::vector<std::array<uint32_t, MaxSymbols>> localFreqs(numThreads);
    const size_t chunkSize = (input.size() + numThreads - 1) / numThreads;

    {
        std::vector<std::jthread> threads;

        for (size_t t = 0; t < numThreads; ++t)
        {
            const size_t start = t * chunkSize;
            const size_t end = std::min(start + chunkSize, input.size());

            threads.emplace_back([&localFreqs, &input, t, start, end]()
            {
                localFreqs[t].fill(0);
                for (size_t i = start; i < end; ++i)
                {
                    localFreqs[t][input[i]]++;
                }
            });
        }
    }

    freq.fill(0);
    for (const auto& lf : localFreqs)
    {
        for (int i = 0; i < MaxSymbols; ++i)
        {
            freq[i] += lf[i];
        }
    }
}

// ============================================================================
// HUFFMAN TREE & CODE GENERATION
// ============================================================================

void Huffman::buildCodeLengths(const std::array<uint32_t, MaxSymbols>& freq,
                               std::array<uint8_t, MaxSymbols>& lengths)
{
    struct Node
    {
        uint32_t frequency;
        int16_t left, right, parent, symbol;
    };

    std::array<Node, MaxSymbols * 2> nodes{};
    int16_t nodeCount = 0;
    lengths.fill(0);

    struct MinNode
    {
        const std::array<Node, MaxSymbols * 2>* nodes;

        bool operator()(int16_t a, int16_t b) const
        {
            if ((*nodes)[a].frequency != (*nodes)[b].frequency)
                return (*nodes)[a].frequency > (*nodes)[b].frequency;
            return a > b;
        }
    };

    std::priority_queue<int16_t, std::vector<int16_t>, MinNode> minHeap((MinNode{&nodes}));

    for (int16_t i = 0; i < MaxSymbols; ++i)
    {
        if (freq[i] > 0)
        {
            nodes[nodeCount] = {freq[i], -1, -1, -1, i};
            minHeap.push(nodeCount++);
        }
    }

    if (minHeap.empty()) return;
    if (minHeap.size() == 1)
    {
        lengths[nodes[minHeap.top()].symbol] = 1;
        return;
    }

    while (minHeap.size() > 1)
    {
        int16_t a = minHeap.top();
        minHeap.pop();
        int16_t b = minHeap.top();
        minHeap.pop();
        nodes[nodeCount] = {nodes[a].frequency + nodes[b].frequency, a, b, -1, -1};
        nodes[a].parent = nodes[b].parent = nodeCount;
        minHeap.push(nodeCount++);
    }

    for (int16_t i = 0; i < nodeCount; ++i)
    {
        if (nodes[i].symbol >= 0)
        {
            int depth = 0;
            int16_t current = i;
            while (nodes[current].parent != -1)
            {
                depth++;
                current = nodes[current].parent;
            }
            if (depth > MaxCodeLength) depth = MaxCodeLength;
            lengths[nodes[i].symbol] = static_cast<uint8_t>(depth);
        }
    }
}

void Huffman::buildCanonicalCodes(const std::array<uint8_t, MaxSymbols>& lengths,
                                  std::array<HuffmanCode, MaxSymbols>& codes)
{
    codes.fill({0, 0});
    std::array<int, MaxCodeLength + 1> blCount{};
    for (int i = 0; i < MaxSymbols; ++i) if (lengths[i]) blCount[lengths[i]]++;

    std::array<uint16_t, MaxCodeLength + 1> nextCode{};
    uint16_t code = 0;
    for (int bits = 1; bits <= MaxCodeLength; ++bits)
    {
        code = static_cast<uint16_t>((code + blCount[bits - 1]) << 1);
        nextCode[bits] = code;
    }

    for (int i = 0; i < MaxSymbols; ++i)
    {
        uint8_t len = lengths[i];
        if (len)
        {
            codes[i] = {nextCode[len], len};
            nextCode[len]++;
        }
    }
}

void Huffman::buildDecodeTable(const std::array<HuffmanCode, MaxSymbols>& codes,
                               std::array<PrimaryDecodeEntry, PrimaryDecodeSize>& primaryTable,
                               std::vector<uint16_t>& secondaryTable)
{
    primaryTable.fill({});
    secondaryTable.clear();

    std::array<uint8_t, PrimaryDecodeSize> maxSecondaryBits{};
    maxSecondaryBits.fill(0);

    // Pass 1: determine required secondary width
    for (int symbol = 0; symbol < MaxSymbols; ++symbol)
    {
        const HuffmanCode& c = codes[symbol];
        if (!c.length || c.length <= PrimaryDecodeBits) continue;

        const auto primaryPrefix = static_cast<uint16_t>(c.bits >> (c.length - PrimaryDecodeBits));
        const auto extraBits = static_cast<uint8_t>(c.length - PrimaryDecodeBits);
        if (extraBits > maxSecondaryBits[primaryPrefix])
        {
            maxSecondaryBits[primaryPrefix] = extraBits;
        }
    }

    // Allocate secondary subtables
    auto nextOffset = uint16_t{0};
    for (int i = 0; i < PrimaryDecodeSize; ++i)
    {
        if (maxSecondaryBits[i] == 0) continue;

        primaryTable[i].secondaryBits = maxSecondaryBits[i];
        primaryTable[i].secondaryOffset = nextOffset;

        const auto subtableSize = static_cast<uint16_t>(1u << maxSecondaryBits[i]);
        secondaryTable.resize(static_cast<size_t>(nextOffset) + subtableSize, 0);
        nextOffset = static_cast<uint16_t>(nextOffset + subtableSize);
    }

    // Pass 2: fill tables
    for (int symbol = 0; symbol < MaxSymbols; ++symbol)
    {
        const HuffmanCode& c = codes[symbol];
        if (c.length == 0) continue;

        if (c.length <= PrimaryDecodeBits)
        {
            const auto replicatedStart = static_cast<uint16_t>(c.bits << (PrimaryDecodeBits - c.length));
            const auto replicatedEnd = static_cast<uint16_t>((c.bits + 1) << (PrimaryDecodeBits - c.length));
            for (auto i = replicatedStart; i < replicatedEnd; ++i)
            {
                primaryTable[i].symbol = static_cast<int16_t>(symbol);
                primaryTable[i].length = c.length;
                primaryTable[i].secondaryBits = 0;
                primaryTable[i].secondaryOffset = 0;
            }
        }
        else
        {
            const auto primaryPrefix = static_cast<uint16_t>(c.bits >> (c.length - PrimaryDecodeBits));
            const PrimaryDecodeEntry& root = primaryTable[primaryPrefix];
            const auto extraBits = static_cast<uint8_t>(c.length - PrimaryDecodeBits);
            const auto suffixMask = static_cast<uint16_t>((1u << extraBits) - 1u);
            const auto suffix = static_cast<uint16_t>(c.bits & suffixMask);

            const auto expandShift = static_cast<uint8_t>(root.secondaryBits - extraBits);
            const auto subStart = static_cast<uint16_t>(suffix << expandShift);
            const auto subEnd = static_cast<uint16_t>((suffix + 1u) << expandShift);

            const auto packed = packSecondaryEntry(static_cast<uint8_t>(symbol), c.length);
            for (auto i = subStart; i < subEnd; ++i)
            {
                secondaryTable[static_cast<size_t>(root.secondaryOffset) + i] = packed;
            }
        }
    }
}

// ============================================================================
// COMPRESSION (with optional parallel frequency counting)
// ============================================================================

std::vector<uint8_t> Huffman::compress(const std::vector<uint8_t>& input,
                                       const CompressOptions& opts)
{
    std::array<uint32_t, MaxSymbols> freq{};
    std::array<uint8_t, MaxSymbols> lengths{};
    std::array<HuffmanCode, MaxSymbols> codes{};

    // Frequency counting: parallel or serial based on options
    if (opts.parallelFrequencyCount && input.size() >= opts.parallelThreshold)
    {
        buildFrequenciesParallel(input, freq, opts.numThreads);
    }
    else
    {
        buildFrequenciesSerial(input, freq);
    }

    buildCodeLengths(freq, lengths);
    buildCanonicalCodes(lengths, codes);

    int usedSymbols = 0;
    for (uint8_t len : lengths)
    {
        if (len) usedSymbols++;
    }

    const size_t denseHeaderBytes = 256;
    const size_t sparseHeaderBytes = (usedSymbols == 0) ? 0 : (1 + static_cast<size_t>(usedSymbols) * 2);
    const bool useSparse = (usedSymbols > 0 && sparseHeaderBytes < denseHeaderBytes);

    BitWriter writer;

    writeByte(writer, kMagic0);
    writeByte(writer, kMagic1);
    writeByte(writer, kFormatVersion);
    writeByte(writer, useSparse ? kModeSparse : kModeDense);
    writeU32BE(writer, static_cast<uint32_t>(input.size()));

    if (useSparse)
    {
        writeByte(writer, static_cast<uint8_t>(usedSymbols - 1));
        for (int symbol = 0; symbol < MaxSymbols; ++symbol)
        {
            if (lengths[symbol])
            {
                writeByte(writer, static_cast<uint8_t>(symbol));
                writeByte(writer, lengths[symbol]);
            }
        }
    }
    else
    {
        for (uint8_t len : lengths)
        {
            writeByte(writer, len);
        }
    }

    for (uint8_t b : input)
    {
        const HuffmanCode& c = codes[b];
        writer.writeBits(c.bits, c.length);
    }

    writer.flush();
    return writer.getBuffer();
}

std::vector<uint8_t> Huffman::compress(const std::vector<uint8_t>& input)
{
    return compress(input, CompressOptions{});
}

// ============================================================================
// DECOMPRESSION (Multi-pass architecture)
// ============================================================================

// Pass 1: Parse compressed stream into symbol array
Huffman::DecodedStream Huffman::parsePass(const std::vector<uint8_t>& compressed)
{
    std::array<HuffmanCode, MaxSymbols> codes{};
    std::array<uint8_t, MaxSymbols> lengths{};
    std::array<PrimaryDecodeEntry, PrimaryDecodeSize> primaryDecodeTable{};
    std::vector<uint16_t> secondaryDecodeTable;

    if (compressed.size() < 8) return {std::vector<uint8_t>(), 0};

    BitReader reader(compressed.data(), compressed.size(), compressed.size() * 8ull);

    const uint8_t magic0 = readByte(reader);
    const uint8_t magic1 = readByte(reader);
    const uint8_t version = readByte(reader);
    const uint8_t mode = readByte(reader);

    if (magic0 != kMagic0 || magic1 != kMagic1 || version != kFormatVersion)
    {
        return {std::vector<uint8_t>(), 0};
    }

    const uint32_t size = readU32BE(reader);
    lengths.fill(0);

    if (mode == kModeDense)
    {
        for (int i = 0; i < MaxSymbols; ++i)
        {
            lengths[i] = readByte(reader);
        }
    }
    else if (mode == kModeSparse)
    {
        if (size == 0) return {std::vector<uint8_t>(), 0};

        const int usedSymbols = static_cast<int>(readByte(reader)) + 1;
        for (int i = 0; i < usedSymbols; ++i)
        {
            uint8_t symbol = readByte(reader);
            uint8_t len = readByte(reader);
            lengths[symbol] = len;
        }
    }
    else
    {
        return {std::vector<uint8_t>(), 0};
    }

    if (size == 0) return {std::vector<uint8_t>(), 0};

    buildCanonicalCodes(lengths, codes);
    buildDecodeTable(codes, primaryDecodeTable, secondaryDecodeTable);

    // Decode all symbols into array (this is the "parse pass")
    std::vector<uint8_t> symbols;
    symbols.reserve(size);

    while (symbols.size() < size)
    {
        const uint32_t rootCode = reader.peekBits(PrimaryDecodeBits);
        const PrimaryDecodeEntry& entry = primaryDecodeTable[rootCode];

        if (entry.symbol >= 0)
        {
            symbols.push_back(static_cast<uint8_t>(entry.symbol));
            reader.consumeBits(entry.length);
            continue;
        }

        if (entry.secondaryBits == 0) break;

        const uint32_t fullCode = reader.peekBits(static_cast<uint8_t>(PrimaryDecodeBits + entry.secondaryBits));
        const uint32_t suffixMask = (1u << entry.secondaryBits) - 1u;
        const uint32_t suffix = fullCode & suffixMask;
        const uint16_t packed = secondaryDecodeTable[static_cast<size_t>(entry.secondaryOffset) + suffix];
        if (packed == 0) break;

        const auto symbol = static_cast<uint8_t>(packed >> 8);
        const auto length = static_cast<uint8_t>(packed & 0xFF);
        symbols.push_back(symbol);
        reader.consumeBits(length);
    }

    return {std::move(symbols), size};
}

// Pass 2: Apply decoded symbols
std::vector<uint8_t> Huffman::applyPass(const DecodedStream& decoded)
{
    // For Huffman, symbols ARE the output (no LZ77 match expansion needed)
    // This pass exists for API consistency with more complex codecs
    return decoded.symbols;
}

// Combined decompress (runs both passes automatically)
std::vector<uint8_t> Huffman::decompress(const std::vector<uint8_t>& input,
                                         const DecompressOptions& opts)
{
    if (opts.separatePasses)
    {
        // Caller will manage passes separately
        // (This is just a flag check; actual separation requires caller to use parsePass/applyPass)
        return {};
    }

    // Standard path: run both passes
    auto decoded = parsePass(input);
    return applyPass(decoded);
}

std::vector<uint8_t> Huffman::decompress(const std::vector<uint8_t>& input)
{
    return decompress(input, DecompressOptions{});
}

// ============================================================================
// CHUNK-BASED COMPRESSION
// ============================================================================

std::vector<uint8_t> Huffman::compressChunk(const std::vector<uint8_t>& chunkData,
                                            const CompressOptions& opts)
{
    std::array<uint32_t, MaxSymbols> freq{};
    std::array<uint8_t, MaxSymbols> lengths{};
    std::array<HuffmanCode, MaxSymbols> codes{};

    if (opts.parallelFrequencyCount && chunkData.size() >= opts.parallelThreshold)
    {
        buildFrequenciesParallel(chunkData, freq, opts.numThreads);
    }
    else
    {
        buildFrequenciesSerial(chunkData, freq);
    }

    buildCodeLengths(freq, lengths);
    buildCanonicalCodes(lengths, codes);

    BitWriter writer;
    writeU32BE(writer, static_cast<uint32_t>(chunkData.size()));

    for (uint8_t len : lengths)
    {
        writeByte(writer, len);
    }

    for (uint8_t b : chunkData)
    {
        const HuffmanCode& c = codes[b];
        writer.writeBits(c.bits, c.length);
    }

    writer.flush();
    return writer.getBuffer();
}

std::vector<uint8_t> Huffman::compressChunked(const std::vector<uint8_t>& input,
                                              const CompressOptions& opts)
{
    if (input.empty())
    {
        BitWriter writer;
        writeByte(writer, kMagic0);
        writeByte(writer, kMagic1);
        writeByte(writer, kFormatVersion);
        writeByte(writer, kModeChunked);
        writeU32BE(writer, 0);
        writeU32BE(writer, 0);
        writer.flush();
        return writer.getBuffer();
    }

    BitWriter globalWriter;
    writeByte(globalWriter, kMagic0);
    writeByte(globalWriter, kMagic1);
    writeByte(globalWriter, kFormatVersion);
    writeByte(globalWriter, kModeChunked);
    writeU32BE(globalWriter, static_cast<uint32_t>(input.size()));

    size_t chunkSize = opts.chunkSize;
    std::vector<std::vector<uint8_t>> chunks;
    for (size_t offset = 0; offset < input.size(); offset += chunkSize)
    {
        size_t end = std::min(offset + chunkSize, input.size());
        chunks.emplace_back(input.begin() + static_cast<std::vector<uint8_t>::difference_type>(offset),
                           input.begin() + static_cast<std::vector<uint8_t>::difference_type>(end));
    }

    writeU32BE(globalWriter, static_cast<uint32_t>(chunks.size()));

    std::vector<std::vector<uint8_t>> compressedChunks;
    compressedChunks.reserve(chunks.size());

    for (const auto& chunk : chunks)
    {
        auto compressed = compressChunk(chunk, opts);
        compressedChunks.push_back(compressed);
    }

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        const auto& original = chunks[i];
        const auto& compressed = compressedChunks[i];

        writeU32BE(globalWriter, static_cast<uint32_t>(original.size()));
        writeU32BE(globalWriter, static_cast<uint32_t>(compressed.size()));

        for (uint8_t b : compressed)
        {
            writeByte(globalWriter, b);
        }
    }

    globalWriter.flush();
    return globalWriter.getBuffer();
}

std::vector<uint8_t> Huffman::compressChunked(const std::vector<uint8_t>& input)
{
    return compressChunked(input, CompressOptions{});
}

// ============================================================================
// CHUNK-BASED DECOMPRESSION
// ============================================================================

std::vector<uint8_t> Huffman::decompressChunk(const std::vector<uint8_t>& compressedChunk)
{
    // Decompress a single chunk with shared header
    if (compressedChunk.size() < 260) return {};  // Need at least 4 (size) + 256 (header) bytes

    BitReader reader(compressedChunk.data(), compressedChunk.size(), compressedChunk.size() * 8ull);

    std::array<uint8_t, MaxSymbols> lengths{};
    std::array<HuffmanCode, MaxSymbols> codes{};
    std::array<PrimaryDecodeEntry, PrimaryDecodeSize> primaryDecodeTable{};
    std::vector<uint16_t> secondaryDecodeTable;

    // Read uncompressed size from chunk header
    const uint32_t uncompressedSize = readU32BE(reader);

    // Read shared entropy header (256 bytes)
    for (int i = 0; i < MaxSymbols; ++i)
    {
        lengths[i] = readByte(reader);
    }

    buildCanonicalCodes(lengths, codes);
    buildDecodeTable(codes, primaryDecodeTable, secondaryDecodeTable);

    // Decode exactly uncompressedSize symbols
    std::vector<uint8_t> decoded;
    decoded.reserve(uncompressedSize);

    while (decoded.size() < static_cast<size_t>(uncompressedSize))
    {
        const uint32_t rootCode = reader.peekBits(PrimaryDecodeBits);
        const PrimaryDecodeEntry& entry = primaryDecodeTable[rootCode];

        if (entry.symbol >= 0)
        {
            decoded.push_back(static_cast<uint8_t>(entry.symbol));
            reader.consumeBits(entry.length);
            continue;
        }

        if (entry.secondaryBits == 0)
        {
            break;
        }

        const uint32_t fullCode = reader.peekBits(static_cast<uint8_t>(PrimaryDecodeBits + entry.secondaryBits));
        const uint32_t suffixMask = (1u << entry.secondaryBits) - 1u;
        const uint32_t suffix = fullCode & suffixMask;

        if (static_cast<size_t>(entry.secondaryOffset) + suffix >= secondaryDecodeTable.size())
        {
            break;
        }

        const uint16_t packed = secondaryDecodeTable[static_cast<size_t>(entry.secondaryOffset) + suffix];
        if (packed == 0) break;

        const auto symbol = static_cast<uint8_t>(packed >> 8);
        const auto length = static_cast<uint8_t>(packed & 0xFF);
        decoded.push_back(symbol);
        reader.consumeBits(length);
    }

    return decoded;
}

std::vector<uint8_t> Huffman::decompressChunked(const std::vector<uint8_t>& input,
                                                const DecompressOptions& opts)
{
    if (input.size() < 12) return {};

    BitReader reader(input.data(), input.size(), input.size() * 8ull);

    const uint8_t magic0 = readByte(reader);
    const uint8_t magic1 = readByte(reader);
    const uint8_t version = readByte(reader);
    const uint8_t mode = readByte(reader);

    if (magic0 != kMagic0 || magic1 != kMagic1 || version != kFormatVersion || mode != kModeChunked)
    {
        return {};
    }

    const uint32_t totalSize = readU32BE(reader);
    const uint32_t chunkCount = readU32BE(reader);

    if (chunkCount == 0) return {};

    std::vector<uint8_t> output;
    output.reserve(totalSize);

    for (uint32_t i = 0; i < chunkCount; ++i)
    {
        [[maybe_unused]] const uint32_t uncompressedSize = readU32BE(reader);
        const uint32_t compressedSize = readU32BE(reader);

        std::vector<uint8_t> compressedChunk;
        compressedChunk.reserve(compressedSize);

        for (uint32_t j = 0; j < compressedSize; ++j)
        {
            compressedChunk.push_back(readByte(reader));
        }

        auto decompressed = decompressChunk(compressedChunk);
        output.insert(output.end(), decompressed.begin(), decompressed.end());
    }

    return output;
}

std::vector<uint8_t> Huffman::decompressChunked(const std::vector<uint8_t>& input)
{
    return decompressChunked(input, DecompressOptions{});
}
