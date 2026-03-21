#include "lz.h"
#include <algorithm>
#include <immintrin.h>
#include <intrin.h>

namespace
{
    // SIMD-accelerated match length comparison using SSE2.
    // Compares 16 bytes at a time, returns the number of matching bytes.
    inline uint32_t simdMatchLength(const uint8_t* const a, const uint8_t* const b, const uint32_t maxLen)
    {
        uint32_t len = 0;

        while (len + 16 <= maxLen)
        {
            const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + len));
            const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + len));
            const __m128i cmp = _mm_cmpeq_epi8(va, vb);
            const int mask = _mm_movemask_epi8(cmp);
            if (mask != 0xFFFF)
            {
                unsigned long idx;
                _BitScanForward(&idx, ~static_cast<unsigned>(mask));
                return len + static_cast<uint32_t>(idx);
            }
            len += 16;
        }

        while (len < maxLen && a[len] == b[len])
            ++len;

        return len;
    }
}

inline uint32_t LZ::hash4(const uint8_t* const data)
{
    uint32_t val;
    std::memcpy(&val, data, 4);
    return _mm_crc32_u32(0, val) >> 16;
}

inline void LZ::updateHashChain(std::vector<int>& hashTable,
                                   std::vector<int>& hashChain,
                                   const uint32_t hash,
                                   const int position)
{
    hashChain[position] = hashTable[hash];
    hashTable[hash] = position;
}

void LZ::promoteRecent(std::array<uint32_t, RecentOffsetCount>& recent, const int index)
{
    if (index == 0) return;
    const uint32_t val = recent[index];
    for (int i = index; i > 0; --i)
        recent[i] = recent[i - 1];
    recent[0] = val;
}

void LZ::insertRecent(std::array<uint32_t, RecentOffsetCount>& recent, const uint32_t distance)
{
    for (int i = RecentOffsetCount - 1; i > 0; --i)
        recent[i] = recent[i - 1];
    recent[0] = distance;
}

LZ::Match LZ::findBestMatch(const std::vector<uint8_t>& chunk,
                                const size_t position,
                                const CompressOptions& opts)
{
    Match best;

    if (position + opts.minMatchLength > chunk.size())
        return best;

    const auto maxDist = std::min(static_cast<uint32_t>(position), opts.maxDistance);
    if (maxDist < 1)
        return best;

    const uint8_t* const data = chunk.data();
    const uint8_t* const current = data + position;
    const auto maxLen = std::min(opts.maxMatchLength,
                                static_cast<uint32_t>(chunk.size() - position));

    for (uint32_t dist = 1; dist <= maxDist; ++dist)
    {
        const uint8_t* const candidate = current - dist;

        if (candidate[0] != current[0])
            continue;

        const uint32_t len = simdMatchLength(candidate, current, maxLen);

        if (len >= opts.minMatchLength && len > best.length)
        {
            best.distance = dist;
            best.length = len;

            if (len >= maxLen)
                break;
        }
    }

    return best;
}

