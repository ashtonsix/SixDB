// Predicate masks over borrowed blocks: the original-row selection of a value
// predicate on column A (x >= t, x < t, t1 <= x < t2, x == v, x != v), as one
// 256-bit row_selection per block, for the row-selected aggregates on column B
// (selected.h) to consume. Included at the end of material.h.
//
// Four inputs stay distinct: the coarse block mask (a block it skips is never
// visited: no descriptor, body or dictionary access, and its output words are
// left exactly as the caller supplied them), an optional fine input selection,
// an optional validity plane for A, and the predicate. The emitted mask is
// predicate AND selection AND validity(A); B's validity is the consumer's, to
// AND into the plane before its aggregate. A visited block whose selection AND
// validity is empty is written 0 before its descriptor or body is read.
//
// Every predicate is x in [lo, last] (inclusive), or its complement (x != v),
// or empty (x < 0, t1 >= t2), so one interval law per kind serves all five:
//   constant       one compare: all active rows or none.
//   runs           one compare per run value (exact-tail run plane), each
//                  passing run filled as a word-level row range.
//   packed         residual interval [lo - base, last - base], clipped to
//                  [0, 2^K): the plane's SeriesPack filters over 64-row windows
//                  with the active word as their active mask (one filter for
//                  >=, ==, and x < t as the complement of >= under active; two
//                  for a range). Bounds [base, base + 2^K - 1] decide whole
//                  blocks without reading the plane.
//   patched        the packed law over inliers (active AND NOT exceptions),
//                  plus each active exception's lo + v_e compared and set.
//   delta          rbase >= 0 with checkpoints: the block is non-decreasing
//                  (the trusted-image premise of monotone_delta_first_ge), so
//                  rows [first_ge(lo), first_ge(last + 1)) hold the interval,
//                  one checkpoint search per bound (none for lo = 0 or
//                  last = UINT64_MAX). Otherwise the prefix is reconstructed
//                  through the last active row and compared.
//   delta_patched, arrival   decode, then compare (no law applies).
// Coded blocks translate the predicate once per dictionary identity (lazily,
// on the first coded block visited with it) by code_translation (material.h),
// the duplicate-safe law COUNT(x == v) shares, so codes, not values, are tested:
//   sorted         the plane must be non-decreasing in code order (strictly
//                  increasing for encoded parts; equal neighbours are allowed,
//                  as in a projected view with duplicate values). Then value
//                  in [lo, last] iff code in [lb(lo), lb(last + 1)), with
//                  lb(t) = lower_bound(D, t - min), so equality is a code
//                  range too and never assumes one code per value. The code
//                  interval runs through the stored-kind law above.
//   otherwise      a truth table of one bit per code, T[k] = pred(min + D[k]),
//                  built from the whole plane (or a cache's tables), so
//                  duplicate values each get their bit. Constant: one bit;
//                  runs: one bit per run; arrival: literals by rank, references
//                  in row order; other kinds decode codes and look up per row.
// A translation that admits every code, or none, answers each coded block of
// that identity without reading its body. With a dictionary_cache, sorted
// bounds and unordered tables come from its per-identity material, keyed by
// comparison and value (eq v for x == v and x != v; T_lo AND NOT T_{last+1}
// from ge material otherwise), and persist across calls.
// Grants: every kind stays within extent_of(...).scan_grant_bytes except a
// non-decreasing delta block with checkpoints, whose bound search reads its
// checkpoints (read_grant_bytes), as monotone COUNT does.
#pragma once

