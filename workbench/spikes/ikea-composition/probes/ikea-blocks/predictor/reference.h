#pragma once

#include "model.h"

#include <bit>
#include <cstdint>

namespace ikea::bec_predictor {

// Scalar feature oracle. It intentionally favours straightforward, inspectable
// byte semantics; the separate SIMD extraction probe owns measured hot bodies.
[[nodiscard]] inline std::uint64_t reference_features(const std::uint8_t* bytes) {
    constexpr unsigned widths[] = {0, 3, 5, 6, 7, 6, 5, 3, 0};
    unsigned pop = 0;
    unsigned cost = 0;
    for (unsigned i = 0; i != 32; ++i) {
        const auto byte_pop = static_cast<unsigned>(std::popcount(bytes[i]));
        pop += byte_pop;
        cost += widths[byte_pop];
    }
    return pack_features(pop < 128 ? 128 - pop : pop - 128, cost);
}

[[nodiscard]] inline std::uint64_t reference_features_all(const std::uint8_t* bytes) {
    constexpr unsigned widths[] = {0, 3, 5, 6, 7, 6, 5, 3, 0};
    unsigned pop = 0, cost = 0, transitions = 0;
    unsigned quarters[4]{};
    for (unsigned i = 0; i != 32; ++i) {
        const auto byte_pop = static_cast<unsigned>(std::popcount(bytes[i]));
        pop += byte_pop;
        cost += widths[byte_pop];
        quarters[i / 8] += byte_pop;
        transitions += static_cast<unsigned>(std::popcount((bytes[i] ^ (bytes[i] >> 1)) & 127u));
        if (i != 31) transitions += (bytes[i] >> 7) ^ (bytes[i + 1] & 1);
    }
    unsigned dispersion = 0;
    for (auto quarter : quarters) dispersion += 4 * quarter < pop ? pop - 4 * quarter : 4 * quarter - pop;
    return pack_features(pop < 128 ? 128 - pop : pop - 128, cost) |
           (std::uint64_t{transitions} << 8) | (std::uint64_t{dispersion} << 24);
}

} // namespace ikea::bec_predictor
