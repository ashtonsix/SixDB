// Edit policies on identical histories (PROGRAMS.md; front A owns the
// histories and the accounting, the substrate owns these candidates). Each
// candidate is built from the same original A and B rows, applies front A's
// ordered edit batches and answers its frozen reads; every answer is checked
// against the independent expected answer after the timers stop.
//
//   corrections        one immutable globally grouped image per column plus
//                      bounded group corrections; never folds.
//   groups16_fold      independently owned 16-block groups plus corrections;
//                      at each fold opportunity every dirty group re-encodes
//                      from its current logical rows (one group's backing).
//   native_groups16    adaptive group-local native FOR (phase 1's native
//                      program at the same 16-block grain): a write that
//                      fits stores in place; otherwise the group rebases or
//                      widens its byte lane.
//
// Reported per candidate and history: time in edit batches, in queries, in
// folds, retained bytes at the end (current view plus corrections), and the
// bytes an original-version reader keeps alive. Edits and folds are timed as
// implemented here: an immature fold is labelled as such, not hidden.
//
// usage: frame_substrate_lifecycle COLS_DIR ROWS ROUNDS A:B...
#include "corrections.h"
#include "grouped.h"
#include "history.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fi = frame_image;
namespace h = frame_histories;
using fi::u128;
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
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
std::vector<u64> coarse_rows(const std::vector<u64> &coarse, std::size_t blocks) {
    std::vector<u64> m(4 * blocks, 0);
    for (std::size_t b = 0; b < blocks; ++b)
        if (coarse.empty() || (coarse[b / 64] >> (b % 64) & 1))
            m[4 * b] = m[4 * b + 1] = m[4 * b + 2] = m[4 * b + 3] = ~u64{0};
    return m;
}

// Answer one read from any column pair offering point/sum/count/mask/masked.
template <class Col> h::answer answer_read(const Col &a, const Col &b, std::size_t blocks, const h::read &r) {
    const auto coarse = coarse_rows(r.coarse, blocks);
    const Col &c = r.column == h::field::a ? a : b;
    h::answer out;
    std::vector<u64> m(4 * blocks);
    switch (r.op) {
    case h::operation::sum:
        if (r.coarse.empty())
            out.sum = c.sum(), out.count = blocks * fi::block_rows;
        else {
            out.sum = c.masked_sum(coarse.data());
            for (u64 w : coarse)
                out.count += std::popcount(w);
        }
        break;
    case h::operation::count_ge:
        c.mask_ge(r.threshold, m.data());
        for (std::size_t i = 0; i < m.size(); ++i)
            out.count += std::popcount(m[i] & coarse[i]);
        break;
    case h::operation::pair:
        a.mask_ge(r.threshold, m.data());
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] &= coarse[i], out.count += std::popcount(m[i]);
        out.sum = b.masked_sum(m.data());
        break;
    case h::operation::points:
        for (std::size_t row : r.point_rows) {
            const std::size_t blk = row / fi::block_rows;
            if (!r.coarse.empty() && !(r.coarse[blk / 64] >> (blk % 64) & 1))
                continue;
            out.sum += b.point(row), ++out.count;
        }
        break;
    }
    return out;
}

