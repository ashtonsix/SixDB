#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <algorithm>

namespace bec_reference {
// Independent bit-at-a-time transcription, carried from the 2026-09-09 probe.
// Recounts every split and enumerates byte ranks; no production tables/helpers.
inline unsigned encode(const std::byte *input, std::byte *out) {
    std::fill_n(out, 47, std::byte{0});
    auto count = [&](unsigned start, unsigned n) {
        unsigned p = 0;
        for (unsigned i = start; i < start + n; ++i)
            p += (std::to_integer<unsigned>(input[i / 8]) >> (i % 8)) & 1;
        return p;
    };
    unsigned bits = 0;
    auto put = [&](unsigned value, unsigned width) {
        for (unsigned b = 0; b < width; ++b)
            out[(bits + b) / 8] |=
                std::byte{static_cast<unsigned char>(((value >> b) & 1) << ((bits + b) % 8))};
        bits += width;
    };
    for (unsigned half = 128; half >= 8; half /= 2)
        for (unsigned start = 0; start < 256; start += 2 * half) {
            auto p = count(start, 2 * half);
            put(count(start, half) - (p > half ? p - half : 0),
                std::bit_width(std::min(p, 2 * half - p)));
        }
    for (unsigned i = 0; i < 32; ++i) {
        const auto value = std::to_integer<unsigned>(input[i]);
        const auto p = std::popcount(value);
        unsigned rank = 0, alternatives = 0;
        for (unsigned b = 0; b < 256; ++b)
            if (std::popcount(b) == p) {
                ++alternatives;
                rank += b < value;
            }
        put(rank, std::bit_width(alternatives - 1));
    }
    return (bits + 7) / 8;
}
inline unsigned population(const std::byte *input) {
    unsigned p = 0;
    for (unsigned i = 0; i < 32; ++i)
        p += std::popcount(std::to_integer<unsigned>(input[i]));
    return p;
}
} // namespace bec_reference
