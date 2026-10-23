// Fused multi-aggregate owner programs: several aggregates over one block in
// one body pass. A fused_request names the wanted aggregates (exact u128 SUM,
// COUNT(x >= t) for up to four thresholds, COUNT(x == v), MIN, MAX); absent
// ones cost no pass and no dictionary preparation. Included at the end of
// material.h, after selected.h, whose dispatch and laws it reuses:
//   - an empty S returns before any body or dictionary access, a full S takes
//     the full-256 program, and only mixed blocks take the row-selected one;
//   - a SUM-only request is the existing SUM, unchanged;
//   - one plane decode (into the narrowest lane, as narrow SUM) feeds every
//     reduction of packed and patched inliers; delta_patched, arrival and a
//     non-monotone delta reconstruct once and reduce the values;
//   - closed forms stay closed: constant and runs per value or per run with
//     rank popcounts; a non-decreasing delta block (rbase >= 0, with
//     checkpoints; the trusted-image premise of monotone_delta_first_ge) keeps
//     its weighted SUM and answers COUNT(>= t) and COUNT(== v) by the
//     checkpoint search (rows [first_ge(v), first_ge(v + 1)) hold v), and
//     MIN/MAX by the first and last selected rows' point reads;
//   - a coded block decodes its codes once. With SUM, MIN or MAX it gathers
//     each row's entry once and every aggregate reduces those values;
//     otherwise sorted COUNTs are the stored-domain program at translated
//     code thresholds, and frequency/arrival COUNTs read truth bits off the
//     one code buffer. Dictionary material comes from the program's holders
//     (code_sum, code_count, code_count_eq) and so from a dictionary_cache
//     when one is given.
#pragma once

