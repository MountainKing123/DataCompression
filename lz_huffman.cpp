#include "lz_huffman.h"
#include "huffman.h"
#include "bitstream.h"
#include <algorithm>
#include <cmath>
#include <intrin.h>

// ---------------------------------------------------------------------------
// Packed offset encoding
// ---------------------------------------------------------------------------

uint8_t LZHuffman::packOffset(const uint32_t distance,
                                uint32_t& extraBits,
                                uint8_t& extraBitCount)
{
    // Distances 1..16: fit directly, no extra bits
    if (distance <= 16)
    {
        extraBits = 0;
        extraBitCount = 0;
        return static_cast<uint8_t>(distance - 1);
    }

    // For larger distances: high nibble = (n-3) where n = BSR(distance),
    // low nibble = 4 mantissa bits below the leading 1.
    // Since BSR(17)=4, the minimum high nibble is (4-3)=1, so packed >= 16.
    unsigned long msb;
    _BitScanReverse(&msb, distance);
    const auto n = static_cast<uint8_t>(msb);

    const auto shift      = static_cast<uint8_t>(n - 4);
    const auto highNibble = static_cast<uint8_t>(shift + 1);   // 1..14
    const auto lowNibble  = static_cast<uint8_t>((distance >> shift) & 0x0F);

    extraBitCount = shift;
    extraBits = distance & ((1u << shift) - 1u);

    return static_cast<uint8_t>((highNibble << 4) | lowNibble);
}

uint32_t LZHuffman::unpackOffset(const uint8_t packed, const uint32_t extraBits)
{
    // Packed values 0..15 are direct distances 1..16
    if (packed < 16)
        return static_cast<uint32_t>(packed) + 1;

    const uint8_t highNibble = packed >> 4;   // shift + 1
    const uint8_t lowNibble  = packed & 0x0F; // 4 mantissa bits
    const auto shift      = static_cast<uint8_t>(highNibble - 1);

    // Reconstruct: implicit leading 1 + 4 mantissa bits + extra bits
    const uint32_t mantissa = (1u << 4) | lowNibble;
    return (mantissa << shift) | extraBits;
}

// ---------------------------------------------------------------------------
// Unified length overflow stream helpers
// ---------------------------------------------------------------------------

void LZHuffman::writeLenOverflow(TokenizedStreams& ts, const uint32_t value)
{
    if (value < 255)
    {
        ts.lenOverflow.push_back(static_cast<uint8_t>(value));
    }
    else
    {
        ts.lenOverflow.push_back(255);
        // Store LE32 in lenOverflowExtra
        ts.lenOverflowExtra.push_back(static_cast<uint8_t>(value));
        ts.lenOverflowExtra.push_back(static_cast<uint8_t>(value >> 8));
        ts.lenOverflowExtra.push_back(static_cast<uint8_t>(value >> 16));
        ts.lenOverflowExtra.push_back(static_cast<uint8_t>(value >> 24));
    }
}

// ---------------------------------------------------------------------------
// Tokenization: convert LZ77 intermediate stream to token-based streams
// ---------------------------------------------------------------------------

