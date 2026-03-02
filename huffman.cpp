#include "huffman.h"
#include <functional>

// Build code lengths
std::array<uint8_t, SYMBOL_COUNT> Huffman::build_code_lengths(const std::array<uint32_t, SYMBOL_COUNT>& freq)
{
    struct Node {
        uint32_t freq;
        int symbol;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        Node(uint32_t f, int s) : freq(f), symbol(s) {}
        Node(std::unique_ptr<Node> l, std::unique_ptr<Node> r)
            : freq(l->freq + r->freq), symbol(-1), left(std::move(l)), right(std::move(r)) {}
    };

    // Build initial nodes
    std::vector<std::unique_ptr<Node>> heap;
    for(int i=0;i<SYMBOL_COUNT;i++)
        if(freq[i]>0)
            heap.push_back(std::make_unique<Node>(freq[i], i));
    if(heap.empty())
        heap.push_back(std::make_unique<Node>(1,0));

    auto cmp = [](const std::unique_ptr<Node>& a, const std::unique_ptr<Node>& b){
        return a->freq > b->freq;
    };

    std::ranges::make_heap(heap, cmp);

    // Build Huffman tree
    while(heap.size() > 1){
        std::ranges::pop_heap(heap, cmp);
        auto a = std::move(heap.back()); heap.pop_back();

        std::ranges::pop_heap(heap, cmp);
        auto b = std::move(heap.back()); heap.pop_back();

        heap.push_back(std::make_unique<Node>(std::move(a), std::move(b)));
        std::ranges::push_heap(heap, cmp);
    }

    Node* root = heap.front().get();
    std::array<uint8_t, SYMBOL_COUNT> code_lengths{};

    std::function<void(Node*, uint8_t)> traverse = [&](const Node* node, const uint8_t depth){
        if(!node) return;
        if(node->symbol >= 0)
            code_lengths[node->symbol] = depth;
        traverse(node->left.get(), depth+1);
        traverse(node->right.get(), depth+1);
    };
    traverse(root, 0);

    return code_lengths;
}

// Build canonical codes
std::array<HuffmanCode, SYMBOL_COUNT> Huffman::build_canonical_codes(const std::array<uint8_t, SYMBOL_COUNT>& lengths)
{
    std::array<HuffmanCode, SYMBOL_COUNT> table{};
    std::vector<std::pair<uint8_t,int>> symbols;

    for(int i=0;i<SYMBOL_COUNT;i++)
        if(lengths[i]>0)
            symbols.emplace_back(lengths[i], i);

    std::ranges::sort(symbols);

    uint16_t code = 0;
    uint8_t prev_len = 0;
    for(auto [len, sym] : symbols){
        code <<= len - prev_len;
        table[sym].code = code;
        table[sym].length = len;
        code++;
        prev_len = len;
    }

    return table;
}

//////////////////////////
// Build fast decode table
//////////////////////////
std::vector<DecodeEntry> Huffman::build_decode_table(const std::array<HuffmanCode, SYMBOL_COUNT>& table, const int max_len)
{
    const size_t size = 1ull << max_len;
    std::vector<DecodeEntry> decode_table(size);

    for(int sym=0;sym<SYMBOL_COUNT;sym++){
        const auto& hc = table[sym];
        if(hc.length==0) continue;

        const int shift = max_len - hc.length;
        const uint32_t start = hc.code << shift;
        const uint32_t end = start + (1u<<shift);
        for(uint32_t i=start;i<end;i++)
            decode_table[i] = {static_cast<uint8_t>(sym), hc.length};
    }

    return decode_table;
}

//////////////////////////
// Compress
//////////////////////////
std::vector<uint8_t> Huffman::compress(const std::vector<uint8_t>& input)
{
    std::array<uint32_t, SYMBOL_COUNT> freq{};
    for(const auto b: input) freq[b]++;

    const auto code_lengths = build_code_lengths(freq);
    const auto table = build_canonical_codes(code_lengths);

    BitWriter writer;

    // Header: write 1 byte per symbol code length
    for(int i=0;i<SYMBOL_COUNT;i++)
        writer.write_bits(code_lengths[i], 8);

    // Encode symbols
    for(const auto b: input){
        const auto& hc = table[b];
        writer.write_bits(hc.code, hc.length);
    }

    writer.flush();
    return writer.data();
}

//////////////////////////
// Decompress
//////////////////////////
std::vector<uint8_t> Huffman::decompress(const std::vector<uint8_t>& input)
{
    BitReader reader(input.data(), input.size());

    std::array<uint8_t, SYMBOL_COUNT> code_lengths{};
    for(int i=0;i<SYMBOL_COUNT;i++)
        code_lengths[i] = static_cast<uint8_t>(reader.read_bits(8));

    auto table = build_canonical_codes(code_lengths);

    int max_len=0;
    for(const auto l: code_lengths) if(l>max_len) max_len=l;

    const auto decode_table = build_decode_table(table, max_len);

    std::vector<uint8_t> output;
    uint64_t bit_buffer = 0;
    int bit_count = 0;

    while(true){
        while(bit_count < max_len){
            if(reader.ptr_ >= reader.end_) break;
            bit_buffer |= static_cast<uint64_t>(*reader.ptr_++) << bit_count;
            bit_count += 8;
        }
        if(bit_count==0) break;

        const auto idx = static_cast<uint32_t>(bit_buffer & (1u<<max_len)-1);
        const auto entry = decode_table[idx];

        output.push_back(entry.symbol);

        bit_buffer >>= entry.length;
        bit_count -= entry.length;
    }

    return output;
}