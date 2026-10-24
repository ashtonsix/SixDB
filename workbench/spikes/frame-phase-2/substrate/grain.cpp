// Fold and reclaim grain (PROGRAMS.md, first witness). Part 1 is the semantic
// trace: an old reader holds a two-group view; one constant block becomes a
// plane and one narrow block becomes wide inside group 0; only group 0 folds;
// old and current views are both re-read against oracles; newly allocated,
// copied and reclaimable bytes are reported, then the same with one edited
// block in every group. Part 2 times full SUM, COUNT and points over
// independently owned groups of 16, 64 and 256 blocks against the single
// globally grouped image, warm and flushed, and the cost of folding one group
// against rebuilding the whole image.
//
// usage: frame_substrate_grain COLS_DIR ROWS ROUNDS COLUMN...
#include "grouped.h"

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
u128 exact_sum(std::span<const u64> v) {
    u128 s = 0;
    for (u64 x : v)
        s += x;
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
    for (int a = 4; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v0 = load(dir + "/" + name + ".u64", want);
        const std::size_t n = v0.size(), blocks = n / fi::block_rows;

        // Part 1: the semantic trace on the first two 16-block groups.
        {
            const std::size_t G = 16, rows2 = 2 * G * fi::block_rows;
            std::vector<u64> v(v0.begin(), v0.begin() + static_cast<long>(rows2));
            const fi::grouped_view original = fi::build_grouped(v, G);
            const auto old_reader = original;  // an immutable reader holds the original groups
            const u128 old_sum = exact_sum(v);
            // Edits inside group 0: a constant block becomes a plane (or the
            // first block if none is constant), and a narrow block becomes wide.
            const fi::image &g0 = original.groups[0]->f;
            std::size_t cblock = 0, nblock = 1;
            for (std::size_t b = 0; b < g0.blocks; ++b)
                if (g0.kind_of[b] == fi::constant) {
                    cblock = b;
                    break;
                }
            for (std::size_t b = 0; b < g0.blocks; ++b)
                if (b != cblock && g0.width[b] <= 16) {
                    nblock = b;
                    break;
                }
            std::vector<u64> cur = v;
            for (unsigned i = 0; i < fi::block_rows; i += 3)
                cur[cblock * fi::block_rows + i] += i;  // breaks the constant / run
            cur[nblock * fi::block_rows + 7] ^= u64{1} << 62;  // widens to 63 bits
            const fi::grouped_view current = fi::fold_group(original, 0, std::span<const u64>(cur).first(G * fi::block_rows));
            const bool shared = current.groups[1] == original.groups[1];
            bool ok = fi::sum(old_reader) == old_sum && fi::sum(current) == exact_sum(cur);
            for (std::size_t r = 0; r < rows2 && ok; r += 37)
                ok = fi::point(old_reader, r) == v[r] && fi::point(current, r) == cur[r];
            const u64 t = cur[rows2 / 2];
            u64 c_old = 0, c_new = 0;
            for (std::size_t i = 0; i < rows2; ++i)
                c_old += v[i] >= t, c_new += cur[i] >= t;
            ok = ok && fi::count_ge(old_reader, t) == c_old && fi::count_ge(current, t) == c_new;
            std::printf("{\"column\":\"%s\",\"witness\":\"two_groups_one_fold\",\"ok\":%s,\"untouched_group_shared\":%s,"
                        "\"edited_blocks\":[%zu,%zu],\"edited_kinds_before\":[%u,%u],\"edited_kinds_after\":[%u,%u],\"edited_widths_before\":[%u,%u],\"edited_widths_after\":[%u,%u],"
                        "\"new_backing_bytes\":%zu,\"copied_unchanged_bytes_group\":%zu,\"reclaimable_after_old_reader\":%zu,"
                        "\"global_rebuild_bytes\":%zu}\n",
                        name.c_str(), ok ? "true" : "false", shared ? "true" : "false", cblock, nblock,
                        unsigned(g0.kind_of[cblock]), unsigned(g0.kind_of[nblock]),
                        unsigned(current.groups[0]->f.kind_of[cblock]), unsigned(current.groups[0]->f.kind_of[nblock]),
                        unsigned(g0.width[cblock]), unsigned(g0.width[nblock]),
                        unsigned(current.groups[0]->f.width[cblock]), unsigned(current.groups[0]->f.width[nblock]),
                        current.groups[0]->bytes(), current.groups[0]->bytes(), original.groups[0]->bytes(),
                        fi::build(cur, fi::placement::by_kind).image_bytes());
            if (!ok)
                return 1;
        }

        // Part 2: reads and fold cost by grain, full slice.
        const fi::image global = fi::build(v0, fi::placement::by_kind);
        const fi::kind_index gindex = fi::index_by_kind(global);
        const u128 oracle = exact_sum(v0);
        std::vector<u64> sorted = v0;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const u64 t = sorted[n / 2];
        u64 count_oracle = 0;
        for (u64 x : v0)
            count_oracle += x >= t;
        std::mt19937_64 rng(5);
        std::vector<u32> rows(65536);
        for (auto &r : rows)
            r = static_cast<u32>(rng() % n);
        u128 point_oracle = 0;
        for (u32 r : rows)
            point_oracle += v0[r];
        auto report = [&](const char *grain, std::size_t bytes, const char *cond, const char *op, double ns, double units) {
            std::printf("{\"column\":\"%s\",\"grain\":\"%s\",\"bytes\":%zu,\"condition\":\"%s\",\"op\":\"%s\",\"ns_per_unit\":%.3f}\n",
                        name.c_str(), grain, bytes, cond, op, ns / units);
        };
        for (std::size_t G : {16u, 64u, 256u, 0u}) {
            std::string grain = G ? "groups" + std::to_string(G) : "global";
            const fi::grouped_view x = G ? fi::build_grouped(v0, G) : fi::grouped_view{};
            const std::size_t gbytes = G ? fi::bytes(x) : global.image_bytes() + 4 * gindex.ids.size() + (global.bytes.size() - global.body_bytes);
            auto evict = [&] {
                if (G)
                    for (const auto &g : x.groups)
                        flush_image(g->f), flush(g->index.ids.data(), 4 * g->index.ids.size());
                else
                    flush_image(global), flush(gindex.ids.data(), 4 * gindex.ids.size());
            };
            struct cell {
                const char *op;
                std::function<u128()> run;
                u128 expect;
                double units;
            };
            const cell cells[] = {
                {"sum", [&] { return G ? fi::sum(x) : fi::sum_indexed(global, gindex); }, oracle, double(blocks)},
                {"count_ge", [&] { return u128{G ? fi::count_ge(x, t) : fi::count_ge(global, t, 0, blocks)}; }, u128{count_oracle}, double(blocks)},
                {"points", [&] {
                     u128 s = 0;
                     for (u32 r : rows)
                         s += G ? fi::point(x, r) : fi::point_default(global, r);
                     return s;
                 },
                 point_oracle, double(rows.size())},
            };
            for (const auto &c : cells)
                if (c.run() != c.expect) {
                    std::fprintf(stderr, "%s %s %s: wrong answer\n", name.c_str(), grain.c_str(), c.op);
                    return 1;
                }
            for (const char *cond : {"warm", "flushed"})
                for (const auto &c : cells) {
                    std::vector<double> ts;
                    for (int r = 0; r < rounds; ++r) {
                        if (cond[0] == 'f')
                            evict();
                        else
                            c.run();
                        bool ok = true;
                        ts.push_back(time_ns([&] { ok = c.run() == c.expect; }));
                        if (!ok)
                            return 1;
                    }
                    report(grain.c_str(), gbytes, cond, c.op, median(ts), c.units);
                }
            // Fold cost: one group re-encoded from its logical rows, or the
            // whole image rebuilt for the global control; per changed group.
            std::vector<double> ts;
            for (int r = 0; r < std::max(3, rounds / 3); ++r) {
                const std::size_t g = rng() % (G ? x.groups.size() : 1);
                const std::size_t step = (G ? G : blocks) * fi::block_rows;
                const auto span = std::span<const u64>(v0).subspan(g * step, std::min(step, n - g * step));
                if (G) {
                    fi::grouped_view y;
                    ts.push_back(time_ns([&] { y = fi::fold_group(x, g, span); }));
                    if (fi::sum(y) != oracle)  // checked after the timer stops
                        return 1;
                } else {
                    fi::image f;
                    ts.push_back(time_ns([&] { f = fi::build(v0, fi::placement::by_kind); }));
                    if (fi::sum(f) != oracle)
                        return 1;
                }
            }
            report(grain.c_str(), gbytes, "warm", "fold_one_group", median(ts), 1.0);
        }
    }
}
