// The substrate's cells re-run on the shared image (descriptor columns over
// front C's bodies), in original address placement and grouped by kind:
// full SUM, random block order with and without extent staging, random
// points, and SUM(B) WHERE A >= t fused per block. Native narrow-FOR
// controls as in the earlier probes (front A's qualified controls are the
// next replacement). Warm and flushed; every answer checked.
//
// usage: frame_substrate_image COLS_DIR ROWS ROUNDS COLUMN[:PREDICATE_COLUMN]...
#include "cells.h"
#include "image.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <optional>
#include <variant>
#include <random>
#include <string>
#include <vector>

namespace fi = frame_image;
using fi::u128;
using fi::u64;
using fi::u8;
using fi::u32;

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
// Hint every line [p, p + n) intersects.
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

struct native {
    u64 min = 0;
    unsigned lane = 8;
    std::vector<u8> bytes;
    explicit native(const std::vector<u64> &v) {
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        min = *lo;
        const u64 span = *hi - *lo;
        lane = span < (u64{1} << 8) ? 1 : span < (u64{1} << 16) ? 2 : span < (u64{1} << 32) ? 4 : 8;
        bytes.resize(v.size() * lane + 64);
        for (std::size_t i = 0; i < v.size(); ++i) {
            const u64 r = v[i] - min;
            std::memcpy(bytes.data() + i * lane, &r, lane);
        }
    }
    u64 at(std::size_t i) const {
        u64 x = 0;
        std::memcpy(&x, bytes.data() + i * lane, lane);
        return x;
    }
    template <class F> decltype(auto) with(F &&f) const {
        switch (lane) {
        case 1: return f(reinterpret_cast<const std::uint8_t *>(bytes.data()));
        case 2: return f(reinterpret_cast<const std::uint16_t *>(bytes.data()));
        case 4: return f(reinterpret_cast<const std::uint32_t *>(bytes.data()));
        default: return f(reinterpret_cast<const u64 *>(bytes.data()));
        }
    }
    // Exact SUM of block b's 256 values (base excluded).
    u128 block(std::size_t b) const {
        return with([&](const auto *x) -> u128 {
            u64 lo = 0, hi = 0;
            for (std::size_t i = b * fi::block_rows; i < (b + 1) * fi::block_rows; ++i)
                lo += u64{x[i]} & 0xffffffffu, hi += u64{x[i]} >> 32;
            return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
        });
    }
};

