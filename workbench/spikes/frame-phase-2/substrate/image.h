// A shared Frame image: the substrate's descriptor columns over front C's
// bodies. Descriptors stay in original block order; body placement is a
// separate choice (original address order, or grouped by kind and width as
// front C's harness stores them). Every operation calls front C's header
// leaves directly, one hoisted loop per address-order segment of equal kind
// and width.
//
// Kinds (front C's "bytes" policy): constant; runs when their body is
// smaller than packed (values in a 1/2/4/8-byte lane); plane256 at K <= 32;
// otherwise a u64 lane. Results are exact stored-domain values.
//
// Read envelope (SEAMS.md): the leaves read past a body by their tabulated
// halo (bodies/README.md, at most 63 bytes for plane loads or 15 lanes past
// the last run value). The image is immutable once built and admits one
// whole-image read window: interior bodies are followed by the next body,
// and the suffix covers the largest halo of any body present.
#pragma once

#include "packed.h"
#include "runs.h"

#include <ikea/intervals.h>
#include <ikea/intervals/author/search.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace frame_image {
using bodies::block_rows;
using bodies::u128;
using bodies::u16;
using bodies::u32;
using bodies::u64;
using bodies::u8;

enum kind : u8 { constant, runs, plane, wide };
enum class placement : u8 { address, by_kind };
// Body choice: the smaller of runs and packed, or packed only (no runs).
enum class policy : u8 { bytes, packed_only };

struct image {
    std::size_t blocks = 0;
    std::vector<u8> kind_of, width;  // width: K for plane and runs; 64 for wide
    std::vector<u64> base;
    std::vector<u32> offset;
    std::vector<u16> n;  // runs per runs block
    std::vector<u8> bytes;
    std::size_t body_bytes = 0;  // excluding the readable suffix
    std::size_t image_bytes() const { return body_bytes + 16 * blocks; }
};

inline unsigned width_of(u64 span) { return span ? 64 - static_cast<unsigned>(std::countl_zero(span)) : 0; }

inline image build(std::span<const u64> v, placement where = placement::address, policy choose = policy::bytes) {
    image f;
    f.blocks = v.size() / block_rows;
    f.kind_of.resize(f.blocks), f.width.resize(f.blocks), f.base.resize(f.blocks);
    f.offset.resize(f.blocks), f.n.resize(f.blocks);
    std::vector<std::vector<u8>> encoded(f.blocks);
    std::vector<u64> r(block_rows);
    for (std::size_t b = 0; b < f.blocks; ++b) {
        const u64 *x = v.data() + b * block_rows;
        const auto [lo, hi] = std::minmax_element(x, x + block_rows);
        const unsigned k = width_of(*hi - *lo);
        for (unsigned i = 0; i < block_rows; ++i)
            r[i] = x[i] - *lo;
        const unsigned runs_n = bodies::count_runs(r.data());
        f.base[b] = *lo;
        auto &out = encoded[b];
        if (runs_n == 1) {
            f.kind_of[b] = constant;
            continue;
        }
        const std::size_t packed_bytes = k > 32 ? 8 * block_rows : 32 * k, run_bytes = bodies::runs_bytes(runs_n, k);
        if (choose == policy::bytes && run_bytes < packed_bytes) {
            f.kind_of[b] = runs, f.width[b] = static_cast<u8>(k), f.n[b] = static_cast<u16>(runs_n);
            out.resize(run_bytes);
            bodies::encode_runs(r.data(), k, out.data());
        } else if (k <= 32) {
            f.kind_of[b] = plane, f.width[b] = static_cast<u8>(k);
            out.resize(bodies::block_bytes(bodies::layout::plane256, k));
            bodies::encode(bodies::layout::plane256, k, r.data(), out.data());
        } else {
            f.kind_of[b] = wide, f.width[b] = 64;
            out.resize(8 * block_rows);
            std::memcpy(out.data(), r.data(), out.size());
        }
    }
    std::vector<std::size_t> order(f.blocks);
    for (std::size_t b = 0; b < f.blocks; ++b)
        order[b] = b;
    if (where == placement::by_kind)
        std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) {
            return std::pair(f.kind_of[x], f.width[x]) < std::pair(f.kind_of[y], f.width[y]);
        });
    for (std::size_t b : order) {
        f.offset[b] = static_cast<u32>(f.bytes.size());
        f.bytes.insert(f.bytes.end(), encoded[b].begin(), encoded[b].end());
    }
    f.body_bytes = f.bytes.size();
    unsigned widest_run_lane = 0;
    for (std::size_t b = 0; b < f.blocks; ++b)
        if (f.kind_of[b] == runs)
            widest_run_lane = std::max(widest_run_lane, bodies::run_lane(f.width[b]));
    f.bytes.resize(f.bytes.size() + std::max<std::size_t>(64, 15 * widest_run_lane));
    return f;
}

