#include "compressor.h"
#include "lz_huffman.h"
#include "lz.h"
#include <iostream>
#include <random>
#include <map>

int main()
{
    constexpr size_t size = 131072;
    std::mt19937 rng(42);
    std::vector<uint8_t> data(size);
    for (auto& b : data)
        b = (rng() % 100 < 90) ? 0xAA : static_cast<uint8_t>(rng() & 0xFF);

    // First, look at the raw LZ77 output
    LZ::CompressOptions lzOpts;
    const auto intermediate = LZ::encodeChunk(data, lzOpts);

    int literals = 0, matches = 0;
    uint64_t totalMatchLen = 0;
    std::map<int, int> matchLenBuckets; // bucket by log2
    std::map<int, int> distBuckets;
    int recentHits = 0;

    for (const auto& sym : intermediate.symbols)
    {
        if (sym.type == LZ::EncodedSymbol::Literal)
        {
            literals++;
        }
        else
        {
            matches++;
            totalMatchLen += sym.length;
            int bucket = 0;
            uint32_t v = sym.length;
            while (v > 1) { v >>= 1; bucket++; }
            matchLenBuckets[bucket]++;

            int db = 0;
            uint32_t d = sym.distance;
            while (d > 1) { d >>= 1; db++; }
            distBuckets[db]++;

            if (sym.isRecentOffset) recentHits++;
        }
    }

    std::cout << "=== LZ77 Analysis of Skewed 128 KiB ===\n\n";
    std::cout << "Literals:      " << literals << "\n";
    std::cout << "Matches:       " << matches << "\n";
    std::cout << "Recent hits:   " << recentHits << " (" << (100.0 * recentHits / matches) << "%)\n";
    std::cout << "Avg match len: " << (matches > 0 ? static_cast<double>(totalMatchLen) / matches : 0) << "\n\n";

    std::cout << "Match length distribution (log2 buckets):\n";
    for (const auto& [bucket, count] : matchLenBuckets)
        std::cout << "  2^" << bucket << " - 2^" << (bucket + 1) - 1 << ": " << count << " matches\n";

    std::cout << "\nDistance distribution (log2 buckets):\n";
    for (const auto& [bucket, count] : distBuckets)
        std::cout << "  2^" << bucket << " - 2^" << (bucket + 1) - 1 << ": " << count << " matches\n";

    // Compress chunk diagnostics (same as before)
    LZHuffman::CompressOptions opts;
    const auto chunk = LZHuffman::compressChunk(data, opts);

    auto streamHeaderSize = [](const LZHuffman::StreamHeader& h) -> size_t {
        size_t s = 1;
        if (h.type == LZHuffman::StreamType::Huffman)
        {
            int used = 0;
            for (uint8_t l : h.codeLengths) if (l) ++used;
            const size_t sparse = 1 + 1 + static_cast<size_t>(used) * 2;
            constexpr size_t dense  = 1 + 256;
            s += (used > 0 && sparse < dense) ? sparse : dense;
        }
        s += 4;
        return s;
    };

    std::cout << "\n=== Chunk Stream Breakdown ===\n";
    std::cout << "  Tokens:      " << chunk.tokenCount << " symbols, "
              << chunk.tokenStream.size() << " encoded bytes\n";
    std::cout << "  Literals:    " << chunk.literalByteCount << " symbols, "
              << chunk.literalStream.size() << " encoded bytes\n";
    std::cout << "  DistPacked:  " << chunk.distPackedCount << " symbols, "
              << chunk.distPackedStream.size() << " encoded bytes\n";
    std::cout << "  Lrl8:        " << chunk.lrl8Count << " symbols, "
              << chunk.lrl8Stream.size() << " encoded bytes\n";
    std::cout << "  ExtraBits:   " << chunk.extraBitsStream.size() << " bytes\n";
    std::cout << "  Lrl8Extra:   " << chunk.lrl8ExtraStream.size() << " bytes\n";

    const size_t totalHeaders = streamHeaderSize(chunk.tokenHeader) + streamHeaderSize(chunk.literalHeader)
                        + streamHeaderSize(chunk.distPackedHeader) + streamHeaderSize(chunk.lrl8Header)
                        + 28 + 4 + 4;
    const size_t totalData = chunk.tokenStream.size() + chunk.literalStream.size()
                     + chunk.distPackedStream.size() + chunk.lrl8Stream.size()
                     + chunk.extraBitsStream.size() + chunk.lrl8ExtraStream.size();

    std::cout << "\n  Header overhead: " << totalHeaders << " bytes\n";
    std::cout << "  Data total:      " << totalData << " bytes\n";
    std::cout << "  Grand total:     " << (totalHeaders + totalData) << " bytes\n";

    // Entropy
    std::array<uint32_t, 256> freq{};
    for (uint8_t b : data) freq[b]++;
    double entropy = 0;
    for (const auto f : freq) {
        if (f == 0) continue;
        const double p = static_cast<double>(f) / static_cast<double>(size);
        entropy -= p * std::log2(p);
    }
    std::cout << "\n  Input entropy:       " << entropy << " bits/symbol\n";
    std::cout << "  Theoretical minimum: " << static_cast<size_t>(entropy * size / 8) << " bytes\n";

    return 0;
}


