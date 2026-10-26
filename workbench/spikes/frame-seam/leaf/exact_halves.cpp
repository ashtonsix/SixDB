// Direct typed-reader check and timing for SeriesPack's exact u128 sums
// (author::exact_regions -> exact_halves_regions), header-only: no SeriesPack
// library is linked, so the same source compiles against the current
// expression.h or against a preserved older copy put first on the include
// path (see run.sh), and the two builds' outputs compare exactly.
//
// Widths 49, 56, 57 and 64 reach the 32-bit-halves body: weighted sums from
// width 49, unweighted and masked sums from width 57. Each case checks an
// independent u128 oracle over random values with runs of maximum values (so
// both u128 halves are used), all/none/period-3/sparse/single-bit/three-in-
// four masks, and partial ranges; then times each case over the whole series.
// Prints one JSON line per case: the result's digest and ns per row.
#include <ikea/effects.h>
#include <ikea/seriespack/reader.h>
#include <ikea/seriespack/view.h>
#include <ikea/seriespack/writer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace sp = ikea::seriespack;
using u64 = std::uint64_t;
using u128 = unsigned __int128;

namespace {
void check(bool ok, const char *what, unsigned K) {
    if (!ok) {
        std::fprintf(stderr, "FAIL K=%u %s\n", K, what);
        std::exit(1);
    }
}
volatile u64 sink;

struct density {
    const char *name;
    std::uint16_t (*mask)(std::size_t origin);
};
const density densities[]{
    {"all", [](std::size_t) { return std::uint16_t{0xffff}; }},
    {"none", [](std::size_t) { return std::uint16_t{0}; }},
    {"period3", [](std::size_t o) { return std::uint16_t(0x9249u << (o / 16 % 3)); }},
    {"three_in_four", [](std::size_t o) { return std::uint16_t(~(0x1111u << (o / 16 % 4))); }},
    {"sparse", [](std::size_t o) { return std::uint16_t(o % 512 == 0 ? 1 : 0); }},
    {"single_high", [](std::size_t) { return std::uint16_t{0x8000}; }},
    {"hashed", [](std::size_t o) { return std::uint16_t(0xb6dbu ^ (o * 0x2311u)); }},
};

template <unsigned K> void run(const char *variant) {
    using L = sp::layout<K>;
    constexpr std::size_t n = 65536 + 77; // a partial last tile
    std::vector<u64> values(n);
    std::mt19937_64 rng(K);
    const u64 top = ~u64{0} >> (64 - K);
    for (std::size_t i = 0; i < n; ++i)
        values[i] = i / 4096 % 3 == 0 ? top - i % 3 : rng() & top;
    std::vector<ikea::byte> payload((n + L::tile_rows - 1) / L::tile_rows * L::tile_bytes + 64);
    const auto view = sp::view<L>::attach(n, {{{payload, L::tile_bytes}, {}, {}}});
    check(bool(view), "attach", K);
    const auto writer = sp::bind_writer(sp::author::expression(*view), n);
    check(bool(writer), "bind writer", K);
    const auto records = writer->effect_capacity_initialize();
    check(bool(records), "effect capacity", K);
    std::vector<ikea::journal::entry> entries(*records);
    ikea::journal effects{entries};
    check(bool(writer->initialize(std::span<const u64>(values), effects)), "initialize", K);
    const sp::dense_source<L> source{payload.data(), n};
    const auto reader = sp::bind_reader(source);
    const auto expr = sp::author::expression(source);
    std::array<std::uint8_t, 64> w64;
    for (unsigned j = 0; j < 64; ++j)
        w64[j] = static_cast<std::uint8_t>(j == 0 ? 255 : 64 - j);
    const std::array<std::uint8_t, 16> w16{255, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0};

    // Correctness over partial ranges first.
    for (std::size_t first : {std::size_t{0}, std::size_t{1}, std::size_t{17}, std::size_t{256}, n - 70})
        for (std::size_t count : {std::size_t{1}, std::size_t{70}, std::size_t{4099}, n - first}) {
            if (count > n - first)
                continue;
            u128 plain = 0, by16 = 0, by64 = 0;
            for (std::size_t i = first; i < first + count; ++i)
                plain += values[i], by16 += u128{values[i]} * w16[i % 16], by64 += u128{values[i]} * w64[i % 64];
            check(reader.sum_exact_unchecked(first, count) == plain, "plain", K);
            check(reader.weighted_sum_unchecked(first, count, w16.data(), 16) == by16, "weighted 16", K);
            check(reader.weighted_sum_unchecked(first, count, w64.data(), 64) == by64, "weighted 64", K);
            for (const auto &d : densities) {
                u128 masked = 0, masked16 = 0, masked64 = 0;
                for (std::size_t i = first; i < first + count; ++i)
                    if (d.mask(i - i % 16) >> (i % 16) & 1)
                        masked += values[i], masked16 += u128{values[i]} * w16[i % 16], masked64 += u128{values[i]} * w64[i % 64];
                check(reader.sum_exact_unchecked(first, count, d.mask) == masked, d.name, K);
                // Weighted and masked together: the generic body directly
                // (the public weighted SUM takes no mask).
                check(sp::author::exact_regions(expr, first, count, w16.data(), 16, true, d.mask) == masked16,
                      "weighted 16 masked", K);
                check(sp::author::exact_regions(expr, first, count, w64.data(), 64, true, d.mask) == masked64,
                      "weighted 64 masked", K);
            }
        }

    const auto time = [&](const char *op, const char *dens, auto &&f) {
        u128 got = f();
        std::vector<double> ns;
        for (int s = 0; s < 9; ++s) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int r = 0; r < 8; ++r) {
                // The payload may have changed: the call cannot be hoisted.
                asm volatile("" : : "r"(payload.data()) : "memory");
                got = f(), sink = u64(got) ^ u64(got >> 64);
            }
            ns.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (8.0 * n));
        }
        std::sort(ns.begin(), ns.end());
        std::printf("{\"variant\":\"%s\",\"K\":%u,\"op\":\"%s\",\"density\":\"%s\",\"result_hi\":%llu,\"result_lo\":%llu,"
                    "\"ns_per_row\":%.4f}\n",
                    variant, K, op, dens, (unsigned long long)u64(got >> 64), (unsigned long long)u64(got), ns[ns.size() / 2]);
    };
    time("sum_exact", "all", [&] { return reader.sum_exact_unchecked(0, n); });
    time("weighted16", "all", [&] { return reader.weighted_sum_unchecked(0, n, w16.data(), 16); });
    time("weighted64", "all", [&] { return reader.weighted_sum_unchecked(0, n, w64.data(), 64); });
    time("weighted16_masked", "period3",
         [&] { return sp::author::exact_regions(expr, 0, n, w16.data(), 16, true, densities[2].mask); });
    for (const auto &d : densities)
        time("masked_exact", d.name, [&] { return reader.sum_exact_unchecked(0, n, d.mask); });
}
} // namespace

int main(int argc, char **argv) {
    const char *variant = argc > 1 ? argv[1] : "current";
    run<49>(variant);
    run<56>(variant);
    run<57>(variant);
    run<64>(variant);
}
