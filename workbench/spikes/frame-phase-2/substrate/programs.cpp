// Fixed stored programs across a workload (Ikea LEAD's direction review):
// one stored representation per column answers every operation, so the
// cost of committing to it is visible against the per-operation best.
// Programs: native narrow FOR (front A's qualified controls where they
// exist), the integer image (smaller of runs and packed, grouped, kind
// index), packed only (no runs), and sorted part dictionaries over the
// integer image. Each reports natural bytes, construction time and the cost
// of SUM, COUNT(x >= median), SUM(B) WHERE A >= its 90th percentile, and
// random points, warm and flushed. Mixes and the oracle gap are computed
// from these rows by summarise_programs.py. Every answer is checked.
//
// usage: frame_substrate_programs COLS_DIR ROWS ROUNDS B:A...
#include "cells.h"
#include "image.h"
#include "sort.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <variant>
#include <vector>

namespace fi = frame_image;
using fi::u128;
using fi::u32;
using fi::u64;
using fi::u8;

namespace {
constexpr std::size_t part_rows = 65536, part_blocks = part_rows / fi::block_rows, requests = 65536;

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
void flush_image(const fi::image &f) {
    flush(f.bytes.data(), f.bytes.size());
    flush(f.base.data(), 8 * f.blocks), flush(f.offset.data(), 4 * f.blocks), flush(f.n.data(), 2 * f.blocks);
    flush(f.kind_of.data(), f.blocks), flush(f.width.data(), f.blocks);
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

// One column under one stored program.
struct program {
    virtual ~program() = default;
    virtual std::size_t bytes() const = 0;
    virtual u128 sum() const = 0;
    virtual u64 count_ge(u64 t) const = 0;
    // Row masks (four words per block) of x >= t; returns the selected count.
    virtual u64 mask_ge(u64 t, u64 *masks) const = 0;
    virtual u128 masked_sum(const u64 *masks) const = 0;
    virtual u64 point(std::size_t row) const = 0;
    virtual void evict() const = 0;
};

struct native_program final : program {
    frame_cells::native control;
    std::vector<u64> all_words;
    frame_cells::block_mask all;
    explicit native_program(const std::vector<u64> &v)
        : control(v), all_words((v.size() / fi::block_rows + 63) / 64, ~u64{0}), all(frame_cells::block_mask::bind(all_words, v.size())) {}
    template <class F> decltype(auto) with(F &&f) const {
        return std::visit([&](const auto &x) -> decltype(auto) { return f(x.data(), x.size()); }, control.values);
    }
    std::size_t bytes() const override {
        return with([](const auto *, std::size_t n) { return n; }) * control.lane();
    }
    u128 sum() const override { return frame_cells::native_sum_once(control, all).sum; }
    u64 count_ge(u64 t) const override {
        if (t <= control.base)
            return control.rows;
        const u64 rhs = t - control.base;
        return with([&](const auto *x, std::size_t n) {
            u64 c = 0;
            for (std::size_t k = 0; k < n; k += 65536) {
                u32 s = 0;
                for (std::size_t i = k; i < std::min(n, k + 65536); ++i)
                    s += u64{x[i]} >= rhs;
                c += s;
            }
            return c;
        });
    }
    u64 mask_ge(u64 t, u64 *masks) const override {
        const u64 rhs = t <= control.base ? 0 : t - control.base;
        return with([&](const auto *x, std::size_t n) {
            u64 c = 0;
            for (std::size_t w = 0; w < n / 64; ++w) {
                u64 m = 0;
                for (unsigned j = 0; j < 64; ++j)
                    m |= u64{u64{x[w * 64 + j]} >= rhs} << j;
                masks[w] = m;
                c += std::popcount(m);
            }
            return c;
        });
    }
    u128 masked_sum(const u64 *masks) const override {
        return with([&](const auto *x, std::size_t n) {
            u64 lo = 0, hi = 0, c = 0;
            for (std::size_t w = 0; w < n / 64; ++w)
                for (u64 m = masks[w]; m; m &= m - 1) {
                    const u64 y = x[w * 64 + static_cast<std::size_t>(std::countr_zero(m))];
                    lo += y & 0xffffffffu, hi += y >> 32, ++c;
                }
            return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32) + static_cast<u128>(control.base) * c;
        });
    }
    u64 point(std::size_t row) const override {
        return with([&](const auto *x, std::size_t) { return control.base + u64{x[row]}; });
    }
    void evict() const override {
        with([](const auto *x, std::size_t n) { flush(x, n * sizeof(*x)); });
    }
};

struct image_program final : program {
    fi::image f;
    fi::kind_index index;
    image_program(const std::vector<u64> &v, fi::policy p) : f(fi::build(v, fi::placement::by_kind, p)), index(fi::index_by_kind(f)) {}
    std::size_t bytes() const override { return f.image_bytes(); }
    u128 sum() const override { return fi::sum_indexed(f, index); }
    u64 count_ge(u64 t) const override { return fi::count_ge(f, t, 0, f.blocks); }
    u64 mask_ge(u64 t, u64 *masks) const override { return fi::mask_ge(f, t, 0, f.blocks, masks); }
    u128 masked_sum(const u64 *masks) const override { return fi::masked_sum(f, masks); }
    u64 point(std::size_t row) const override { return fi::point_default(f, row); }
    void evict() const override {
        flush_image(f);
        flush(index.ids.data(), 4 * index.ids.size());
    }
};

struct dict_program final : program {
    std::vector<std::vector<u64>> dict;
    fi::image codes;
    explicit dict_program(const std::vector<u64> &v) {
        std::vector<u64> c(v.size());
        for (std::size_t first = 0; first < v.size(); first += part_rows) {
            const std::size_t limit = std::min(v.size(), first + part_rows);
            std::vector<u64> d(v.begin() + first, v.begin() + limit);
            std::sort(d.begin(), d.end());
            d.erase(std::unique(d.begin(), d.end()), d.end());
            for (std::size_t i = first; i < limit; ++i)
                c[i] = std::lower_bound(d.begin(), d.end(), v[i]) - d.begin();
            dict.push_back(std::move(d));
        }
        codes = fi::build(c, fi::placement::by_kind);
    }
    std::size_t bytes() const override {
        std::size_t n = codes.image_bytes();
        for (const auto &d : dict)
            n += 8 * d.size();
        return n;
    }
    u64 code_threshold(std::size_t part, u64 t) const {
        return std::lower_bound(dict[part].begin(), dict[part].end(), t) - dict[part].begin();
    }
    // Codes of one block, unpacked, for SUM and masked SUM (the per-code weight
    // leaf is front C's pending work).
    void unpack(std::size_t b, u32 *x) const {
        const u8 *p = codes.bytes.data() + codes.offset[b];
        const unsigned w = codes.width[b];
        switch (codes.kind_of[b]) {
        case fi::constant: std::fill(x, x + fi::block_rows, 0u); break;
        case fi::runs: fi::with_runs(w, [&](auto r) { decltype(r)::unpack(p, codes.n[b], x); }); break;
        case fi::plane: fi::with_plane(w, [&](auto K) { fi::P<K>::unpack(p, x); }); break;
        default:
            for (unsigned i = 0; i < fi::block_rows; ++i) {
                u64 y;
                std::memcpy(&y, p + 8 * i, 8);
                x[i] = static_cast<u32>(y);
            }
        }
    }
    u128 block_sum(std::size_t b, const u64 *sel) const {
        const u64 *d = dict[b / part_blocks].data() + codes.base[b];
        alignas(64) u32 x[fi::block_rows + 32];  // run unpack writes up to 32 rows past 256 (bodies/README.md, write envelopes)
        unpack(b, x);
        u64 lo = 0, hi = 0;
        for (unsigned i = 0; i < fi::block_rows; ++i) {
            const u64 y = sel && !(sel[i / 64] >> (i % 64) & 1) ? 0 : d[x[i]];
            lo += y & 0xffffffffu, hi += y >> 32;
        }
        return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
    }
    u128 sum() const override {
        u128 s = 0;
        for (std::size_t b = 0; b < codes.blocks; ++b)
            s += block_sum(b, nullptr);
        return s;
    }
    u64 count_ge(u64 t) const override {
        u64 n = 0;
        for (std::size_t first = 0, part = 0; first < codes.blocks; first += part_blocks, ++part)
            n += fi::count_ge(codes, code_threshold(part, t), first, std::min(codes.blocks, first + part_blocks));
        return n;
    }
    u64 mask_ge(u64 t, u64 *masks) const override {
        u64 n = 0;
        for (std::size_t first = 0, part = 0; first < codes.blocks; first += part_blocks, ++part)
            n += fi::mask_ge(codes, code_threshold(part, t), first, std::min(codes.blocks, first + part_blocks), masks);
        return n;
    }
    u128 masked_sum(const u64 *masks) const override {
        u128 s = 0;
        for (std::size_t b = 0; b < codes.blocks; ++b) {
            const u64 *m = masks + 4 * b;
            if (m[0] | m[1] | m[2] | m[3])
                s += block_sum(b, m);
        }
        return s;
    }
    u64 point(std::size_t row) const override { return dict[row / part_rows][fi::point_default(codes, row)]; }
    void evict() const override {
        flush_image(codes);
        for (const auto &d : dict)
            flush(d.data(), 8 * d.size());
    }
};

using maker = std::function<std::unique_ptr<program>(const std::vector<u64> &)>;
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS B:A...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    const std::pair<const char *, maker> programs[] = {
        {"native", [](const std::vector<u64> &v) { return std::unique_ptr<program>(new native_program(v)); }},
        {"integer", [](const std::vector<u64> &v) { return std::unique_ptr<program>(new image_program(v, fi::policy::bytes)); }},
        {"packed_only", [](const std::vector<u64> &v) { return std::unique_ptr<program>(new image_program(v, fi::policy::packed_only)); }},
        {"dictionary", [](const std::vector<u64> &v) { return std::unique_ptr<program>(new dict_program(v)); }},
    };
    for (int arg = 4; arg < argc; ++arg) {
        const std::string spec = argv[arg];
        const auto colon = spec.find(':');
        const std::string bn = spec.substr(0, colon), an = spec.substr(colon + 1);
        const auto bv = load(dir + "/" + bn + ".u64", want);
        const std::size_t n = bv.size(), blocks = n / fi::block_rows;
        const auto av = load(dir + "/" + an + ".u64", n);
        std::vector<u64> sorted = bv;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const u64 t_count = sorted[n / 2];
        sorted = av;
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(0.9 * n), sorted.end());
        const u64 t_pair = sorted[static_cast<std::size_t>(0.9 * n)];
        u128 sum_oracle = 0, pair_oracle = 0, point_oracle = 0;
        u64 count_oracle = 0;
        for (std::size_t i = 0; i < n; ++i) {
            sum_oracle += bv[i], count_oracle += bv[i] >= t_count;
            if (av[i] >= t_pair)
                pair_oracle += bv[i];
        }
        std::mt19937_64 rng(7);
        std::vector<u32> rows(requests);
        for (auto &r : rows)
            r = static_cast<u32>(rng() % n);
        for (u32 r : rows)
            point_oracle += bv[r];
        std::vector<u64> masks(4 * blocks);
        for (const auto &[pname, make] : programs) {
            std::unique_ptr<program> b, a;
            const double build_ns = time_ns([&] { b = make(bv); });
            a = make(av);
            struct cell {
                const char *op;
                std::function<u128()> run;
                u128 expect;
                double units;
                std::function<void()> evict;
            };
            const cell cells[] = {
                {"sum", [&] { return b->sum(); }, sum_oracle, double(blocks), [&] { b->evict(); }},
                {"count_ge", [&] { return u128{b->count_ge(t_count)}; }, u128{count_oracle}, double(blocks), [&] { b->evict(); }},
                {"pair_sum_10pct", [&] {
                     a->mask_ge(t_pair, masks.data());
                     return b->masked_sum(masks.data());
                 },
                 pair_oracle, double(blocks), [&] { a->evict(), b->evict(); }},
                {"points", [&] {
                     u128 s = 0;
                     for (u32 r : rows)
                         s += b->point(r);
                     return s;
                 },
                 point_oracle, double(requests), [&] { b->evict(); }},
            };
            for (const auto &c : cells)
                if (c.run() != c.expect) {
                    std::fprintf(stderr, "%s %s %s: wrong answer\n", spec.c_str(), pname, c.op);
                    return 1;
                }
            for (const char *cond : {"warm", "flushed"})
                for (const auto &c : cells) {
                    std::vector<double> t;
                    for (int r = 0; r < rounds; ++r) {
                        if (cond[0] == 'f')
                            c.evict();
                        else
                            c.run();
                        bool ok = true;
                        t.push_back(time_ns([&] { ok = c.run() == c.expect; }));
                        if (!ok)
                            return 1;
                    }
                    std::printf("{\"column\":\"%s\",\"program\":\"%s\",\"condition\":\"%s\",\"op\":\"%s\",\"ns\":%.0f,"
                                "\"ns_per_unit\":%.3f,\"bytes\":%zu,\"build_ns_per_block\":%.1f,\"blocks\":%zu}\n",
                                spec.c_str(), pname, cond, c.op, median(t), median(t) / c.units, b->bytes(), build_ns / blocks, blocks);
                }
        }
    }
}
