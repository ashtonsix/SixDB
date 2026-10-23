// Row-selected aggregates over borrowed blocks: exact SUM (u128), COUNT(x >= t),
// COUNT(x == v) and |S| under a 256-bit row selection S (the caller's
// selection AND validity), with per-kind algebra instead of decode-then-mask
// wherever a law exists. Included at the end of material.h; the coded-block
// members of code_sum, code_count and code_count_eq are defined here.
//
// Dispatch shared by every operation: an empty S returns before any body or
// dictionary access; a full S takes the existing full-256 operation unchanged.
// Laws per kind (u0 the anchor or base, s_j stored residuals, s_0 = 0):
//   constant       base * |S|.
//   runs           sum over runs of value * popcount(S AND run), each
//                  popcount a difference of two per-word prefix ranks.
//   packed         |S| * base + the plane's masked native packet sum
//                  (catalogue masked_sum, K <= 56; wider: split halves).
//   patched        inliers S' = S AND NOT E: |S'| * base + masked plane sum
//                  (exception slots hold 0); exceptions by position test.
//   delta          sum_{i in S} x_i = |S| u0 + rbase * sum_{i in S} i
//                  + sum_j s_j c_j, with c_j = |{i in S : i >= j}| a suffix
//                  popcount, so sum_{i in S} i = sum_{j >= 1} c_j. The full
//                  form's weights 256 - j become c_j <= 256, so its lane bounds
//                  hold. A selected row's value depends on unselected rows'
//                  residuals (c_j counts them), so the plane is read through
//                  the last selected row: rows past it have c_j = 0.
//                  Equivalently sum_{i in S} S(i), which for at most 64
//                  selected rows is a walk of segment sums, and for at most 64
//                  unselected rows U is the full SUM minus sum_{i in U} x_i
//                  (that form reads the whole plane, still no checkpoints).
//   delta_patched  as delta over S' = S AND NOT E with c'_j its suffix
//                  counts, steps m(i) = |{j in (f, i] : j not in E}|, so
//                  sum_{i in S'} m(i) = sum over step rows j of c'_j; selected
//                  exceptions add lo + v_e.
//   arrival        a literal row's code is F plus the flagged rows before it,
//                  and a reference's index counts every earlier reference, so
//                  codes of selected rows depend on unselected rows' flags:
//                  the flag words are read whole and references enumerated
//                  in row order, filtered by S.
// COUNT: constant, runs, packed (catalogue masked_count_ge/eq, the 64-row
// filters with the selection as active mask) and patched follow the same
// split; a non-decreasing delta block (rbase >= 0, the trusted-image premise
// of monotone_delta_count_ge) is popcount(S AND rows >= first), with first from
// the checkpoint search (so it reads checkpoints, as the full COUNT does), or
// from a prefix scan that stops at t without checkpoints. Other delta COUNTs
// reconstruct the prefix through the last selected row. Coded blocks go
// through the dictionary as the full operations do.
#pragma once

