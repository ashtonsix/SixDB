#include <ikea/hashtable/geometry.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

namespace ht = ikea::hashtable;
constexpr auto scratch = ht::geometry::for_population(64, 64);
static_assert(scratch.b == 4 && scratch.slots == 8 && scratch.capacity == 128);
static_assert(ht::geometry::for_population(8, 256).b == 0);
static_assert(ht::geometry::for_population(64, 8, 1000).b == 0);
static_assert(ht::geometry::for_population(64, 8, 850).b == 1);
static_assert(ht::geometry::for_population(64, 24, 750).b == 2);
static_assert(ht::geometry::for_population(64, 96, 750).b == 4);

int main() {
    // At a ceiling boundary, one more distinct key must either change geometry
    // or enter a domain-covering bitmap. Integer comparison avoids float drift.
    for (unsigned w = 1; w <= 64; ++w) {
        const auto domain = __uint128_t{1} << w;
        for (unsigned load : {1u, 425u, 750u, 850u, 1000u}) {
            auto check = [&](std::uint64_t n) {
                const auto selected = ht::geometry::for_population(w, n, load);
                assert(selected.w == w && selected.b <= w);
                const auto demand = __uint128_t(n) * 1000;
                assert(!selected.tiles || demand <= selected.capacity * load);
                // Every smaller exponent must fail the advertised ceiling.
                for (unsigned b = 0; b < selected.b; ++b) {
                    const auto earlier = ht::geometry::at(w, b);
                    assert(earlier.tiles && demand > earlier.capacity * load);
                }
            };
            check(0);
            check(1);
            check(domain == (__uint128_t{1} << 64)
                      ? std::numeric_limits<std::uint64_t>::max()
                      : std::uint64_t(domain));
            for (unsigned b = 0; b <= w; ++b) {
                const auto capacity = ht::geometry::at(w, b).capacity;
                const auto edge = capacity * load / 1000;
                if (edge <= std::numeric_limits<std::uint64_t>::max())
                    for (int delta : {-1, 0, 1}) {
                        if (delta < 0 && !edge)
                            continue;
                        const auto n = delta < 0 ? edge - 1 : edge + unsigned(delta);
                        if (n <= domain && n <= std::numeric_limits<std::uint64_t>::max())
                            check(std::uint64_t(n));
                    }
            }
        }
    }
    const std::array invalid{std::array<std::uint64_t, 3>{0, 0, 850},
                             std::array<std::uint64_t, 3>{65, 0, 850},
                             std::array<std::uint64_t, 3>{8, 257, 850},
                             std::array<std::uint64_t, 3>{64, 1, 0},
                             std::array<std::uint64_t, 3>{64, 1, 1001}};
    for (const auto& p : invalid) {
        bool rejected = false;
        try {
            (void)ht::geometry::for_population(unsigned(p[0]), p[1], unsigned(p[2]));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected);
    }
}