LZ::IntermediateStream LZ::encodeChunk(const std::vector<uint8_t>& chunk,
                                           const CompressOptions& opts)
{
    IntermediateStream stream;
    stream.uncompressedSize = chunk.size();
    stream.symbols.reserve(chunk.size());

    if (chunk.size() < 4)
    {
        for (uint8_t b : chunk)
        {
            EncodedSymbol sym;
            sym.type = EncodedSymbol::Literal;
            sym.literal = b;
            stream.symbols.push_back(sym);
        }
        return stream;
    }

    const auto tableSize = opts.hashTableSize;
    std::vector<int> hashTable(tableSize, -1);
    std::vector<int> hashChain(chunk.size(), -1);

    const uint8_t* const data = chunk.data();
    const auto chunkSize = chunk.size();
    const auto tableMask = static_cast<uint32_t>(tableSize - 1);

    std::array<uint32_t, RecentOffsetCount> recent = {
        RecentOffsetInit, RecentOffsetInit, RecentOffsetInit
    };

    // Find best match at a given position (RLE + recent + hash chain).
    // Does NOT update the hash table — caller decides when to insert.
    struct MatchResult
    {
        Match match;
        bool isRecent = false;
        uint8_t recentIdx = 0;
    };

    auto findMatch = [&](const size_t p) -> MatchResult
    {
        MatchResult result;
        const auto maxLen = std::min(opts.maxMatchLength,
                                         static_cast<uint32_t>(chunkSize - p));

        // RLE check (distance=1)
        if (p >= 1 && maxLen >= opts.minMatchLength)
        {
            const uint8_t* const cur = data + p;
            const uint8_t* const cand = cur - 1;
            if (cand[0] == cur[0])
            {
                const uint32_t len = simdMatchLength(cand, cur, maxLen);
                if (len >= opts.minMatchLength)
                {
                    result.match.distance = 1;
                    result.match.length = len;
                }
            }
        }

        // Recent offsets
        if (p >= opts.minMatchLength)
        {
            for (int ri = 0; ri < RecentOffsetCount; ++ri)
            {
                const uint32_t dist = recent[ri];
                if (dist > p || dist == 0) continue;

                const uint8_t* const cur = data + p;
                const uint8_t* const cand = cur - dist;
                const uint32_t len = simdMatchLength(cand, cur, maxLen);

                if (len >= opts.minMatchLength &&
                    (len > result.match.length ||
                     (len == result.match.length && dist <= result.match.distance)))
                {
                    result.match.distance = dist;
                    result.match.length = len;
                    result.isRecent = true;
                    result.recentIdx = static_cast<uint8_t>(ri);
                }
            }
        }

        // Hash chain search
        if (p + 4 <= chunkSize)
        {
            const uint32_t h = hash4(data + p) & tableMask;
            const auto maxDist = std::min(static_cast<uint32_t>(p), opts.maxDistance);

            int candidate = hashTable[h];
            size_t chainSteps = 0;

            while (candidate >= 0 && chainSteps < opts.maxChainLength)
            {
                const auto dist = static_cast<uint32_t>(p - static_cast<size_t>(candidate));
                if (dist > maxDist)
                {
                    candidate = hashChain[candidate];
                    ++chainSteps;
                    continue;
                }

                const uint8_t* const cPtr = data + candidate;
                const uint8_t* const cur = data + p;

                if (cPtr[0] == cur[0])
                {
                    const uint32_t len = simdMatchLength(cPtr, cur, maxLen);

                    if (len >= opts.minMatchLength &&
                        (len > result.match.length ||
                         (len == result.match.length && dist < result.match.distance)))
                    {
                        result.match.distance = dist;
                        result.match.length = len;
                        result.isRecent = false;

                        if (len >= maxLen)
                            break;
                    }
                }

                candidate = hashChain[candidate];
                ++chainSteps;
            }
        }

        return result;
    };

    // Insert hash entry for a position
    auto insertHash = [&](const size_t p)
    {
        if (p + 4 <= chunkSize)
        {
            const uint32_t h = hash4(data + p) & tableMask;
            updateHashChain(hashTable, hashChain, h, static_cast<int>(p));
        }
    };

    // Hash skipped positions inside a match, with stride for long matches
    auto hashSkipped = [&](const size_t matchStart, const uint32_t matchLen)
    {
        if (matchLen <= 1) return;
        const size_t stride = (matchLen > opts.hashSkipLen) ? opts.hashSkipStride : 1;
        for (size_t i = 1; i < matchLen && matchStart + i + 4 <= chunkSize; i += stride)
        {
            const uint32_t h = hash4(data + matchStart + i) & tableMask;
            updateHashChain(hashTable, hashChain, h, static_cast<int>(matchStart + i));
        }
    };

    auto emitLiteral = [&](const size_t p)
    {
        EncodedSymbol sym;
        sym.type = EncodedSymbol::Literal;
        sym.literal = chunk[p];
        stream.symbols.push_back(sym);
    };

    auto emitMatch = [&](const MatchResult& mr)
    {
        EncodedSymbol sym;
        sym.type = EncodedSymbol::Match;
        sym.distance = mr.match.distance;
        sym.length = mr.match.length;
        sym.isRecentOffset = mr.isRecent;
        sym.recentIndex = mr.recentIdx;
        stream.symbols.push_back(sym);

        if (mr.isRecent)
            promoteRecent(recent, mr.recentIdx);
        else
            insertRecent(recent, mr.match.distance);
    };

    size_t pos = 0;
    while (pos < chunkSize)
    {
        auto mr = findMatch(pos);
        insertHash(pos);  // insert AFTER search so pos doesn't find itself

        if (!mr.match.isValid())
        {
            emitLiteral(pos);
            ++pos;
            continue;
        }

        // Lazy matching: only for short matches where pos+1 might beat us.
        // Long matches are almost never improved by looking ahead.
        if (opts.lazyMatching && mr.match.length < 32 && pos + 1 < chunkSize)
        {
            auto nextMr = findMatch(pos + 1);
            insertHash(pos + 1);  // insert AFTER search

            if (nextMr.match.isValid() && nextMr.match.length > mr.match.length + 1)
            {
                // Better match at pos+1: emit current position as literal
                emitLiteral(pos);
                ++pos;

                // Use the match at pos+1
                hashSkipped(pos, nextMr.match.length);
                emitMatch(nextMr);
                pos += nextMr.match.length;
                continue;
            }
        }

        // Commit to the match at pos
        hashSkipped(pos, mr.match.length);
        emitMatch(mr);
        pos += mr.match.length;
    }

    return stream;
}

std::vector<uint8_t> LZ::decodeStream(const IntermediateStream& stream)
{
    std::vector<uint8_t> output;
    output.reserve(stream.uncompressedSize);

    std::array<uint32_t, RecentOffsetCount> recent = {
        RecentOffsetInit, RecentOffsetInit, RecentOffsetInit
    };

    for (const auto& sym : stream.symbols)
    {
        if (sym.type == EncodedSymbol::Literal)
        {
            output.push_back(sym.literal);
        }
        else
        {
            const uint32_t dist = sym.distance;

            // Maintain mirrored recent-offsets cache
            if (sym.isRecentOffset)
                promoteRecent(recent, sym.recentIndex);
            else
                insertRecent(recent, dist);

            const size_t copyPos = output.size() - dist;
            for (uint32_t i = 0; i < sym.length; ++i)
            {
                output.push_back(output[copyPos + i]);
            }
        }
    }

    return output;
}