namespace frame_encoding {

constexpr unsigned fused_thresholds = 4;

struct fused_request {
    bool sum = false;
    unsigned ge_count = 0;  // COUNT(x >= ge[j]) for j < ge_count <= fused_thresholds
    std::array<u64, fused_thresholds> ge{};
    bool eq = false;  // COUNT(x == eq_value)
    u64 eq_value = 0;
    bool min = false, max = false;
    bool sum_only() const { return sum && !ge_count && !eq && !min && !max; }
};

// Results over the selected rows. Unrequested fields keep these defaults;
// min and max are meaningful only when rows > 0. rows is always |S|.
struct fused_result {
    u128 sum = 0;
    std::array<u64, fused_thresholds> ge{};
    u64 eq = 0;
    u64 rows = 0;
    u64 min = UINT64_MAX, max = 0;
    friend bool operator==(const fused_result &, const fused_result &) = default;
};

namespace fused_detail {
// Per comparison, for values base + r with r in [0, 2^width): decided for
// every row (none, all) or a residual compare.
enum class test : std::uint8_t { none, all, compare };
struct plan {
    std::array<test, fused_thresholds> ge{};
    std::array<u64, fused_thresholds> ge_rhs{};
    test eq = test::none;
    u64 eq_rhs = 0;
    bool compares = false;
    // Branch-free per value: COUNT(>=) j counts r when (r >= ge_rhs[j]) & ge_on[j]
    // (all: rhs 0), COUNT(==) when (r == eq_rhs) & eq_on.
    std::array<u64, fused_thresholds> ge_on{};
    u64 eq_on = 0;
};
inline plan plan_for(const fused_request &q, u64 base, unsigned width) {
    plan p;
    auto above = [&](u64 rhs) { return width < 64 && (rhs >> width) != 0; };
    for (unsigned j = 0; j < q.ge_count; ++j) {
        const u64 t = q.ge[j];
        if (t <= base)
            p.ge[j] = test::all, p.ge_on[j] = 1;
        else if (above(t - base))
            p.ge[j] = test::none;
        else
            p.ge[j] = test::compare, p.ge_rhs[j] = t - base, p.ge_on[j] = 1, p.compares = true;
    }
    if (q.eq && q.eq_value >= base && !above(q.eq_value - base))
        p.eq = test::compare, p.eq_rhs = q.eq_value - base, p.eq_on = 1, p.compares = true;
    return p;
}

// Residual-domain totals of one group of rows sharing a base.
struct totals {
    u128 sum = 0;
    std::array<u64, fused_thresholds> ge{};
    u64 eq = 0, rows = 0;
    u64 min = UINT64_MAX, max = 0;
};
inline void finish(fused_result &r, const fused_request &q, u64 base, const totals &t) {
    if (q.sum)
        r.sum += static_cast<u128>(base) * t.rows + t.sum;
    for (unsigned j = 0; j < q.ge_count; ++j)
        r.ge[j] += t.ge[j];
    r.eq += t.eq;
    if (t.rows) {
        if (q.min)
            r.min = std::min(r.min, base + t.min);
        if (q.max)
            r.max = std::max(r.max, base + t.max);
    }
}
// No value is needed: only the decided comparisons and the row count.
inline bool rows_only(const fused_request &q, const plan &p) { return !q.sum && !q.min && !q.max && !p.compares; }
inline void decided(totals &t, const fused_request &q, const plan &p, u64 n) {
    t.rows += n;
    for (unsigned j = 0; j < q.ge_count; ++j)
        t.ge[j] += p.ge[j] == test::all ? n : 0;
}

// One residual r of weight w > 0 (constant, a run, an exception), branch
// free: a run's comparisons are data-dependent.
inline void add_value(totals &t, const fused_request &q, const plan &p, u64 r, u64 w) {
    t.rows += w;
    if (q.sum)
        t.sum += static_cast<u128>(r) * w;
    for (unsigned j = 0; j < q.ge_count; ++j)
        t.ge[j] += w * (static_cast<u64>(r >= p.ge_rhs[j]) & p.ge_on[j]);
    t.eq += w * (static_cast<u64>(r == p.eq_rhs) & p.eq_on);
    t.min = std::min(t.min, r);
    t.max = std::max(t.max, r);
}
// Remove one row's contribution (the dense masked form's complement rows);
// min and max are recomputed by the caller.
inline void remove_value(totals &t, const fused_request &q, const plan &p, u64 r) {
    t.rows -= 1;
    if (q.sum)
        t.sum -= r;
    for (unsigned j = 0; j < q.ge_count; ++j)
        t.ge[j] -= static_cast<u64>(r >= p.ge_rhs[j]) & p.ge_on[j];
    t.eq -= static_cast<u64>(r == p.eq_rhs) & p.eq_on;
}

// Lane accumulators that hold n <= 256 rows without overflow, so the loops
// keep the decoded lane's vector width: counts in u16 for 8- and 16-bit lanes;
// sums in u16 (K <= 8), u32 (K <= 24), u64 (K <= 56), else 32-bit halves.
template <class T> using count_lane = std::conditional_t<sizeof(T) <= 2, std::uint16_t, T>;
template <class A, class T> A lane_sum(const T *r, unsigned n) {
    A s = 0;
    for (unsigned i = 0; i < n; ++i)
        s = static_cast<A>(s + r[i]);
    return s;
}
template <class T> u128 lane_total(const T *r, unsigned n, unsigned width) {
    if constexpr (sizeof(T) == 1)
        return lane_sum<std::uint16_t>(r, n);
    else if constexpr (sizeof(T) == 2)
        return lane_sum<std::uint32_t>(r, n);
    else if constexpr (sizeof(T) == 4)
        return width <= 24 ? lane_sum<std::uint32_t>(r, n) : lane_sum<u64>(r, n);
    else
        return width <= 56 ? lane_sum<u64>(r, n) : split_sum(r, n);
}
template <class T, class F> u64 lane_count(const T *r, unsigned n, F &&holds) {
    count_lane<T> c = 0;
    for (unsigned i = 0; i < n; ++i)
        c = static_cast<count_lane<T>>(c + holds(r[i]));
    return c;
}

// Every requested reduction over n residuals in lane T, each a tight loop over
// the one decoded buffer (L1-resident).
template <class T>
void rows_into(totals &t, const T *r, unsigned n, const fused_request &q, const plan &p, unsigned width) {
    t.rows += n;
    if (q.sum)
        t.sum += lane_total(r, n, width);
    for (unsigned j = 0; j < q.ge_count; ++j) {
        if (p.ge[j] == test::all) {
            t.ge[j] += n;
        } else if (p.ge[j] == test::compare) {
            const T rhs = static_cast<T>(p.ge_rhs[j]);
            t.ge[j] += lane_count(r, n, [rhs](T x) { return static_cast<count_lane<T>>(x >= rhs); });
        }
    }
    if (p.eq == test::compare) {
        const T rhs = static_cast<T>(p.eq_rhs);
        t.eq += lane_count(r, n, [rhs](T x) { return static_cast<count_lane<T>>(x == rhs); });
    }
    if (q.min) {
        T m = static_cast<T>(~T{0});
        for (unsigned i = 0; i < n; ++i)
            m = std::min(m, r[i]);
        t.min = std::min<u64>(t.min, m);
    }
    if (q.max) {
        T m = 0;
        for (unsigned i = 0; i < n; ++i)
            m = std::max(m, r[i]);
        t.max = std::max<u64>(t.max, m);
    }
}

// The same over n <= 64 residuals of which row i counts when bit i of m is
// set (m != 0 and no bit at or past n).
template <class T>
void word_into(totals &t, const T *r, unsigned n, u64 m, const fused_request &q, const plan &p, unsigned width) {
    const unsigned selected_rows = static_cast<unsigned>(std::popcount(m));
    t.rows += selected_rows;
    alignas(64) T keep[64];
    for (unsigned i = 0; i < n; ++i)
        keep[i] = static_cast<T>(T{0} - static_cast<T>(m >> i & 1));
    if (q.sum) {
        alignas(64) T kept[64];
        for (unsigned i = 0; i < n; ++i)
            kept[i] = static_cast<T>(r[i] & keep[i]);
        if constexpr (sizeof(T) <= 4) {
            t.sum += lane_total(kept, n, width);
        } else if (width <= 56) {
            t.sum += lane_sum<u64>(kept, n);
        } else {
            u64 lo = 0, hi = 0;
            for (unsigned i = 0; i < n; ++i) {
                const u64 x = r[i] & keep[i];
                lo += x & 0xffffffffu;
                hi += x >> 32;
            }
            t.sum += static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
        }
    }
    for (unsigned j = 0; j < q.ge_count; ++j) {
        if (p.ge[j] == test::all) {
            t.ge[j] += selected_rows;
        } else if (p.ge[j] == test::compare) {
            const T rhs = static_cast<T>(p.ge_rhs[j]);
            count_lane<T> c = 0;
            for (unsigned i = 0; i < n; ++i)
                c = static_cast<count_lane<T>>(c + (static_cast<count_lane<T>>(r[i] >= rhs) & static_cast<count_lane<T>>(keep[i] & 1)));
            t.ge[j] += c;
        }
    }
    if (p.eq == test::compare) {
        const T rhs = static_cast<T>(p.eq_rhs);
        count_lane<T> c = 0;
        for (unsigned i = 0; i < n; ++i)
            c = static_cast<count_lane<T>>(c + (static_cast<count_lane<T>>(r[i] == rhs) & static_cast<count_lane<T>>(keep[i] & 1)));
        t.eq += c;
    }
    if (q.min) {
        T x = static_cast<T>(~T{0});
        for (unsigned i = 0; i < n; ++i)
            x = std::min(x, static_cast<T>(r[i] | static_cast<T>(~keep[i])));
        t.min = std::min<u64>(t.min, x);
    }
    if (q.max) {
        T x = 0;
        for (unsigned i = 0; i < n; ++i)
            x = std::max(x, static_cast<T>(r[i] & keep[i]));
        t.max = std::max<u64>(t.max, x);
    }
}

// Selections with at most this many unselected rows (in [0, take)) take the
// complement: every row's reductions, minus the few unselected rows (as
// selected.h's delta SUM does at 64); selections with at most
// fused_sparse_word_rows selected rows in a word visit those rows alone.
constexpr unsigned fused_dense_unselected = 64;
constexpr unsigned fused_sparse_word_rows = 8;

// Rows [0, take) of v under s, with at most fused_dense_unselected of them
// unselected. v is scratch: min and max overwrite the unselected rows.
template <class T>
void dense_into(totals &t, T *v, unsigned take, const row_selection &s, const fused_request &q, const plan &p,
                unsigned width) {
    fused_request counts = q;
    counts.min = counts.max = false;
    rows_into(t, v, take, counts, p, width);
    auto each_unselected = [&](auto &&visit) {
        for (unsigned w = 0; w * 64 < take; ++w) {
            u64 m = ~s[w];
            if (take < w * 64 + 64)
                m &= (u64{1} << (take - w * 64)) - 1;
            for (; m; m &= m - 1)
                visit(w * 64 + static_cast<unsigned>(std::countr_zero(m)));
        }
    };
    each_unselected([&](unsigned i) { remove_value(t, q, p, v[i]); });
    if (q.min) {
        each_unselected([&](unsigned i) { v[i] = static_cast<T>(~T{0}); });
        T m = static_cast<T>(~T{0});
        for (unsigned i = 0; i < take; ++i)
            m = std::min(m, v[i]);
        t.min = std::min<u64>(t.min, m);
    }
    if (q.max) {
        each_unselected([&](unsigned i) { v[i] = 0; });
        T m = 0;
        for (unsigned i = 0; i < take; ++i)
            m = std::max(m, v[i]);
        t.max = std::max<u64>(t.max, m);
    }
}
// One 64-row word of a mixed selection: its few selected rows one at a time,
// or the masked loops.
template <class T>
void selected_word_into(totals &t, const T *v, unsigned n, u64 m, const fused_request &q, const plan &p,
                        unsigned width) {
    if (std::popcount(m) <= static_cast<int>(fused_sparse_word_rows)) {
        for (; m; m &= m - 1)
            add_value(t, q, p, v[std::countr_zero(m)], 1);
        return;
    }
    word_into(t, v, n, m, q, p, width);
}

// A full 256-row residual plane, decoded once into lane T: whole for s null or
// a dense selection, else only the 64-row words holding a selected row.
template <class T>
void plane_into(totals &t, lane_range<T> range, const std::uint8_t *plane, const row_selection *s,
                const fused_request &q, const plan &p, unsigned width) {
    alignas(64) T buffer[block_rows];
    if (!s) {
        range(plane, 0, block_rows, buffer);
        rows_into(t, buffer, block_rows, q, p, width);
        return;
    }
    if (selected_count(*s) + fused_dense_unselected >= block_rows) {
        range(plane, 0, block_rows, buffer);
        dense_into(t, buffer, block_rows, *s, q, p, width);
        return;
    }
    for (unsigned w = 0; w < 4; ++w)
        if ((*s)[w]) {
            range(plane, w * 64, 64, buffer);
            selected_word_into(t, buffer, 64, (*s)[w], q, p, width);
        }
}
inline void plane_into(totals &t, const borrowed_block &k, const row_selection *s, const fused_request &q,
                       const plan &p) {
    const unsigned width = k.d.width;
    if (rows_only(q, p))
        return decided(t, q, p, s ? selected_count(*s) : block_rows);
    const auto &c = plane_catalogue;
    if (c.range8[width])
        plane_into<std::uint8_t>(t, c.range8[width], k.body, s, q, p, width);
    else if (c.range16[width])
        plane_into<std::uint16_t>(t, c.range16[width], k.body, s, q, p, width);
    else if (c.range32[width])
        plane_into<std::uint32_t>(t, c.range32[width], k.body, s, q, p, width);
    else
        plane_into<u64>(t, c.range[width], k.body, s, q, p, width);
}

// Decoded values v[0, take): all of them (s null), or the selected rows (no
// selected row at or past take).
// v is scratch (the dense form overwrites unselected rows).
inline void values_into(totals &t, u64 *v, unsigned take, const row_selection *s, const fused_request &q,
                        const plan &p, unsigned width) {
    if (!s)
        return rows_into(t, v, take, q, p, width);
    if (selected_count(*s) + fused_dense_unselected >= take)
        return dense_into(t, v, take, *s, q, p, width);
    for (unsigned w = 0; w * 64 < take; ++w)
        if ((*s)[w])
            selected_word_into(t, v + w * 64, std::min(64u, take - w * 64), (*s)[w], q, p, width);
}

// Runs: one Intervals pass writes each run's weight (its size, or its
// selected rows through two per-word prefix ranks); reductions then loop over
// the value and weight arrays, branch free.
// The same weights straight off the u8 ends (the narrow programs).
inline void walk_weights(const borrowed_block &k, const row_selection *s, std::uint32_t *w) {
    if (!s)
        return walk_runs(k, [&](unsigned rank, unsigned first, unsigned limit) { w[rank] = limit - first; });
    const selection_rank ranks(*s);
    walk_runs(k, [&](unsigned rank, unsigned first, unsigned limit) { w[rank] = ranks.between(first, limit); });
}
inline void run_weights(const borrowed_block &k, const row_selection *s, std::uint32_t *w) {
    if (!s)
        return each_run(k, [&](unsigned rank, unsigned size) { w[rank] = size; });
    unsigned row = 0;
    const selection_rank ranks(*s);
    each_run(k, [&](unsigned rank, unsigned size) {
        w[rank] = ranks.between(row, row + size);
        row += size;
    });
}
// n residuals values[i] (below 2^width; any lane, or an in-place view) with
// weights w (summing to at most 256).
template <class V>
void weighted_into(totals &t, const V &values, const std::uint32_t *w, unsigned n, const fused_request &q,
                   const plan &p, unsigned width) {
    auto v = [&](unsigned i) { return static_cast<u64>(values[i]); };
    u64 rows = 0;
    for (unsigned i = 0; i < n; ++i)
        rows += w[i];
    t.rows += rows;
    if (q.sum) {
        if (width <= 56) {  // sum v_i w_i <= 256 (2^56 - 1)
            u64 x = 0;
            for (unsigned i = 0; i < n; ++i)
                x += v(i) * w[i];
            t.sum += x;
        } else {
            u128 x = 0;
            for (unsigned i = 0; i < n; ++i)
                x += static_cast<u128>(v(i)) * w[i];
            t.sum += x;
        }
    }
    for (unsigned j = 0; j < q.ge_count; ++j) {
        if (p.ge[j] == test::all) {
            t.ge[j] += rows;
        } else if (p.ge[j] == test::compare) {
            const u64 rhs = p.ge_rhs[j];
            u64 c = 0;
            for (unsigned i = 0; i < n; ++i)  // masks, not selects: a select became a branch
                c += w[i] & (u64{0} - static_cast<u64>(v(i) >= rhs));
            t.ge[j] += c;
        }
    }
    if (p.eq == test::compare) {
        const u64 rhs = p.eq_rhs;
        u64 c = 0;
        for (unsigned i = 0; i < n; ++i)
            c += w[i] & (u64{0} - static_cast<u64>(v(i) == rhs));
        t.eq += c;
    }
    if (q.min) {
        u64 m = UINT64_MAX;
        for (unsigned i = 0; i < n; ++i)
            m = std::min(m, v(i) | (u64{0} - static_cast<u64>(w[i] == 0)));
        t.min = std::min(t.min, m);
    }
    if (q.max) {
        u64 m = 0;
        for (unsigned i = 0; i < n; ++i)
            m = std::max(m, v(i) & (u64{0} - static_cast<u64>(w[i] != 0)));
        t.max = std::max(t.max, m);
    }
}

inline unsigned first_selected(const row_selection &s) {
    for (unsigned w = 0; w < 4; ++w)
        if (s[w])
            return w * 64 + static_cast<unsigned>(std::countr_zero(s[w]));
    std::abort();
}

// Stored-domain aggregates over all 256 rows (s null) or a mixed selection,
// accumulated into r (rows excepted).
inline void stored_into(fused_result &r, const borrowed_block &k, const row_selection *s, const fused_request &q) {
    const auto &d = k.d;
    alignas(64) u64 buffer[block_rows];
    totals t;
    switch (d.type) {
    case kind::constant:
        add_value(t, q, plan_for(q, d.base, 0), 0, s ? selected_count(*s) : block_rows);
        return finish(r, q, d.base, t);
    case kind::runs: {
        const plan p = plan_for(q, d.base, bound_width(k));
        if (rows_only(q, p)) {
            decided(t, q, p, s ? selected_count(*s) : block_rows);
            return finish(r, q, d.base, t);
        }
        if (!p.compares && !q.min && !q.max) {
            // Every COUNT decided by the bounds (as the separate COUNT's
            // shortcut): the existing per-run SUM alone reads the body.
            fused_request counts = q;
            counts.sum = false;
            decided(t, counts, p, s ? selected_count(*s) : block_rows);
            finish(r, counts, d.base, t);
            if (q.sum)
                r.sum += s ? masked_stored_sum_mixed(k, *s) : stored_sum(k);
            return;
        }
        alignas(64) std::uint32_t weights[block_rows];
        if (run_values_program != run_program::wide) {
            // Weights straight off the u8 ends, values in their narrowest lane.
            walk_weights(k, s, weights);
            with_run_values(k, run_values_program,
                            [&](const auto &v) { weighted_into(t, v, weights, d.count, q, p, d.width); });
            return finish(r, q, d.base, t);
        }
        read_plane(k, 0, d.count, buffer);
        run_weights(k, s, weights);
        weighted_into(t, buffer, weights, d.count, q, p, d.width);
        return finish(r, q, d.base, t);
    }
    case kind::packed:
        plane_into(t, k, s, q, plan_for(q, d.base, bound_width(k)));
        return finish(r, q, d.base, t);
    case kind::patched: {
        // Inliers S' = S AND NOT E over the plane (exception slots hold 0),
        // exceptions as absolute values.
        const auto pv = patches(k);
        const row_selection exceptions = exception_rows(pv);
        const row_selection inliers = and_not(s ? *s : row_selection{~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}}, exceptions);
        if (!selection_empty(inliers))
            plane_into(t, k, &inliers, q, plan_for(q, d.base, bound_width(k)));
        finish(r, q, d.base, t);
        totals e;
        const plan absolute = plan_for(q, 0, 64);
        alignas(64) u64 exc[block_rows];
        read_exceptions(pv, exc);
        for (unsigned i = 0; i < pv.n; ++i)
            if (!s || selected(*s, pv.positions[i]))
                add_value(e, q, absolute, pv.lo + exc[i], 1);
        return finish(r, q, 0, e);
    }
    case kind::delta: {
        const i64 rbase = delta_rbase(k);
        if (rbase >= 0 && k.delta_checkpoints != checkpoints::none) {
            // Non-decreasing: closed forms over the checkpoint search.
            if (q.sum)
                r.sum += s ? masked_stored_sum_mixed(k, *s) : stored_sum(k);
            auto in = [&](unsigned first, unsigned limit) { return s ? selected_in(*s, first, limit) : limit - first; };
            for (unsigned j = 0; j < q.ge_count; ++j)
                r.ge[j] += in(monotone_delta_first_ge(k, rbase, q.ge[j]), block_rows);
            if (q.eq) {
                const unsigned first = monotone_delta_first_ge(k, rbase, q.eq_value);
                const unsigned limit =
                    q.eq_value == UINT64_MAX ? block_rows : monotone_delta_first_ge(k, rbase, q.eq_value + 1);
                r.eq += in(first, limit);
            }
            if (q.min)
                r.min = std::min(r.min, s ? stored_at(k, first_selected(*s)) : d.base);
            if (q.max)
                r.max = std::max(r.max, stored_at(k, s ? last_selected(*s) : block_rows - 1));
            return;
        }
        // Reconstruct through the last selected row, then reduce the values.
        const unsigned take = s ? last_selected(*s) + 1 : block_rows;
        read_plane(k, 0, take, buffer);
        u64 x = d.base;
        buffer[0] = x;
        for (unsigned i = 1; i < take; ++i) {
            x += static_cast<u64>(rbase) + buffer[i];
            buffer[i] = x;
        }
        values_into(t, buffer, take, s, q, plan_for(q, 0, 64), 64);
        return finish(r, q, 0, t);
    }
    case kind::delta_patched:
    case kind::arrival:
        decode_stored(k, buffer);
        values_into(t, buffer, block_rows, s, q, plan_for(q, 0, 64), 64);
        return finish(r, q, 0, t);
    }
    std::abort();
}
}  // namespace fused_detail