// Model rewrites that cannot stay in place: each listed block's body is
// copied to the end of the image and its descriptor repointed; the old body
// becomes dead bytes. Logical contents are unchanged, so placement decays
// while answers stay exact. The readable suffix is kept after the new end.
inline void relocate(image &f, std::span<const u32> ids) {
    const std::size_t suffix = f.bytes.size() - f.body_bytes;
    f.bytes.resize(f.body_bytes);
    for (u32 b : ids) {
        std::size_t len = 0;
        switch (f.kind_of[b]) {
        case constant: continue;
        case runs: len = bodies::runs_bytes(f.n[b], f.width[b]); break;
        case plane: len = 32 * std::size_t{f.width[b]}; break;
        default: len = 8 * block_rows;
        }
        const std::size_t at = f.bytes.size();
        f.bytes.resize(at + len);
        std::memcpy(f.bytes.data() + at, f.bytes.data() + f.offset[b], len);
        f.offset[b] = static_cast<u32>(at);
    }
    f.body_bytes = f.bytes.size();
    f.bytes.resize(f.bytes.size() + suffix);
}

// Compaction: copy every live body into a fresh buffer in the chosen
// placement order, repoint descriptors and drop dead bytes. Restores the
// order relocation decays. Descriptors and logical contents are unchanged.
inline void compact(image &f, placement where) {
    std::vector<std::size_t> order(f.blocks);
    for (std::size_t b = 0; b < f.blocks; ++b)
        order[b] = b;
    if (where == placement::by_kind)
        std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) {
            return std::pair(f.kind_of[x], f.width[x]) < std::pair(f.kind_of[y], f.width[y]);
        });
    const std::size_t suffix = f.bytes.size() - f.body_bytes;
    std::vector<u8> out;
    out.reserve(f.body_bytes + suffix);
    for (std::size_t b : order) {
        std::size_t len = 0;
        switch (f.kind_of[b]) {
        case constant: f.offset[b] = static_cast<u32>(out.size()); continue;
        case runs: len = bodies::runs_bytes(f.n[b], f.width[b]); break;
        case plane: len = 32 * std::size_t{f.width[b]}; break;
        default: len = 8 * block_rows;
        }
        const std::size_t at = out.size();
        out.insert(out.end(), f.bytes.begin() + f.offset[b], f.bytes.begin() + f.offset[b] + len);
        f.offset[b] = static_cast<u32>(at);
    }
    f.body_bytes = out.size();
    out.resize(out.size() + suffix);
    f.bytes = std::move(out);
}

// Exact sum of a u64 lane body by 32-bit halves, optionally over selected rows.
inline u128 wide_sum(const u8 *p, const u64 *sel = nullptr) {
    u64 lo = 0, hi = 0;
    for (unsigned i = 0; i < block_rows; ++i) {
        u64 x;
        std::memcpy(&x, p + 8 * i, 8);
        if (sel)
            x &= -(sel[i / 64] >> (i % 64) & 1);
        lo += x & 0xffffffffu, hi += x >> 32;
    }
    return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
}

template <class F> void with_plane(unsigned k, F &&f) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ((k == I + 1 ? (f(std::integral_constant<unsigned, I + 1>{}), 0) : 0), ...);
    }(std::make_index_sequence<32>{});
}
template <class F> void with_runs(unsigned k, F &&f) {
    if (k <= 8)
        f(bodies::runs<u8>{});
    else if (k <= 16)
        f(bodies::runs<u16>{});
    else if (k <= 32)
        f(bodies::runs<u32>{});
    else
        f(bodies::runs<u64>{});
}
template <unsigned K> using P = bodies::packed<bodies::layout::plane256, K>;

// Maximal address-order segments of equal (kind, width) among blocks [first, limit).
template <class Seg> void segments(const image &f, std::size_t first, std::size_t limit, Seg &&seg) {
    for (std::size_t b = first; b < limit;) {
        std::size_t e = b + 1;
        while (e < limit && f.kind_of[e] == f.kind_of[b] && f.width[e] == f.width[b])
            ++e;
        seg(f.kind_of[b], f.width[b], b, e);
        b = e;
    }
}

