#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace bec_reference {
// Frozen quadrant model from the 2026-09-09 ikea-blocks/predictor study.
// Keep independent of production features/tables. Arithmetic matches that
// study's Python/C++ Q12 cross-check; its corpus/holdout evidence stays there.
inline std::array<unsigned, 4> predictor_features(const std::byte *bytes) {
    unsigned p = 0, cost = 0, transitions = 0, q[4]{};
    unsigned previous = 0;
    for (unsigned i = 0; i < 256; ++i) {
        const unsigned bit = (std::to_integer<unsigned>(bytes[i / 8]) >> (i % 8)) & 1;
        p += bit;
        q[i / 64] += bit;
        transitions += i && bit != previous;
        previous = bit;
    }
    constexpr unsigned widths[]{0, 3, 5, 6, 7, 6, 5, 3, 0};
    for (unsigned i = 0; i < 32; ++i)
        cost += widths[std::popcount(std::to_integer<unsigned>(bytes[i]))];
    unsigned dispersion = 0;
    for (auto count : q)
        dispersion += count * 4 < p ? p - count * 4 : count * 4 - p;
    return {p, cost, transitions, dispersion};
}
inline unsigned predictor(const std::byte *bytes) {
    auto f = predictor_features(bytes);
    if (f[0] == 0 || f[0] == 256)
        return 0;
    constexpr std::int64_t c[5][5]{{182208, -1416, 6, 812, -45},
                                   {75051, -501, -46, 751, -73},
                                   {50282, -188, -53, 691, -66},
                                   {50567, -49, -28, 636, -72},
                                   {27276, -117, 2178, 0, -24}};
    const unsigned minority = std::min(f[0], 256 - f[0]);
    const unsigned piece =
        f[1] == 0 ? 4 : unsigned(minority > 8) + unsigned(minority > 32) + unsigned(minority > 96);
    const auto *a = c[piece];
    auto sum = a[0] + a[1] * (128 - minority) + a[2] * f[2] + a[3] * f[1] + a[4] * f[3];
    // Explicit floor division keeps this oracle independent of signed shifts.
    sum += 2048;
    const auto rounded = sum >= 0 ? sum / 4096 : -((-sum + 4095) / 4096);
    return std::clamp<std::int64_t>(rounded, 0, 47);
}
} // namespace bec_reference