// Stored-domain fused aggregates of one block (values, or codes if coded).
inline fused_result fused_stored(const borrowed_block &k, const row_selection &s, const fused_request &q) {
    fused_result r;
    if (selection_empty(s))
        return r;
    const bool full = selection_full(s);
    r.rows = full ? block_rows : selected_count(s);
    if (q.sum_only())
        r.sum = full ? stored_sum(k) : masked_stored_sum_mixed(k, s);
    else
        fused_detail::stored_into(r, k, full ? nullptr : &s, q);
    return r;
}

// Dictionary holders for a fused program: entries through code_sum's state,
// translated thresholds and truth tables through code_count, equality's code,
// code range or truth table through code_count_eq; each prepared lazily, per
// edition, and only for the aggregates requested.
struct fused_codes {
    code_sum sums;
    std::array<code_count, fused_thresholds> counts;
    code_count_eq equal;
    fused_codes(const fused_request &q, dictionary_cache *cache)
        : sums(cache),
          counts{code_count(q.ge[0], cache), code_count(q.ge[1], cache), code_count(q.ge[2], cache),
                 code_count(q.ge[3], cache)},
          equal(q.eq_value, cache) {}
    // Decoded min-relative entries of p's edition (code_sum's state, so a
    // SUM-only call through sums reuses them).
    const u64 *entries(const dictionary_view &p) {
        auto &c = sums;
        if (!c.ready || p.edition != c.edition) {
            c.ready = true;
            c.edition = p.edition;
            if (c.cache) {
                c.held = c.cache->values(p);
            } else {
                c.values.resize(p.count);
                read_dictionary(p, 0, c.values.size(), c.values.data());
            }
        }
        return c.cache ? c.held->data() : c.values.data();
    }