// Exact SUM of one block, any kind (for unordered visits).
inline u128 block_sum(const image &f, std::size_t b) {
    const u8 *p = f.bytes.data() + f.offset[b];
    u128 s = static_cast<u128>(f.base[b]) * block_rows;
    switch (f.kind_of[b]) {
    case constant: break;
    case runs: with_runs(f.width[b], [&](auto r) { s += decltype(r)::sum(p, f.n[b]); }); break;
    case plane: with_plane(f.width[b], [&](auto K) { s += P<K>::sum(p); }); break;
    default: s += wide_sum(p);
    }
    return s;
}

inline u128 sum(const image &f) {
    u128 total = 0;
    segments(f, 0, f.blocks, [&](u8 k, unsigned w, std::size_t first, std::size_t limit) {
        u128 bases = 0, s = 0;
        for (std::size_t b = first; b < limit; ++b)
            bases += f.base[b];
        const u8 *img = f.bytes.data();
        switch (k) {
        case constant: break;
        case runs:
            with_runs(w, [&](auto r) {
                for (std::size_t b = first; b < limit; ++b)
                    s += decltype(r)::sum(img + f.offset[b], f.n[b]);
            });
            break;
        case plane:
            with_plane(w, [&](auto K) {
                for (std::size_t b = first; b < limit; ++b)
                    s += P<K>::sum(img + f.offset[b]);
            });
            break;
        default:
            for (std::size_t b = first; b < limit; ++b)
                s += wide_sum(img + f.offset[b]);
        }
        total += bases * block_rows + s;
    });
    return total;
}

// The same SUM visiting blocks by a whole-image (kind, width) index: one
// hoisted loop per kind and width over every such block, wherever it sits.
struct kind_index {
    std::vector<u32> ids;                                   // grouped block ids
    std::vector<std::pair<std::size_t, std::size_t>> runs;  // [first, limit) into ids
};
inline kind_index index_by_kind(const image &f) {
    kind_index x;
    x.ids.resize(f.blocks);
    for (std::size_t b = 0; b < f.blocks; ++b)
        x.ids[b] = static_cast<u32>(b);
    std::stable_sort(x.ids.begin(), x.ids.end(), [&](u32 a, u32 b) {
        return std::pair(f.kind_of[a], f.width[a]) < std::pair(f.kind_of[b], f.width[b]);
    });
    for (std::size_t i = 0; i < x.ids.size();) {
        std::size_t e = i + 1;
        while (e < x.ids.size() && f.kind_of[x.ids[e]] == f.kind_of[x.ids[i]] && f.width[x.ids[e]] == f.width[x.ids[i]])
            ++e;
        x.runs.push_back({i, e});
        i = e;
    }
    return x;
}
inline u128 sum_indexed(const image &f, const kind_index &x) {
    u128 total = 0;
    const u8 *img = f.bytes.data();
    for (const auto [first, limit] : x.runs) {
        const u32 b0 = x.ids[first];
        const unsigned w = f.width[b0];
        u128 bases = 0, s = 0;
        for (std::size_t i = first; i < limit; ++i)
            bases += f.base[x.ids[i]];
        switch (f.kind_of[b0]) {
        case constant: break;
        case runs:
            with_runs(w, [&](auto r) {
                for (std::size_t i = first; i < limit; ++i)
                    s += decltype(r)::sum(img + f.offset[x.ids[i]], f.n[x.ids[i]]);
            });
            break;
        case plane:
            with_plane(w, [&](auto K) {
                for (std::size_t i = first; i < limit; ++i)
                    s += P<K>::sum(img + f.offset[x.ids[i]]);
            });
            break;
        default:
            for (std::size_t i = first; i < limit; ++i)
                s += wide_sum(img + f.offset[x.ids[i]]);
        }
        total += bases * block_rows + s;
    }
    return total;
}

