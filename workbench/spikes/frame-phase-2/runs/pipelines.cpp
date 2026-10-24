// Pipelines with and without RLE (Ashton, 2026-10-24): does the RLE stage
// earn its place, and how should Local and Bulk be weighed behind it?
//
// Every 256-row block of a column goes through one of two pipelines.
// Constant blocks exit early in both (same cost, reported apart).
//
//   no RLE   FOR (block minimum) -> bounds (cheapest width with outliers as
//            (u8 position, value) at the residual's byte width) -> SeriesPack
//            at that width under one preset: compact (Local), bulk_x86 or
//            bulk_arm. A 256-row block is whole tiles in every preset.
//   group RLE as RLE, but the RLE blocks of each 16-block group share one
//            sizes series and one values series (one long read per group).
//   RLE      run sizes (minus one) with the bounds stage and G16 checkpoints,
//            and run values with FOR, both packed by SeriesPack Local at
//            16-row placement (runs.README.md); taken only where its bytes
//            are smaller, else the block falls through to no RLE.
//
// Work over the whole column: SUM, COUNT(x >= median), and 65,536 random
// points. SeriesPack's own operations serve the packed blocks (typed sum for
// widths <= 48, read-then-sum above; filter in 64-row windows; get); run
// blocks decode sizes and values and weight them. Every answer is checked
// against the raw column. Warm.
//
// usage: frame_runs_pipelines COLS_DIR ROWS ROUNDS COLUMN...
#include <ikea/intervals.h>
#include <ikea/seriespack.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace sp = ikea::seriespack;
namespace in = ikea::intervals;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using u128 = unsigned __int128;

namespace {
constexpr unsigned block_rows = 256, max_runs = 128;

std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(f.tellg()) / 8) / block_rows * block_rows;
    std::vector<u64> v(rows);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
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
unsigned bits(u64 x) { return std::max(1u, static_cast<unsigned>(std::bit_width(x))); }
unsigned value_bytes(u64 x) { return (bits(x) + 7) / 8; }

