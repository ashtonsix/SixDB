#pragma once
// Independent oracle written from README.md's wire description. It shares no
// code or tables with the codec: ranks are counted by enumeration, and block
// length bounds come from a memoised recursion rather than the codec's DP.
#include "bec4096.h"
#include <map>
#include <vector>

namespace bec4096::reference {
struct stream {
    std::vector<bool> bits;
    void put(unsigned value, unsigned width) {
        for (unsigned i = 0; i < width; ++i)
            bits.push_back(value >> i & 1);
    }
};
inline unsigned width_of(unsigned alternatives) {
    unsigned w = 0;
    while ((1u << w) <= alternatives)
        ++w;
    return w;
}
inline unsigned count(const bitset4096 &x, unsigned first, unsigned size) {
    unsigned n = 0;
    for (unsigned i = first; i < first + size; ++i)
        n += x.contains(i);
    return n;
}
// Rank of byte b among bytes with the same population, in increasing value.
inline unsigned byte_rank(unsigned b) {
    unsigned r = 0;
    for (unsigned v = 0; v < b; ++v)
        r += std::popcount(v) == std::popcount(b);
    return r;
}
inline unsigned choose8(unsigned c) {
    unsigned n = 0;
    for (unsigned v = 0; v < 256; ++v)
        n += unsigned(std::popcount(v)) == c;
    return n;
}
// Bec256 stream for positions first..first+255: breadth-first splits, then ranks.
inline void block(const bitset4096 &x, unsigned first, stream &out) {
    for (unsigned size = 256; size > 8; size /= 2)
        for (unsigned r = first; r < first + 256; r += size) {
            const unsigned p = count(x, r, size), q = count(x, r, size / 2), h = size / 2;
            out.put(q - (p > h ? p - h : 0), width_of(std::min(p, size - p)));
        }
    for (unsigned j = 0; j < 32; ++j) {
        unsigned b = 0;
        for (unsigned i = 0; i < 8; ++i)
            b |= unsigned(x.contains(first + 8 * j + i)) << i;
        out.put(byte_rank(b), width_of(choose8(std::popcount(b)) - 1));
    }
}
// Fewest and most stream bits over all 256-position blocks of population c.
inline std::pair<unsigned, unsigned> extremes(unsigned size, unsigned c) {
    static std::map<std::pair<unsigned, unsigned>, std::pair<unsigned, unsigned>> memo;
    if (auto it = memo.find({size, c}); it != memo.end())
        return it->second;
    std::pair<unsigned, unsigned> result;
    if (size == 8)
        result = {width_of(choose8(c) - 1), width_of(choose8(c) - 1)};
    else {
        unsigned lo = ~0u, hi = 0;
        for (unsigned q = 0; q <= c; ++q) {
            if (q > size / 2 || c - q > size / 2)
                continue;
            auto [a, b] = extremes(size / 2, q);
            auto [d, e] = extremes(size / 2, c - q);
            lo = std::min(lo, a + d);
            hi = std::max(hi, b + e);
        }
        const unsigned w = width_of(std::min(c, size - c));
        result = {w + lo, w + hi};
    }
    memo[{size, c}] = result;
    return result;
}
inline std::vector<byte> encode(const bitset4096 &x) {
    stream out;
    for (unsigned size = 4096; size > 256; size /= 2)
        for (unsigned r = 0; r < 4096; r += size) {
            const unsigned p = count(x, r, size), q = count(x, r, size / 2), h = size / 2;
            out.put(q - (p > h ? p - h : 0), width_of(std::min(p, size - p)));
        }
    std::vector<stream> blocks(16);
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned c = count(x, 256 * k, 256);
        if (c && c != 256)
            block(x, 256 * k, blocks[k]);
    }
    for (unsigned k = 0; k < 15; ++k) {
        const unsigned c = count(x, 256 * k, 256);
        auto [lo, hi] = extremes(256, c);
        out.put(unsigned(blocks[k].bits.size()) - lo, width_of(hi - lo));
    }
    for (auto &b : blocks)
        out.bits.insert(out.bits.end(), b.bits.begin(), b.bits.end());
    std::vector<byte> result((out.bits.size() + 7) / 8);
    for (std::size_t i = 0; i < out.bits.size(); ++i)
        if (out.bits[i])
            result[i / 8] |= byte(1u << (i % 8));
    return result;
}
} // namespace bec4096::reference