// Row masks of x >= t for blocks [first, limit) (four words per block);
// returns the selected count.
inline u64 mask_ge(const image &f, u64 t, std::size_t first, std::size_t limit, u64 *out) {
    u64 total = 0;
    segments(f, first, limit, [&](u8 k, unsigned w, std::size_t s0, std::size_t s1) {
        const u8 *img = f.bytes.data();
        const u64 top = w >= 64 ? ~u64{0} : (u64{1} << w) - 1;
        for (std::size_t b = s0; b < s1; ++b) {
            u64 *m = out + 4 * b;
            const u64 base = f.base[b];
            if (t <= base) {
                m[0] = m[1] = m[2] = m[3] = ~u64{0};
                total += block_rows;
                continue;
            }
            const u64 c = t - base;
            if (k == constant || c > top) {
                m[0] = m[1] = m[2] = m[3] = 0;
                continue;
            }
            const u8 *p = img + f.offset[b];
            if (k == runs)
                with_runs(w, [&](auto r) { decltype(r)::mask_ge(p, f.n[b], c, m); });
            else if (k == plane)
                with_plane(w, [&](auto K) { P<K>::mask_ge(p, c, m); });
            else
                bodies::lanes<u64>::mask_ge(p, c, m);
            total += std::popcount(m[0]) + std::popcount(m[1]) + std::popcount(m[2]) + std::popcount(m[3]);
        }
    });
    return total;
}

// COUNT(x >= t) through the count leaves (no mask stores), with bounds
// deciding whole blocks.
inline u64 count_ge(const image &f, u64 t, std::size_t first, std::size_t limit) {
    u64 total = 0;
    segments(f, first, limit, [&](u8 k, unsigned w, std::size_t s0, std::size_t s1) {
        const u8 *img = f.bytes.data();
        const u64 top = w >= 64 ? ~u64{0} : (u64{1} << w) - 1;
        auto bounds = [&](std::size_t b, u64 &c) {
            const u64 base = f.base[b];
            if (t <= base) {
                total += block_rows;
                return false;
            }
            c = t - base;
            return k != constant && c <= top;
        };
        u64 c = 0;
        if (k == runs)
            with_runs(w, [&](auto r) {
                for (std::size_t b = s0; b < s1; ++b)
                    if (bounds(b, c))
                        total += decltype(r)::count_ge(img + f.offset[b], f.n[b], c);
            });
        else if (k == plane)
            with_plane(w, [&](auto K) {
                for (std::size_t b = s0; b < s1; ++b)
                    if (bounds(b, c))
                        total += P<K>::count_ge(img + f.offset[b], c);
            });
        else
            for (std::size_t b = s0; b < s1; ++b)
                if (bounds(b, c))
                    total += k == wide ? bodies::lanes<u64>::template count<false>(img + f.offset[b], c) : 0;
    });
    return total;
}

// Exact SUM over selected rows of one block.
inline u128 block_masked_sum(const image &f, std::size_t b, const u64 *sel) {
    const unsigned cnt = std::popcount(sel[0]) + std::popcount(sel[1]) + std::popcount(sel[2]) + std::popcount(sel[3]);
    u128 s = static_cast<u128>(f.base[b]) * cnt;
    const u8 *p = f.bytes.data() + f.offset[b];
    switch (f.kind_of[b]) {
    case constant: break;
    case runs: with_runs(f.width[b], [&](auto r) { s += decltype(r)::masked_sum(p, f.n[b], sel); }); break;
    case plane: with_plane(f.width[b], [&](auto K) { s += P<K>::masked_sum(p, sel); }); break;
    default: s += wide_sum(p, sel);
    }
    return s;
}

template <bool UseIntervals> u64 point_with(const image &f, std::size_t row);

