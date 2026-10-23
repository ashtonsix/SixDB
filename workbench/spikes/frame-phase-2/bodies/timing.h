// Timing shared by the measurement drivers: arms run in rotated order, each
// timing repeats until it covers 2 ms, every arm is checked against the first.
#pragma once

#include "simd.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace bodies {
namespace {
using clk = std::chrono::steady_clock;

std::vector<u64> selection(std::size_t blocks, unsigned one_in, std::mt19937_64 &rng) {
    std::vector<u64> s(4 * blocks);
    for (auto &w : s) {
        u64 x = 0;
        for (unsigned i = 0; i < 64; ++i)
            x |= u64{rng() % one_in == 0} << i;
        w = x;
    }
    return s;
}

struct arm {
    std::string name;
    std::size_t bytes;
    std::function<u64()> run;
};

// Runs every arm `rounds` times in rotated order; each timing repeats the
// sweep until it covers at least 2 ms. Prints the median per unit.
void measure(const std::string &head, std::vector<arm> &arms, unsigned rounds, double units, u64 *mask_out,
             std::size_t mask_words) {
    u64 want = 0, want_mask = 0;
    for (std::size_t i = 0; i < arms.size(); ++i) {
        u64 got = arms[i].run(), mask = 0;
        for (std::size_t w = 0; mask_out && w < mask_words; ++w)
            mask = mask * 0x9e3779b97f4a7c15ull + mask_out[w];
        if (i == 0)
            want = got, want_mask = mask;
        else if (got != want || mask != want_mask) {
            std::fprintf(stderr, "MISMATCH %s arm=%s\n", head.c_str(), arms[i].name.c_str());
            std::exit(1);
        }
    }
    u64 sink = 0;
    unsigned reps = 1;
    for (;;) {
        const auto t0 = clk::now();
        for (unsigned r = 0; r < reps; ++r)
            sink += arms[0].run();
        if (std::chrono::duration<double, std::nano>(clk::now() - t0).count() > 2e6 || reps >= (1u << 22))
            break;
        reps *= 2;
    }
    std::vector<std::vector<double>> t(arms.size());
    for (unsigned round = 0; round < rounds; ++round)
        for (std::size_t i = 0; i < arms.size(); ++i) {
            const std::size_t a = (i + round) % arms.size();
            const auto t0 = clk::now();
            for (unsigned r = 0; r < reps; ++r)
                sink += arms[a].run();
            t[a].push_back(std::chrono::duration<double, std::nano>(clk::now() - t0).count() / reps / units);
        }
    for (std::size_t i = 0; i < arms.size(); ++i) {
        auto &v = t[i];
        std::sort(v.begin(), v.end());
        std::printf("{\"isa\":\"%s\",%s,\"arm\":\"%s\",\"bytes\":%zu,\"ns\":%.4f,\"min\":%.4f,\"max\":%.4f}\n", isa_name(),
                    head.c_str(), arms[i].name.c_str(), arms[i].bytes, v[v.size() / 2], v.front(), v.back());
    }
    std::fflush(stdout);
    if (sink == 0x123456789)
        std::puts("");
}

}  // namespace
}  // namespace bodies