// Adaptive group-local native FOR, 16 blocks per group.
struct native_groups {
    struct group {
        u64 base = 0;
        unsigned lane = 1;
        std::vector<u8> bytes;
        u64 at(std::size_t i) const {
            u64 x = 0;
            std::memcpy(&x, bytes.data() + i * lane, lane);
            return base + x;
        }
        void encode(std::span<const u64> v) {
            const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
            base = *lo;
            const u64 span = *hi - *lo;
            lane = span < (u64{1} << 8) ? 1 : span < (u64{1} << 16) ? 2 : span < (u64{1} << 32) ? 4 : 8;
            bytes.assign(v.size() * lane + 8, 0);
            for (std::size_t i = 0; i < v.size(); ++i) {
                const u64 r = v[i] - base;
                std::memcpy(bytes.data() + i * lane, &r, lane);
            }
        }
    };
    static constexpr std::size_t rows_per_group = 16 * fi::block_rows;
    std::vector<group> groups;
    std::size_t rebuilds = 0;
    explicit native_groups(std::span<const u64> v) {
        for (std::size_t first = 0; first < v.size(); first += rows_per_group) {
            groups.emplace_back();
            groups.back().encode(v.subspan(first, std::min(rows_per_group, v.size() - first)));
        }
    }
    u64 point(std::size_t row) const { return groups[row / rows_per_group].at(row % rows_per_group); }
    void write(std::size_t row, u64 value) {
        group &g = groups[row / rows_per_group];
        const std::size_t i = row % rows_per_group;
        const u64 limit = g.lane == 8 ? ~u64{0} : (u64{1} << (8 * g.lane)) - 1;
        if (value >= g.base && value - g.base <= limit) {
            const u64 r = value - g.base;
            std::memcpy(g.bytes.data() + i * g.lane, &r, g.lane);
            return;
        }
        // Rebase or widen: re-encode the group with the new value.
        std::vector<u64> cur((g.bytes.size() - 8) / g.lane);
        for (std::size_t k = 0; k < cur.size(); ++k)
            cur[k] = g.at(k);
        cur[i] = value;
        g.encode(cur);
        ++rebuilds;
    }
    // Typed per-group loops so the native control vectorises (a scalar
    // per-row callback would be a weak control).
    template <class F> void each_group(F &&f) const {
        std::size_t row = 0;
        for (const auto &g : groups) {
            const std::size_t n = (g.bytes.size() - 8) / g.lane;
            switch (g.lane) {
            case 1: f(row, g, reinterpret_cast<const std::uint8_t *>(g.bytes.data()), n); break;
            case 2: f(row, g, reinterpret_cast<const std::uint16_t *>(g.bytes.data()), n); break;
            case 4: f(row, g, reinterpret_cast<const std::uint32_t *>(g.bytes.data()), n); break;
            default: f(row, g, reinterpret_cast<const u64 *>(g.bytes.data()), n);
            }
            row += n;
        }
    }
    u128 sum() const {
        u128 s = 0;
        each_group([&](std::size_t, const group &g, const auto *x, std::size_t n) {
            u64 lo = 0, hi = 0;
            for (std::size_t i = 0; i < n; ++i)
                lo += u64{x[i]} & 0xffffffffu, hi += u64{x[i]} >> 32;
            s += static_cast<u128>(lo) + (static_cast<u128>(hi) << 32) + static_cast<u128>(g.base) * n;
        });
        return s;
    }
    u64 count_ge(u64 t) const {
        u64 c = 0;
        each_group([&](std::size_t, const group &g, const auto *x, std::size_t n) {
            if (t <= g.base) {
                c += n;
                return;
            }
            const u64 rhs = t - g.base;
            u64 k = 0;
            for (std::size_t i = 0; i < n; ++i)
                k += u64{x[i]} >= rhs;
            c += k;
        });
        return c;
    }
    u64 mask_ge(u64 t, u64 *masks) const {
        u64 c = 0;
        each_group([&](std::size_t row, const group &g, const auto *x, std::size_t n) {
            const u64 rhs = t <= g.base ? 0 : t - g.base;
            for (std::size_t w = 0; w < n / 64; ++w) {
                u64 m = 0;
                for (unsigned j = 0; j < 64; ++j)
                    m |= u64{u64{x[w * 64 + j]} >= rhs} << j;
                masks[row / 64 + w] = m;
                c += std::popcount(m);
            }
        });
        return c;
    }
    u128 masked_sum(const u64 *masks) const {
        u128 s = 0;
        each_group([&](std::size_t row, const group &g, const auto *x, std::size_t n) {
            u64 lo = 0, hi = 0, c = 0;
            for (std::size_t w = 0; w < n / 64; ++w) {
                const u64 m = masks[row / 64 + w];
                for (unsigned j = 0; j < 64; ++j) {
                    const u64 keep = -(m >> j & 1);
                    const u64 y = u64{x[w * 64 + j]} & keep;
                    lo += y & 0xffffffffu, hi += y >> 32, c += keep & 1;
                }
            }
            s += static_cast<u128>(lo) + (static_cast<u128>(hi) << 32) + static_cast<u128>(g.base) * c;
        });
        return s;
    }
    std::size_t rows() const {
        std::size_t n = 0;
        for (const auto &g : groups)
            n += (g.bytes.size() - 8) / g.lane;
        return n;
    }
    std::size_t bytes() const {
        std::size_t n = 0;
        for (const auto &g : groups)
            n += g.bytes.size() + 16;
        return n;
    }
};