// SUM over the rows selected by per-block masks (four words per block),
// hoisted per address segment of B; empty masks skip the block before its
// body is addressed, full masks take the block SUM leaf, and blocks with at
// most `sparse` selected rows of a packed or wide body read those rows as
// points (the fourth route the geometry probe asked for: a masked leaf costs
// about a full block however few rows it keeps). Run blocks keep the masked
// leaf: a run point costs more than the whole masked run sum (README,
// selection geometry).
inline u128 masked_sum(const image &f, const u64 *masks, unsigned sparse = 0) {
    u128 total = 0;
    const u8 *img = f.bytes.data();
    segments(f, 0, f.blocks, [&](u8 k, unsigned w, std::size_t s0, std::size_t s1) {
        auto each = [&](auto &&full, auto &&part) {
            for (std::size_t b = s0; b < s1; ++b) {
                const u64 *m = masks + 4 * b;
                if (!(m[0] | m[1] | m[2] | m[3]))
                    continue;
                const unsigned cnt = std::popcount(m[0]) + std::popcount(m[1]) + std::popcount(m[2]) + std::popcount(m[3]);
                total += static_cast<u128>(f.base[b]) * cnt;
                const u8 *p = img + f.offset[b];
                if (cnt <= sparse && (k == plane || k == wide)) {
                    u128 t = 0;
                    for (unsigned q = 0; q < 4; ++q)
                        for (u64 x = m[q]; x; x &= x - 1)
                            t += point_with<false>(f, b * block_rows + 64 * q + static_cast<unsigned>(std::countr_zero(x))) - f.base[b];
                    total += t;
                    continue;
                }
                total += cnt == block_rows ? full(p, b) : part(p, b, m);
            }
        };
        switch (k) {
        case constant: each([](const u8 *, std::size_t) { return u128{0}; }, [](const u8 *, std::size_t, const u64 *) { return u128{0}; }); break;
        case runs:
            with_runs(w, [&](auto r) {
                using R = decltype(r);
                each([&](const u8 *p, std::size_t b) { return u128{R::sum(p, f.n[b])}; },
                     [&](const u8 *p, std::size_t b, const u64 *m) { return u128{R::masked_sum(p, f.n[b], m)}; });
            });
            break;
        case plane:
            with_plane(w, [&](auto K) {
                each([K](const u8 *p, std::size_t) { return u128{P<K>::sum(p)}; },
                     [K](const u8 *p, std::size_t, const u64 *m) { return u128{P<K>::masked_sum(p, m)}; });
            });
            break;
        default: each([](const u8 *p, std::size_t) { return wide_sum(p); }, [](const u8 *p, std::size_t, const u64 *m) { return wide_sum(p, m); });
        }
    });
    return total;
}

// One point read. UseIntervals finds a run block's run through
// ikea::intervals' native search over the u8 (end - 1) plane with a 32-byte
// readable window (covered by the image's whole-image read grant; SEAMS.md);
// otherwise front C's in-leaf count. One inlined body for both, so the
// comparison measures the search rather than call shape.
// The run holding row i of a run block, through ikea::intervals. Out of line
// so the point dispatcher stays small for the other kinds (the Intervals
// thread traced the Graviton 4 point loss to dispatcher code shape).
[[gnu::noinline]] inline unsigned run_rank(const u8 *ends, unsigned n, unsigned i) {
    namespace iv = ikea::intervals;
    return iv::lower_bound(iv::author::with_readable_window<32>(iv::direct_source<std::uint8_t>{ends, n}), i);
}

template <bool UseIntervals> [[gnu::always_inline]] inline u64 point_with(const image &f, std::size_t row) {
    const std::size_t b = row / block_rows;
    const unsigned i = static_cast<unsigned>(row % block_rows);
    const u8 *p = f.bytes.data() + f.offset[b];
    u64 v = 0;
    switch (f.kind_of[b]) {
    case constant: break;
    case runs:
        if constexpr (UseIntervals) {
            const unsigned n = f.n[b];
            std::memcpy(&v, p + n + std::size_t{run_rank(p, n, i)} * bodies::run_lane(f.width[b]), bodies::run_lane(f.width[b]));
        } else {
            with_runs(f.width[b], [&](auto rr) { v = decltype(rr)::point(p, f.n[b], i); });
        }
        break;
    case plane: with_plane(f.width[b], [&](auto K) { v = P<K>::point(p, i); }); break;
    default: std::memcpy(&v, p + 8 * i, 8);
    }
    return f.base[b] + v;
}
inline u64 point(const image &f, std::size_t row) { return point_with<false>(f, row); }
inline u64 point_intervals(const image &f, std::size_t row) { return point_with<true>(f, row); }

// The point path the substrate uses by default (SEAMS.md, run lookup):
// ikea::intervals on every ISA. With the search out of line it wins on Zen 5
// and Graviton 4 alike (image-v6); the earlier Graviton loss was dispatcher
// code shape.
inline u64 point_default(const image &f, std::size_t row) { return point_with<true>(f, row); }

// Body extent of block b, for staging.
inline std::pair<const u8 *, std::size_t> body(const image &f, std::size_t b) {
    std::size_t len = 0;
    switch (f.kind_of[b]) {
    case constant: break;
    case runs: len = bodies::runs_bytes(f.n[b], f.width[b]); break;
    case plane: len = 32 * std::size_t{f.width[b]}; break;
    default: len = 8 * block_rows;
    }
    return {f.bytes.data() + f.offset[b], len};
}
}  // namespace frame_image
