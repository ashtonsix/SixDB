// Selection geometry with selectivity held fixed: the same number of selected
// rows spread over more or fewer blocks, clustered or scattered, with rows
// contiguous or random inside each touched block. SUM over the selected rows
// of one column on the shared image: Frame routes each block (skip, unmasked
// leaf when fully selected, masked leaf otherwise) beside two native controls
// over the narrow-FOR array (set-bit gather and a branch-free full pass).
// Supplied fine masks only; predicate production is a separate cell. Warm and
// flushed; every answer is checked.
//
// usage: frame_substrate_geometry COLS_DIR ROWS ROUNDS COLUMN...
#include "image.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace fi = frame_image;
using fi::u128;
using fi::u32;
using fi::u64;
using fi::u8;

namespace {
std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(in.tellg()) / 8) / fi::block_rows * fi::block_rows;
    std::vector<u64> v(rows);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}
void flush(const void *p, std::size_t n) {
    auto a = reinterpret_cast<std::uintptr_t>(p) & ~std::uintptr_t{63};
    const auto e = reinterpret_cast<std::uintptr_t>(p) + n;
    for (; a < e; a += 64) {
#if defined(__x86_64__)
        asm volatile("clflushopt (%0)" ::"r"(a) : "memory");
#elif defined(__aarch64__)
        asm volatile("dc civac, %0" ::"r"(a) : "memory");
#endif
    }
#if defined(__x86_64__)
    asm volatile("mfence" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("dsb ish" ::: "memory");
#endif
}
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS COLUMN...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    for (int a = 4; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v = load(dir + "/" + name + ".u64", want);
        const std::size_t n = v.size(), blocks = n / fi::block_rows;
        const fi::image img = fi::build(v, fi::placement::by_kind);
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        const u64 min = *lo, span = *hi - *lo;
        const unsigned lane = span < (u64{1} << 8) ? 1 : span < (u64{1} << 16) ? 2 : span < (u64{1} << 32) ? 4 : 8;
        std::vector<u8> nat(n * lane + 8);
        for (std::size_t i = 0; i < n; ++i) {
            const u64 r = v[i] - min;
            std::memcpy(nat.data() + i * lane, &r, lane);
        }
        auto nat_at = [&](std::size_t i) {
            u64 x = 0;
            std::memcpy(&x, nat.data() + i * lane, 8);
            return lane == 8 ? x : x & ((u64{1} << (8 * lane)) - 1);
        };
        std::mt19937_64 rng(53);
        // Selected rows: 1/16 of the column. Spread s: touched blocks =
        // selected / 256 * s, each holding 256 / s selected rows.
        const std::size_t selected = n / 16;
        for (unsigned spread : {1u, 2u, 4u, 8u, 16u}) {
            const std::size_t touched = std::min(blocks, selected / fi::block_rows * spread);
            const unsigned per_block = static_cast<unsigned>(selected / touched);
            for (const char *layout : {"clustered", "scattered"}) {
                for (const char *rows_in : {"contiguous", "random"}) {
                    if (per_block == fi::block_rows && rows_in[0] == 'r')
                        continue;
                    std::vector<u32> which(blocks);
                    for (std::size_t b = 0; b < blocks; ++b)
                        which[b] = static_cast<u32>(b);
                    if (layout[0] == 's')
                        std::shuffle(which.begin(), which.end(), rng);
                    else {
                        const std::size_t start = rng() % (blocks - touched + 1);
                        for (std::size_t i = 0; i < touched; ++i)
                            which[i] = static_cast<u32>(start + i);
                    }
                    std::vector<u64> masks(4 * blocks, 0);
                    std::vector<unsigned> slots(fi::block_rows);
                    for (unsigned i = 0; i < fi::block_rows; ++i)
                        slots[i] = i;
                    for (std::size_t i = 0; i < touched; ++i) {
                        const u32 b = which[i];
                        if (rows_in[0] == 'r')
                            std::shuffle(slots.begin(), slots.end(), rng);
                        const unsigned first = rows_in[0] == 'c' ? static_cast<unsigned>(rng() % (fi::block_rows - per_block + 1)) : 0;
                        for (unsigned k = 0; k < per_block; ++k) {
                            const unsigned r = rows_in[0] == 'c' ? first + k : slots[k];
                            masks[4 * b + r / 64] |= u64{1} << (r % 64);
                        }
                    }
                    u128 oracle = 0;
                    std::size_t chosen = 0;
                    for (std::size_t i = 0; i < n; ++i)
                        if (masks[i / 64] >> (i % 64) & 1)
                            oracle += v[i], ++chosen;
                    struct arm {
                        const char *name;
                        std::function<u128()> run;
                        std::function<void()> evict;
                    };
                    const arm arms[] = {
                        {"frame_routed", [&] { return fi::masked_sum(img, masks.data()); },
                         [&] {
                             flush(img.bytes.data(), img.bytes.size());
                             flush(img.base.data(), 8 * blocks), flush(img.offset.data(), 4 * blocks);
                             flush(img.n.data(), 2 * blocks), flush(img.kind_of.data(), blocks), flush(img.width.data(), blocks);
                         }},
                        {"native_gather", [&] {
                             u64 lo = 0, hi = 0, c = 0;
                             for (std::size_t w = 0; w < n / 64; ++w)
                                 for (u64 m = masks[w]; m; m &= m - 1) {
                                     const u64 x = nat_at(w * 64 + static_cast<std::size_t>(std::countr_zero(m)));
                                     lo += x & 0xffffffffu, hi += x >> 32, ++c;
                                 }
                             return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32) + static_cast<u128>(min) * c;
                         },
                         [&] { flush(nat.data(), nat.size()); }},
                        {"native_full_pass", [&] {
                             // Branch-free over typed lanes, 64 rows per mask word.
                             auto pass = [&](const auto *x) {
                                 u64 lo = 0, hi = 0, c = 0;
                                 for (std::size_t w = 0; w < n / 64; ++w) {
                                     const u64 m = masks[w];
                                     for (unsigned j = 0; j < 64; ++j) {
                                         const u64 keep = -(m >> j & 1);
                                         const u64 y = u64{x[w * 64 + j]} & keep;
                                         lo += y & 0xffffffffu, hi += y >> 32, c += keep & 1;
                                     }
                                 }
                                 return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32) + static_cast<u128>(min) * c;
                             };
                             switch (lane) {
                             case 1: return pass(reinterpret_cast<const std::uint8_t *>(nat.data()));
                             case 2: return pass(reinterpret_cast<const std::uint16_t *>(nat.data()));
                             case 4: return pass(reinterpret_cast<const std::uint32_t *>(nat.data()));
                             default: return pass(reinterpret_cast<const u64 *>(nat.data()));
                             }
                         },
                         [&] { flush(nat.data(), nat.size()); }},
                    };
                    constexpr std::size_t narms = sizeof arms / sizeof arms[0];
                    for (const auto &x : arms)
                        if (x.run() != oracle) {
                            std::fprintf(stderr, "%s spread %u %s %s %s: wrong answer\n", name.c_str(), spread, layout, rows_in, x.name);
                            return 1;
                        }
                    for (const char *cond : {"warm", "flushed"}) {
                        std::vector<std::vector<double>> samples(narms);
                        for (int r = 0; r < rounds; ++r)
                            for (std::size_t j = 0; j < narms; ++j) {
                                const std::size_t i = (j + static_cast<std::size_t>(r)) % narms;
                                if (cond[0] == 'f')
                                    arms[i].evict();
                                else
                                    arms[i].run();
                                bool ok = true;
                                samples[i].push_back(time_ns([&] { ok = arms[i].run() == oracle; }));
                                if (!ok)
                                    return 1;
                            }
                        for (std::size_t i = 0; i < narms; ++i) {
                            const double ns = median(samples[i]);
                            std::printf("{\"column\":\"%s\",\"spread\":%u,\"touched_blocks\":%zu,\"rows_per_block\":%u,\"layout\":\"%s\","
                                        "\"rows_in_block\":\"%s\",\"condition\":\"%s\",\"arm\":\"%s\",\"ns\":%.0f,\"ns_per_selected_row\":%.3f}\n",
                                        name.c_str(), spread, touched, per_block, layout, rows_in, cond, arms[i].name, ns, ns / chosen);
                        }
                    }
                }
            }
        }
    }
}