namespace frame_encoding {

// The selected literal (flagged) rows of an arrival block with their rank among
// all literal rows: visit(row, rank).
template <class V> void each_selected_literal(const std::array<u64, 4> &w, const row_selection &s, V &&visit) {
    unsigned rank = 0;
    for (unsigned word = 0; word < 4; ++word) {
        for (u64 hit = w[word] & s[word]; hit; hit &= hit - 1) {
            const unsigned bit = static_cast<unsigned>(std::countr_zero(hit));
            visit(word * 64 + bit, rank + static_cast<unsigned>(std::popcount(w[word] & ((u64{1} << bit) - 1))));
        }
        rank += static_cast<unsigned>(std::popcount(w[word]));
    }
}

// The row of the q-th flagged row (q < literals).
inline unsigned select_literal(const std::array<u64, 4> &w, unsigned q) {
    for (unsigned word = 0; word < 4; ++word) {
        const unsigned n = static_cast<unsigned>(std::popcount(w[word]));
        if (q < n) {
            u64 m = w[word];
            for (unsigned i = 0; i < q; ++i)
                m &= m - 1;
            return word * 64 + static_cast<unsigned>(std::countr_zero(m));
        }
        q -= n;
    }
    std::abort();
}

// Exception positions as a row mask.
inline row_selection exception_rows(const patch_view &p) {
    row_selection e{};
    for (unsigned i = 0; i < p.n; ++i)
        e[p.positions[i] / 64] |= u64{1} << (p.positions[i] % 64);
    return e;
}
inline row_selection and_not(const row_selection &a, const row_selection &b) {
    return {a[0] & ~b[0], a[1] & ~b[1], a[2] & ~b[2], a[3] & ~b[3]};
}

// Selected rows before row r (r <= 256) in two operations, from per-word
// prefix counts: run loops ask once per run boundary, and suffix counts once
// per row without a serial chain.
struct selection_rank {
    const row_selection &s;
    unsigned prefix[5];
    explicit selection_rank(const row_selection &sel) : s(sel) {
        prefix[0] = 0;
        for (unsigned w = 0; w < 4; ++w)
            prefix[w + 1] = prefix[w] + static_cast<unsigned>(std::popcount(sel[w]));
    }
    unsigned at(unsigned r) const {
        const unsigned w = r / 64, b = r % 64;
        return w == 4 ? prefix[4] : prefix[w] + static_cast<unsigned>(std::popcount(s[w] & ((u64{1} << b) - 1)));
    }
    unsigned between(unsigned first, unsigned limit) const { return at(limit) - at(first); }
};

// Run-value programs (runs.h) over a mixed selection: each run weighs its
// selected rows, two per-word prefix ranks, with u64 v * weight sums.
inline u128 masked_runs_sum(const borrowed_block &k, const row_selection &s, run_program program) {
    const selection_rank ranks(s);
    const u128 base = static_cast<u128>(k.d.base) * ranks.prefix[4];
    return with_run_values(k, program, [&](const auto &v) -> u128 {
        if (k.d.width <= 56) {
            u64 total = 0;
            walk_runs(k, [&](unsigned r, unsigned first, unsigned limit) { total += u64{v[r]} * ranks.between(first, limit); });
            return base + total;
        }
        u128 total = 0;
        walk_runs(k, [&](unsigned r, unsigned first, unsigned limit) {
            total += static_cast<u128>(v[r]) * ranks.between(first, limit);
        });
        return base + total;
    });
}
template <class Keep>
u64 masked_runs_count(const borrowed_block &k, const row_selection &s, run_program program, Keep &&keep) {
    const selection_rank ranks(s);
    return with_run_values(k, program, [&](const auto &v) {
        u64 total = 0;
        walk_runs(k, [&](unsigned r, unsigned first, unsigned limit) {
            total += ranks.between(first, limit) & (u64{0} - static_cast<u64>(keep(u64{v[r]})));
        });
        return total;
    });
}

// c_j = |{i in S : i >= j}| = |S| - rank(j) for j < take (take = last
// selected row + 1).
inline void suffix_counts(const row_selection &s, unsigned take, std::uint16_t *c) {
    const selection_rank ranks(s);
    const unsigned total = ranks.prefix[4];
    for (unsigned j = 0; j < take; ++j)
        c[j] = static_cast<std::uint16_t>(total - ranks.at(j));
}

// sum_{j=1}^{take-1} c_j s_j over the plane's rows [0, take) in lane T.
template <class T>
u64 masked_weighted_residuals(lane_range<T> range, const std::uint8_t *plane, const std::uint16_t *c, unsigned take) {
    alignas(64) T s[block_rows];
    range(plane, 0, take, s);
    u64 w = 0;
    for (unsigned j = 1; j < take; ++j) {
        if constexpr (sizeof(T) <= 2)
            w += static_cast<std::uint32_t>(c[j]) * s[j];
        else
            w += static_cast<u64>(c[j]) * s[j];
    }
    return w;
}

// sum_{j=1}^{take-1} c_j s_j exactly, for any residual width.
inline i128 masked_weighted(const borrowed_block &k, const std::uint16_t *c, unsigned take) {
    const auto &ops = plane_kernels(k.d.width);
    if (ops.range8)
        return masked_weighted_residuals(ops.range8, k.body, c, take);
    if (ops.range16)
        return masked_weighted_residuals(ops.range16, k.body, c, take);
    if (ops.range32)
        return masked_weighted_residuals(ops.range32, k.body, c, take);
    alignas(64) u64 buffer[block_rows];
    read_plane(k, 0, take, buffer);
    if (k.d.width <= 47) {  // c_j s_j < 2^55, and 255 of them fit u64
        u64 w = 0;
        for (unsigned j = 1; j < take; ++j)
            w += c[j] * buffer[j];
        return w;
    }
    i128 t = 0;
    for (unsigned j = 1; j < take; ++j)
        t += static_cast<i128>(c[j]) * buffer[j];
    return t;
}

// sum_{i in rows} x_i over a delta block by segment sums of the residual
// plane through the last such row: S(i) for ascending rows adds the residuals
// since the previous one, in 32-bit halves (each half prefix is below 2^40 and
// at most 256 held prefixes below 2^48), so long segments vectorise and the
// sum is exact at any width. For sparse rows, or the complement of dense ones.
// Selections with at most this many rows, or this many unselected, take
// segments (paired sweep on Jena p and T over 0/32/64/96/128: 64 is best at
// 1-10% NULL, and segments lose 2x at 50%, where the suffix counts stay).
constexpr unsigned delta_segment_rows = 64;
inline i128 delta_rows_sum(const borrowed_block &k, const row_selection &rows) {
    const unsigned take = last_selected(rows) + 1;
    alignas(64) u64 buffer[block_rows];
    read_plane(k, 0, take, buffer);
    u64 lo = 0, hi = 0, held_lo = 0, held_hi = 0, index = 0;
    unsigned j = 0;
    for (unsigned word = 0; word < 4; ++word)
        for (u64 hit = rows[word]; hit; hit &= hit - 1) {
            const unsigned r = word * 64 + static_cast<unsigned>(std::countr_zero(hit));
            for (; j <= r; ++j)
                lo += buffer[j] & 0xffffffffu, hi += buffer[j] >> 32;
            held_lo += lo, held_hi += hi, index += r;
        }
    const u128 held = static_cast<u128>(held_lo) + (static_cast<u128>(held_hi) << 32);
    return static_cast<i128>(k.d.base) * selected_count(rows) + static_cast<i128>(delta_rbase(k)) * static_cast<i128>(index) +
           static_cast<i128>(held);
}

// Exact sum of the selected entries of a decoded 256-value buffer.
inline u128 masked_split_sum(const u64 *v, const row_selection &s) {
    u64 lo = 0, hi = 0;
    for (unsigned i = 0; i < block_rows; ++i) {
        const u64 keep = u64{0} - selected(s, i);
        lo += v[i] & keep & 0xffffffffu;
        hi += (v[i] & keep) >> 32;
    }
    return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
}

// The masked plane sum of a full 256-row plane at any width.
inline u128 masked_plane_sum(const borrowed_block &k, const row_selection &s) {
    if (k.d.width <= 56)
        return plane_kernels(k.d.width).masked_sum(k.body, s.data());
    alignas(64) u64 buffer[block_rows];
    read_plane(k, 0, block_rows, buffer);
    return masked_split_sum(buffer, s);
}

// Stored-domain SUM over a non-empty, non-full selection (no dispatch).
inline u128 masked_stored_sum_mixed(const borrowed_block &k, const row_selection &s) {
    const auto &d = k.d;
    alignas(64) u64 buffer[block_rows];
    switch (d.type) {
    case kind::constant:
        return static_cast<u128>(d.base) * selected_count(s);
    case kind::packed:
        return static_cast<u128>(d.base) * selected_count(s) + masked_plane_sum(k, s);
    case kind::patched: {
        const auto p = patches(k);
        const row_selection inliers = and_not(s, exception_rows(p));
        u128 total = static_cast<u128>(d.base) * selected_count(inliers);
        if (!selection_empty(inliers))
            total += plane_kernels(d.width).masked_sum(k.body, inliers.data());  // K <= 56
        alignas(64) u64 exc[block_rows];
        read_exceptions(p, exc);
        for (unsigned e = 0; e < p.n; ++e)
            if (selected(s, p.positions[e]))
                total += static_cast<u128>(p.lo) + exc[e];
        return total;
    }
    case kind::delta: {
        // Few selected rows: their prefixes by segments. Few unselected rows:
        // the full SUM minus theirs. Otherwise suffix-count weights.
        const unsigned n = selected_count(s);
        if (n <= delta_segment_rows)
            return static_cast<u128>(delta_rows_sum(k, s));
        if (n >= block_rows - delta_segment_rows) {
            const row_selection rest = and_not({~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}}, s);
            return static_cast<u128>(static_cast<i128>(stored_sum(k)) - delta_rows_sum(k, rest));
        }
        const unsigned take = last_selected(s) + 1;
        alignas(64) std::uint16_t c[block_rows];
        suffix_counts(s, take, c);
        u64 index_sum = 0;  // sum_{i in S} i = sum_{j >= 1} c_j
        for (unsigned j = 1; j < take; ++j)
            index_sum += c[j];
        const i128 t = static_cast<i128>(d.base) * selected_count(s) +
                       static_cast<i128>(delta_rbase(k)) * static_cast<i128>(index_sum) + masked_weighted(k, c, take);
        return static_cast<u128>(t);
    }
    case kind::delta_patched: {
        const auto p = patches(k);
        const row_selection exceptions = exception_rows(p);
        const row_selection chain = and_not(s, exceptions);
        i128 t = 0;
        if (!selection_empty(chain)) {
            unsigned f = 0;
            while (f < p.n && p.positions[f] == f)
                ++f;  // the anchor row
            const unsigned take = last_selected(chain) + 1;
            alignas(64) std::uint16_t c[block_rows];
            suffix_counts(chain, take, c);
            u64 steps = 0;  // sum_{i in S'} m(i): step rows j > f outside E
            for (unsigned j = f + 1; j < take; ++j)
                steps += selected(exceptions, j) ? 0 : c[j];
            t = static_cast<i128>(d.base) * selected_count(chain) +
                static_cast<i128>(delta_rbase(k)) * static_cast<i128>(steps) + masked_weighted(k, c, take);
        }
        alignas(64) u64 exc[block_rows];
        read_exceptions(p, exc);
        for (unsigned e = 0; e < p.n; ++e)
            if (selected(s, p.positions[e]))
                t += static_cast<i128>(p.lo) + exc[e];
        return static_cast<u128>(t);
    }
    case kind::runs: {
        if (run_values_program != run_program::wide)
            return masked_runs_sum(k, s, run_values_program);
        read_plane(k, 0, d.count, buffer);
        u128 total = 0;
        unsigned row = 0;
        const selection_rank ranks(s);
        each_run(k, [&](unsigned rank, unsigned size) {
            total += static_cast<u128>(buffer[rank] + d.base) * ranks.between(row, row + size);
            row += size;
        });
        return total;
    }
    case kind::arrival: {
        // Codes are below 2^32.
        const auto w = arrival_flags(k);
        u64 total = 0;
        each_selected_literal(w, s, [&](unsigned, unsigned rank) { total += d.base + rank; });
        each_arrival_reference(k, w, [&](unsigned row, u64 code) { total += selected(s, row) ? code : 0; });
        return total;
    }
    }
    std::abort();
}

