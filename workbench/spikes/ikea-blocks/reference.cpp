#include "codec.h"
#include "tables.h"
#include <algorithm>
#include <array>
#include <bit>

namespace ikea_probe {
namespace {
// Deliberately independent bit-at-a-time oracle: scan the covered positions
// afresh for every split; never use a SIMD kernel's pyramid or packing helper.
template<class Emit> unsigned fields(const std::uint8_t* input, Emit emit) {
    auto count = [&](unsigned start, unsigned n) {
        unsigned p = 0;
        for (unsigned i = start; i != start + n; ++i)
            p += (input[i / 8] >> (i % 8)) & 1;
        return p;
    };
    unsigned bits = 0;
    auto put = [&](unsigned v, unsigned n) { emit(v, n, bits); bits += n; };
    for (unsigned half = 128; half >= 8; half /= 2)
        for (unsigned start = 0; start != 256; start += half * 2) {
            auto p = count(start, half * 2);
            put(count(start, half) - (p > half ? p - half : 0),
                std::bit_width(std::min(p, 2 * half - p)));
        }
    for (unsigned i = 0; i != 32; ++i) {
        auto p = std::popcount(input[i]);
        unsigned rank = 0, alternatives = 0;
        for (unsigned b = 0; b != 256; ++b)
            if (std::popcount(b) == p) { alternatives++; rank += b < input[i]; }
        put(rank, std::bit_width(alternatives - 1));
    }
    return bits;
}
}
unsigned encode_reference(const std::uint8_t* in, std::uint8_t* out) {
    std::fill_n(out, 47, 0);
    return fields(in, [&](unsigned v, unsigned n, unsigned pos) {
        for (unsigned b = 0; b != n; ++b) out[(pos+b)/8] |= ((v>>b)&1) << ((pos+b)%8);
    });
}
unsigned exact_bits_reference(const std::uint8_t* in) {
    return fields(in, [](unsigned, unsigned, unsigned) {});
}
Validation validate(std::span<const std::uint8_t> body, unsigned card) {
    if (card > 256) return {Invalid::cardinality, 0};
    unsigned pos = 0;
    auto take = [&](unsigned n, unsigned& v) {
        if (pos + n > body.size() * 8) return false;
        v = 0;
        for (unsigned j = 0; j != n; ++j) v |= ((body[(pos+j)/8] >> ((pos+j)%8)) & 1) << j;
        pos += n;
        return true;
    };
    std::array<unsigned, 64> tree{};
    tree[1] = card;
    for (unsigned level = 1, half = 128; level != 32; level *= 2, half /= 2)
        for (unsigned j = level; j != 2 * level; ++j) {
            unsigned p = tree[j], v;
            auto k = std::min(p, 2 * half - p);
            if (!take(std::bit_width(k), v)) return {Invalid::truncated, pos};
            if (v > k) return {Invalid::split, pos};
            tree[2*j] = v + (p > half ? p - half : 0);
            tree[2*j+1] = p - tree[2*j];
        }
    for (unsigned j = 32; j != 64; ++j) {
        unsigned v, p = tree[j];
        if (!take(bec::byte_width[p], v)) return {Invalid::truncated, pos};
        if (v >= bec::choices[p]) return {Invalid::rank, pos};
    }
    if ((pos+7)/8 != body.size()) return {Invalid::trailing, pos};
    if (pos%8 && (body.back() >> (pos%8))) return {Invalid::padding, pos};
    return {Invalid::none, pos};
}
}
