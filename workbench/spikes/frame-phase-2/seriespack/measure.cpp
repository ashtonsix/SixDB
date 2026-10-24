// SeriesPack remeasured through its own maintained operations (Ashton,
// 2026-10-24): phase 2 compared only Local, and only through phase 1's leaves.
// For each width K and preset (compact = Local; bulk_x86 and bulk_arm =
// striped residuals at their widths), SeriesPack is packed by its own writer
// and read by its own typed reader: sum over all rows, sum under a random
// 50% row mask, filter >= median in 64-row windows, decode to u32 with read,
// and random points with get. Beside it: the native byte-lane-rounded array
// and front C's plane256 image (sum and count only) as the reference for what
// a scan leaf can reach. Uniform random K-bit values; every answer is checked
// against an independent scalar loop over the values.
//
// Conditions: warm (one part, 65,536 rows, resident after one run) and
// flushed (the array's lines flushed before each run; a conditioning step,
// not proof of DRAM service).
//
// usage: frame_seriespack_measure ROWS_FLUSHED ROUNDS
#include "../substrate/image.h"
#include "cells.h"

#include <ikea/seriespack.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <span>
#include <type_traits>
#include <string>
#include <vector>

namespace sp = ikea::seriespack;
namespace fi = frame_image;
using fi::u128;
using fi::u32;
using fi::u64;
using fi::u8;

