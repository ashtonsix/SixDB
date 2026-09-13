#pragma once
#include <ikea/bec256/detail/tables.h>
#include <algorithm>
#include <bit>
#include <cstdint>

namespace ikea::bec256::detail {
struct size_features {
    unsigned population, enum_bits, transitions, quarters;
};
// Frozen Q12 quadrant model from the 2026-09-09 predictor study. Structural
// holdout coverage earns the extra features: byte histograms alone cannot
// distinguish clustered full/zero bytes from expensive permutations of them.
// Coefficient provenance and accuracy remain in Workbench, not in wire identity.
inline unsigned estimate(size_features f) noexcept {
    if (f.population == 0 || f.population == 256)
        return 0;
    struct coefficients {
        std::int32_t bias, distance, transitions, enum_bits, quarters;
    };
    static constexpr coefficients model[]{{182208, -1416, 6, 812, -45},
                                          {75051, -501, -46, 751, -73},
                                          {50282, -188, -53, 691, -66},
                                          {50567, -49, -28, 636, -72},
                                          {27276, -117, 2178, 0, -24}};
    const unsigned minority = std::min(f.population, 256 - f.population);
    const auto c = model[f.enum_bits == 0 ? 4
                                          : unsigned(minority > 8) + unsigned(minority > 32) +
                                                unsigned(minority > 96)];
    const auto acc = c.bias + c.distance * std::int32_t(128 - minority) +
                     c.transitions * std::int32_t(f.transitions) +
                     c.enum_bits * std::int32_t(f.enum_bits) +
                     c.quarters * std::int32_t(f.quarters);
    // C++23 signed right shift is floor division; adding half rounds ties up.
    return std::clamp((acc + 2048) >> 12, 0, 47);
}
inline size_features features_scalar(const std::uint8_t *input) noexcept {
    size_features f{};
    unsigned quarters[4]{};
    for (unsigned i = 0; i < 32; ++i) {
        const unsigned p = std::popcount(input[i]);
        f.population += p;
        f.enum_bits += byte_width[p];
        quarters[i / 8] += p;
        f.transitions += std::popcount((unsigned(input[i]) ^ (input[i] >> 1)) & 127u);
        if (i + 1 < 32)
            f.transitions += (input[i] >> 7) ^ (input[i + 1] & 1);
    }
    for (auto q : quarters)
        f.quarters += q * 4 < f.population ? f.population - q * 4 : q * 4 - f.population;
    return f;
}
} // namespace ikea::bec256::detail