    // Logical aggregates of a coded block over all rows (s null) or a mixed
    // selection, accumulated into r (rows excepted).
    void into(fused_result &r, const borrowed_block &k, const row_selection *s, const fused_request &q) {
        using namespace fused_detail;
        const auto &p = k.dictionary;
        const auto &d = k.d;
        const u64 n = s ? selected_count(*s) : block_rows;
        alignas(64) u64 buffer[block_rows];
        if (q.sum || q.min || q.max) {
            // One gather per row (per run, per block for constant) feeds all.
            const u64 *e = entries(p);
            const plan pl = plan_for(q, p.min, p.width);
            totals t;
            if (d.type == kind::constant) {
                add_value(t, q, pl, e[d.base], n);
            } else if (d.type == kind::runs) {
                alignas(64) std::uint32_t weights[block_rows];
                if (run_values_program != run_program::wide) {
                    walk_weights(k, s, weights);
                    with_run_values(k, run_values_program, [&](const auto &v) {
                        for (unsigned i = 0; i < d.count; ++i)
                            buffer[i] = e[d.base + v[i]];
                    });
                } else {
                    read_plane(k, 0, d.count, buffer);
                    run_weights(k, s, weights);
                    for (unsigned i = 0; i < d.count; ++i)
                        buffer[i] = e[d.base + buffer[i]];
                }
                weighted_into(t, buffer, weights, d.count, q, pl, p.width);
            } else {
                decode_stored(k, buffer);
                if (!s || n + fused_dense_unselected >= block_rows) {
                    for (unsigned i = 0; i < block_rows; ++i)
                        buffer[i] = e[buffer[i]];
                } else {  // only the words values_into reads
                    for (unsigned w = 0; w < 4; ++w)
                        if ((*s)[w])
                            for (unsigned i = w * 64; i < w * 64 + 64; ++i)
                                buffer[i] = e[buffer[i]];
                }
                values_into(t, buffer, block_rows, s, q, pl, p.width);
            }
            return finish(r, q, p.min, t);
        }
        if (q.eq)
            equal.prepare(p);
        // Equality is one code (an injective identity, or a value held once),
        // or a set of codes: a sorted code range or a truth table under the
        // duplicate-safe law, or every code.
        const auto &m = equal.match;
        const bool eq = q.eq && m.single();
        const bool eq_set = q.eq && !eq && m.law != code_law::none;
        for (unsigned j = 0; j < q.ge_count; ++j)
            counts[j].prepare(p);
        if (p.order == dictionary_order::sorted) {
            // Order-preserving codes: the stored program at code thresholds
            // (a threshold past every entry becomes count, which no code reaches).
            fused_request c;
            c.ge_count = q.ge_count;
            for (unsigned j = 0; j < q.ge_count; ++j)
                c.ge[j] = std::min<u64>(counts[j].code_threshold, p.count);
            c.eq = eq;
            c.eq_value = m.lo;
            fused_result t;
            stored_into(t, k, s, c);
            for (unsigned j = 0; j < q.ge_count; ++j)
                r.ge[j] += t.ge[j];
            r.eq += t.eq;
            if (eq_set)  // a code range: equality's own stored COUNTs
                r.eq += s ? equal.masked(k, *s) : equal.count(k);
            return;
        }
        // Frequency and arrival: truth bits and the equality code off one
        // code buffer.
        auto matches = [&](u64 code) { return eq ? code == m.lo : eq_set && (m.law == code_law::all || m.holds(code)); };
        auto weigh = [&](u64 code, u64 w) {
            for (unsigned j = 0; j < q.ge_count; ++j)
                r.ge[j] += counts[j].holds(code) ? w : 0;
            r.eq += matches(code) ? w : 0;
        };
        if (d.type == kind::constant)
            return weigh(d.base, n);
        if (d.type == kind::runs) {
            alignas(64) std::uint32_t weights[block_rows];
            if (run_values_program != run_program::wide) {
                walk_weights(k, s, weights);
                with_run_values(k, run_values_program, [&](const auto &v) {
                    for (unsigned i = 0; i < d.count; ++i)
                        weigh(d.base + v[i], weights[i]);
                });
                return;
            }
            read_plane(k, 0, d.count, buffer);
            run_weights(k, s, weights);
            for (unsigned i = 0; i < d.count; ++i)
                weigh(d.base + buffer[i], weights[i]);
            return;
        }
        decode_stored(k, buffer);
        const row_selection all{~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}};
        const row_selection &sel = s ? *s : all;
        for (unsigned j = 0; j < q.ge_count; ++j) {
            u64 c = 0;
            for (unsigned w = 0; w < 4; ++w)
                for (u64 hit = sel[w]; hit; hit &= hit - 1)
                    c += counts[j].holds(buffer[w * 64 + static_cast<unsigned>(std::countr_zero(hit))]);
            r.ge[j] += c;
        }
        if (eq) {
            u64 c = 0;
            for (unsigned i = 0; i < block_rows; ++i)
                c += selected(sel, i) && buffer[i] == m.lo;
            r.eq += c;
        } else if (eq_set) {
            u64 c = 0;
            for (unsigned i = 0; i < block_rows; ++i)
                c += selected(sel, i) && matches(buffer[i]);
            r.eq += c;
        }
    }
};