namespace frame_encoding {

// Width-indexed plane filters (predicate.cpp) over one full 256-row plane, by
// 64-row word: out[w] holds the rows of active[w] whose residual is >= rhs,
// == rhs, or in [lo, end). A word with no active row reads nothing and is 0.
using plane_filter_fn = void (*)(const std::uint8_t *, const u64 *active, u64 rhs, u64 *out);
using plane_between_fn = void (*)(const std::uint8_t *, const u64 *active, u64 lo, u64 end, u64 *out);
struct plane_filter_catalogue_t {
    std::array<plane_filter_fn, 65> ge, eq;
    std::array<plane_between_fn, 65> between;
};
extern const plane_filter_catalogue_t plane_filters;

enum class predicate_op : std::uint8_t { ge, lt, between, eq, ne };
inline const char *predicate_name(predicate_op op) {
    switch (op) {
    case predicate_op::ge: return "ge";
    case predicate_op::lt: return "lt";
    case predicate_op::between: return "between";
    case predicate_op::eq: return "eq";
    case predicate_op::ne: return "ne";
    }
    return "?";
}

// ge: x >= a; lt: x < a; between: a <= x < b; eq: x == a; ne: x != a.
struct value_predicate {
    predicate_op op;
    u64 a = 0, b = 0;
};

// x in [lo, last], negated for ne; empty admits no value (before negation).
struct value_interval {
    u64 lo = 0, last = 0;
    bool empty = false, negate = false;
    bool contains(u64 x) const { return !empty && x - lo <= last - lo; }
};
inline value_interval interval_of(const value_predicate &p) {
    constexpr u64 top = ~u64{0};
    switch (p.op) {
    case predicate_op::ge: return {p.a, top};
    case predicate_op::lt: return p.a ? value_interval{0, p.a - 1} : value_interval{0, 0, true};
    case predicate_op::between: return p.a < p.b ? value_interval{p.a, p.b - 1} : value_interval{0, 0, true};
    case predicate_op::eq: return {p.a, p.a};
    case predicate_op::ne: return {p.a, p.a, false, true};
    }
    std::abort();
}

// Rows [first, limit) as a selection, by word.
inline row_selection rows_between(unsigned first, unsigned limit) {
    row_selection m{};
    for (unsigned w = 0; w < 4; ++w) {
        const unsigned lo = w * 64, f = std::max(first, lo), l = std::min(limit, lo + 64);
        if (f < l)
            m[w] = (l - f == 64 ? ~u64{0} : (u64{1} << (l - f)) - 1) << (f - lo);
    }
    return m;
}
inline row_selection and_rows(const row_selection &a, const row_selection &b) {
    return {a[0] & b[0], a[1] & b[1], a[2] & b[2], a[3] & b[3]};
}

// Rows of a among the first n decoded values with v in [lo, last].
inline row_selection buffer_rows(const u64 *v, unsigned n, u64 lo, u64 last, const row_selection &a) {
    const u64 span = last - lo;
    row_selection r{};
    for (unsigned w = 0; w * 64 < n; ++w) {
        if (!a[w])
            continue;
        const unsigned m = std::min(64u, n - w * 64);
        u64 bits = 0;
        for (unsigned i = 0; i < m; ++i)
            bits |= static_cast<u64>(v[w * 64 + i] - lo <= span) << i;
        r[w] = bits & a[w];
    }
    return r;
}

// Rows of a whose plane value base + r has r in a full 256-row plane at the
// block's width, for stored values in [lo, last] (lo <= last): bounds decide
// the whole plane where they can, else one or two filters.
inline row_selection plane_interval_rows(const std::uint8_t *plane, unsigned width, u64 base, u64 lo, u64 last,
                                         const row_selection &a) {
    const u64 top = width >= 64 ? ~u64{0} : (u64{1} << width) - 1;
    const u64 max = base + top < base ? ~u64{0} : base + top;
    if (last < base || lo > max)
        return {};
    if (lo <= base && last >= max)
        return a;
    const u64 rlo = lo <= base ? 0 : lo - base, rlast = std::min(last - base, top);
    row_selection r;
    if (rlo == rlast) {
        plane_filters.eq[width](plane, a.data(), rlo, r.data());
    } else if (rlast == top) {
        plane_filters.ge[width](plane, a.data(), rlo, r.data());
    } else if (rlo == 0) {  // x < last + 1: the complement of >= under active
        plane_filters.ge[width](plane, a.data(), rlast + 1, r.data());
        r = and_not(a, r);
    } else {
        plane_filters.between[width](plane, a.data(), rlo, rlast + 1, r.data());
    }
    return r;
}

// Rows of a (non-empty) whose stored value (value, or code if coded) lies in
// [lo, last] (lo <= last), by the per-kind laws above.
inline row_selection stored_interval_rows(const borrowed_block &k, u64 lo, u64 last, const row_selection &a) {
    const auto &d = k.d;
    const value_interval in{lo, last};
    alignas(64) u64 buffer[block_rows];
    switch (d.type) {
    case kind::constant:
        return in.contains(d.base) ? a : row_selection{};
    case kind::packed:
        return plane_interval_rows(k.body, d.width, d.base, lo, last, a);
    case kind::runs: {
        const u64 top = d.width >= 64 ? ~u64{0} : (u64{1} << d.width) - 1;
        const u64 max = d.base + top < d.base ? ~u64{0} : d.base + top;
        if (last < d.base || lo > max)
            return {};
        if (lo <= d.base && last >= max)
            return a;
        read_plane(k, 0, d.count, buffer);
        row_selection r{};
        unsigned row = 0;
        each_run(k, [&](unsigned rank, unsigned size) {
            if (in.contains(d.base + buffer[rank])) {
                const row_selection run = rows_between(row, row + size);
                for (unsigned w = 0; w < 4; ++w)
                    r[w] |= run[w];
            }
            row += size;
        });
        return and_rows(r, a);
    }
    case kind::patched: {
        const auto p = patches(k);
        const row_selection exceptions = exception_rows(p);
        const row_selection inliers = and_not(a, exceptions);
        row_selection r{};
        if (!selection_empty(inliers))
            r = plane_interval_rows(k.body, d.width, d.base, lo, last, inliers);
        const row_selection hit = and_rows(a, exceptions);
        if (!selection_empty(hit)) {
            read_exceptions(p, buffer);
            for (unsigned e = 0; e < p.n; ++e) {
                const unsigned row = p.positions[e];
                if (selected(hit, row) && in.contains(p.lo + buffer[e]))
                    r[row / 64] |= u64{1} << (row % 64);
            }
        }
        return r;
    }
    case kind::delta: {
        const i64 rbase = delta_rbase(k);
        if (rbase >= 0 && k.delta_checkpoints != checkpoints::none) {
            const unsigned first = lo ? monotone_delta_first_ge(k, rbase, lo) : 0;
            if (first >= block_rows)
                return {};
            const unsigned limit = last == ~u64{0} ? block_rows : monotone_delta_first_ge(k, rbase, last + 1);
            return and_rows(rows_between(first, limit), a);
        }
        // Reconstruct through the last active row; a non-decreasing block
        // stops at the first row past the interval.
        const unsigned take = last_selected(a) + 1;
        read_plane(k, 0, take, buffer);
        u64 x = d.base;
        buffer[0] = x;
        unsigned n = take;
        for (unsigned i = 1; i < take; ++i) {
            x += static_cast<u64>(rbase) + buffer[i];
            buffer[i] = x;
            if (rbase >= 0 && x > last) {
                n = i + 1;
                break;
            }
        }
        return buffer_rows(buffer, n, lo, last, a);
    }
    case kind::delta_patched:
    case kind::arrival:
        decode_stored(k, buffer);
        return buffer_rows(buffer, block_rows, lo, last, a);
    }
    std::abort();
}

// The predicate evaluator for one call: holds the predicate's interval and the
// current dictionary identity's translation. Touches nothing until a block is
// visited, and no dictionary until a coded block with a non-empty active
// selection is.
class predicate_mask {
public:
    explicit predicate_mask(const value_predicate &p, dictionary_cache *cache = nullptr)
        : interval(interval_of(p)), cache(cache) {}
    value_interval interval;