inline u128 masked_stored_sum(const borrowed_block &k, const row_selection &s) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return stored_sum(k);
    return masked_stored_sum_mixed(k, s);
}

// Stored COUNT(x >= t) over a non-empty, non-full selection.
inline u64 masked_stored_count_ge_mixed(const borrowed_block &k, const row_selection &s, u64 threshold) {
    const auto &d = k.d;
    alignas(64) u64 buffer[block_rows];
    if ((d.type == kind::constant || d.type == kind::runs || d.type == kind::packed) && d.base >= threshold)
        return selected_count(s);
    switch (d.type) {
    case kind::constant:
        return 0;
    case kind::packed: {
        const u64 rhs = threshold - d.base;
        if (d.width < 64 && rhs >> d.width)
            return 0;
        return plane_kernels(d.width).masked_count_ge(k.body, s.data(), rhs);
    }
    case kind::runs: {
        const u64 rhs = threshold - d.base;
        if (d.width < 64 && rhs >> d.width)
            return 0;
        if (run_values_program != run_program::wide)
            return masked_runs_count(k, s, run_values_program, [rhs](u64 v) { return v >= rhs; });
        read_plane(k, 0, d.count, buffer);
        u64 total = 0;
        unsigned row = 0;
        const selection_rank ranks(s);
        each_run(k, [&](unsigned rank, unsigned size) {
            total += (buffer[rank] >= rhs) * ranks.between(row, row + size);
            row += size;
        });
        return total;
    }
    case kind::patched: {
        const auto p = patches(k);
        const row_selection inliers = and_not(s, exception_rows(p));
        alignas(64) u64 exc[block_rows];
        read_exceptions(p, exc);
        u64 total = 0;
        for (unsigned e = 0; e < p.n; ++e)
            total += selected(s, p.positions[e]) && p.lo + exc[e] >= threshold;
        if (selection_empty(inliers))
            return total;
        if (d.base >= threshold)
            return total + selected_count(inliers);
        const u64 rhs = threshold - d.base;
        if (d.width < 64 && rhs >> d.width)
            return total;
        return total + plane_kernels(d.width).masked_count_ge(k.body, inliers.data(), rhs);
    }
    case kind::delta: {
        const i64 rbase = delta_rbase(k);
        if (rbase >= 0 && k.delta_checkpoints != checkpoints::none)
            return selected_in(s, monotone_delta_first_ge(k, rbase, threshold), block_rows);
        // Reconstruct through the last selected row; a non-decreasing block
        // without checkpoints stops at the first row reaching t.
        const unsigned take = last_selected(s) + 1;
        read_plane(k, 0, take, buffer);
        u64 x = d.base, total = 0;
        if (rbase >= 0 && x >= threshold)
            return selected_count(s);
        total += selected(s, 0) && x >= threshold;
        for (unsigned i = 1; i < take; ++i) {
            x += static_cast<u64>(rbase) + buffer[i];
            if (rbase >= 0 && x >= threshold)
                return selected_in(s, i, block_rows);
            total += selected(s, i) && x >= threshold;
        }
        return total;
    }
    case kind::delta_patched:
    case kind::arrival: {
        decode_stored(k, buffer);
        u64 total = 0;
        for (unsigned i = 0; i < block_rows; ++i)
            total += selected(s, i) && buffer[i] >= threshold;
        return total;
    }
    }
    std::abort();
}