// One request over many blocks; dictionary material persists across blocks
// (and across calls, with a dictionary_cache).
struct fused_program {
    fused_request q;
    fused_codes codes;
    explicit fused_program(const fused_request &request, dictionary_cache *cache = nullptr)
        : q(request), codes(request, cache) {}
    // All 256 rows of a block.
    void block(fused_result &r, const borrowed_block &k) {
        r.rows += block_rows;
        if (!k.coded) {
            if (q.sum_only())
                r.sum += stored_sum(k);
            else
                fused_detail::stored_into(r, k, nullptr, q);
            return;
        }
        if (q.sum_only())
            r.sum += codes.sums(k);
        else
            codes.into(r, k, nullptr, q);
    }
    // The rows of S: empty returns before any body or dictionary access, full
    // takes block().
    void masked(fused_result &r, const borrowed_block &k, const row_selection &s) {
        if (selection_empty(s))
            return;
        if (selection_full(s))
            return block(r, k);
        r.rows += selected_count(s);
        if (!k.coded) {
            if (q.sum_only())
                r.sum += masked_stored_sum_mixed(k, s);
            else
                fused_detail::stored_into(r, k, &s, q);
            return;
        }
        if (q.sum_only())
            r.sum += codes.sums.masked(k, s);
        else
            codes.into(r, k, &s, q);
    }
};