    // Rows of active whose logical value satisfies the predicate.
    row_selection operator()(const borrowed_block &k, const row_selection &active) {
        if (selection_empty(active))
            return {};
        if (interval.empty)
            return interval.negate ? active : row_selection{};
        if (!k.coded)
            return finish(stored_interval_rows(k, interval.lo, interval.last, active), active);
        prepare(k.dictionary);
        switch (match.law) {
        case code_law::none: return {};
        case code_law::all: return active;
        case code_law::codes: return finish(stored_interval_rows(k, match.lo, match.last, active), active);
        case code_law::truth: return truth_rows(k, active);
        }
        std::abort();
    }

    // Lower bound of t's code under a sorted identity: the first code whose
    // value reaches t (count if none).
    u64 sorted_bound(const dictionary_view &p, u64 t) {
        return cache ? cache->threshold(p, t).code_threshold : dictionary_lower_bound(p, t);
    }

private:
    dictionary_cache *cache;
    bool ready = false;
    dictionary_identity edition{};
    code_translation match;  // the current identity's law (the shared duplicate-safe one)

    row_selection finish(const row_selection &in, const row_selection &active) const {
        return interval.negate ? and_not(active, in) : in;
    }
    bool holds(u64 code) const { return match.holds(code); }

    void prepare(const dictionary_view &p) {
        if (ready && p.edition == edition)
            return;
        ready = true;
        edition = p.edition;
        match.build(p, interval.lo, interval.last, interval.negate, cache);
    }