inline u64 masked_stored_count_ge(const borrowed_block &k, const row_selection &s, u64 threshold) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return stored_count_ge(k, threshold);
    return masked_stored_count_ge_mixed(k, s, threshold);
}

// Stored COUNT(x == v) over a non-empty, non-full selection.
inline u64 masked_stored_count_eq_mixed(const borrowed_block &k, const row_selection &s, u64 v) {
    const auto &d = k.d;
    alignas(64) u64 buffer[block_rows];
    switch (d.type) {
    case kind::constant:
        return d.base == v ? selected_count(s) : 0;
    case kind::packed:
    case kind::runs: {
        if (v < d.base)
            return 0;
        const u64 rhs = v - d.base;
        if (d.width < 64 && rhs >> d.width)
            return 0;
        if (d.type == kind::packed)
            return plane_kernels(d.width).masked_count_eq(k.body, s.data(), rhs);
        if (run_values_program != run_program::wide)
            return masked_runs_count(k, s, run_values_program, [rhs](u64 v) { return v == rhs; });
        read_plane(k, 0, d.count, buffer);
        u64 total = 0;
        unsigned row = 0;
        const selection_rank ranks(s);
        each_run(k, [&](unsigned rank, unsigned size) {
            total += (buffer[rank] == rhs) * ranks.between(row, row + size);
            row += size;
        });
        return total;
    }
    case kind::patched: {
        const auto p = patches(k);
        const row_selection inliers = and_not(s, exception_rows(p));
        alignas(64) u64 exc[block_rows];
        read_exceptions(p, exc);
        u64 total = 0;
        for (unsigned e = 0; e < p.n; ++e)
            total += selected(s, p.positions[e]) && p.lo + exc[e] == v;
        if (selection_empty(inliers) || v < d.base)
            return total;
        const u64 rhs = v - d.base;
        if (d.width < 64 && rhs >> d.width)
            return total;
        return total + plane_kernels(d.width).masked_count_eq(k.body, inliers.data(), rhs);
    }
    case kind::arrival: {
        // A code in [F, F + L) is one literal row's; references are filtered.
        const auto w = arrival_flags(k);
        const unsigned literals = arrival_literals(w);
        if (v >= d.base + literals)
            return 0;
        u64 total = v >= d.base && selected(s, select_literal(w, static_cast<unsigned>(v - d.base)));
        each_arrival_reference(k, w, [&](unsigned row, u64 code) { total += code == v && selected(s, row); });
        return total;
    }
    case kind::delta: {
        const unsigned take = last_selected(s) + 1;
        read_plane(k, 0, take, buffer);
        const u64 rbase = static_cast<u64>(delta_rbase(k));
        u64 x = d.base, total = selected(s, 0) && x == v;
        for (unsigned i = 1; i < take; ++i) {
            x += rbase + buffer[i];
            total += selected(s, i) && x == v;
        }
        return total;
    }
    case kind::delta_patched: {
        decode_stored(k, buffer);
        u64 total = 0;
        for (unsigned i = 0; i < block_rows; ++i)
            total += selected(s, i) && buffer[i] == v;
        return total;
    }
    }
    std::abort();
}

