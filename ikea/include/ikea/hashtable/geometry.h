#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace ikea::hashtable {
struct geometry {
    unsigned w, b, k, bytes, tiles, slots, maximum_encoding;
    __uint128_t capacity;
    constexpr bool admits(unsigned encoding) const {
        return !tiles || (encoding >= k && encoding <= maximum_encoding);
    }
    static constexpr geometry at(unsigned w, unsigned b) {
        if (!w || w > 64 || b > w)
            throw std::invalid_argument("W/b geometry");
        const auto k = w - b;
        if (k <= 9) {
            const auto slots = 1u << k;
            return {w, b, k, (slots + 7) / 8, 0, slots, k, __uint128_t{1} << w};
        }
        const unsigned bytes = k <= 30 ? 64 : 96, tiles = (bytes - 1) / (k + 1), slots = 8 * tiles;
        return {w,
                b,
                k,
                bytes,
                tiles,
                slots,
                std::min(64u, (bytes - 1) / tiles - 1),
                (__uint128_t{1} << b) * slots};
    }
    /// First bucket exponent meeting an expected-distinct nominal primary-load
    /// ceiling in per mille. Bitmap domains need no spill headroom. This plans
    /// geometry only: imbalance can spill, and backing/value bytes, allocation
    /// admission, supported specializations and later growth remain caller-owned.
    static constexpr geometry for_population(unsigned w, std::uint64_t distinct,
                                              unsigned max_load_permille = 850) {
        if (!w || w > 64 || !max_load_permille || max_load_permille > 1000 ||
            __uint128_t(distinct) > (__uint128_t{1} << w))
            throw std::invalid_argument("population/load domain");
        for (unsigned b = 0; b < w; ++b) {
            const auto candidate = at(w, b);
            if (!candidate.tiles ||
                __uint128_t(distinct) * 1000 <= candidate.capacity * max_load_permille)
                return candidate;
        }
        return at(w, w);
    }
};
/// Percentage target band [p,2p], closest to p; nearest load if unattainable.
/// This is not a maximum occupancy policy. Check capacity against any separate
/// caller ceiling and supported bucket palette before constructing the owner.
inline geometry choose_geometry(unsigned w, std::uint64_t n, long double p) {
    if (!w || w > 64 || !(p > 0 && p <= 100) || __uint128_t(n) > (__uint128_t{1} << w))
        throw std::invalid_argument("population/load domain");
    auto best = geometry::at(w, 0);
    bool found = false;
    long double distance = std::numeric_limits<long double>::infinity();
    for (unsigned b = 0; b <= w; ++b) {
        auto g = geometry::at(w, b);
        const auto load = 100 * static_cast<long double>(n) / static_cast<long double>(g.capacity);
        const bool fits = load >= p && load <= 2 * p;
        const auto d = std::abs(load - p);
        if ((fits && !found) || (fits == found && d < distance)) {
            best = g;
            found = fits;
            distance = d;
        }
    }
    return best;
}
} // namespace ikea::hashtable