LZHuffman::TokenizedStreams
LZHuffman::tokenize(const LZ::IntermediateStream& intermediate,
                     const std::vector<uint8_t>& input)
{
    TokenizedStreams ts;
    BidirectionalBitWriter extraWriter;

    std::array<uint32_t, LZ::RecentOffsetCount> recent = {
        LZ::RecentOffsetInit, LZ::RecentOffsetInit, LZ::RecentOffsetInit
    };

    // Track current position in the original input for sub-literal computation
    size_t inputPos = 0;
    uint32_t lastOffset = LZ::RecentOffsetInit;

    const auto& syms = intermediate.symbols;
    size_t i = 0;

    while (i < syms.size())
    {
        // Collect literal run
        const size_t litStart = i;
        while (i < syms.size() && syms[i].type == LZ::EncodedSymbol::Literal)
            ++i;
        const size_t litCount = i - litStart;

        // Trailing literals with no following match
        if (i >= syms.size())
        {
            uint8_t litlenField;
            if (litCount <= 2)
            {
                litlenField = static_cast<uint8_t>(litCount);
            }
            else
            {
                litlenField = LitLenEscape;
                writeLenOverflow(ts, static_cast<uint32_t>(litCount - LitLenEscape));
            }
            ts.tokens.push_back(makeToken(litlenField, 0, 0));
            for (size_t j = 0; j < litCount; ++j)
            {
                const uint8_t raw = syms[litStart + j].literal;
                ts.literals.push_back(raw);
                // Subtracted literal: delta from prediction at (pos - lastOffset)
                const uint8_t prediction = (inputPos >= lastOffset)
                    ? input[inputPos - lastOffset] : 0;
                ts.subLiterals.push_back(static_cast<uint8_t>(raw - prediction));
                ++inputPos;
            }
            break;
        }

        // Match at syms[i]
        const auto& match = syms[i];
        ++i;

        // Encode litlen
        uint8_t litlenField;
        if (litCount <= 2)
        {
            litlenField = static_cast<uint8_t>(litCount);
        }
        else
        {
            litlenField = LitLenEscape;
            // lenOverflow: litlen overflow = litCount - 3
            writeLenOverflow(ts, static_cast<uint32_t>(litCount - LitLenEscape));
        }

        for (size_t j = 0; j < litCount; ++j)
        {
            const uint8_t raw = syms[litStart + j].literal;
            ts.literals.push_back(raw);
            const uint8_t prediction = (inputPos >= lastOffset)
                ? input[inputPos - lastOffset] : 0;
            ts.subLiterals.push_back(static_cast<uint8_t>(raw - prediction));
            ++inputPos;
        }

        // Encode matchlen
        uint8_t matchlenField;
        const uint32_t matchLen = match.length;
        if (matchLen <= MatchLenInlineMax)
        {
            matchlenField = static_cast<uint8_t>(matchLen - MatchLenBase);
        }
        else
        {
            matchlenField = MatchLenEscape;
            writeLenOverflow(ts, matchLen - (MatchLenInlineMax + 1));
        }

        // Encode offset mode and update lastOffset for sub-literal prediction
        uint8_t offsetMode;
        uint32_t matchDist;
        if (match.isRecentOffset)
        {
            offsetMode = static_cast<uint8_t>(match.recentIndex + 1);
            matchDist = recent[match.recentIndex];
            LZ::promoteRecent(recent, match.recentIndex);
        }
        else
        {
            offsetMode = 0;
            matchDist = match.distance;
            uint32_t extraBits = 0;
            uint8_t extraBitCount = 0;
            const uint8_t packed = packOffset(match.distance, extraBits, extraBitCount);
            ts.distPacked.push_back(packed);
            if (extraBitCount > 0)
                extraWriter.writeBits(extraBits, extraBitCount);
            LZ::insertRecent(recent, match.distance);
        }
        lastOffset = matchDist;
        inputPos += matchLen;

        ts.tokens.push_back(makeToken(litlenField, matchlenField, offsetMode));
    }

    ts.extraBitsData = extraWriter.finish();
    ts.extraBitCount = static_cast<uint32_t>(extraWriter.getTotalBits());
    ts.extraBitsMode = extraWriter.getMode();
    return ts;
}

// ---------------------------------------------------------------------------
// RLE pre-pass: collapses runs of identical bytes
//
// Protocol:
//   Byte < 0xFF:  literal (emit as-is)
//   0xFF:         escape marker
//     Followed by 0xFF:             one literal 0xFF byte
//     Followed by <val> <count>:    run of (count + 3) copies of val (val != 0xFF)
// ---------------------------------------------------------------------------

std::vector<uint8_t> LZHuffman::rleEncode(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> out;
    out.reserve(input.size());

    size_t i = 0;
    while (i < input.size())
    {
        const uint8_t b = input[i];
        size_t runLen = 1;
        while (i + runLen < input.size() && input[i + runLen] == b && runLen < 258)
            ++runLen;

        if (b == 0xFF)
        {
            // Literal 0xFF bytes: each one needs escaping as (0xFF, 0xFF)
            for (size_t j = 0; j < runLen; ++j)
            {
                out.push_back(0xFF);
                out.push_back(0xFF);
            }
            i += runLen;
        }
        else if (runLen >= 3)
        {
            // Run of non-0xFF byte: (0xFF, val, count-3)
            out.push_back(0xFF);
            out.push_back(b);
            out.push_back(static_cast<uint8_t>(runLen - 3));
            i += runLen;
        }
        else
        {
            // 1 or 2 literal bytes (not 0xFF)
            for (size_t j = 0; j < runLen; ++j)
                out.push_back(b);
            i += runLen;
        }
    }

    return out;
}