inline u64 masked_stored_count_eq(const borrowed_block &k, const row_selection &s, u64 v) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return stored_count_eq(k, v);
    return masked_stored_count_eq_mixed(k, s, v);
}

// Logical SUM of a coded block over a non-empty, non-full selection:
// |S| m + sum_{i in S} D[code_i]; lookup(code) = D[code].
template <class L> u128 masked_coded_sum(const borrowed_block &k, const row_selection &s, L &&lookup) {
    const auto &d = k.d;
    const auto &p = k.dictionary;
    const unsigned n = selected_count(s);
    u128 total = static_cast<u128>(p.min) * n;
    if (d.type == kind::constant)
        return total + static_cast<u128>(lookup(d.base)) * n;
    alignas(64) u64 buffer[block_rows];
    if (d.type == kind::runs) {
        read_plane(k, 0, d.count, buffer);
        unsigned row = 0;
        const selection_rank ranks(s);
        each_run(k, [&](unsigned rank, unsigned size) {
            total += static_cast<u128>(lookup(d.base + buffer[rank])) * ranks.between(row, row + size);
            row += size;
        });
        return total;
    }
    if (d.type == kind::arrival) {
        const auto w = arrival_flags(k);
        each_selected_literal(w, s, [&](unsigned, unsigned rank) { total += lookup(d.base + rank); });
        each_arrival_reference(k, w, [&](unsigned row, u64 code) {
            if (selected(s, row))
                total += lookup(code);
        });
        return total;
    }
    decode_stored(k, buffer);
    for (unsigned word = 0; word < 4; ++word)
        for (u64 hit = s[word]; hit; hit &= hit - 1)
            total += lookup(buffer[word * 64 + static_cast<unsigned>(std::countr_zero(hit))]);
    return total;
}

