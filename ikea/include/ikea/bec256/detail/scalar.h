#pragma once
#include <ikea/bec256/detail/tables.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace ikea::bec256::detail {
// Portable execution fallback, separate from the independent test oracle.
// Fields fit in one machine word; only the final logical bytes are emitted.
inline unsigned encode_scalar(const std::uint8_t *input, unsigned population, std::uint8_t *out) {
    std::array<unsigned, 64> tree{};
    for (unsigned i = 0; i < 32; ++i)
        tree[32 + i] = std::popcount(input[i]);
    for (unsigned i = 31; i; --i)
        tree[i] = tree[2 * i] + tree[2 * i + 1];
    tree[1] = population;
    unsigned position = 0;
    std::memset(out, 0, 47);
    auto put = [&](unsigned value, unsigned width) {
        const unsigned byte = position / 8, shift = position % 8;
        if (width) {
            out[byte] |= std::uint8_t(value << shift);
            if (width + shift > 8)
                out[byte + 1] |= std::uint8_t(value >> (8 - shift));
        }
        position += width;
    };
    for (unsigned level = 1, half = 128; level < 32; level *= 2, half /= 2)
        for (unsigned i = level; i < level * 2; ++i)
            put(tree[2 * i] - (tree[i] > half ? tree[i] - half : 0),
                std::bit_width(std::min(tree[i], 2 * half - tree[i])));
    for (unsigned i = 0; i < 32; ++i)
        put(codes.rank[input[i]], byte_width[tree[32 + i]]);
    return (position + 7) / 8;
}
inline void decode_scalar(const std::uint8_t *body, unsigned population, std::uint8_t *out) {
    std::array<unsigned, 64> tree{};
    tree[1] = population;
    unsigned position = 0;
    auto take = [&](unsigned width) {
        const unsigned byte = position / 8, shift = position % 8;
        position += width;
        if (!width)
            return 0u;
        unsigned value = body[byte] >> shift;
        if (width + shift > 8)
            value |= unsigned(body[byte + 1]) << (8 - shift);
        return value & ((1u << width) - 1);
    };
    for (unsigned level = 1, half = 128; level < 32; level *= 2, half /= 2)
        for (unsigned i = level; i < level * 2; ++i) {
            tree[2 * i] = take(std::bit_width(std::min(tree[i], 2 * half - tree[i]))) +
                          (tree[i] > half ? tree[i] - half : 0);
            tree[2 * i + 1] = tree[i] - tree[2 * i];
        }
    for (unsigned i = 0; i < 32; ++i)
        out[i] = codes.value[byte_base[tree[32 + i]] + take(byte_width[tree[32 + i]])];
}
} // namespace ikea::bec256::detail
