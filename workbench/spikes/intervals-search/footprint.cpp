// Short u8 planes (Frame run ends, up to 256 bytes = 4 lines) outside L1/L2:
// bisection issues its line misses one after another, a whole-plane count
// issues them together. Arms over blocks spread across FOOTPRINT bytes:
//   current    ikea::intervals direct_source<u8> (+32 B readable window)
//   before     the frozen pre-change search
//   leaf       front C's in-leaf count (bodies/runs.h point): 16-byte steps
//   whole      one accumulated count over the whole plane, clamped spans
//   touch      prefetch every line of the plane, then the current search
//   intervals_search_footprint FOOTPRINT_BYTES [trials]
#include "candidates.h"
#include <ikea/intervals/author/search.h>
#include "control/search_before.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {
using namespace probe;
using u64 = std::uint64_t;
constexpr unsigned queries = 1u << 16;

// Front C's leaf loop, transcribed: count ends below row in 16-lane steps.
[[gnu::always_inline]] inline unsigned leaf_count(const std::uint8_t *body, unsigned n, unsigned row) {
    typedef std::uint8_t e16 __attribute__((ext_vector_type(16)));
    typedef std::uint16_t h16 __attribute__((ext_vector_type(16)));
    const e16 iota = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    e16 before = 0;
    for (unsigned r0 = 0; r0 < n; r0 += 16) {
        e16 x;
        std::memcpy(&x, body + r0, 16);
        const auto below = (x < std::uint8_t(row)) & (iota < std::uint8_t(n - r0 > 16 ? 16 : n - r0));
        before += __builtin_bit_cast(e16, below) & 1;
    }
    return __builtin_reduce_add(__builtin_convertvector(before, h16));
}
// Whole plane in 64-byte spans clamped inside the array, one reduction per span.
[[gnu::always_inline]] inline unsigned whole_count(const std::uint8_t *p, unsigned n, std::uint8_t q) {
    if (n < 64)
        return iv::detail::count_short<edge::at_or_after, 32>(p, n, q);
    unsigned total = 0, i = 0;
    for (; i + 64 <= n; i += 64)
        total += iv::detail::count_below<edge::at_or_after, 64>(p + i, q);
    if (i < n)
        total += iv::detail::count_below<edge::at_or_after, 64, true>(p + (n - 64), q, 64 - (n - i), 64);
    return total;
}

template <int Arm> [[gnu::always_inline]] inline unsigned search(const std::uint8_t *p, unsigned n, std::uint8_t q) {
    if constexpr (Arm == 0)
        return iv::lower_bound(iv::author::with_readable_window<32>(iv::direct_source<std::uint8_t>{p, n}), q);
    else if constexpr (Arm == 1)
        return iv::frozen::hybrid_array<edge::at_or_after, true>(p, n, q);
    else if constexpr (Arm == 2)
        return leaf_count(p, n, q);
    else if constexpr (Arm == 3)
        return whole_count(p, n, q);
    else {
        for (unsigned o = 0; o < n; o += 64)
            __builtin_prefetch(p + o);
        __builtin_prefetch(p + n - 1);
        return iv::lower_bound(iv::author::with_readable_window<32>(iv::direct_source<std::uint8_t>{p, n}), q);
    }
}
const char *names[] = {"current", "before", "leaf", "whole", "touch"};

struct fixture {
    std::vector<std::uint8_t> bytes;
    std::vector<std::size_t> offset;
    std::vector<std::uint16_t> count;
    std::vector<std::uint32_t> qb, salt;
    std::vector<std::uint8_t> qv;
};
template <int Arm> [[gnu::noinline]] u64 independent(const fixture &f) {
    u64 s = 0;
    for (unsigned i = 0; i < queries; ++i) {
        const auto b = f.qb[i];
        const auto *p = f.bytes.data() + f.offset[b];
        const unsigned n = f.count[b], r = search<Arm>(p, n, f.qv[i]);
        s += r + p[n + r]; // the run's value byte, as a point read would load
    }
    return s;
}
double now() {
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

int main(int argc, char **argv) {
    const std::size_t footprint = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : (64u << 20);
    const unsigned trials = argc > 2 ? unsigned(std::atoi(argv[2])) : 7;
    std::mt19937_64 rng(11);
    for (const unsigned n : {8u, 16u, 34u, 64u, 128u, 256u})
        for (const bool vary : {false, true}) {
            fixture f;
            // Blocks back to back at arbitrary offsets: [ends n][values n].
            while (f.bytes.size() < footprint) {
                const unsigned c = vary ? 1 + unsigned(rng() % n) : n;
                f.offset.push_back(f.bytes.size());
                f.count.push_back(std::uint16_t(c));
                std::vector<std::uint8_t> ends(c);
                for (auto &e : ends)
                    e = std::uint8_t(rng());
                std::sort(ends.begin(), ends.end());
                ends[c - 1] = 255;
                f.bytes.insert(f.bytes.end(), ends.begin(), ends.end());
                for (unsigned i = 0; i < c; ++i)
                    f.bytes.push_back(std::uint8_t(rng()));
            }
            f.bytes.resize(f.bytes.size() + 64); // image suffix covering the window
            const unsigned blocks = unsigned(f.offset.size());
            for (unsigned i = 0; i < queries; ++i) {
                f.qb.push_back(unsigned(rng() % blocks));
                f.qv.push_back(std::uint8_t(rng()));
            }
            using fn = u64 (*)(const fixture &);
            const fn arms[] = {independent<0>, independent<1>, independent<2>, independent<3>, independent<4>};
            const u64 want = arms[1](f);
            std::vector<std::vector<double>> samples(5);
            for (unsigned a = 0; a < 5; ++a)
                if (arms[a](f) != want) {
                    std::fprintf(stderr, "%s n %u: wrong\n", names[a], n);
                    return 1;
                }
            for (unsigned t = 0; t < trials; ++t)
                for (unsigned k = 0; k < 5; ++k) {
                    const unsigned a = (k + t) % 5;
                    const double t0 = now();
                    if (arms[a](f) != want)
                        return 1;
                    samples[a].push_back((now() - t0) / queries);
                }
            for (unsigned a = 0; a < 5; ++a) {
                auto &s = samples[a];
                std::sort(s.begin(), s.end());
                std::printf("{\"footprint\":%zu,\"n\":%u,\"counts\":\"%s\",\"arm\":\"%s\",\"median\":%.3f,\"min\":%.3f}\n",
                            footprint, n, vary ? "vary" : "fixed", names[a], s[s.size() / 2], s.front());
            }
            std::fflush(stdout);
        }
}