inline u128 code_sum::masked(const borrowed_block &k, const row_selection &s) {
    if (selection_empty(s))
        return 0;  // before any dictionary preparation
    if (selection_full(s))
        return (*this)(k);
    if (!ready || k.dictionary.edition != edition) {
        ready = true;
        edition = k.dictionary.edition;
        if (cache) {
            held = cache->values(k.dictionary);
        } else {
            values.resize(k.dictionary.count);
            read_dictionary(k.dictionary, 0, values.size(), values.data());
        }
    }
    const u64 *entries = cache ? held->data() : values.data();
    return masked_coded_sum(k, s, [entries](u64 code) { return entries[code]; });
}

// Selected rows (non-empty, non-full s) of an unordered coded block whose code
// has its truth bit set, by coded_truth_count's per-kind split.
inline u64 masked_coded_truth_count(const borrowed_block &k, const row_selection &s, const u64 *bits) {
    const auto &d = k.d;
    if (d.type == kind::constant)
        return truth_holds(bits, d.base) ? selected_count(s) : 0;
    alignas(64) u64 buffer[block_rows];
    u64 total = 0;
    if (d.type == kind::arrival) {
        const auto w = arrival_flags(k);
        each_selected_literal(w, s, [&](unsigned, unsigned rank) { total += truth_holds(bits, d.base + rank); });
        each_arrival_reference(k, w,
                               [&](unsigned row, u64 code) { total += selected(s, row) && truth_holds(bits, code); });
        return total;
    }
    if (d.type == kind::runs) {
        read_plane(k, 0, d.count, buffer);
        unsigned row = 0;
        const selection_rank ranks(s);
        each_run(k, [&](unsigned rank, unsigned size) {
            total += truth_holds(bits, d.base + buffer[rank]) * ranks.between(row, row + size);
            row += size;
        });
        return total;
    }
    decode_stored(k, buffer);
    for (unsigned word = 0; word < 4; ++word)
        for (u64 hit = s[word]; hit; hit &= hit - 1)
            total += truth_holds(bits, buffer[word * 64 + static_cast<unsigned>(std::countr_zero(hit))]);
    return total;
}