struct image_column {
    fi::image f;
    fi::kind_index index;
    explicit image_column(std::span<const u64> v) : f(fi::build(v, fi::placement::by_kind)), index(fi::index_by_kind(f)) {}
    u64 point(std::size_t row) const { return fi::point_default(f, row); }
    u128 sum() const { return fi::sum_indexed(f, index); }
    u64 count_ge(u64 t) const { return fi::count_ge(f, t, 0, f.blocks); }
    u64 mask_ge(u64 t, u64 *masks) const { return fi::mask_ge(f, t, 0, f.blocks, masks); }
    u128 masked_sum(const u64 *masks) const { return fi::masked_sum(f, masks); }
    std::size_t bytes() const { return f.image_bytes() + (f.bytes.size() - f.body_bytes) + 4 * index.ids.size(); }
};

struct totals {
    double edit_ns = 0, query_ns = 0, fold_ns = 0;
    std::size_t failed = 0, queries = 0;
};

template <class Cand, class Fold>
totals drive(Cand &a, Cand &b, std::size_t blocks, const h::trace &trace, const std::vector<h::expected_read> &expected, Fold &&fold) {
    totals t;
    std::size_t next = 0, applied = 0;
    for (std::size_t e = 0; e < trace.events.size(); ++e) {
        const auto &ev = trace.events[e];
        if (ev.kind == h::event_kind::edits) {
            t.edit_ns += time_ns([&] {
                for (const auto &x : trace.batches[ev.index].edits)
                    (x.column == h::field::a ? a : b).write(x.row, x.value);
            });
            ++applied;
            if (std::find(trace.fold_after_batches.begin(), trace.fold_after_batches.end(), applied) != trace.fold_after_batches.end())
                t.fold_ns += time_ns([&] { fold(); });
            continue;
        }
        while (next < expected.size() && expected[next].event_index < e)
            ++next;
        const auto &x = expected[next];
        h::answer got;
        t.query_ns += time_ns([&] { got = answer_read(a, b, blocks, trace.reads[x.read_index]); });
        ++t.queries;
        t.failed += !(got == x.current);  // checked after the timer stops
    }
    return t;
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS A:B...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    for (int arg = 4; arg < argc; ++arg) {
        const std::string spec = argv[arg];
        const auto colon = spec.find(':');
        h::columns original{load(dir + "/" + spec.substr(0, colon) + ".u64", want), {}};
        original.b = load(dir + "/" + spec.substr(colon + 1) + ".u64", original.a.size());
        const std::size_t n = original.a.size(), blocks = n / fi::block_rows;
        for (auto pattern : {h::edit_pattern::repeated_hot, h::edit_pattern::dispersed})
            for (auto mix : {h::read_mix::reduction, h::read_mix::selection, h::read_mix::point_heavy}) {
                auto batches = h::make_batches(original, pattern, h::edit_values::local_replacement);
                auto reads = h::make_reads(original, mix, h::thresholds::varying);
                if (!batches || !reads)
                    return 1;
                const auto trace = h::make_trace(std::move(*batches), std::move(*reads), h::chronology::interleaved);
                const auto expected = h::prepare_expected(original, trace);
                if (!expected)
                    return 1;
                const char *pname = pattern == h::edit_pattern::dispersed ? "dispersed" : "hot";
                const char *mname = mix == h::read_mix::reduction ? "reduction" : mix == h::read_mix::selection ? "selection" : "points";
                auto report = [&](const char *cand, int r, const totals &t, std::size_t retained, std::size_t old_reader) {
                    std::printf("{\"pair\":\"%s\",\"history\":\"%s\",\"mix\":\"%s\",\"candidate\":\"%s\",\"round\":%d,\"ok\":%s,"
                                "\"edit_ms\":%.3f,\"query_ms\":%.3f,\"fold_ms\":%.3f,\"retained_bytes\":%zu,\"old_reader_bytes\":%zu}\n",
                                spec.c_str(), pname, mname, cand, r, t.failed ? "false" : "true", t.edit_ns / 1e6, t.query_ns / 1e6,
                                t.fold_ns / 1e6, retained, old_reader);
                };
                for (int r = 0; r < rounds; ++r) {
                    {
                        const image_column ai(original.a), bi(original.b);
                        fi::corrected_over<image_column> a(ai, blocks), b(bi, blocks);
                        const totals t = drive(a, b, blocks, trace, *expected, [] {});
                        report("corrections", r, t, ai.bytes() + bi.bytes() + a.entries * 9 + b.entries * 9, 0);
                    }
                    {
                        fi::grouped_column ag{fi::build_grouped(original.a, 16)}, bg{fi::build_grouped(original.b, 16)};
                        const fi::grouped_column a0 = ag, b0 = bg;  // the original-version reader
                        auto ac = std::make_unique<fi::corrected_over<fi::grouped_column>>(ag, blocks);
                        auto bc = std::make_unique<fi::corrected_over<fi::grouped_column>>(bg, blocks);
                        // Fold: re-encode every dirty group from its current logical
                        // rows into a new group; corrections restart over the new view.
                        auto fold_one = [&](fi::grouped_column &col, std::unique_ptr<fi::corrected_over<fi::grouped_column>> &c) {
                            std::vector<u64> rows(16 * fi::block_rows);
                            fi::grouped_view next = col.view;
                            for (std::size_t g = 0; g < col.view.groups.size(); ++g) {
                                bool dirty = false;
                                for (std::size_t b = g * 16; b < std::min(blocks, g * 16 + 16); ++b)
                                    dirty |= c->is_dirty(b);
                                if (!dirty)
                                    continue;
                                const std::size_t first = g * 16 * fi::block_rows, len = col.view.groups[g]->f.blocks * fi::block_rows;
                                rows.resize(len);
                                for (std::size_t i = 0; i < len; ++i)
                                    rows[i] = c->point(first + i);
                                next = fi::fold_group(next, g, rows);
                            }
                            col.view = std::move(next);
                            c = std::make_unique<fi::corrected_over<fi::grouped_column>>(col, blocks);
                        };
                        struct pair_ref {
                            std::unique_ptr<fi::corrected_over<fi::grouped_column>> *a, *b;
                        };
                        // drive() takes candidates by reference; wrap the owning pointers.
                        struct handle {
                            std::unique_ptr<fi::corrected_over<fi::grouped_column>> *c;
                            u64 point(std::size_t r) const { return (*c)->point(r); }
                            u128 sum() const { return (*c)->sum(); }
                            u64 count_ge(u64 t) const { return (*c)->count_ge(t); }
                            u64 mask_ge(u64 t, u64 *m) const { return (*c)->mask_ge(t, m); }
                            u128 masked_sum(const u64 *m) const { return (*c)->masked_sum(m); }
                            void write(std::size_t r, u64 v) { (*c)->write(r, v); }
                        } ha{&ac}, hb{&bc};
                        const totals t = drive(ha, hb, blocks, trace, *expected, [&] { fold_one(ag, ac), fold_one(bg, bc); });
                        std::size_t old_alive = 0;  // groups only the original reader still holds
                        for (std::size_t g = 0; g < a0.view.groups.size(); ++g) {
                            if (a0.view.groups[g] != ag.view.groups[g])
                                old_alive += a0.view.groups[g]->bytes();
                            if (b0.view.groups[g] != bg.view.groups[g])
                                old_alive += b0.view.groups[g]->bytes();
                        }
                        report("groups16_fold", r, t, fi::bytes(ag.view) + fi::bytes(bg.view) + (ac->entries + bc->entries) * 9, old_alive);
                    }
                    {
                        native_groups a(original.a), b(original.b);
                        const totals t = drive(a, b, blocks, trace, *expected, [] {});
                        report("native_groups16", r, t, a.bytes() + b.bytes(), 0);
                    }
                }
            }
    }
}