// Logical fused aggregates of one block (a lone coded block prepares its
// dictionary per call).
inline fused_result fused_block(const borrowed_block &k, const fused_request &q) {
    fused_program program(q);
    fused_result r;
    program.block(r, k);
    return r;
}
inline fused_result masked_fused_block(const borrowed_block &k, const row_selection &s, const fused_request &q) {
    fused_result r;
    if (selection_empty(s))
        return r;
    fused_program program(q);
    program.masked(r, k, s);
    return r;
}

// Column forms: a coarse block mask, and optionally a row plane of four words
// per block (tested before the descriptor, as masked_encoded_sum does).
inline fused_result encoded_fused(const column &c, const block_mask &mask, const fused_request &q,
                                  dictionary_cache *cache = nullptr) {
    fused_program program(q, cache);
    fused_result r;
    for_each_block(mask, [&](std::size_t b) { program.block(r, borrow(c, b)); });
    return r;
}
inline fused_result masked_encoded_fused(const column &c, const block_mask &mask, std::span<const u64> rows,
                                         const fused_request &q, dictionary_cache *cache = nullptr) {
    fused_program program(q, cache);
    fused_result r;
    for_each_block(mask, [&](std::size_t b) {
        const row_selection s = selection_at(rows, b);
        if (!selection_empty(s))
            program.masked(r, borrow(c, b), s);
    });
    return r;
}
}  // namespace frame_encoding
