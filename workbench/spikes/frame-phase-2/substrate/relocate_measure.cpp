// Placement under writes: rewritten bodies that no longer fit go to the end
// of the image (descriptor repointed), so address order and kind grouping
// decay. Full SUM (address segments and kind index), random-order visits
// with extent staging, and random points, at 0-50% of blocks relocated, for
// address and kind-grouped placement, warm and flushed. Answers checked.
//
// usage: frame_substrate_relocate COLS_DIR ROWS ROUNDS COLUMN...
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
inline void prefetch_range(const void *p, std::size_t n) {
    if (!n)
        return;
    __builtin_prefetch(p);
    const auto end = reinterpret_cast<std::uintptr_t>(p) + n;
    for (auto a = (reinterpret_cast<std::uintptr_t>(p) | 63) + 1; a < end; a += 64)
        __builtin_prefetch(reinterpret_cast<const void *>(a));
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
        u128 oracle = 0;
        for (u64 x : v)
            oracle += x;
        std::mt19937_64 rng(31);
        std::vector<u32> order(blocks);
        for (std::size_t b = 0; b < blocks; ++b)
            order[b] = static_cast<u32>(b);
        std::vector<u32> victims = order, random_ids = order;
        std::shuffle(victims.begin(), victims.end(), rng);
        std::shuffle(random_ids.begin(), random_ids.end(), rng);
        constexpr std::size_t requests = 65536;
        std::vector<u32> rows(requests);
        for (auto &r : rows)
            r = static_cast<u32>(rng() % n);
        u128 point_oracle = 0;
        for (u32 r : rows)
            point_oracle += v[r];
        for (const char *where : {"address", "by_kind"}) {
            for (double share : {0.0, 0.01, 0.1, 0.5}) {
                fi::image f = fi::build(v, where[0] == 'a' ? fi::placement::address : fi::placement::by_kind);
                const std::size_t moved = static_cast<std::size_t>(share * blocks);
                fi::relocate(f, std::span<const u32>(victims.data(), moved));
                const fi::kind_index index = fi::index_by_kind(f);
                struct arm {
                    const char *name;
                    std::function<u128()> run;
                    u128 expect;
                };
                const arm arms[] = {
                    {"sum_segments", [&] { return fi::sum(f); }, oracle},
                    {"sum_kind_index", [&] { return fi::sum_indexed(f, index); }, oracle},
                    {"random_staged", [&] {
                         u128 t = 0;
                         const std::size_t k = random_ids.size();
                         for (std::size_t i = 0; i < k; ++i) {
                             if (i + 8 < k) {
                                 const u32 b = random_ids[i + 8];
                                 __builtin_prefetch(f.base.data() + b), __builtin_prefetch(f.offset.data() + b);
                                 __builtin_prefetch(f.n.data() + b), __builtin_prefetch(f.kind_of.data() + b);
                                 __builtin_prefetch(f.width.data() + b);
                             }
                             if (i + 4 < k) {
                                 const auto [p, len] = fi::body(f, random_ids[i + 4]);
                                 prefetch_range(p, len);
                             }
                             t += fi::block_sum(f, random_ids[i]);
                         }
                         return t;
                     },
                     oracle},
                    {"points", [&] {
                         u128 s = 0;
                         for (u32 r : rows)
                             s += fi::point(f, r);
                         return s;
                     },
                     point_oracle},
                };
                auto evict = [&] {
                    flush(f.bytes.data(), f.bytes.size());
                    flush(f.base.data(), 8 * blocks), flush(f.offset.data(), 4 * blocks), flush(f.n.data(), 2 * blocks);
                    flush(f.kind_of.data(), blocks), flush(f.width.data(), blocks);
                    flush(index.ids.data(), 4 * index.ids.size());
                };
                constexpr std::size_t narms = sizeof arms / sizeof arms[0];
                for (const auto &x : arms)
                    if (x.run() != x.expect) {
                        std::fprintf(stderr, "%s %s %g %s: wrong answer\n", name.c_str(), where, share, x.name);
                        return 1;
                    }
                for (const char *cond : {"warm", "flushed"}) {
                    std::vector<std::vector<double>> samples(narms);
                    for (int r = 0; r < rounds; ++r)
                        for (std::size_t j = 0; j < narms; ++j) {
                            const std::size_t i = (j + static_cast<std::size_t>(r)) % narms;
                            if (cond[0] == 'f')
                                evict();
                            else
                                arms[i].run();
                            bool ok = true;
                            samples[i].push_back(time_ns([&] { ok = arms[i].run() == arms[i].expect; }));
                            if (!ok)
                                return 1;
                        }
                    for (std::size_t i = 0; i < narms; ++i) {
                        const bool pts = std::string(arms[i].name) == "points";
                        const double ns = median(samples[i]);
                        std::printf("{\"column\":\"%s\",\"placement\":\"%s\",\"relocated\":%g,\"condition\":\"%s\",\"arm\":\"%s\","
                                    "\"ns_per_unit\":%.3f,\"unit\":\"%s\",\"live_bytes\":%zu,\"total_bytes\":%zu}\n",
                                    name.c_str(), where, share, cond, arms[i].name, ns / (pts ? requests : blocks),
                                    pts ? "point" : "block", fi::build(v).body_bytes, f.body_bytes);
                    }
                }
            }
        }
    }
}