inline u64 code_count::masked(const borrowed_block &k, const row_selection &s) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return count(k);
    prepare(k.dictionary);
    if (k.dictionary.order == dictionary_order::sorted)
        return code_threshold >= k.dictionary.count ? 0 : masked_stored_count_ge_mixed(k, s, code_threshold);
    return masked_coded_truth_count(k, s, truth_bits);
}

inline u64 code_count_eq::masked(const borrowed_block &k, const row_selection &s) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return count(k);
    prepare(k.dictionary);
    switch (match.law) {
    case code_law::none: return 0;
    case code_law::all: return selected_count(s);
    case code_law::codes:
        if (match.lo == match.last)
            return masked_stored_count_eq_mixed(k, s, match.lo);
        return masked_stored_count_ge_mixed(k, s, match.lo) -
               (match.last + 1 < k.dictionary.count ? masked_stored_count_ge_mixed(k, s, match.last + 1) : 0);
    case code_law::truth: return masked_coded_truth_count(k, s, match.bits);
    }
    std::abort();
}

// Logical operations on one block (a lone coded block translates per call).
inline u128 masked_block_sum(const borrowed_block &k, const row_selection &s) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return block_sum(k);
    if (!k.coded)
        return masked_stored_sum_mixed(k, s);
    return masked_coded_sum(k, s, [&](u64 code) { return dictionary_at(k.dictionary, code); });
}
inline u64 masked_block_count_ge(const borrowed_block &k, const row_selection &s, u64 t) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return block_count_ge(k, t);
    if (!k.coded)
        return masked_stored_count_ge_mixed(k, s, t);
    code_count codes(t);
    return codes.masked(k, s);
}
inline u64 masked_block_count_eq(const borrowed_block &k, const row_selection &s, u64 v) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return block_count_eq(k, v);
    if (!k.coded)
        return masked_stored_count_eq_mixed(k, s, v);
    code_count_eq codes(v);
    return codes.masked(k, s);
}

