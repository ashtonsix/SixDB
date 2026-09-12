#pragma once
#include "model.h"
#include <cassert>
#include <vector>

namespace tuple_composition_probe {
using namespace tuple_runtime;
// Each output bit is zero (-1) or a bit of the input packet. This restricted
// algebra covers byte selection, bounded shifts/masks and disjoint OR. It does
// not purport to describe arithmetic, predicates or an Engine expression IR.
using bit_routes = std::array<short, 512>;
constexpr void finish_controls(shuffle& p, bool left) {
    for (auto& indices : p.avx2_index) indices.fill(255);
    for (unsigned i = 0; i < 64; ++i) {
        if (p.mask[i]) {
            p.routes |= 1u << (p.index[i] / 16);
            p.avx2_index[p.index[i] / 16][i] = p.index[i] % 16;
            p.shifting |= p.shift[i] != 0;
        }
        p.masking |= p.mask[i] != 255;
        p.bit_index[i] = (8 * (i % 8) - p.shift[i]) & 63;
        const unsigned s = left ? p.shift[i] : -p.shift[i];
        (i & 1 ? p.odd_factor : p.even_factor)[i / 2] = left ? 1u << s : 1u << (8 - s);
    }
}
inline bit_routes routes(const shuffle& p) {
    bit_routes out; out.fill(-1);
    for (unsigned i = 0; i < 64; ++i) for (int b = 0; b < 8; ++b) {
        const int source_bit = b - p.shift[i];
        if ((p.mask[i] & (1u << b)) && source_bit >= 0 && source_bit < 8)
            out[i * 8 + b] = p.index[i] * 8 + source_bit;
    }
    return out;
}
inline bit_routes compose(const bit_routes& outer, const bit_routes& inner) {
    bit_routes out;
    for (unsigned b = 0; b < 512; ++b) out[b] = outer[b] < 0 ? -1 : inner[outer[b]];
    return out;
}
inline bit_routes unite(const bit_routes& a, const bit_routes& b) {
    bit_routes out;
    for (unsigned i = 0; i < 512; ++i) {
        // Only zero, identical, or disjoint contributions fit this algebra.
        assert(a[i] < 0 || b[i] < 0 || a[i] == b[i]);
        out[i] = a[i] < 0 ? b[i] : a[i];
    }
    return out;
}
struct route_term { shuffle operation; bool left = false, identity = false; };
inline std::vector<route_term> lower(const bit_routes& bits) {
    std::vector<route_term> result;
    for (unsigned i = 0; i < 64; ++i) for (unsigned b = 0; b < 8; ++b) {
        const int source = bits[i * 8 + b];
        if (source < 0) continue;
        const int shift = int(b) - source % 8;
        const bool left = shift >= 0;
        auto t = result.begin();
        for (; t != result.end(); ++t)
            if (t->left == left && (!t->operation.mask[i] ||
                (t->operation.index[i] == source / 8 && t->operation.shift[i] == shift))) break;
        if (t == result.end()) { result.emplace_back(); t = result.end() - 1; t->left = left; }
        t->operation.index[i] = source / 8;
        t->operation.shift[i] = shift;
        t->operation.mask[i] |= 1u << b;
    }
    for (auto& t : result) {
        finish_controls(t.operation, t.left);
        t.identity = true;
        for (unsigned i = 0; i < 64; ++i)
            t.identity &= t.operation.index[i] == i && t.operation.shift[i] == 0 && t.operation.mask[i] == 255;
    }
    return result;
}
} // namespace tuple_composition_probe