std::vector<uint8_t> LZHuffman::rleDecode(const std::vector<uint8_t>& encoded, const uint32_t expectedSize)
{
    std::vector<uint8_t> out;
    out.reserve(expectedSize);

    size_t i = 0;
    while (i < encoded.size() && out.size() < expectedSize)
    {
        const uint8_t b = encoded[i++];
        if (b != 0xFF)
        {
            out.push_back(b);
        }
        else
        {
            if (i >= encoded.size()) break;
            const uint8_t val = encoded[i++];
            if (val == 0xFF)
            {
                // Escaped literal 0xFF
                out.push_back(0xFF);
            }
            else
            {
                // Run: (count + 3) copies of val
                if (i >= encoded.size()) break;
                const uint8_t count = encoded[i++];
                const uint32_t runLen = static_cast<uint32_t>(count) + 3;
                for (uint32_t j = 0; j < runLen && out.size() < expectedSize; ++j)
                    out.push_back(val);
            }
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Huffman encode/decode a byte stream
// ---------------------------------------------------------------------------

std::vector<uint8_t> LZHuffman::encodeStream(
    const std::vector<uint8_t>& input,
    const std::array<Huffman::HuffmanCode, 256>& codes,
    const bool interleaved)
{
    if (input.empty()) return {};

    if (!interleaved)
    {
        // Original sequential encoding
        BitWriter writer;
        for (const uint8_t b : input)
            writer.writeBits(codes[b].bits, codes[b].length);
        writer.flush();
        return writer.getBuffer();
    }

    // 3-stream interleaved encoding
    InterleavedBitWriter writer;
    for (const uint8_t b : input)
        writer.writeBits(codes[b].bits, codes[b].length);
    return writer.getInterleaved();
}

std::vector<uint8_t> LZHuffman::decodeStream(
    const std::vector<uint8_t>& encoded,
    const uint32_t expectedBytes,
    const std::array<Huffman::PrimaryDecodeEntry, Huffman::PrimaryDecodeSize>& primaryTable,
    const std::vector<uint16_t>& secondaryTable,
    const bool interleaved)
{
    std::vector<uint8_t> out;
    if (expectedBytes == 0) return out;
    out.reserve(expectedBytes);

    if (!interleaved)
    {
        // Original sequential decoding
        BitReader reader(encoded.data(), encoded.size(), encoded.size() * 8ull);

        while (out.size() < expectedBytes)
        {
            const uint32_t rootCode = reader.peekBits(Huffman::PrimaryDecodeBits);
            const auto& entry = primaryTable[rootCode];

            if (entry.symbol >= 0)
            {
                out.push_back(static_cast<uint8_t>(entry.symbol));
                reader.consumeBits(entry.length);
                continue;
            }

            if (entry.secondaryBits == 0) break;

            const auto totalBits = static_cast<uint8_t>(Huffman::PrimaryDecodeBits + entry.secondaryBits);
            const uint32_t fullCode = reader.peekBits(totalBits);
            const uint32_t suffixMask = (1u << entry.secondaryBits) - 1u;
            const uint32_t suffix = fullCode & suffixMask;

            const auto idx = static_cast<size_t>(entry.secondaryOffset) + suffix;
            if (idx >= secondaryTable.size()) break;

            const uint16_t packed = secondaryTable[idx];
            if (packed == 0) break;

            const auto symbol = static_cast<uint8_t>(packed >> 8);
            const auto length = static_cast<uint8_t>(packed & 0xFF);
            out.push_back(symbol);
            reader.consumeBits(length);
        }

        return out;
    }

    // 3-stream interleaved decoding
    InterleavedBitReader reader(encoded.data(), encoded.size());

    while (out.size() < expectedBytes)
    {
        const uint32_t rootCode = reader.peekBits(Huffman::PrimaryDecodeBits);
        const auto& entry = primaryTable[rootCode];

        if (entry.symbol >= 0)
        {
            out.push_back(static_cast<uint8_t>(entry.symbol));
            reader.consumeBits(entry.length);
            continue;
        }

        if (entry.secondaryBits == 0) break;

        const auto totalBits = static_cast<uint8_t>(Huffman::PrimaryDecodeBits + entry.secondaryBits);
        const uint32_t fullCode = reader.peekBits(totalBits);
        const uint32_t suffixMask = (1u << entry.secondaryBits) - 1u;
        const uint32_t suffix = fullCode & suffixMask;

        const auto idx = static_cast<size_t>(entry.secondaryOffset) + suffix;
        if (idx >= secondaryTable.size()) break;

        const uint16_t packed = secondaryTable[idx];
        if (packed == 0) break;

        const auto symbol = static_cast<uint8_t>(packed >> 8);
        const auto length = static_cast<uint8_t>(packed & 0xFF);
        out.push_back(symbol);
        reader.consumeBits(length);
    }

    return out;
}

// ---------------------------------------------------------------------------
// Top-level chunk compress / decompress
// ---------------------------------------------------------------------------

LZHuffman::CompressedChunk
LZHuffman::compressChunk(const std::vector<uint8_t>& input,
                           const CompressOptions& opts)
{
    CompressedChunk result;
    result.uncompressedSize = static_cast<uint32_t>(input.size());

    LZ::CompressOptions lzOpts;
    lzOpts.minMatchLength = opts.minMatchLength;
    lzOpts.maxMatchLength = opts.maxMatchLength;
    lzOpts.maxDistance     = opts.maxDistance;

    const auto intermediate = LZ::encodeChunk(input, lzOpts);
    auto ts = tokenize(intermediate, input);

    result.tokenCount       = static_cast<uint32_t>(ts.tokens.size());
    result.literalByteCount = static_cast<uint32_t>(ts.literals.size());
    result.distPackedCount  = static_cast<uint32_t>(ts.distPacked.size());
    result.lenOverflowCount      = static_cast<uint32_t>(ts.lenOverflow.size());
    result.extraBitCount    = ts.extraBitCount;
    result.lenOverflowExtraCount = static_cast<uint32_t>(ts.lenOverflowExtra.size() / 4);

    // Compare entropy of raw literals vs subtracted literals to decide mode
    auto histogramCost = [](const std::vector<uint8_t>& data) -> double
    {
        if (data.empty()) return 0.0;
        std::array<uint32_t, 256> freq{};
        for (uint8_t b : data) freq[b]++;
        double cost = 0.0;
        const auto n = static_cast<double>(data.size());
        for (uint32_t f : freq)
        {
            if (f == 0) continue;
            const double p = static_cast<double>(f) / n;
            cost -= p * std::log2(p);
        }
        return cost * n;
    };

    const double rawCost = histogramCost(ts.literals);
    const double subCost = histogramCost(ts.subLiterals);
    result.literalSubMode = (subCost < rawCost);

    const auto& chosenLiterals = result.literalSubMode ? ts.subLiterals : ts.literals;

    auto encodeOneStream = [&opts](const std::vector<uint8_t>& raw,
                              StreamHeader& header,
                              std::vector<uint8_t>& outStream)
    {
        if (raw.empty())
        {
            header.type = StreamType::Raw;
            header.interleaved = false;
            outStream = {};
            return;
        }

        // Estimate Huffman-encoded size from histogram without actual encoding.
        // Returns (estimated bytes, code lengths). Much cheaper than a full encode.
        auto estimateHuffman = [](const std::vector<uint8_t>& data)
            -> std::pair<size_t, std::array<uint8_t, 256>>
        {
            std::array<uint32_t, 256> freq{};
            for (uint8_t b : data) freq[b]++;

            std::array<uint8_t, 256> lengths{};
            Huffman::buildCodeLengths(freq, lengths);

            uint64_t totalBits = 0;
            for (int i = 0; i < 256; ++i)
            {
                if (freq[i] > 0 && lengths[i] > 0)
                    totalBits += static_cast<uint64_t>(freq[i]) * lengths[i];
            }

            const size_t estimatedBytes = (totalBits + 7) / 8;
            return {estimatedBytes, lengths};
        };

        // Actual Huffman encode (only called for the winning mode)
        auto doEncode = [&opts](const std::vector<uint8_t>& data,
                                const std::array<uint8_t, 256>& lengths)
            -> std::vector<uint8_t>
        {
            std::array<Huffman::HuffmanCode, 256> codes{};
            Huffman::buildCanonicalCodes(lengths, codes);
            return encodeStream(data, codes, opts.useInterleaved);
        };

        // Option 1: Huffman on raw data
        const auto [huffEst, huffLengths] = estimateHuffman(raw);
        size_t bestEstimate = huffEst;
        const bool huffBetter = (huffEst < raw.size());

        // Option 2: RLE + Huffman (only if enabled)
        std::vector<uint8_t> rleData;
        std::array<uint8_t, 256> rleLengths{};
        bool rleBetter = false;

        if (opts.useRLE)
        {
            rleData = rleEncode(raw);
            if (rleData.size() < raw.size())
            {
                const auto [est, lens] = estimateHuffman(rleData);
                rleLengths = lens;
                if (est + 4 < bestEstimate && est + 4 < raw.size())
                {
                    bestEstimate = est + 4;
                    rleBetter = true;
                }
            }
        }

        // Now do the actual encode only for the winning mode
        if (rleBetter)
        {
            auto encoded = doEncode(rleData, rleLengths);
            if (encoded.size() + 4 < raw.size())
            {
                header.type = StreamType::RLEHuffman;
                header.interleaved = opts.useInterleaved;
                header.codeLengths = rleLengths;
                header.rleEncodedSize = static_cast<uint32_t>(rleData.size());
                outStream = std::move(encoded);
                return;
            }
            // Estimate was optimistic; fall through to try plain Huffman
            rleBetter = false;
        }

        if (huffBetter)
        {
            auto encoded = doEncode(raw, huffLengths);
            if (encoded.size() < raw.size())
            {
                header.type = StreamType::Huffman;
                header.interleaved = opts.useInterleaved;
                header.codeLengths = huffLengths;
                outStream = std::move(encoded);
                return;
            }
        }

        // Raw fallback
        header.type = StreamType::Raw;
        header.interleaved = false;
        outStream = raw;
    };

    encodeOneStream(ts.tokens,       result.tokenHeader,         result.tokenStream);
    encodeOneStream(chosenLiterals,  result.literalHeader,       result.literalStream);
    encodeOneStream(ts.distPacked,   result.distPackedHeader,    result.distPackedStream);
    encodeOneStream(ts.lenOverflow,  result.lenOverflowHeader,   result.lenOverflowStream);

    result.extraBitsStream       = ts.extraBitsData;
    result.extraBitsMode         = ts.extraBitsMode;
    result.lenOverflowExtraStream = ts.lenOverflowExtra;

    return result;
}

std::vector<uint8_t>
LZHuffman::decompressChunk(const CompressedChunk& chunk)
{
    auto decodeOneStream = [](const StreamHeader& header,
                              const std::vector<uint8_t>& encoded,
                              const uint32_t expectedBytes) -> std::vector<uint8_t>
    {
        if (expectedBytes == 0) return {};
        if (header.type == StreamType::Raw)
            return encoded;

        // Both Huffman and RLEHuffman need the Huffman decode step
        std::array<Huffman::HuffmanCode, 256> codes{};
        Huffman::buildCanonicalCodes(header.codeLengths, codes);

        std::array<Huffman::PrimaryDecodeEntry, Huffman::PrimaryDecodeSize> primaryTable{};
        std::vector<uint16_t> secondaryTable;
        Huffman::buildDecodeTable(codes, primaryTable, secondaryTable);

        if (header.type == StreamType::Huffman)
        {
            return decodeStream(encoded, expectedBytes, primaryTable, secondaryTable, header.interleaved);
        }

        // RLEHuffman: Huffman-decode to get the RLE stream, then RLE-decode
        const auto rleStream = decodeStream(encoded, header.rleEncodedSize, primaryTable, secondaryTable, header.interleaved);
        return rleDecode(rleStream, expectedBytes);
    };

    const auto tokens      = decodeOneStream(chunk.tokenHeader,         chunk.tokenStream,         chunk.tokenCount);
    const auto literals    = decodeOneStream(chunk.literalHeader,       chunk.literalStream,       chunk.literalByteCount);
    const auto distPacked  = decodeOneStream(chunk.distPackedHeader,    chunk.distPackedStream,    chunk.distPackedCount);
    const auto lenOverflow = decodeOneStream(chunk.lenOverflowHeader,   chunk.lenOverflowStream,   chunk.lenOverflowCount);

    BidirectionalBitReader extraReader(chunk.extraBitsMode,
                                       chunk.extraBitsStream.data(),
                                       chunk.extraBitsStream.size());

    std::array<uint32_t, LZ::RecentOffsetCount> recent = {
        LZ::RecentOffsetInit, LZ::RecentOffsetInit, LZ::RecentOffsetInit
    };

    uint32_t lastOffset = LZ::RecentOffsetInit;

    std::vector<uint8_t> output;
    output.reserve(chunk.uncompressedSize);

    size_t litIdx              = 0;
    size_t distIdx             = 0;
    size_t lenOverflowIdx      = 0;
    size_t lenOverflowExtraIdx = 0;   // byte index into lenOverflowExtraStream (each entry = 4 bytes)

    // Read one lenOverflow value: byte 0-254 direct, 255 = read LE32 from lenOverflowExtraStream
    auto readLenOverflow = [&]() -> uint32_t {
        const uint8_t escapeByte = lenOverflow.at(lenOverflowIdx++);
        if (escapeByte < 255)
            return escapeByte;
        // Escape: read LE32 from lenOverflowExtraStream
        const auto& ex = chunk.lenOverflowExtraStream;
        const uint32_t le32Value = static_cast<uint32_t>(ex[lenOverflowExtraIdx])
                                 | (static_cast<uint32_t>(ex[lenOverflowExtraIdx + 1]) << 8)
                                 | (static_cast<uint32_t>(ex[lenOverflowExtraIdx + 2]) << 16)
                                 | (static_cast<uint32_t>(ex[lenOverflowExtraIdx + 3]) << 24);
        lenOverflowExtraIdx += 4;
        return le32Value;
    };

    for (uint32_t ti = 0; ti < chunk.tokenCount; ++ti)
    {
        const uint8_t token          = tokens[ti];
        uint32_t litlen              = token & 0x03;
        const uint32_t matchlenField = (token >> 2) & 0x0F;
        const uint32_t offsetMode    = (token >> 6) & 0x03;

        // Decode litlen overflow via lenOverflow stream
        if (litlen == LitLenEscape)
            litlen = LitLenEscape + readLenOverflow();

        // Copy literals (reconstruct from deltas if literalSubMode)
        for (uint32_t j = 0; j < litlen; ++j)
        {
            if (litIdx < literals.size())
            {
                const uint8_t stored = literals[litIdx++];
                if (chunk.literalSubMode)
                {
                    const uint8_t prediction = (output.size() >= lastOffset)
                        ? output[output.size() - lastOffset] : 0;
                    output.push_back(static_cast<uint8_t>(stored + prediction));
                }
                else
                {
                    output.push_back(stored);
                }
            }
        }

        if (output.size() >= chunk.uncompressedSize)
            break;

        // Decode matchlen
        uint32_t matchLen;
        if (matchlenField < MatchLenEscape)
        {
            matchLen = matchlenField + MatchLenBase;
        }
        else
        {
            // lenOverflow: overflow = matchLen - 18; reconstruct matchLen
            matchLen = MatchLenInlineMax + 1 + readLenOverflow();
        }

        // Decode offset
        uint32_t dist;
        if (offsetMode == 0)
        {
            const uint8_t packed = distPacked.at(distIdx++);
            if (packed < 16)
            {
                dist = unpackOffset(packed, 0);
            }
            else
            {
                const auto shift = static_cast<uint8_t>((packed >> 4) - 1);
                uint32_t extraBits = 0;
                if (shift > 0)
                {
                    extraBits = extraReader.peekBits(shift);
                    extraReader.consumeBits(shift);
                }
                dist = unpackOffset(packed, extraBits);
            }
            LZ::insertRecent(recent, dist);
        }
        else
        {
            const int ri = static_cast<int>(offsetMode) - 1;
            dist = recent[ri];
            LZ::promoteRecent(recent, ri);
        }
        lastOffset = dist;

        // Copy match
        const size_t copyPos = output.size() - dist;
        for (uint32_t j = 0; j < matchLen; ++j)
            output.push_back(output[copyPos + j]);
    }

    return output;
}

