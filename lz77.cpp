#include "lz77.h"
#include <algorithm>
#include <cstring>

inline uint32_t LZ77::hash4(const uint8_t* data)
{
    uint32_t hash = 0;
    hash |= static_cast<uint32_t>(data[0]) << 24;
    hash |= static_cast<uint32_t>(data[1]) << 16;
    hash |= static_cast<uint32_t>(data[2]) << 8;
    hash |= static_cast<uint32_t>(data[3]);
    return (hash * HASH_SEED) >> 16;
}

inline void LZ77::updateHashChain(std::vector<int>& hashTable,
                                   std::vector<int>& hashChain,
                                   uint32_t hash,
                                   int position)
{
    hashChain[position] = hashTable[hash];
    hashTable[hash] = position;
}

void LZ77::promoteRecent(std::array<uint32_t, RecentOffsetCount>& recent, int index)
{
    if (index == 0) return;
    uint32_t val = recent[index];
    for (int i = index; i > 0; --i)
        recent[i] = recent[i - 1];
    recent[0] = val;
}

void LZ77::insertRecent(std::array<uint32_t, RecentOffsetCount>& recent, uint32_t distance)
{
    for (int i = RecentOffsetCount - 1; i > 0; --i)
        recent[i] = recent[i - 1];
    recent[0] = distance;
}

LZ77::Match LZ77::findBestMatch(const std::vector<uint8_t>& chunk,
                                size_t position,
                                const CompressOptions& opts)
{
    Match best;

    if (position + opts.minMatchLength > chunk.size())
        return best;

    const auto maxDist = std::min(static_cast<uint32_t>(position), opts.maxDistance);
    if (maxDist < 1)
        return best;

    const uint8_t* data = chunk.data();
    const uint8_t* current = data + position;
    const auto maxLen = std::min(opts.maxMatchLength,
                                static_cast<uint32_t>(chunk.size() - position));

    for (uint32_t dist = 1; dist <= maxDist; ++dist)
    {
        const uint8_t* candidate = current - dist;

        if (candidate[0] != current[0])
            continue;

        uint32_t len = 0;
        while (len < maxLen && candidate[len] == current[len])
            ++len;

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

LZ77::IntermediateStream LZ77::encodeChunk(const std::vector<uint8_t>& chunk,
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

    const uint8_t* data = chunk.data();
    const auto chunkSize = chunk.size();

    std::array<uint32_t, RecentOffsetCount> recent = {
        RecentOffsetInit, RecentOffsetInit, RecentOffsetInit
    };

    size_t pos = 0;
    while (pos < chunkSize)
    {
        Match best;
        bool bestIsRecent = false;
        uint8_t bestRecentIdx = 0;

        const auto maxLen = std::min(opts.maxMatchLength,
                                     static_cast<uint32_t>(chunkSize - pos));

        // Probe recent offsets first
        if (pos >= opts.minMatchLength)
        {
            for (int ri = 0; ri < RecentOffsetCount; ++ri)
            {
                uint32_t dist = recent[ri];
                if (dist > pos || dist == 0) continue;

                const uint8_t* cur = data + pos;
                const uint8_t* cand = cur - dist;

                uint32_t len = 0;
                while (len < maxLen && cand[len] == cur[len])
                    ++len;

                if (len >= opts.minMatchLength && len > best.length)
                {
                    best.distance = dist;
                    best.length = len;
                    bestIsRecent = true;
                    bestRecentIdx = static_cast<uint8_t>(ri);
                }
            }
        }

        // Hash chain search (may find longer match than recent)
        if (pos + 4 <= chunkSize)
        {
            const uint32_t h = hash4(data + pos) & static_cast<uint32_t>(tableSize - 1);
            const auto maxDist = std::min(static_cast<uint32_t>(pos), opts.maxDistance);

            int candidate = hashTable[h];
            size_t chainSteps = 0;

            while (candidate >= 0 && chainSteps < opts.maxChainLength)
            {
                const auto dist = static_cast<uint32_t>(pos - static_cast<size_t>(candidate));
                if (dist > maxDist)
                {
                    candidate = hashChain[candidate];
                    ++chainSteps;
                    continue;
                }

                const uint8_t* cPtr = data + candidate;
                const uint8_t* cur = data + pos;

                if (cPtr[0] == cur[0])
                {
                    uint32_t len = 0;
                    while (len < maxLen && cPtr[len] == cur[len])
                        ++len;

                    if (len >= opts.minMatchLength && len > best.length)
                    {
                        best.distance = dist;
                        best.length = len;
                        bestIsRecent = false;

                        if (len >= maxLen)
                            break;
                    }
                }

                candidate = hashChain[candidate];
                ++chainSteps;
            }

            updateHashChain(hashTable, hashChain, h, static_cast<int>(pos));
        }

        if (best.isValid())
        {
            EncodedSymbol sym;
            sym.type = EncodedSymbol::Match;
            sym.distance = best.distance;
            sym.length = best.length;
            sym.isRecentOffset = bestIsRecent;
            sym.recentIndex = bestRecentIdx;
            stream.symbols.push_back(sym);

            // Update hash chain for skipped positions
            for (size_t i = 1; i < best.length && pos + i + 4 <= chunkSize; ++i)
            {
                const uint32_t h = hash4(data + pos + i) & static_cast<uint32_t>(tableSize - 1);
                updateHashChain(hashTable, hashChain, h, static_cast<int>(pos + i));
            }

            // Update recent offsets
            if (bestIsRecent)
                promoteRecent(recent, bestRecentIdx);
            else
                insertRecent(recent, best.distance);

            pos += best.length;
        }
        else
        {
            EncodedSymbol sym;
            sym.type = EncodedSymbol::Literal;
            sym.literal = chunk[pos];
            stream.symbols.push_back(sym);
            ++pos;
        }
    }

    return stream;
}

std::vector<uint8_t> LZ77::decodeStream(const IntermediateStream& stream)
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
            uint32_t dist = sym.distance;

            // Maintain mirrored recent-offsets cache
            if (sym.isRecentOffset)
                promoteRecent(recent, sym.recentIndex);
            else
                insertRecent(recent, dist);

            size_t copyPos = output.size() - dist;
            for (uint32_t i = 0; i < sym.length; ++i)
            {
                output.push_back(output[copyPos + i]);
            }
        }
    }

    return output;
}