    // Unordered coded block: one truth bit per code (complement folded in).
    row_selection truth_rows(const borrowed_block &k, const row_selection &a) const {
        const auto &d = k.d;
        alignas(64) u64 buffer[block_rows];
        row_selection r{};
        switch (d.type) {
        case kind::constant:
            return holds(d.base) ? a : row_selection{};
        case kind::runs: {
            read_plane(k, 0, d.count, buffer);
            unsigned row = 0;
            each_run(k, [&](unsigned rank, unsigned size) {
                if (holds(d.base + buffer[rank])) {
                    const row_selection run = rows_between(row, row + size);
                    for (unsigned w = 0; w < 4; ++w)
                        r[w] |= run[w];
                }
                row += size;
            });
            return and_rows(r, a);
        }
        case kind::arrival: {
            const auto w = arrival_flags(k);
            each_selected_literal(w, a, [&](unsigned row, unsigned rank) {
                r[row / 64] |= static_cast<u64>(holds(d.base + rank)) << (row % 64);
            });
            each_arrival_reference(k, w, [&](unsigned row, u64 code) {
                r[row / 64] |= static_cast<u64>(holds(code)) << (row % 64);
            });
            return and_rows(r, a);
        }
        default:
            decode_stored(k, buffer);
            for (unsigned w = 0; w < 4; ++w) {
                if (!a[w])
                    continue;
                u64 bits = 0;
                for (unsigned i = 0; i < 64; ++i)
                    bits |= static_cast<u64>(holds(buffer[w * 64 + i])) << i;
                r[w] = bits & a[w];
            }
            return r;
        }
    }
};

// One block's predicate rows from a fresh evaluator (a lone coded block
// translates per call).
inline row_selection block_predicate_rows(const borrowed_block &k, const value_predicate &p,
                                          const row_selection &active) {
    predicate_mask m(p);
    return m(k, active);
}

// Decode-then-compare control: the whole block's logical values, then the
// interval test per row. Same empty dispatch, no laws.
inline row_selection reference_predicate_rows(const borrowed_block &k, const value_interval &in,
                                              const row_selection &active) {
    if (selection_empty(active))
        return {};
    if (in.empty)
        return in.negate ? active : row_selection{};
    alignas(64) u64 v[block_rows];
    decode_block(k, v);
    const row_selection r = buffer_rows(v, block_rows, in.lo, in.last, active);
    return in.negate ? and_not(active, r) : r;
}

// The fine inputs of an emission: four words per block each, or empty for
// "every row". They are ANDed into the active selection; neither is the
// coarse mask.
struct predicate_inputs {
    std::span<const u64> selection;  // the caller's input fine selection
    std::span<const u64> validity;   // A's validity (1 = valid)
};
inline row_selection active_rows(const predicate_inputs &in, std::size_t b) {
    row_selection a{~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}};
    if (!in.selection.empty())
        a = and_rows(a, selection_at(in.selection, b));
    if (!in.validity.empty())
        a = and_rows(a, selection_at(in.validity, b));
    return a;
}

// Emit the predicate's mask into out (four words per block) for every block of
// coarse, over blocks from borrow_at(b) (called only for a visited block with a
// non-empty active selection). Blocks outside coarse keep their out words (and
// counts) as supplied. counts, when non-empty, takes each visited block's
// |mask|. Returns the total selected rows. eval holds the dictionary
// translation, so one evaluator can serve several calls under the identity
// premise.
template <class Borrow>
u64 emit_predicate_masks(const block_mask &coarse, Borrow &&borrow_at, predicate_mask &eval, std::span<u64> out,
                         const predicate_inputs &in = {}, std::span<std::uint16_t> counts = {}) {
    u64 total = 0;
    for_each_block(coarse, [&](std::size_t b) {
        const row_selection a = active_rows(in, b);
        const row_selection r = selection_empty(a) ? row_selection{} : eval(borrow_at(b), a);
        std::memcpy(out.data() + 4 * b, r.data(), sizeof r);
        const unsigned n = selected_count(r);
        if (!counts.empty())
            counts[b] = static_cast<std::uint16_t>(n);
        total += n;
    });
    return total;
}

// Column form: blocks borrowed from the image, dictionary identities
// {image, part}.
inline u64 predicate_masks(const column &c, const block_mask &coarse, const value_predicate &p, std::span<u64> out,
                           const predicate_inputs &in = {}, dictionary_cache *cache = nullptr,
                           std::span<std::uint16_t> counts = {}) {
    predicate_mask eval(p, cache);
    return emit_predicate_masks(coarse, [&](std::size_t b) { return borrow(c, b); }, eval, out, in, counts);
}

// The decode-then-compare control over a column.
inline u64 reference_predicate_masks(const column &c, const block_mask &coarse, const value_predicate &p,
                                     std::span<u64> out, const predicate_inputs &in = {}) {
    const value_interval iv = interval_of(p);
    u64 total = 0;
    for_each_block(coarse, [&](std::size_t b) {
        const row_selection a = active_rows(in, b);
        const row_selection r = selection_empty(a) ? row_selection{} : reference_predicate_rows(borrow(c, b), iv, a);
        std::memcpy(out.data() + 4 * b, r.data(), sizeof r);
        total += selected_count(r);
    });
    return total;
}
}  // namespace frame_encoding
