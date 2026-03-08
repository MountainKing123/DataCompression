#include "lz77_huffman.h"
#include "bitstream.h"
#include <algorithm>

// Distance class table: DEFLATE-style, extended to cover distances up to 131072+
const LZ77Huffman::ClassEntry LZ77Huffman::distClassTable[MaxDistClasses] = {
    {     1, 0}, {     2, 0}, {     3, 0}, {     4, 0},
    {     5, 1}, {     7, 1}, {     9, 2}, {    13, 2},
    {    17, 3}, {    25, 3}, {    33, 4}, {    49, 4},
    {    65, 5}, {    97, 5}, {   129, 6}, {   193, 6},
    {   257, 7}, {   385, 7}, {   513, 8}, {   769, 8},
    {  1025, 9}, {  1537, 9}, {  2049,10}, {  3073,10},
    {  4097,11}, {  6145,11}, {  8193,12}, { 12289,12},
    { 16385,13}, { 24577,13}, { 32769,14}, { 49153,14},
    { 65537,15}, { 98305,15}, {131073,16}, {196609,16},
};

// Length class table: covers match lengths 3..65538
const LZ77Huffman::ClassEntry LZ77Huffman::lenClassTable[MaxLenClasses] = {
    {    3, 0}, {    4, 0}, {    5, 0}, {    6, 0},
    {    7, 0}, {    8, 0}, {    9, 0}, {   10, 0},
    {   11, 1}, {   13, 1}, {   15, 1}, {   17, 1},
    {   19, 2}, {   23, 2}, {   27, 2}, {   31, 2},
    {   35, 3}, {   43, 3}, {   51, 3}, {   59, 3},
    {   67, 4}, {   83, 4}, {   99, 4}, {  115, 4},
    {  131, 5}, {  163, 5}, {  195, 6}, {  259, 7},
    {  387, 16},
};

uint8_t LZ77Huffman::distanceToClass(uint32_t distance)
{
    for (int i = MaxDistClasses - 1; i >= 0; --i)
        if (distance >= distClassTable[i].base)
            return static_cast<uint8_t>(i);
    return 0;
}

uint8_t LZ77Huffman::lengthToClass(uint32_t length)
{
    for (int i = MaxLenClasses - 1; i >= 0; --i)
        if (length >= lenClassTable[i].base)
            return static_cast<uint8_t>(i);
    return 0;
}

// ---------------------------------------------------------------------------
// Overflow varint: simple byte-level encoding for litlen/matchlen overflow
// ---------------------------------------------------------------------------