// Decode-then-mask control: the whole block's logical values, then the
// selected rows. The same empty/full dispatch, so it differs from the
// per-kind arms only on mixed selections.
inline u128 reference_block_sum(const borrowed_block &k, const row_selection &s) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return block_sum(k);
    alignas(64) u64 v[block_rows];
    decode_block(k, v);
    return masked_split_sum(v, s);
}
inline u64 reference_block_count_ge(const borrowed_block &k, const row_selection &s, u64 t) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return block_count_ge(k, t);
    alignas(64) u64 v[block_rows];
    decode_block(k, v);
    u64 n = 0;
    for (unsigned i = 0; i < block_rows; ++i)
        n += selected(s, i) && v[i] >= t;
    return n;
}
inline u64 reference_block_count_eq(const borrowed_block &k, const row_selection &s, u64 x) {
    if (selection_empty(s))
        return 0;
    if (selection_full(s))
        return block_count_eq(k, x);
    alignas(64) u64 v[block_rows];
    decode_block(k, v);
    u64 n = 0;
    for (unsigned i = 0; i < block_rows; ++i)
        n += selected(s, i) && v[i] == x;
    return n;
}

// Column aggregates under a coarse block mask and a row plane of four words per
// block (rows.size() >= 4 * blocks). The coarse mask is tested before any
// descriptor; an empty block selection before the descriptor, body or
// dictionary; a full one takes the existing full-256 operation.
inline u128 masked_encoded_sum(const column &c, const block_mask &mask, std::span<const u64> rows,
                               dictionary_cache *cache = nullptr) {
    u128 total = 0;
    code_sum codes(cache);
    for_each_block(mask, [&](std::size_t b) {
        const row_selection s = selection_at(rows, b);
        if (selection_empty(s))
            return;
        const auto k = borrow(c, b);
        if (selection_full(s))
            total += k.coded ? codes(k) : stored_sum(k);
        else
            total += k.coded ? codes.masked(k, s) : masked_stored_sum_mixed(k, s);
    });
    return total;
}
inline u64 masked_encoded_count_ge(const column &c, const block_mask &mask, std::span<const u64> rows, u64 t,
                                   dictionary_cache *cache = nullptr) {
    u64 total = 0;
    code_count codes(t, cache);
    for_each_block(mask, [&](std::size_t b) {
        const row_selection s = selection_at(rows, b);
        if (selection_empty(s))
            return;
        const auto k = borrow(c, b);
        if (selection_full(s))
            total += k.coded ? codes.count(k) : stored_count_ge(k, t);
        else
            total += k.coded ? codes.masked(k, s) : masked_stored_count_ge_mixed(k, s, t);
    });
    return total;
}
inline u64 masked_encoded_count_eq(const column &c, const block_mask &mask, std::span<const u64> rows, u64 v,
                                   dictionary_cache *cache = nullptr) {
    u64 total = 0;
    code_count_eq codes(v, cache);
    for_each_block(mask, [&](std::size_t b) {
        const row_selection s = selection_at(rows, b);
        if (selection_empty(s))
            return;
        const auto k = borrow(c, b);
        if (selection_full(s))
            total += k.coded ? codes.count(k) : stored_count_eq(k, v);
        else
            total += k.coded ? codes.masked(k, s) : masked_stored_count_eq_mixed(k, s, v);
    });
    return total;
}
// |S| over the mask: the row plane alone, no column access.
inline u64 masked_selected_rows(const block_mask &mask, std::span<const u64> rows) {
    u64 total = 0;
    for_each_block(mask, [&](std::size_t b) { total += selected_count(selection_at(rows, b)); });
    return total;
}
// The decode-then-mask control over a column (uncached dictionary lookups).
inline u128 reference_masked_sum(const column &c, const block_mask &mask, std::span<const u64> rows) {
    u128 total = 0;
    for_each_block(mask, [&](std::size_t b) {
        const row_selection s = selection_at(rows, b);
        if (!selection_empty(s))
            total += reference_block_sum(borrow(c, b), s);
    });
    return total;
}
inline u64 reference_masked_count_ge(const column &c, const block_mask &mask, std::span<const u64> rows, u64 t) {
    u64 total = 0;
    for_each_block(mask, [&](std::size_t b) {
        const row_selection s = selection_at(rows, b);
        if (!selection_empty(s))
            total += reference_block_count_ge(borrow(c, b), s, t);
    });
    return total;
}
}  // namespace frame_encoding