struct arm {
    const char *cell, *name;
    std::function<u128()> run;
    std::function<void()> evict;
};
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS COLUMN[:PREDICATE]...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    for (int arg = 4; arg < argc; ++arg) {
        const std::string spec = argv[arg];
        const auto colon = spec.find(':');
        const std::string bn = spec.substr(0, colon), an = colon == std::string::npos ? "" : spec.substr(colon + 1);
        const auto bv = load(dir + "/" + bn + ".u64", want);
        const std::size_t n = bv.size(), blocks = n / fi::block_rows;
        const fi::image addr = fi::build(bv, fi::placement::address), grouped = fi::build(bv, fi::placement::by_kind);
        const native nat(bv);
        // Front A's qualified controls (SEAMS.md, controls): typed owners and an
        // all-blocks mask, borrowed immutably for the run.
        const frame_cells::native control_b(bv);
        const std::vector<u64> all_words((blocks + 63) / 64, ~u64{0});
        const auto all_blocks = frame_cells::block_mask::bind(all_words, n);
        const fi::kind_index addr_index = fi::index_by_kind(addr), grouped_index = fi::index_by_kind(grouped);
        u128 oracle = 0;
        for (u64 x : bv)
            oracle += x;

        std::mt19937_64 rng(97);
        std::vector<u32> random_ids(blocks);
        for (std::size_t b = 0; b < blocks; ++b)
            random_ids[b] = static_cast<u32>(b);
        std::shuffle(random_ids.begin(), random_ids.end(), rng);
        constexpr std::size_t requests = 65536;
        std::vector<u32> rows(requests);
        for (auto &r : rows)
            r = static_cast<u32>(rng() % n);
        u128 point_oracle = 0;
        for (u32 r : rows)
            point_oracle += bv[r];

        auto evict_image = [](const fi::image &f) {
            return [&f] {
                flush(f.bytes.data(), f.bytes.size());
                flush(f.base.data(), 8 * f.blocks), flush(f.offset.data(), 4 * f.blocks), flush(f.n.data(), 2 * f.blocks);
                flush(f.kind_of.data(), f.blocks), flush(f.width.data(), f.blocks);
            };
        };
        auto evict_native = [&] { flush(nat.bytes.data(), nat.bytes.size()); };
        auto random_visit = [&](const fi::image &f, bool staged) {
            return [&f, &random_ids, staged] {
                u128 t = 0;
                const std::size_t k = random_ids.size();
                for (std::size_t i = 0; i < k; ++i) {
                    if (staged) {
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
                    }
                    t += fi::block_sum(f, random_ids[i]);
                }
                return t;
            };
        };
        auto native_random = [&](bool staged) {
            return [&, staged] {
                u128 t = 0;
                const std::size_t k = random_ids.size(), bytes = fi::block_rows * nat.lane;
                for (std::size_t i = 0; i < k; ++i) {
                    if (staged && i + 4 < k)
                        prefetch_range(nat.bytes.data() + random_ids[i + 4] * bytes, bytes);
                    t += nat.block(random_ids[i]);
                }
                return t + static_cast<u128>(nat.min) * n;
            };
        };
        std::vector<arm> arms = {
            {"sum", "frame_address", [&] { return fi::sum(addr); }, evict_image(addr)},
            {"sum", "frame_by_kind", [&] { return fi::sum(grouped); }, evict_image(grouped)},
            {"sum", "frame_address_kind_index", [&] { return fi::sum_indexed(addr, addr_index); }, evict_image(addr)},
            {"sum", "frame_by_kind_kind_index", [&] { return fi::sum_indexed(grouped, grouped_index); }, evict_image(grouped)},
            {"sum", "native_qualified", [&] { return frame_cells::native_sum_once(control_b, all_blocks).sum; },
             [&] {
                 std::visit([](const auto &v) { flush(v.data(), v.size() * sizeof(v[0])); }, control_b.values);
             }},
            {"sum", "native", [&] {
                 // The strong control: lane-width u32 counters over 65,536-row
                 // chunks for u8/u16 lanes, split halves for wider lanes.
                 return nat.with([&](const auto *x) -> u128 {
                     using T = std::remove_cvref_t<decltype(*x)>;
                     u128 r = 0;
                     if constexpr (sizeof(T) <= 2) {
                         for (std::size_t c = 0; c < n; c += 65536) {
                             u32 t = 0;
                             for (std::size_t i = c; i < std::min(n, c + 65536); ++i)
                                 t += x[i];
                             r += t;
                         }
                     } else {
                         u64 lo = 0, hi = 0;
                         for (std::size_t i = 0; i < n; ++i)
                             lo += u64{x[i]} & 0xffffffffu, hi += u64{x[i]} >> 32;
                         r = static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
                     }
                     return r + static_cast<u128>(nat.min) * n;
                 });
             },
             evict_native},
            {"random_sum", "frame_address", random_visit(addr, false), evict_image(addr)},
            {"random_sum", "frame_address_staged", random_visit(addr, true), evict_image(addr)},
            {"random_sum", "frame_by_kind", random_visit(grouped, false), evict_image(grouped)},
            {"random_sum", "frame_by_kind_staged", random_visit(grouped, true), evict_image(grouped)},
            {"random_sum", "native", native_random(false), evict_native},
            {"random_sum", "native_staged", native_random(true), evict_native},
            {"points", "frame", [&] {
                 u128 s = 0;
                 for (u32 r : rows)
                     s += fi::point(addr, r);
                 return s;
             },
             evict_image(addr)},
            {"points", "frame_default", [&] {
                 u128 s = 0;
                 for (u32 r : rows)
                     s += fi::point_default(grouped, r);
                 return s;
             },
             evict_image(grouped)},
            {"points", "frame_intervals", [&] {
                 u128 s = 0;
                 for (u32 r : rows)
                     s += fi::point_intervals(addr, r);
                 return s;
             },
             evict_image(addr)},
            {"points", "frame_by_kind", [&] {
                 u128 s = 0;
                 for (u32 r : rows)
                     s += fi::point(grouped, r);
                 return s;
             },
             evict_image(grouped)},
            {"points", "native", [&] {
                 u128 s = 0;
                 for (u32 r : rows)
                     s += nat.at(r) + nat.min;
                 return s;
             },
             evict_native},
        };
        std::vector<u128> expect(arms.size(), oracle);
        for (std::size_t i = 0; i < arms.size(); ++i)
            if (std::string(arms[i].cell) == "points")
                expect[i] = point_oracle;

        // SUM(B) WHERE A >= t at three selectivities, fused per block on both
        // placements, beside front A's qualified pair control.
        std::vector<u64> av;
        std::optional<native> anat;
        std::optional<frame_cells::native> control_a;
        fi::image aimg, agrouped;
        std::vector<u64> masks(4 * blocks);
        std::vector<u64> thresholds;
        if (!an.empty()) {
            av = load(dir + "/" + an + ".u64", n);
            aimg = fi::build(av, fi::placement::address);
            agrouped = fi::build(av, fi::placement::by_kind);
            anat.emplace(av);  // one owner, borrowed by both the timed and the evict callback
            control_a.emplace(av);
            std::vector<u64> sorted = av;
            std::sort(sorted.begin(), sorted.end());
            for (double q : {0.01, 0.1, 0.5})
                thresholds.push_back(sorted[static_cast<std::size_t>((1.0 - q) * (n - 1))]);
        }
        static const char *pair_cells[] = {"pair_sum_1pct", "pair_sum_10pct", "pair_sum_50pct"};
        for (std::size_t qi = 0; qi < thresholds.size(); ++qi) {
            const u64 t = thresholds[qi];
            u128 pair_oracle = 0;
            for (std::size_t i = 0; i < n; ++i)
                if (av[i] >= t)
                    pair_oracle += bv[i];
            auto fused = [&, t](const fi::image &ai, const fi::image &bi) {
                return [&, t] {
                    u128 s = 0;
                    for (std::size_t b = 0; b < blocks; ++b) {
                        if (!fi::mask_ge(ai, t, b, b + 1, masks.data()))
                            continue;
                        s += fi::block_masked_sum(bi, b, masks.data() + 4 * b);
                    }
                    return s;
                };
            };
            arms.push_back({pair_cells[qi], "frame_fused", fused(aimg, addr), [&] { evict_image(aimg)(), evict_image(addr)(); }});
            arms.push_back({pair_cells[qi], "frame_fused_by_kind", fused(agrouped, grouped),
                            [&] { evict_image(agrouped)(), evict_image(grouped)(); }});
            arms.push_back({pair_cells[qi], "frame_staged_by_kind", [&, t] {
                                fi::mask_ge(agrouped, t, 0, blocks, masks.data());
                                return fi::masked_sum(grouped, masks.data());
                            },
                            [&] { evict_image(agrouped)(), evict_image(grouped)(); }});
            arms.push_back({pair_cells[qi], "native_qualified",
                            [&, t] { return frame_cells::native_pair_once(*control_a, control_b, t, all_blocks).sum; },
                            [&] {
                                std::visit([](const auto &v) { flush(v.data(), v.size() * sizeof(v[0])); }, control_a->values);
                                std::visit([](const auto &v) { flush(v.data(), v.size() * sizeof(v[0])); }, control_b.values);
                            }});
            for (int k = 0; k < 4; ++k)
                expect.push_back(pair_oracle);
        }
        // Optional cell filter (CELLS=sum,points,...) for focused runs.
        if (const char *keep = std::getenv("CELLS")) {
            const std::string list = std::string(",") + keep + ",";
            std::vector<arm> kept;
            std::vector<u128> kept_expect;
            for (std::size_t i = 0; i < arms.size(); ++i) {
                std::string cell = arms[i].cell;
                if (cell.rfind("pair_sum", 0) == 0)
                    cell = "pair_sum";
                if (list.find("," + cell + ",") != std::string::npos)
                    kept.push_back(arms[i]), kept_expect.push_back(expect[i]);
            }
            arms = kept, expect = kept_expect;
        }

        for (std::size_t i = 0; i < arms.size(); ++i)
            if (arms[i].run() != expect[i]) {
                std::fprintf(stderr, "%s %s %s: wrong answer\n", spec.c_str(), arms[i].cell, arms[i].name);
                return 1;
            }
        for (const char *cond : {"warm", "flushed"}) {
            std::vector<std::vector<double>> samples(arms.size());
            for (int r = 0; r < rounds; ++r)
                for (std::size_t j = 0; j < arms.size(); ++j) {
                    const std::size_t i = (j + static_cast<std::size_t>(r)) % arms.size();
                    if (cond[0] == 'f')
                        arms[i].evict();
                    else
                        arms[i].run();
                    bool ok = true;
                    samples[i].push_back(time_ns([&] { ok = arms[i].run() == expect[i]; }));
                    if (!ok)
                        return 1;
                }
            for (std::size_t i = 0; i < arms.size(); ++i) {
                const double ns = median(samples[i]);
                const bool pts = std::string(arms[i].cell) == "points";
                std::printf("{\"column\":\"%s\",\"rows\":%zu,\"condition\":\"%s\",\"cell\":\"%s\",\"arm\":\"%s\",\"ns\":%.0f,"
                            "\"ns_per_unit\":%.3f,\"unit\":\"%s\",\"image_bytes\":%zu,\"native_bytes\":%zu}\n",
                            spec.c_str(), n, cond, arms[i].cell, arms[i].name, ns, ns / (pts ? requests : blocks),
                            pts ? "point" : "block", addr.image_bytes(), n * nat.lane);
            }
        }
    }
}
