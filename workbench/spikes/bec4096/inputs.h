#pragma once
// Synthetic 4096-position shapes shared by checks and benchmarks.
#include "bec4096.h"
#include <numeric>
#include <random>
#include <string>

namespace bec4096::inputs {
enum class shape {
    random,   // every bit independent, p = 1/2
    runs,     // each block a run of full bytes, varying length
    terminal, // alternating empty/full blocks
    mixed,    // blocks drawn from empty, full, sparse, dense and random
    sparse,   // ~16 set positions per 4096
    dense,    // ~16 clear positions per 4096
    worst,    // population four in every byte (the maximum body)
    clustered // a few dense 256-blocks, rest empty
};
inline constexpr shape all[]{shape::random, shape::runs,   shape::terminal, shape::mixed,
                             shape::sparse, shape::dense,  shape::worst,    shape::clustered};
inline std::string name(shape s) {
    constexpr const char *names[]{"random", "runs",  "terminal", "mixed",
                                  "sparse", "dense", "worst",    "clustered"};
    return names[unsigned(s)];
}
inline void set_random_positions(bitset4096 &x, unsigned first, unsigned size, unsigned n,
                                 std::mt19937_64 &rng) {
    for (unsigned i = 0; i < n; ++i)
        x.set(first + unsigned(rng() % size));
}
inline bitset4096 make(shape s, unsigned seed) {
    std::mt19937_64 rng(0xbec4096u + seed * 7919u + unsigned(s) * 104729u);
    bitset4096 x;
    switch (s) {
    case shape::random:
        for (auto &w : x.words)
            w = rng();
        break;
    case shape::runs:
        for (unsigned k = 0; k < 16; ++k) {
            const unsigned n = (seed + 7 * k) % 33;
            for (unsigned j = 0; j < 8 * n; ++j)
                x.set(256 * k + j);
        }
        break;
    case shape::terminal:
        for (unsigned k = 0; k < 16; ++k)
            if ((k + seed) % 2)
                for (unsigned j = 0; j < 4; ++j)
                    x.words[4 * k + j] = ~std::uint64_t{0};
        break;
    case shape::mixed:
        for (unsigned k = 0; k < 16; ++k)
            switch (rng() % 5) {
            case 0:
                break;
            case 1:
                for (unsigned j = 0; j < 4; ++j)
                    x.words[4 * k + j] = ~std::uint64_t{0};
                break;
            case 2:
                set_random_positions(x, 256 * k, 256, 1 + rng() % 8, rng);
                break;
            case 3:
                for (unsigned j = 0; j < 4; ++j)
                    x.words[4 * k + j] = rng() | rng() | rng();
                break;
            default:
                for (unsigned j = 0; j < 4; ++j)
                    x.words[4 * k + j] = rng();
            }
        break;
    case shape::sparse:
        set_random_positions(x, 0, 4096, 16, rng);
        break;
    case shape::dense:
        for (auto &w : x.words)
            w = ~std::uint64_t{0};
        for (unsigned i = 0; i < 16; ++i)
            x.set(unsigned(rng() % 4096), false);
        break;
    case shape::worst:
        for (unsigned b = 0; b < 512; ++b) {
            std::array<unsigned, 8> order;
            std::iota(order.begin(), order.end(), 0);
            std::shuffle(order.begin(), order.end(), rng);
            for (unsigned i = 0; i < 4; ++i)
                x.set(8 * b + order[i]);
        }
        break;
    case shape::clustered:
        for (unsigned i = 0; i < 3; ++i) {
            const unsigned k = unsigned(rng() % 16);
            for (unsigned j = 0; j < 4; ++j)
                x.words[4 * k + j] = rng() | rng();
        }
        break;
    }
    return x;
}
} // namespace bec4096::inputs
