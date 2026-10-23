// Coarse block masks: SUM over the blocks a supplied mask selects, clustered
// (runs of 16 blocks, one group) or scattered (single blocks) at equal
// density, warm and with the touched inputs flushed before every call.
// Frame runs phase 1's owner (which skips empty 16-block groups) and a
// per-block arm that walks set bits; the native control sums the selected
// 256-row spans of the narrow-FOR array. Every answer is checked.
//
// usage: frame_substrate_masks COLS_DIR ROWS ROUNDS COLUMN...
#include "material.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <utility>
#include <string>
#include <vector>

namespace fe = frame_encoding;
using u64 = std::uint64_t;
using u128 = unsigned __int128;

namespace {
std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(in.tellg()) / 8) / fe::block_rows * fe::block_rows;
    std::vector<u64> v(rows);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}

// Evict [p, p + n) from every cache level, then fence. Conditioning only:
// it does not prove where later loads are served from.
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

template <class T> u64 span_sum(const T *x) {
    std::uint32_t s = 0;  // 256 (2^16 - 1) < 2^32
    for (unsigned i = 0; i < fe::block_rows; ++i)
        s += x[i];
    return s;
}
template <> u64 span_sum<std::uint32_t>(const std::uint32_t *x) {
    u64 s = 0;
    for (unsigned i = 0; i < fe::block_rows; ++i)
        s += x[i];
    return s;
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
    for (int arg = 4; arg < argc; ++arg) {
        const std::string name = argv[arg];
        const auto v = load(dir + "/" + name + ".u64", want);
        const std::size_t n = v.size(), blocks = n / fe::block_rows;
        fe::encode_options o;
        o.bind_decoders = false;
        const fe::column c = fe::encode(v, o);
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        const u64 min = *lo, span = *hi - *lo;
        const unsigned lane = span < (u64{1} << 8) ? 1 : span < (u64{1} << 16) ? 2 : span < (u64{1} << 32) ? 4 : 8;
        std::vector<std::uint8_t> nat(n * lane);
        for (std::size_t i = 0; i < n; ++i) {
            const u64 r = v[i] - min;
            std::memcpy(nat.data() + i * lane, &r, lane);
        }
        auto native_block = [&](std::size_t b) -> u128 {
            const std::uint8_t *x = nat.data() + b * fe::block_rows * lane;
            switch (lane) {
            case 1: return span_sum(reinterpret_cast<const std::uint8_t *>(x));
            case 2: return span_sum(reinterpret_cast<const std::uint16_t *>(x));
            case 4: return span_sum(reinterpret_cast<const std::uint32_t *>(x));
            default: {
                const u64 *y = reinterpret_cast<const u64 *>(x);
                u64 l = 0, h = 0;
                for (unsigned i = 0; i < fe::block_rows; ++i)
                    l += y[i] & 0xffffffffu, h += y[i] >> 32;
                return static_cast<u128>(l) + (static_cast<u128>(h) << 32);
            }
            }
        };
        std::mt19937_64 rng(97);
        for (const char *pattern : {"clustered", "scattered"}) {
            for (unsigned denom : {64u, 16u, 4u, 1u}) {
                if (denom == 1 && pattern[0] == 's')
                    continue;  // a full mask has one layout
                fe::block_mask m(blocks);
                const std::size_t target = blocks / denom;
                std::size_t set = 0;
                if (denom == 1) {
                    for (std::size_t b = 0; b < blocks; ++b)
                        m.set(b);
                    set = blocks;
                } else if (pattern[0] == 'c') {
                    std::vector<std::size_t> groups(blocks / fe::group_blocks);
                    for (std::size_t g = 0; g < groups.size(); ++g)
                        groups[g] = g;
                    std::shuffle(groups.begin(), groups.end(), rng);
                    for (std::size_t g = 0; set < target && g < groups.size(); ++g)
                        for (std::size_t b = groups[g] * fe::group_blocks; b < (groups[g] + 1) * fe::group_blocks; ++b)
                            m.set(b), ++set;
                } else {
                    std::vector<std::size_t> all(blocks);
                    for (std::size_t b = 0; b < blocks; ++b)
                        all[b] = b;
                    std::shuffle(all.begin(), all.end(), rng);
                    for (; set < target; ++set)
                        m.set(all[set]);
                }
                std::vector<std::uint32_t> ids;
                for (std::size_t w = 0; w < m.words.size(); ++w)
                    for (u64 x = m.words[w]; x; x &= x - 1)
                        ids.push_back(static_cast<std::uint32_t>(w * 64 + std::countr_zero(x)));
                // The same blocks visited in an order no prefetcher can follow,
                // and that order regrouped by (kind, width): does grouping
                // matter once accesses are unpredictable? No software prefetch.
                std::vector<std::uint32_t> random_ids = ids;
                std::shuffle(random_ids.begin(), random_ids.end(), rng);
                std::vector<std::uint32_t> kind_ids = random_ids;
                std::stable_sort(kind_ids.begin(), kind_ids.end(), [&](std::uint32_t x, std::uint32_t y) {
                    const auto &a = c.blocks[x], &b = c.blocks[y];
                    return std::pair(a.type, a.width) < std::pair(b.type, b.width);
                });
                u128 oracle = 0;
                for (std::uint32_t b : ids)
                    for (std::size_t i = b * fe::block_rows; i < (b + 1) * fe::block_rows; ++i)
                        oracle += v[i];
                struct arm {
                    const char *name;
                    std::function<u128()> run;
                    std::function<void()> evict;
                };
                const arm arms[] = {
                    {"frame_owner", [&] { return fe::encoded_sum(c, m); },
                     [&] {
                         flush(c.payload.data(), c.payload.size());
                         flush(c.blocks.data(), c.descriptor_bytes());
                     }},
                    {"frame_bits", [&] {
                         u128 t = 0;
                         for (std::uint32_t b : ids)
                             t += fe::block_sum(c, b);
                         return t;
                     },
                     [&] {
                         flush(c.payload.data(), c.payload.size());
                         flush(c.blocks.data(), c.descriptor_bytes());
                     }},
                    {"frame_bits_random", [&] {
                         u128 t = 0;
                         for (std::uint32_t b : random_ids)
                             t += fe::block_sum(c, b);
                         return t;
                     },
                     [&] {
                         flush(c.payload.data(), c.payload.size());
                         flush(c.blocks.data(), c.descriptor_bytes());
                     }},
                    {"frame_bits_random_by_kind", [&] {
                         u128 t = 0;
                         for (std::uint32_t b : kind_ids)
                             t += fe::block_sum(c, b);
                         return t;
                     },
                     [&] {
                         flush(c.payload.data(), c.payload.size());
                         flush(c.blocks.data(), c.descriptor_bytes());
                     }},
                    {"frame_bits_random_staged", [&] {
                         // Two-stage software prefetch: descriptor 16 ahead,
                         // then the body's first three lines 8 ahead.
                         u128 t = 0;
                         const std::size_t k = random_ids.size();
                         for (std::size_t i = 0; i < k; ++i) {
                             if (i + 16 < k)
                                 __builtin_prefetch(c.blocks.data() + random_ids[i + 16]);
                             if (i + 8 < k) {
                                 const std::uint8_t *body = c.payload.data() + c.blocks[random_ids[i + 8]].offset;
                                 __builtin_prefetch(body), __builtin_prefetch(body + 64), __builtin_prefetch(body + 128);
                             }
                             t += fe::block_sum(c, random_ids[i]);
                         }
                         return t;
                     },
                     [&] {
                         flush(c.payload.data(), c.payload.size());
                         flush(c.blocks.data(), c.descriptor_bytes());
                     }},
                    {"native_spans_random_staged", [&] {
                         u128 t = 0;
                         const std::size_t k = random_ids.size();
                         const std::size_t bytes = fe::block_rows * lane;
                         for (std::size_t i = 0; i < k; ++i) {
                             if (i + 4 < k) {
                                 const std::uint8_t *x = nat.data() + random_ids[i + 4] * bytes;
                                 for (std::size_t o = 0; o < bytes; o += 64)
                                     __builtin_prefetch(x + o);
                             }
                             t += native_block(random_ids[i]);
                         }
                         return t + static_cast<u128>(min) * ids.size() * fe::block_rows;
                     },
                     [&] { flush(nat.data(), nat.size()); }},
                    {"native_spans_random", [&] {
                         u128 t = 0;
                         for (std::uint32_t b : random_ids)
                             t += native_block(b);
                         return t + static_cast<u128>(min) * ids.size() * fe::block_rows;
                     },
                     [&] { flush(nat.data(), nat.size()); }},
                    {"native_spans", [&] {
                         u128 t = 0;
                         for (std::uint32_t b : ids)
                             t += native_block(b);
                         return t + static_cast<u128>(min) * ids.size() * fe::block_rows;
                     },
                     [&] { flush(nat.data(), nat.size()); }},
                };
                constexpr std::size_t narms = sizeof arms / sizeof arms[0];
                for (const char *cond : {"warm", "flushed"}) {
                    const bool cold = cond[0] == 'f';
                    std::vector<std::vector<double>> samples(narms);
                    for (int r = 0; r < rounds; ++r)
                        for (std::size_t j = 0; j < narms; ++j) {
                            const std::size_t i = (j + static_cast<std::size_t>(r)) % narms;
                            if (cold)
                                arms[i].evict();
                            else
                                arms[i].run();
                            bool ok = true;
                            samples[i].push_back(time_ns([&] { ok = arms[i].run() == oracle; }));
                            if (!ok) {
                                std::fprintf(stderr, "%s %s %s: wrong answer\n", name.c_str(), pattern, arms[i].name);
                                return 1;
                            }
                        }
                    for (std::size_t i = 0; i < narms; ++i)
                        std::printf("{\"column\":\"%s\",\"rows\":%zu,\"pattern\":\"%s\",\"selected_blocks\":%zu,\"density\":%.4f,"
                                    "\"condition\":\"%s\",\"arm\":\"%s\",\"ns\":%.0f,\"ns_per_selected_block\":%.2f,"
                                    "\"image_bytes\":%zu,\"native_bytes\":%zu}\n",
                                    name.c_str(), n, pattern, ids.size(), double(ids.size()) / blocks, cond, arms[i].name,
                                    median(samples[i]), median(samples[i]) / std::max<std::size_t>(1, ids.size()),
                                    c.payload.size() + c.descriptor_bytes(), nat.size());
                }
            }
        }
    }
}