namespace {
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
struct free_bytes {
    void operator()(ikea::byte *p) const { std::free(p); }
};

struct arm {
    const char *op, *name;
    double units;  // rows for scans, requests for points
    std::function<u64()> run;
    u64 expect;
    std::function<void()> evict;
};

struct inputs {
    std::vector<u64> v;
    std::vector<std::uint16_t> mask16;  // per 16-row region, the 50% row mask
    std::vector<u32> rows;    // random point requests
    u64 t = 0;                // filter threshold: the median
    u64 sum = 0, msum = 0, count = 0, points = 0, decoded = 0;
};

inputs make(unsigned K, std::size_t n, std::mt19937_64 &rng) {
    inputs in;
    in.v.resize(n);
    for (auto &x : in.v)
        x = K == 64 ? rng() : rng() & ((u64{1} << K) - 1);
    in.mask16.resize(n / 16);
    for (auto &m : in.mask16)
        m = static_cast<std::uint16_t>(rng());
    in.rows.resize(65536);
    for (auto &r : in.rows)
        r = static_cast<u32>(rng() % n);
    std::vector<u64> s = in.v;
    std::nth_element(s.begin(), s.begin() + n / 2, s.end());
    in.t = s[n / 2];
    for (std::size_t i = 0; i < n; ++i) {
        in.sum += in.v[i];
        if (in.mask16[i / 16] >> (i % 16) & 1)
            in.msum += in.v[i];
        in.count += in.v[i] >= in.t;
    }
    for (u32 r : in.rows)
        in.points += in.v[r];
    for (std::size_t i = 0; i < n; i += 61)
        in.decoded += in.v[i] * (i % 7 + 1);
    return in;
}

// Sampled, position-weighted checksum of a decoded u32 buffer (every 61st
// row), cheap enough to leave the timed read dominant. Each read is also
// checked in full against the values before timing.
u64 weigh(const u32 *x, std::size_t n) {
    u64 s = 0;
    for (std::size_t i = 0; i < n; i += 61)
        s += u64{x[i]} * (i % 7 + 1);
    return s;
}

template <unsigned K, sp::preset P>
void seriespack_arms(const inputs &in, const char *name, std::vector<arm> &arms, std::vector<std::shared_ptr<void>> &keep,
                     std::vector<u32> &out) {
    using L = sp::preset_layout<K, P>;
    const std::size_t n = in.v.size();
    const std::size_t bytes = n / L::tile_rows * L::tile_bytes;
    std::shared_ptr<ikea::byte> storage(static_cast<ikea::byte *>(std::aligned_alloc(64, (bytes + 63) / 64 * 64)), free_bytes{});
    keep.push_back(storage);
    const std::span<ikea::byte> plane(storage.get(), bytes);
    auto attached = sp::view<L>::attach(n, {{{plane, L::tile_bytes}, {}, {}}});
    if (!attached) {
        std::fprintf(stderr, "K=%u %s: attach: %s\n", K, name, std::string(sp::describe(attached.error())).c_str());
        std::abort();
    }
    auto view = std::make_shared<sp::view<L>>(*attached);
    keep.push_back(view);
    {
        const auto writer = sp::bind_writer(*view);
        if (!writer)
            std::abort();
        const auto write = writer->template erase<u32>();
        const auto capacity = writer->effect_capacity_initialize();
        if (!capacity)
            std::abort();
        std::vector<ikea::journal::entry> records(*capacity);
        ikea::journal effects{records};
        std::vector<u32> input(in.v.begin(), in.v.end());
        if (const auto done = write.initialize(input, effects); !done) {
            std::fprintf(stderr, "K=%u %s: initialize: %s\n", K, name, std::string(sp::describe(done.error())).c_str());
            std::abort();
        }
    }
    auto reader = std::make_shared<decltype(sp::bind_reader(*view))>(sp::bind_reader(*view));
    keep.push_back(reader);
    const auto *r = reader.get();
    const auto *m16 = in.mask16.data();
    auto evict = [storage, bytes] { flush(storage.get(), bytes); };
    arms.push_back({"sum", name, double(n), [r, n] { return r->sum_unchecked(0, n, ~u64{0}, [](std::size_t) { return std::uint16_t{0xffff}; }); }, in.sum, evict});
    arms.push_back({"masked_sum_50pct", name, double(n),
                    [r, n, m16] { return r->sum_unchecked(0, n, ~u64{0}, [m16](std::size_t row) { return m16[row / 16]; }); }, in.msum, evict});
    const u64 t = in.t;
    arms.push_back({"count_ge", name, double(n), [r, n, t] {
                        u64 c = 0;
                        for (std::size_t i = 0; i < n; i += 64)
                            c += std::popcount(r->template filter_unchecked<ikea::comparison::greater_equal>(i, 64, t));
                        return c;
                    },
                    in.count, evict});
    u32 *o = out.data();
    arms.push_back({"read_u32", name, double(n), [r, n, o] {
                        r->read_unchecked(0, n, o);
                        return weigh(o, n);
                    },
                    in.decoded, evict});
    const u32 *rows = in.rows.data();
    const std::size_t nr = in.rows.size();
    arms.push_back({"points", name, double(nr), [r, rows, nr] {
                        u64 s = 0;
                        for (std::size_t i = 0; i < nr; ++i)
                            s += r->get_unchecked(rows[i]);
                        return s;
                    },
                    in.points, evict});
}

template <class T>
void native_arms(const inputs &in, std::vector<arm> &arms, std::vector<std::shared_ptr<void>> &keep, std::vector<u32> &out) {
    const std::size_t n = in.v.size();
    auto a = std::make_shared<std::vector<T>>(in.v.begin(), in.v.end());
    keep.push_back(a);
    const T *x = a->data();
    const auto *m16 = in.mask16.data();
    auto evict = [a] { flush(a->data(), a->size() * sizeof(T)); };
    // Front A's qualified native SUM control over the same values.
    auto control = std::make_shared<frame_cells::native>(in.v);
    auto all_words = std::make_shared<std::vector<u64>>((n / fi::block_rows + 63) / 64, ~u64{0});
    keep.push_back(control), keep.push_back(all_words);
    const auto all = frame_cells::block_mask::bind(*all_words, n);
    arms.push_back({"sum", "native", double(n), [c = control.get(), all] { return static_cast<u64>(frame_cells::native_sum_once(*c, all).sum); },
                    in.sum, [c = control.get()] { std::visit([](const auto &v) { flush(v.data(), v.size() * sizeof(v[0])); }, c->values); }});
    // Byte of mask bits -> eight lane masks, so the select is a plain AND.
    auto table = std::make_shared<std::array<std::array<T, 8>, 256>>();
    for (unsigned b = 0; b < 256; ++b)
        for (unsigned j = 0; j < 8; ++j)
            (*table)[b][j] = (b >> j & 1) ? static_cast<T>(~T{0}) : T{0};
    keep.push_back(table);
    arms.push_back({"masked_sum_50pct", "native", double(n), [x, n, m16, tab = table.get()] {
                        // Lane-mask AND from a 256-entry table, chunked u32 (u64 for u32 lanes) sums.
                        using A = std::conditional_t<sizeof(T) <= 2, u32, u64>;
                        u64 s = 0;
                        for (std::size_t k = 0; k < n; k += 32768) {
                            A acc = 0;
                            for (std::size_t h = k / 8; h < std::min(n, k + 32768) / 8; ++h) {
                                const T *keep = (*tab)[(m16[h / 2] >> (8 * (h % 2))) & 0xff].data();
                                for (unsigned j = 0; j < 8; ++j)
                                    acc += A(x[8 * h + j] & keep[j]);
                            }
                            s += acc;
                        }
                        return s;
                    },
                    in.msum, evict});
    const u64 t = in.t;
    arms.push_back({"count_ge", "native", double(n), [x, n, t] {
                        // Compare in the lane type, count in chunked u32 lanes.
                        const T tt = static_cast<T>(t);
                        u64 c = 0;
                        for (std::size_t k = 0; k < n; k += 65536) {
                            u32 s = 0;
                            for (std::size_t i = k; i < std::min(n, k + 65536); ++i)
                                s += x[i] >= tt;
                            c += s;
                        }
                        return c;
                    },
                    in.count, evict});
    u32 *o = out.data();
    arms.push_back({"read_u32", "native", double(n), [x, n, o] {
                        for (std::size_t i = 0; i < n; ++i)
                            o[i] = static_cast<u32>(x[i]);
                        return weigh(o, n);
                    },
                    in.decoded, evict});
    const u32 *rows = in.rows.data();
    const std::size_t nr = in.rows.size();
    arms.push_back({"points", "native", double(nr), [x, rows, nr] {
                        u64 s = 0;
                        for (std::size_t i = 0; i < nr; ++i)
                            s += x[rows[i]];
                        return s;
                    },
                    in.points, evict});
}

void plane256_arms(const inputs &in, std::vector<arm> &arms, std::vector<std::shared_ptr<void>> &keep) {
    auto f = std::make_shared<fi::image>(fi::build(in.v, fi::placement::address, fi::policy::packed_only));
    keep.push_back(f);
    const fi::image *p = f.get();
    const double n = double(in.v.size());
    auto evict = [p] {
        flush(p->bytes.data(), p->bytes.size());
        flush(p->base.data(), 8 * p->blocks), flush(p->offset.data(), 4 * p->blocks), flush(p->n.data(), 2 * p->blocks);
        flush(p->kind_of.data(), p->blocks), flush(p->width.data(), p->blocks);
    };
    arms.push_back({"sum", "plane256_reference", n, [p] { return static_cast<u64>(fi::sum(*p)); }, in.sum, evict});
    const u64 t = in.t;
    arms.push_back({"count_ge", "plane256_reference", n, [p, t] { return fi::count_ge(*p, t, 0, p->blocks); }, in.count, evict});
}

template <unsigned K> void width(std::size_t n_flushed, int rounds, std::mt19937_64 &rng) {
    for (const char *cond : {"warm", "flushed"}) {
        const std::size_t n = cond[0] == 'w' ? 65536 : n_flushed;
        const inputs in = make(K, n, rng);
        std::vector<arm> arms;
        std::vector<std::shared_ptr<void>> keep;
        std::vector<u32> out(n);
        seriespack_arms<K, sp::preset::compact>(in, "seriespack_compact", arms, keep, out);
        if constexpr (sp::resolve(sp::preset::bulk_x86, K) != sp::resolve(sp::preset::compact, K))
            seriespack_arms<K, sp::preset::bulk_x86>(in, "seriespack_bulk_x86", arms, keep, out);
        if constexpr (sp::resolve(sp::preset::bulk_arm, K) != sp::resolve(sp::preset::compact, K) &&
                      sp::resolve(sp::preset::bulk_arm, K) != sp::resolve(sp::preset::bulk_x86, K))
            seriespack_arms<K, sp::preset::bulk_arm>(in, "seriespack_bulk_arm", arms, keep, out);
        if constexpr (K <= 8)
            native_arms<std::uint8_t>(in, arms, keep, out);
        else if constexpr (K <= 16)
            native_arms<std::uint16_t>(in, arms, keep, out);
        else
            native_arms<std::uint32_t>(in, arms, keep, out);
        plane256_arms(in, arms, keep);
        for (const auto &a : arms) {
            if (std::string(a.op) == "read_u32") {
                a.run();
                for (std::size_t i = 0; i < n; ++i)
                    if (out[i] != in.v[i]) {
                        std::fprintf(stderr, "K=%u %s %s read_u32: row %zu wrong\n", K, cond, a.name, i);
                        std::exit(1);
                    }
            }
            if (a.run() != a.expect) {
                std::fprintf(stderr, "K=%u %s %s %s: wrong answer %llu want %llu\n", K, cond, a.name, a.op,
                             (unsigned long long)a.run(), (unsigned long long)a.expect);
                std::exit(1);
            }
        }
        std::vector<std::vector<double>> samples(arms.size());
        for (int round = 0; round < rounds; ++round)
            for (std::size_t j = 0; j < arms.size(); ++j) {
                const std::size_t i = (j + static_cast<std::size_t>(round)) % arms.size();
                if (cond[0] == 'f')
                    arms[i].evict();
                else
                    arms[i].run();
                bool ok = true;
                samples[i].push_back(time_ns([&] { ok = arms[i].run() == arms[i].expect; }));
                if (!ok)
                    std::exit(1);
            }
        for (std::size_t i = 0; i < arms.size(); ++i) {
            const double ns = median(samples[i]);
            std::printf("{\"k\":%u,\"condition\":\"%s\",\"rows\":%zu,\"arm\":\"%s\",\"op\":\"%s\",\"ns\":%.0f,"
                        "\"ns_per_unit\":%.4f,\"ns_per_256_rows\":%.2f}\n",
                        K, cond, n, arms[i].name, arms[i].op, ns, ns / arms[i].units, ns / arms[i].units * 256);
        }
        std::fflush(stdout);
    }
}

template <unsigned... K> void widths(std::size_t n, int rounds, std::mt19937_64 &rng, std::integer_sequence<unsigned, K...>) {
    (width<K>(n, rounds, rng), ...);
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s ROWS_FLUSHED ROUNDS\n", argv[0]);
        return 2;
    }
    const std::size_t n = std::strtoull(argv[1], nullptr, 10) / 65536 * 65536;
    const int rounds = std::atoi(argv[2]);
    std::mt19937_64 rng(2026);
    widths(n, rounds, rng, std::integer_sequence<unsigned, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 15, 16, 20, 24, 32>{});
}