// One SeriesPack array of 256-row blocks at width K under one preset, read
// through its typed reader.
struct packed_width {
    virtual ~packed_width() = default;
    virtual u128 sum(std::size_t block) const = 0;
    virtual unsigned count_ge(std::size_t block, u64 rhs) const = 0;
    virtual u64 get(std::size_t row) const = 0;
    virtual std::size_t bytes() const = 0;
};
template <unsigned K, sp::preset P> struct packed_width_at final : packed_width {
    using L = sp::preset_layout<K, P>;
    std::vector<ikea::byte> storage;
    std::optional<sp::const_view<L>> view;
    std::optional<decltype(sp::bind_reader(std::declval<const sp::const_view<L> &>()))> reader;
    explicit packed_width_at(const std::vector<u64> &values) : storage(values.size() / L::tile_rows * L::tile_bytes) {
        const sp::dense_output target{K, L::storage, storage, values.size()};
        std::vector<ikea::journal::entry> records(*sp::effect_capacity_initialize(target));
        ikea::journal effects{records};
        if (!sp::initialize(target, std::span<const u64>(values), effects))
            std::abort();
        auto v = sp::const_view<L>::attach(values.size(), {{{std::span<const ikea::byte>(storage), L::tile_bytes}, {}, {}}});
        if (!v)
            std::abort();
        view.emplace(*v);
        reader.emplace(sp::bind_reader(*view));
    }
    u128 sum(std::size_t block) const override {
        const std::size_t first = block * block_rows;
        if constexpr (K <= 48) {
            return reader->sum_unchecked(first, block_rows, ~u64{0}, [](std::size_t) { return u16{0xffff}; });
        } else {
            alignas(64) u64 x[block_rows];
            reader->read_unchecked(first, block_rows, x);
            u128 s = 0;
            for (u64 y : x)
                s += y;
            return s;
        }
    }
    unsigned count_ge(std::size_t block, u64 rhs) const override {
        const std::size_t first = block * block_rows;
        unsigned c = 0;
        for (unsigned w = 0; w < 4; ++w)
            c += std::popcount(reader->template filter_unchecked<ikea::comparison::greater_equal>(first + 64 * w, 64, rhs));
        return c;
    }
    u64 get(std::size_t row) const override { return reader->get_unchecked(row); }
    std::size_t bytes() const override { return storage.size(); }
};
template <sp::preset P> std::unique_ptr<packed_width> make_width(unsigned K, const std::vector<u64> &values) {
    std::unique_ptr<packed_width> out;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((K == I + 1 ? (out = std::make_unique<packed_width_at<I + 1, P>>(values), 0) : 0), ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    return out;
}
template <sp::preset P> std::size_t tile_bytes_for(unsigned K) {
    std::size_t out = 0;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((K == I + 1 ? (out = block_rows / sp::preset_layout<I + 1, P>::tile_rows * sp::preset_layout<I + 1, P>::tile_bytes, 0) : 0), ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    return out;
}

// Small Local series at 16-row placement (run sizes, run values), one array
// per width, read through erased readers.
struct small_series {
    std::array<std::vector<u64>, 65> staged;
    std::array<std::vector<ikea::byte>, 65> storage;
    std::array<std::optional<sp::erased_reader<u64>>, 65> readers;
    u32 append(unsigned K, const std::vector<u64> &f) {
        auto &s = staged[K];
        const u32 at = static_cast<u32>(s.size());
        s.insert(s.end(), f.begin(), f.end());
        s.resize((s.size() + 15) / 16 * 16, 0);
        return at;
    }
    std::size_t bytes() const {
        std::size_t n = 0;
        for (const auto &b : storage)
            n += b.size();
        return n;
    }
    void seal() {
        for (unsigned K = 1; K <= 64; ++K) {
            if (staged[K].empty())
                continue;
            storage[K].resize(staged[K].size() / 8 * K);
            const sp::dense_output target{K, sp::geometry::local, storage[K], staged[K].size()};
            std::vector<ikea::journal::entry> records(*sp::effect_capacity_initialize(target));
            ikea::journal effects{records};
            if (!sp::initialize(target, std::span<const u64>(staged[K]), effects))
                std::abort();
            auto r = sp::bind_reader<u64>(sp::dense_input{K, sp::geometry::local, storage[K], staged[K].size()});
            if (!r)
                std::abort();
            readers[K].emplace(*r);
        }
    }
};

enum class kind : u8 { constant, packed, runs };

template <sp::preset P> struct pipeline {
    bool rle, group_rle;
    // Group-continuous RLE: the RLE blocks of each 16-block group share one
    // sizes series and one values series (values relative to the group's
    // smallest block minimum), with u16 group checkpoints every 16 runs in
    // group coordinates (the j-th RLE block of a group owns [256j, 256j+256)).
    struct pending_run {
        std::size_t block;
        std::vector<unsigned> ends;
        std::vector<u64> values;  // absolute
    };
    std::vector<pending_run> pending;
    struct run_group {
        u64 base;
        u8 size_width, value_width;
        u32 size_at, value_at, runs, blocks, cp_at, cp_end, ex_at, ex_end;
    };
    std::vector<run_group> groups;
    std::vector<u32> block_group, block_first_run, block_j;  // per RLE block (indexed by slot)
    std::vector<std::pair<u16, u8>> group_size_exceptions;
    std::vector<kind> kinds;
    std::vector<u64> base;
    std::vector<u8> width;
    std::vector<u32> slot;  // packed: block index within its width array; runs: index into run tables
    std::array<std::unique_ptr<packed_width>, 65> packed;
    std::vector<std::pair<u8, u64>> exceptions;  // packed blocks: (position, residual)
    std::vector<u32> ex_first;
    // Run blocks.
    small_series sizes, values;
    struct run_desc {
        u8 n, size_width, value_width;
        u32 size_at, value_at, cp_at, ex_at, ex_end;
    };
    std::vector<run_desc> runs;
    std::vector<u16> checkpoints;
    std::vector<std::pair<u8, u8>> size_exceptions;
    std::size_t bytes = 0, constant_blocks = 0, run_blocks = 0, packed_blocks = 0;

    pipeline(const std::vector<u64> &v, bool with_rle, bool grouped = false) : rle(with_rle), group_rle(grouped) {
        const std::size_t blocks = v.size() / block_rows;
        std::array<std::vector<u64>, 65> staged;
        for (std::size_t b = 0; b < blocks; ++b) {
            const u64 *x = v.data() + b * block_rows;
            const auto [lo, hi] = std::minmax_element(x, x + block_rows);
            const u64 mn = *lo, span = *hi - *lo;
            if (span == 0) {
                kinds.push_back(kind::constant), base.push_back(mn), width.push_back(0), slot.push_back(0);
                ex_first.push_back(static_cast<u32>(exceptions.size()));
                bytes += 10, ++constant_blocks;  // base, width, kind
                continue;
            }
            // No-RLE candidate: cheapest width with outliers.
            const unsigned full = bits(span), vb = value_bytes(span);
            unsigned pk = full;
            std::size_t pbytes = tile_bytes_for<P>(full);
            for (unsigned K = 1; K < full; ++K) {
                std::size_t ex = 0;
                for (unsigned i = 0; i < block_rows; ++i)
                    ex += (x[i] - mn) >> K != 0;
                const std::size_t c = tile_bytes_for<P>(K) + (ex ? 1 + ex * (1 + vb) : 0);
                if (c < pbytes)
                    pbytes = c, pk = K;
            }
            // RLE candidate.
            std::vector<unsigned> ends;
            std::vector<u64> rv;
            for (unsigned i = 1; i <= block_rows; ++i)
                if (i == block_rows || x[i] != x[i - 1])
                    ends.push_back(i), rv.push_back(x[i - 1] - mn);
            std::size_t rbytes = ~std::size_t{0};
            unsigned sk = 0, vk = 0;
            std::vector<u64> sz;
            std::vector<std::pair<u8, u8>> sex;
            if (rle && ends.size() <= max_runs) {
                for (std::size_t i = 0; i < ends.size(); ++i)
                    sz.push_back(ends[i] - (i ? ends[i - 1] : 0) - 1);
                const unsigned sfull = bits(*std::max_element(sz.begin(), sz.end()));
                const std::size_t padded = (sz.size() + 15) / 16 * 16;
                sk = sfull;
                std::size_t sb = padded / 8 * sfull;
                for (unsigned K = 1; K < sfull; ++K) {
                    std::size_t ex = 0;
                    for (u64 s : sz)
                        ex += s >> K != 0;
                    const std::size_t c = padded / 8 * K + (ex ? 1 + 2 * ex : 0);
                    if (c < sb)
                        sb = c, sk = K;
                }
                for (std::size_t i = 0; i < sz.size(); ++i)
                    if (sz[i] >> sk)
                        sex.push_back({static_cast<u8>(i), static_cast<u8>(sz[i])}), sz[i] = 0;
                vk = bits(*std::max_element(rv.begin(), rv.end()));
                rbytes = sb + (sz.size() - 1) / 16 + padded / 8 * vk;
            }
            if (rbytes < pbytes && group_rle) {
                kinds.push_back(kind::runs), base.push_back(mn), width.push_back(0), slot.push_back(static_cast<u32>(pending.size()));
                ex_first.push_back(static_cast<u32>(exceptions.size()));
                std::vector<u64> abs;
                for (u64 r : rv)
                    abs.push_back(r + mn);
                pending.push_back({b, ends, abs});
                ++run_blocks;
            } else if (rbytes < pbytes) {
                kinds.push_back(kind::runs), base.push_back(mn), width.push_back(0), slot.push_back(static_cast<u32>(runs.size()));
                ex_first.push_back(static_cast<u32>(exceptions.size()));
                run_desc d{static_cast<u8>(ends.size()), static_cast<u8>(sk), static_cast<u8>(vk), sizes.append(sk, sz), values.append(vk, rv),
                           static_cast<u32>(checkpoints.size()), static_cast<u32>(size_exceptions.size()), 0};
                for (std::size_t i = 0; i < ends.size(); i += 16)
                    checkpoints.push_back(static_cast<u16>(i ? ends[i - 1] : 0));
                size_exceptions.insert(size_exceptions.end(), sex.begin(), sex.end());
                d.ex_end = static_cast<u32>(size_exceptions.size());
                runs.push_back(d);
                bytes += rbytes + 11, ++run_blocks;  // base, run count, two widths, kind
            } else {
                kinds.push_back(kind::packed), base.push_back(mn), width.push_back(static_cast<u8>(pk));
                slot.push_back(static_cast<u32>(staged[pk].size() / block_rows));
                ex_first.push_back(static_cast<u32>(exceptions.size()));
                for (unsigned i = 0; i < block_rows; ++i) {
                    const u64 r = x[i] - mn;
                    if (r >> pk)
                        exceptions.push_back({static_cast<u8>(i), r}), staged[pk].push_back(0);
                    else
                        staged[pk].push_back(r);
                }
                bytes += pbytes + 10, ++packed_blocks;
            }
        }
        ex_first.push_back(static_cast<u32>(exceptions.size()));
        for (unsigned K = 1; K <= 64; ++K)
            if (!staged[K].empty())
                packed[K] = make_width<P>(K, staged[K]);
        if (group_rle)
            build_groups();
        sizes.seal(), values.seal();
    }
    void build_groups() {
        block_group.resize(pending.size()), block_first_run.resize(pending.size()), block_j.resize(pending.size());
        for (std::size_t first = 0; first < pending.size();) {
            std::size_t last = first;
            const std::size_t g16 = pending[first].block / 16;
            while (last < pending.size() && pending[last].block / 16 == g16)
                ++last;
            run_group grp{};
            grp.base = ~u64{0};
            for (std::size_t i = first; i < last; ++i)
                for (u64 x : pending[i].values)
                    grp.base = std::min(grp.base, x);
            std::vector<u64> sz, val;
            grp.cp_at = static_cast<u32>(checkpoints.size());
            for (std::size_t i = first; i < last; ++i) {
                const auto &pr = pending[i];
                const unsigned j = static_cast<unsigned>(i - first);
                block_group[i] = static_cast<u32>(groups.size()), block_first_run[i] = static_cast<u32>(sz.size()), block_j[i] = j;
                for (std::size_t r = 0; r < pr.ends.size(); ++r) {
                    const unsigned start = (r ? pr.ends[r - 1] : 0);
                    if (sz.size() % 16 == 0)
                        checkpoints.push_back(static_cast<u16>(j * block_rows + start));
                    sz.push_back(pr.ends[r] - start - 1);
                    val.push_back(pr.values[r] - grp.base);
                }
            }
            grp.cp_end = static_cast<u32>(checkpoints.size());
            // Bounds over the group's sizes.
            const unsigned sfull = bits(*std::max_element(sz.begin(), sz.end()));
            const std::size_t padded = (sz.size() + 15) / 16 * 16;
            unsigned sk = sfull;
            std::size_t sb = padded / 8 * sfull;
            for (unsigned K = 1; K < sfull; ++K) {
                std::size_t ex = 0;
                for (u64 x : sz)
                    ex += x >> K != 0;
                const std::size_t c = padded / 8 * K + (ex ? 1 + 3 * ex : 0);
                if (c < sb)
                    sb = c, sk = K;
            }
            grp.ex_at = static_cast<u32>(group_size_exceptions.size());
            for (std::size_t i = 0; i < sz.size(); ++i)
                if (sz[i] >> sk)
                    group_size_exceptions.push_back({static_cast<u16>(i), static_cast<u8>(sz[i])}), sz[i] = 0;
            grp.ex_end = static_cast<u32>(group_size_exceptions.size());
            grp.size_width = static_cast<u8>(sk);
            grp.value_width = static_cast<u8>(bits(*std::max_element(val.begin(), val.end())));
            grp.size_at = sizes.append(sk, sz), grp.value_at = values.append(grp.value_width, val);
            grp.runs = static_cast<u32>(sz.size()), grp.blocks = static_cast<u32>(last - first);
            // Bytes: both series, checkpoints after the first, outliers, a u16
            // first-run index per block, and the group's base and widths.
            bytes += sb + padded / 8 * grp.value_width + 2 * (grp.cp_end - grp.cp_at - 1) + 2 * grp.blocks + 10 + grp.blocks;  // + kind per block
            groups.push_back(grp);
            first = last;
        }
    }
    // Group-continuous RLE contributions to SUM and COUNT.
    template <class F> void each_group(F &&f) const {
        static thread_local std::vector<u64> s, val;
        for (const auto &g : groups) {
            const unsigned padded = (g.runs + 15) / 16 * 16;
            s.resize(padded), val.resize(padded);
            sizes.readers[g.size_width]->read_unchecked(g.size_at, padded, s.data());
            values.readers[g.value_width]->read_unchecked(g.value_at, padded, val.data());
            for (u32 e = g.ex_at; e < g.ex_end; ++e)
                s[group_size_exceptions[e].first] = group_size_exceptions[e].second;
            f(g, s.data(), val.data());
        }
    }

    u128 sum() const {
        u128 total = 0;
        alignas(64) u64 s[max_runs + 16], val[max_runs + 16];
        for (std::size_t b = 0; b < kinds.size(); ++b) {
            switch (kinds[b]) {
            case kind::constant: total += static_cast<u128>(base[b]) * block_rows; break;
            case kind::packed:
                total += packed[width[b]]->sum(slot[b]) + static_cast<u128>(base[b]) * block_rows;
                for (u32 e = ex_first[b]; e < ex_first[b + 1]; ++e)
                    total += exceptions[e].second;
                break;
            case kind::runs: {
                if (group_rle)
                    break;
                const auto &d = runs[slot[b]];
                const unsigned padded = (d.n + 15u) / 16 * 16;
                sizes.readers[d.size_width]->read_unchecked(d.size_at, padded, s);
                values.readers[d.value_width]->read_unchecked(d.value_at, padded, val);
                for (u32 e = d.ex_at; e < d.ex_end; ++e)
                    s[size_exceptions[e].first] = size_exceptions[e].second;
                u128 t = 0;
                for (unsigned i = 0; i < d.n; ++i)
                    t += static_cast<u128>(s[i] + 1) * val[i];
                total += t + static_cast<u128>(base[b]) * block_rows;
                break;
            }
            }
        }
        if (group_rle)
            each_group([&](const run_group &g, const u64 *sz, const u64 *val) {
                u128 t = 0;
                for (unsigned i = 0; i < g.runs; ++i)
                    t += static_cast<u128>(sz[i] + 1) * val[i];
                total += t + static_cast<u128>(g.base) * g.blocks * block_rows;
            });
        return total;
    }
    u64 count_ge(u64 t) const {
        u64 c = 0;
        alignas(64) u64 s[max_runs + 16], val[max_runs + 16];
        for (std::size_t b = 0; b < kinds.size(); ++b) {
            if (group_rle && kinds[b] == kind::runs)
                continue;  // counted per group below
            if (t <= base[b]) {
                c += block_rows;
                continue;
            }
            const u64 rhs = t - base[b];
            switch (kinds[b]) {
            case kind::constant: break;
            case kind::packed: {
                if (bits(rhs) > width[b] && ex_first[b] == ex_first[b + 1])
                    break;  // above every packed residual and no outliers
                unsigned k = bits(rhs) > width[b] ? 0 : packed[width[b]]->count_ge(slot[b], rhs);
                for (u32 e = ex_first[b]; e < ex_first[b + 1]; ++e)
                    k += exceptions[e].second >= rhs;  // the slot held 0 < rhs
                c += k;
                break;
            }
            case kind::runs: {
                const auto &d = runs[slot[b]];
                const unsigned padded = (d.n + 15u) / 16 * 16;
                sizes.readers[d.size_width]->read_unchecked(d.size_at, padded, s);
                values.readers[d.value_width]->read_unchecked(d.value_at, padded, val);
                for (u32 e = d.ex_at; e < d.ex_end; ++e)
                    s[size_exceptions[e].first] = size_exceptions[e].second;
                for (unsigned i = 0; i < d.n; ++i)
                    c += val[i] >= rhs ? s[i] + 1 : 0;
                break;
            }
            }
        }
        if (group_rle)
            each_group([&](const run_group &g, const u64 *sz, const u64 *val) {
                if (t <= g.base) {
                    c += u64{g.blocks} * block_rows;
                    return;
                }
                const u64 rhs = t - g.base;
                for (unsigned i = 0; i < g.runs; ++i)
                    c += val[i] >= rhs ? sz[i] + 1 : 0;
            });
        return c;
    }
    u64 group_point(std::size_t b, unsigned r) const {
        const u32 i = slot[b];
        const auto &g = groups[block_group[i]];
        const unsigned R = block_j[i] * block_rows + r;
        const u16 *cp = checkpoints.data() + g.cp_at;
        const unsigned regions = g.cp_end - g.cp_at;
        const unsigned k = in::upper_bound(in::direct_source<u16>{cp, regions}, R) - 1;
        const unsigned first = k * 16, m = std::min(16u, g.runs - first);
        alignas(64) u64 w64[16];
        alignas(64) u8 w[16];
        sizes.readers[g.size_width]->read_unchecked(g.size_at + first, 16, w64);
        for (unsigned q = 0; q < 16; ++q)
            w[q] = q < m ? static_cast<u8>(w64[q] + 1) : 0;
        const auto *e0 = group_size_exceptions.data() + g.ex_at, *e1 = group_size_exceptions.data() + g.ex_end;
        for (auto *e = std::lower_bound(e0, e1, std::pair<u16, u8>{static_cast<u16>(first), 0}); e < e1 && e->first < first + m; ++e)
            w[e->first - first] = static_cast<u8>(e->second + 1);
        const u16 limit = k + 1 < regions ? cp[k + 1] : static_cast<u16>(g.blocks * block_rows);
        const unsigned rank = first + in::reverse(in::sparse_sizes<16, u8, u16>{w, cp + k, m, cp[k], limit}, R);
        return g.base + values.readers[g.value_width]->get_unchecked(g.value_at + rank);
    }
    u64 point(std::size_t row) const {
        const std::size_t b = row / block_rows;
        const unsigned r = static_cast<unsigned>(row % block_rows);
        switch (kinds[b]) {
        case kind::constant: return base[b];
        case kind::packed: {
            const auto *e0 = exceptions.data() + ex_first[b], *e1 = exceptions.data() + ex_first[b + 1];
            if (e0 != e1) {
                const auto *e = std::lower_bound(e0, e1, std::pair<u8, u64>{static_cast<u8>(r), 0});
                if (e < e1 && e->first == r)
                    return base[b] + e->second;
            }
            return base[b] + packed[width[b]]->get(std::size_t{slot[b]} * block_rows + r);
        }
        case kind::runs: {
            if (group_rle)
                return group_point(b, r);
            const auto &d = runs[slot[b]];
            const u16 *cp = checkpoints.data() + d.cp_at;
            const unsigned regions = (d.n + 15u) / 16;
            const unsigned g = in::upper_bound(in::direct_source<u16>{cp, regions}, r) - 1;
            const unsigned first = g * 16, m = std::min(16u, d.n - first);
            alignas(64) u64 w64[16];
            alignas(64) u8 w[16];
            sizes.readers[d.size_width]->read_unchecked(d.size_at + first, 16, w64);
            for (unsigned i = 0; i < 16; ++i)
                w[i] = i < m ? static_cast<u8>(w64[i] + 1) : 0;
            for (u32 e = d.ex_at; e < d.ex_end; ++e)
                if (size_exceptions[e].first - first < m)
                    w[size_exceptions[e].first - first] = static_cast<u8>(size_exceptions[e].second + 1);
            const u16 limit = g + 1 < regions ? cp[g + 1] : u16(256);
            const unsigned rank = first + in::reverse(in::sparse_sizes<16, u8, u16>{w, cp + g, m, cp[g], limit}, r);
            return base[b] + values.readers[d.value_width]->get_unchecked(d.value_at + rank);
        }
        }
        return 0;
    }
};

struct arm {
    std::string name;
    std::size_t bytes, constant_blocks, run_blocks, packed_blocks;
    std::function<u128()> sum;
    std::function<u64()> count;
    std::function<u64()> points;
};

template <sp::preset P>
void add(std::vector<arm> &arms, std::vector<std::shared_ptr<void>> &keep, const std::vector<u64> &v, bool rle, const char *name, u64 t,
         const std::vector<u32> &rows, bool grouped = false) {
    auto p = std::make_shared<pipeline<P>>(v, rle, grouped);
    keep.push_back(p);
    const auto *q = p.get();
    arms.push_back({name, q->bytes, q->constant_blocks, q->run_blocks, q->packed_blocks, [q] { return q->sum(); }, [q, t] { return q->count_ge(t); },
                    [q, &rows] {
                        u64 s = 0;
                        for (u32 r : rows)
                            s += q->point(r);
                        return s;
                    }});
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
        const std::size_t n = v.size(), blocks = n / block_rows;
        std::vector<u64> sorted = v;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const u64 t = sorted[n / 2];
        std::mt19937_64 rng(29);
        std::vector<u32> rows(65536);
        for (auto &r : rows)
            r = static_cast<u32>(rng() % n);
        u128 sum_oracle = 0;
        u64 count_oracle = 0, point_oracle = 0;
        for (u64 x : v)
            sum_oracle += x, count_oracle += x >= t;
        for (u32 r : rows)
            point_oracle += v[r];
        std::vector<arm> arms;
        std::vector<std::shared_ptr<void>> keep;
        add<sp::preset::compact>(arms, keep, v, false, "no_rle_compact", t, rows);
        add<sp::preset::bulk_x86>(arms, keep, v, false, "no_rle_bulk_x86", t, rows);
        add<sp::preset::bulk_arm>(arms, keep, v, false, "no_rle_bulk_arm", t, rows);
        add<sp::preset::compact>(arms, keep, v, true, "rle_then_compact", t, rows);
        add<sp::preset::bulk_x86>(arms, keep, v, true, "rle_then_bulk_x86", t, rows);
        add<sp::preset::bulk_arm>(arms, keep, v, true, "rle_then_bulk_arm", t, rows);
        add<sp::preset::compact>(arms, keep, v, true, "group_rle_then_compact", t, rows, true);
        add<sp::preset::bulk_x86>(arms, keep, v, true, "group_rle_then_bulk_x86", t, rows, true);
        add<sp::preset::bulk_arm>(arms, keep, v, true, "group_rle_then_bulk_arm", t, rows, true);
        for (const auto &x : arms)
            if (x.sum() != sum_oracle || x.count() != count_oracle || x.points() != point_oracle) {
                std::fprintf(stderr, "%s %s: wrong answer (sum %d count %d points %d)\n", name.c_str(), x.name.c_str(), x.sum() == sum_oracle,
                             x.count() == count_oracle, x.points() == point_oracle);
                return 1;
            }
        std::vector<std::vector<double>> ts(arms.size()), tc(arms.size()), tp(arms.size());
        for (int r = 0; r < rounds; ++r)
            for (std::size_t j = 0; j < arms.size(); ++j) {
                const std::size_t i = (j + static_cast<std::size_t>(r)) % arms.size();
                bool ok = true;
                arms[i].sum();
                ts[i].push_back(time_ns([&] { ok &= arms[i].sum() == sum_oracle; }));
                tc[i].push_back(time_ns([&] { ok &= arms[i].count() == count_oracle; }));
                tp[i].push_back(time_ns([&] { ok &= arms[i].points() == point_oracle; }));
                if (!ok)
                    return 1;
            }
        for (std::size_t i = 0; i < arms.size(); ++i)
            std::printf("{\"column\":\"%s\",\"arm\":\"%s\",\"blocks\":%zu,\"constant_blocks\":%zu,\"run_blocks\":%zu,\"packed_blocks\":%zu,"
                        "\"bits_per_value\":%.3f,\"sum_ns_per_block\":%.2f,\"count_ns_per_block\":%.2f,\"ns_per_point\":%.2f}\n",
                        name.c_str(), arms[i].name.c_str(), blocks, arms[i].constant_blocks, arms[i].run_blocks, arms[i].packed_blocks,
                        8.0 * arms[i].bytes / n, median(ts[i]) / blocks, median(tc[i]) / blocks, median(tp[i]) / rows.size());
    }
}