void LZ77Huffman::encodeOverflowVarInt(std::vector<uint8_t>& out, uint32_t value)
{
    while (value >= 128)
    {
        out.push_back(static_cast<uint8_t>(value & 0x7F) | 0x80);
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

uint32_t LZ77Huffman::decodeOverflowVarInt(const std::vector<uint8_t>& data, size_t& pos)
{
    uint32_t result = 0;
    uint32_t shift = 0;
    while (pos < data.size())
    {
        uint8_t b = data[pos++];
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Tokenization: convert LZ77 intermediate stream to token-based streams
// ---------------------------------------------------------------------------

LZ77Huffman::TokenizedStreams
LZ77Huffman::tokenize(const LZ77::IntermediateStream& intermediate)
{
    TokenizedStreams ts;
    BitWriter extraWriter;

    std::array<uint32_t, LZ77::RecentOffsetCount> recent = {
        LZ77::RecentOffsetInit, LZ77::RecentOffsetInit, LZ77::RecentOffsetInit
    };

    const auto& syms = intermediate.symbols;
    size_t i = 0;

    while (i < syms.size())
    {
        // Collect literal run
        size_t litStart = i;
        while (i < syms.size() && syms[i].type == LZ77::EncodedSymbol::Literal)
            ++i;
        size_t litCount = i - litStart;

        // If we've hit end of symbols with no match following, emit trailing literals
        if (i >= syms.size())
        {
            // Emit literals without a match token (special: litlen with matchlen=0)
            // We use a convention: last token can have matchlen field = 0 meaning "no match"
            // Actually, to keep it clean: emit a token with litlen and matchlen=0,
            // where matchlen field 0 means match length 3. But there's no match to copy.
            // Better approach: pack remaining literals directly and emit tokens with
            // litlen only, no match. We'll use a special convention where if we're at
            // end-of-stream, the decoder stops after copying literals.

            // Simpler: if there are trailing literals, emit them in batches with
            // dummy tokens. The decoder will stop once uncompressedSize bytes are produced.
            size_t batch = std::min<size_t>(litCount, 2);
            uint8_t token;
            if (litCount > 2)
            {
                token = makeToken(LitLenEscape, 0, 0);
                encodeOverflowVarInt(ts.overflow, static_cast<uint32_t>(litCount - LitLenEscape));
                batch = litCount;
            }
            else
            {
                token = makeToken(static_cast<uint8_t>(batch), 0, 0);
            }

            ts.tokens.push_back(token);

            for (size_t j = 0; j < batch; ++j)
                ts.literals.push_back(syms[litStart + j].literal);

            litCount -= batch;
            litStart += batch;
            break;
        }

        // We have a match at syms[i]
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
            encodeOverflowVarInt(ts.overflow, static_cast<uint32_t>(litCount - LitLenEscape));
        }

        // Push literal bytes
        for (size_t j = 0; j < litCount; ++j)
            ts.literals.push_back(syms[litStart + j].literal);

        // Encode matchlen
        uint8_t matchlenField;
        uint32_t matchLen = match.length;
        if (matchLen - MatchLenBase <= 14)
        {
            matchlenField = static_cast<uint8_t>(matchLen - MatchLenBase);
        }
        else
        {
            matchlenField = MatchLenEscape;
            // For overflow matchlen, encode the full length using length class table
            uint8_t lc = lengthToClass(matchLen);
            ts.lenClasses.push_back(lc);
            if (lenClassTable[lc].extraBits > 0)
            {
                uint32_t extra = matchLen - lenClassTable[lc].base;
                extraWriter.writeBits(extra, lenClassTable[lc].extraBits);
            }
        }

        // Encode offset mode
        uint8_t offsetMode;
        if (match.isRecentOffset)
        {
            offsetMode = static_cast<uint8_t>(match.recentIndex + 1);
            LZ77::promoteRecent(recent, match.recentIndex);
        }
        else
        {
            offsetMode = 0;
            uint8_t dc = distanceToClass(match.distance);
            ts.distClasses.push_back(dc);
            if (distClassTable[dc].extraBits > 0)
            {
                uint32_t extra = match.distance - distClassTable[dc].base;
                extraWriter.writeBits(extra, distClassTable[dc].extraBits);
            }
            LZ77::insertRecent(recent, match.distance);
        }

        ts.tokens.push_back(makeToken(litlenField, matchlenField, offsetMode));
    }

    extraWriter.flush();
    ts.extraBitsData = extraWriter.getBuffer();
    ts.extraBitCount = static_cast<uint32_t>(extraWriter.getTotalBits());
    return ts;
}

// ---------------------------------------------------------------------------
// Huffman encode/decode a byte stream
// ---------------------------------------------------------------------------

std::vector<uint8_t> LZ77Huffman::encodeStream(
    const std::vector<uint8_t>& input,
    const std::array<Huffman::HuffmanCode, 256>& codes)
{
    if (input.empty()) return {};
    BitWriter writer;
    for (uint8_t b : input)
        writer.writeBits(codes[b].bits, codes[b].length);
    writer.flush();
    return writer.getBuffer();
}

std::vector<uint8_t> LZ77Huffman::decodeStream(
    const std::vector<uint8_t>& encoded,
    uint32_t expectedBytes,
    const std::array<Huffman::PrimaryDecodeEntry, Huffman::PrimaryDecodeSize>& primaryTable,
    const std::vector<uint16_t>& secondaryTable)
{
    std::vector<uint8_t> out;
    if (expectedBytes == 0) return out;
    out.reserve(expectedBytes);

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

// ---------------------------------------------------------------------------
// Top-level chunk compress / decompress
// ---------------------------------------------------------------------------

LZ77Huffman::CompressedChunk
LZ77Huffman::compressChunk(const std::vector<uint8_t>& input,
                           const CompressOptions& opts)
{
    CompressedChunk result;
    result.uncompressedSize = static_cast<uint32_t>(input.size());

    LZ77::CompressOptions lzOpts;
    lzOpts.minMatchLength = opts.minMatchLength;
    lzOpts.maxMatchLength = opts.maxMatchLength;
    lzOpts.maxDistance     = opts.maxDistance;

    auto intermediate = LZ77::encodeChunk(input, lzOpts);
    auto ts = tokenize(intermediate);

    result.tokenCount       = static_cast<uint32_t>(ts.tokens.size());
    result.literalByteCount = static_cast<uint32_t>(ts.literals.size());
    result.distClassCount   = static_cast<uint32_t>(ts.distClasses.size());
    result.lenClassCount    = static_cast<uint32_t>(ts.lenClasses.size());
    result.extraBitCount    = ts.extraBitCount;
    result.overflowCount    = static_cast<uint32_t>(ts.overflow.size());

    auto encodeOneStream = [](const std::vector<uint8_t>& raw,
                              StreamHeader& header,
                              std::vector<uint8_t>& outStream)
    {
        if (raw.empty())
        {
            header.type = StreamType::Raw;
            outStream = {};
            return;
        }

        std::array<uint32_t, 256> freq{};
        for (uint8_t b : raw) freq[b]++;

        std::array<uint8_t, 256> lengths{};
        Huffman::buildCodeLengths(freq, lengths);

        std::array<Huffman::HuffmanCode, 256> codes{};
        Huffman::buildCanonicalCodes(lengths, codes);

        auto encoded = encodeStream(raw, codes);
        if (encoded.size() < raw.size())
        {
            header.type = StreamType::Huffman;
            header.codeLengths = lengths;
            outStream = std::move(encoded);
        }
        else
        {
            header.type = StreamType::Raw;
            outStream = raw;
        }
    };

    encodeOneStream(ts.tokens, result.tokenHeader, result.tokenStream);
    encodeOneStream(ts.literals, result.literalHeader, result.literalStream);
    encodeOneStream(ts.distClasses, result.distClassHeader, result.distClassStream);
    encodeOneStream(ts.lenClasses, result.lenClassHeader, result.lenClassStream);

    result.extraBitsStream = ts.extraBitsData;
    result.overflowStream  = ts.overflow;

    return result;
}

std::vector<uint8_t>
LZ77Huffman::decompressChunk(const CompressedChunk& chunk)
{
    auto decodeOneStream = [](const StreamHeader& header,
                              const std::vector<uint8_t>& encoded,
                              uint32_t expectedBytes) -> std::vector<uint8_t>
    {
        if (expectedBytes == 0) return {};
        if (header.type == StreamType::Raw)
            return encoded;

        std::array<Huffman::HuffmanCode, 256> codes{};
        Huffman::buildCanonicalCodes(header.codeLengths, codes);

        std::array<Huffman::PrimaryDecodeEntry, Huffman::PrimaryDecodeSize> primaryTable{};
        std::vector<uint16_t> secondaryTable;
        Huffman::buildDecodeTable(codes, primaryTable, secondaryTable);

        return decodeStream(encoded, expectedBytes, primaryTable, secondaryTable);
    };

    auto tokens     = decodeOneStream(chunk.tokenHeader,     chunk.tokenStream,     chunk.tokenCount);
    auto literals   = decodeOneStream(chunk.literalHeader,   chunk.literalStream,   chunk.literalByteCount);
    auto distClasses = decodeOneStream(chunk.distClassHeader, chunk.distClassStream, chunk.distClassCount);
    auto lenClasses = decodeOneStream(chunk.lenClassHeader,  chunk.lenClassStream,  chunk.lenClassCount);

    BitReader extraReader(chunk.extraBitsStream.data(), chunk.extraBitsStream.size(),
                          chunk.extraBitsStream.size() * 8ull);

    std::array<uint32_t, LZ77::RecentOffsetCount> recent = {
        LZ77::RecentOffsetInit, LZ77::RecentOffsetInit, LZ77::RecentOffsetInit
    };

    std::vector<uint8_t> output;
    output.reserve(chunk.uncompressedSize);

    size_t litIdx = 0;
    size_t distIdx = 0;
    size_t lenIdx = 0;
    size_t overflowPos = 0;

    for (uint32_t ti = 0; ti < chunk.tokenCount; ++ti)
    {
        uint8_t token = tokens[ti];
        uint32_t litlen = token & 0x03;
        uint32_t matchlenField = (token >> 2) & 0x0F;
        uint32_t offsetMode = (token >> 6) & 0x03;

        // Decode litlen
        if (litlen == LitLenEscape)
            litlen = LitLenEscape + decodeOverflowVarInt(chunk.overflowStream, overflowPos);

        // Copy literals
        for (uint32_t j = 0; j < litlen; ++j)
        {
            if (litIdx < literals.size())
                output.push_back(literals[litIdx++]);
        }

        // If output is full, stop (trailing-literal-only tokens have matchlenField=0)
        if (output.size() >= chunk.uncompressedSize)
            break;

        // Decode matchlen
        uint32_t matchLen;
        if (matchlenField == 0 && output.size() >= chunk.uncompressedSize)
        {
            // Trailing literal token with no actual match
            break;
        }

        if (matchlenField < MatchLenEscape)
        {
            matchLen = matchlenField + MatchLenBase;
        }
        else
        {
            // Overflow: read length class
            uint8_t lc = lenClasses.at(lenIdx++);
            matchLen = lenClassTable[lc].base;
            if (lenClassTable[lc].extraBits > 0)
            {
                uint32_t extra = extraReader.peekBits(lenClassTable[lc].extraBits);
                extraReader.consumeBits(lenClassTable[lc].extraBits);
                matchLen += extra;
            }
        }

        // Decode offset
        uint32_t dist;
        if (offsetMode == 0)
        {
            uint8_t dc = distClasses.at(distIdx++);
            dist = distClassTable[dc].base;
            if (distClassTable[dc].extraBits > 0)
            {
                uint32_t extra = extraReader.peekBits(distClassTable[dc].extraBits);
                extraReader.consumeBits(distClassTable[dc].extraBits);
                dist += extra;
            }
            LZ77::insertRecent(recent, dist);
        }
        else
        {
            int ri = static_cast<int>(offsetMode) - 1;
            dist = recent[ri];
            LZ77::promoteRecent(recent, ri);
        }

        // Copy match
        size_t copyPos = output.size() - dist;
        for (uint32_t j = 0; j < matchLen; ++j)
            output.push_back(output[copyPos + j]);
    }

    return output;
}
