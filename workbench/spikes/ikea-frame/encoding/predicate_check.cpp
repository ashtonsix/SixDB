// Predicate-mask correctness for the private Frame material (predicate.h).
// Every emitted mask is compared, complete, with a mask computed here from the
// input values by the predicate's own comparison (never through the owner's
// interval or dictionary laws), restricted to the active rows.
//   blocks   every block, its body ending at a PROT_NONE page (delta bodies
//            outside the monotone law also with only their scan grant), and a
//            coded block's dictionary plane alone ending at another; random and
//            adversarial thresholds per block (0, UINT64_MAX, below the block
//            minimum, above its maximum, run and row values and their
//            neighbours, rows either side of each checkpoint spacing, the
//            descriptor base and its first unrepresentable residual), every
//            predicate, adversarial and random active selections; per-call,
//            cached and decode-then-compare forms. An empty active selection is
//            evaluated over a body and dictionary inside PROT_NONE.
//   columns  coarse masks (all, sparse, none) x fine inputs (none, a mixed row
//            plane, a validity plane, both) x column-level predicates. Output
//            words are pre-filled with a sentinel: skipped blocks must keep it.
//            The block source used by the emitter fails if asked for a coarse-
//            skipped block or one whose active rows are empty, so neither's
//            descriptor, body or dictionary is reached.
//   editions old, mirrored new, old, an unchanged copy, new through one shared
//            dictionary_cache, and duplicate-valued projected dictionary views
//            built by hand (non-decreasing with equal neighbours labelled
//            sorted; arbitrary duplicates labelled frequency).
//   pairs    --with=B.u64: masks on A (with A's validity) feed masked SUM,
//            COUNT(>= t) and COUNT(== v) on B under B's validity, against the
//            input oracle.
#include "material.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include <sys/mman.h>
#include <unistd.h>

namespace {
using namespace frame_encoding;

[[noreturn]] void fail(const char *what, std::size_t b, const char *detail = "") {
    std::fprintf(stderr, "predicate check: %s at block %zu %s\n", what, b, detail);
    std::exit(1);
}

// [PROT_NONE page][n readable pages][PROT_NONE page], as in borrowed_check.
struct guarded {
    std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)), n;
    std::uint8_t *base;
    explicit guarded(std::size_t bytes) : n((bytes + page - 1) / page + 1) {
        void *m = ::mmap(nullptr, (n + 2) * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED)
            fail("mmap", 0);
        base = static_cast<std::uint8_t *>(m);
        if (::mprotect(base, page, PROT_NONE) || ::mprotect(base + (n + 1) * page, page, PROT_NONE))
            fail("mprotect", 0);
    }
    guarded(const guarded &) = delete;
    ~guarded() { ::munmap(base, (n + 2) * page); }
    std::uint8_t *lo() const { return base + page; }
    std::uint8_t *hi() const { return base + (n + 1) * page; }
    std::uint8_t *forbidden() const { return base; }  // inside the low guard
};

u64 mix(u64 x) {
    x += 0x9e3779b97f4a7c15u;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9u;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebu;
    return x ^ (x >> 31);
}
// Each row with probability per_mille / 1000.
row_selection random_selection(u64 seed, unsigned per_mille) {
    row_selection s{};
    for (unsigned i = 0; i < block_rows; ++i)
        if (mix(seed * 1000003u + i) % 1000 < per_mille)
            s[i / 64] |= u64{1} << (i % 64);
    return s;
}
row_selection rows_of(std::initializer_list<unsigned> rows) {
    row_selection s{};
    for (unsigned r : rows)
        s[r / 64] |= u64{1} << (r % 64);
    return s;
}
constexpr row_selection every_row{~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}};

// The oracle law, by the predicate's own comparison.
bool holds(const value_predicate &p, u64 x) {
    switch (p.op) {
    case predicate_op::ge: return x >= p.a;
    case predicate_op::lt: return x < p.a;
    case predicate_op::between: return p.a <= x && x < p.b;
    case predicate_op::eq: return x == p.a;
    case predicate_op::ne: return x != p.a;
    }
    std::abort();
}
row_selection oracle(const u64 *v, const value_predicate &p, const row_selection &active) {
    row_selection r{};
    for (unsigned i = 0; i < block_rows; ++i)
        if (selected(active, i) && holds(p, v[i]))
            r[i / 64] |= u64{1} << (i % 64);
    return r;
}

// Codes per row as the oracle derives them from the input (borrowed_check's
// laws): sorted rank, frequency rank (ties by ascending value), or first
// occurrence within the part.
std::vector<u64> stored_oracle(const column &c, std::span<const u64> values) {
    std::vector<u64> stored(values.begin(), values.begin() + c.rows);
    if (c.options.dictionary == dictionary_order::none)
        return stored;
    const std::size_t nblocks = c.blocks.size();
    for (std::size_t first = 0; first < nblocks; first += part_blocks) {
        const std::size_t limit = std::min(first + part_blocks, nblocks);
        std::map<u64, std::uint32_t> frequency;
        for (std::size_t i = first * block_rows; i < limit * block_rows; ++i)
            ++frequency[values[i]];
        std::vector<std::pair<u64, std::uint32_t>> order(frequency.begin(), frequency.end());
        if (c.options.dictionary == dictionary_order::frequency)
            std::stable_sort(order.begin(), order.end(), [](auto a, auto b) { return a.second > b.second; });
        std::map<u64, u64> code;
        if (c.options.dictionary == dictionary_order::arrival) {
            for (std::size_t i = first * block_rows; i < limit * block_rows; ++i)
                code.emplace(values[i], code.size());
        } else {
            for (std::size_t r = 0; r < order.size(); ++r)
                code[order[r].first] = r;
        }
        for (std::size_t b = first; b < limit; ++b)
            if (is_coded(c, b))
                for (std::size_t i = b * block_rows; i < (b + 1) * block_rows; ++i)
                    stored[i] = code[values[i]];
    }
    return stored;
}

std::vector<value_predicate> predicates_at(std::span<const u64> thresholds, u64 seed) {
    std::vector<value_predicate> out;
    for (u64 t : thresholds)
        for (auto op : {predicate_op::ge, predicate_op::lt, predicate_op::eq, predicate_op::ne})
            out.push_back({op, t});
    const std::size_t n = thresholds.size();
    for (std::size_t i = 0; i < n; ++i) {
        const u64 t = thresholds[i], u = thresholds[mix(seed + i) % n];
        out.push_back({predicate_op::between, t, u});  // either order: t >= u is empty
        out.push_back({predicate_op::between, t, t});
        out.push_back({predicate_op::between, t, t + 1});
    }
    return out;
}

struct tally {
    std::size_t block = 0, cached = 0, reference = 0, scan_grant = 0, empty_guard = 0, column = 0, skipped_words = 0,
                edition = 0, projected = 0, pair = 0;
};

void load(const char *path, std::vector<u64> &values) {
    std::FILE *f = std::fopen(path, "rb");
    if (!f)
        std::exit(2);
    std::fseek(f, 0, SEEK_END);
    const long end = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (end <= 0 || end % 8 || (end / 8) % block_rows)
        std::exit(2);
    values.resize(static_cast<std::size_t>(end) / 8);
    if (std::fread(values.data(), 8, values.size(), f) != values.size())
        std::exit(2);
    std::fclose(f);
}

// A row plane mixing empty, full, sparse, dense and adversarial blocks.
std::vector<u64> mixed_plane(std::size_t nblocks, u64 seed) {
    std::vector<u64> plane(4 * nblocks);
    for (std::size_t b = 0; b < nblocks; ++b) {
        row_selection s{};
        switch (mix(seed + b) % 7) {
        case 0: break;
        case 1: s = every_row; break;
        case 2: s = random_selection(seed + b, 10); break;
        case 3: s = random_selection(seed + b, 500); break;
        case 4: s = random_selection(seed + b, 990); break;
        case 5: s = rows_of({0, 15, 16, 17, 63, 64, 255}); break;
        default: s = rows_between(100, 256); break;
        }
        std::memcpy(plane.data() + 4 * b, s.data(), sizeof s);
    }
    return plane;
}
// Validity: about 3% NULL, with every 13th block entirely NULL and every 11th
// entirely valid.
std::vector<u64> validity_plane(std::size_t nblocks, u64 seed) {
    std::vector<u64> plane(4 * nblocks);
    for (std::size_t b = 0; b < nblocks; ++b) {
        const row_selection s = b % 13 == 0   ? row_selection{}
                                : b % 11 == 0 ? every_row
                                              : and_not(every_row, random_selection(seed + b, 30));
        std::memcpy(plane.data() + 4 * b, s.data(), sizeof s);
    }
    return plane;
}

// Column-level thresholds: extremes, quantiles, the most frequent value and an
// absent one.
std::vector<u64> column_thresholds(std::span<const u64> values) {
    std::vector<u64> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const u64 lo = sorted.front(), hi = sorted.back();
    std::vector<u64> t{0, ~u64{0}, lo, hi, lo - 1, hi + 1};
    for (unsigned q : {1, 10, 25, 50, 75, 90, 99})
        t.push_back(sorted[sorted.size() * q / 100]);
    u64 best = lo, run = 0, best_run = 0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        run = i && sorted[i] == sorted[i - 1] ? run + 1 : 1;
        if (run > best_run)
            best_run = run, best = sorted[i];
    }
    t.push_back(best);
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i] > sorted[i - 1] + 1) {
            t.push_back(sorted[i - 1] + 1);  // absent, inside the range
            break;
        }
    return t;
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s NAME FILE.u64 [--checkpoints=native|packed|every64|none] [--delta-exceptions] "
                     "[--dictionary=none|sorted|frequency|arrival] [--with=B.u64]\n",
                     argv[0]);
        return 2;
    }
    encode_options options;
    options.bind_decoders = false;
    const char *with = nullptr;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--delta-exceptions") options.delta_exceptions = true;
        else if (a == "--dictionary=none") options.dictionary = dictionary_order::none;
        else if (a == "--dictionary=sorted") options.dictionary = dictionary_order::sorted;
        else if (a == "--dictionary=frequency") options.dictionary = dictionary_order::frequency;
        else if (a == "--dictionary=arrival") options.dictionary = dictionary_order::arrival;
        else if (a == "--checkpoints=native") options.delta_checkpoints = checkpoints::native;
        else if (a == "--checkpoints=packed") options.delta_checkpoints = checkpoints::packed;
        else if (a == "--checkpoints=every64") options.delta_checkpoints = checkpoints::every64;
        else if (a == "--checkpoints=none") options.delta_checkpoints = checkpoints::none;
        else if (a == "--run-values=byte_lanes") options.run_values = encode_options::run_width::byte_lanes;
        else if (a == "--packed-byte-lanes") options.packed_byte_lanes = true;
        else if (a == "--run-program=wide") run_values_program = run_program::wide;
        else if (a == "--run-program=narrow") run_values_program = run_program::narrow;
        else if (a == "--run-program=in_place") run_values_program = run_program::in_place;
        else if (a.rfind("--with=", 0) == 0) with = argv[i] + 7;
        else return 2;
    }
    std::vector<u64> values;
    load(argv[2], values);
    const column c = encode(values, options);
    const std::size_t nblocks = c.blocks.size();
    const checkpoints mode = c.options.delta_checkpoints;
    tally n;
    std::map<std::string, std::size_t> kinds;
    dictionary_cache shared;

    // Blocks, each body ending at a guard page and its dictionary at another.
    std::size_t max_body = 1, max_dictionary = 1;
    for (std::size_t b = 0; b < nblocks; ++b)
        max_body = std::max(max_body, extent_of(c.blocks[b], mode, c.payload.data() + c.blocks[b].offset,
                                                c.payload.size() - c.blocks[b].offset)
                                          ->read_grant_bytes);
    for (const auto &p : c.parts)
        max_dictionary = std::max(max_dictionary, p.count ? plane_bytes(p.width, p.count) : 0);
    guarded body_page(max_body), scan_page(max_body), dictionary_page(max_dictionary), none_page(1);
    std::size_t placed_part = SIZE_MAX, part_ready = SIZE_MAX;
    std::vector<value_predicate> part_preds;
    std::vector<predicate_mask> part_cold, part_warm;
    const std::uint8_t *placed_plane = nullptr;
    const row_selection selections[] = {
        rows_of({0}),         rows_of({255}),          rows_of({1, 254}),
        rows_of({15, 16, 17, 63, 64, 65, 127, 128, 191, 192, 240}),
        random_selection(1, 1), random_selection(2, 100), random_selection(3, 500), random_selection(4, 990),
        and_not(every_row, rows_of({0})), and_not(every_row, rows_of({255})), rows_between(0, 100),
        rows_between(200, 256), {0x5555555555555555u, 0x5555555555555555u, 0x5555555555555555u, 0x5555555555555555u}};
    constexpr std::size_t nsel = std::size(selections);
    for (std::size_t b = 0; b < nblocks; ++b) {
        const auto &d = c.blocks[b];
        borrowed_block k = borrow(c, b);
        const auto x = *extent_of(d, mode, k.body, k.grant);
        const char *kind_names[] = {"constant", "runs", "packed", "delta", "patched", "delta_patched", "arrival"};
        ++kinds[std::string(k.coded ? "coded_" : "") + kind_names[static_cast<unsigned>(d.type)]];
        // Place the body ending at the guard.
        std::uint8_t *at = body_page.hi() - x.read_grant_bytes;
        std::memcpy(at, k.body, x.read_grant_bytes);
        k.body = at, k.grant = x.read_grant_bytes;
        if (k.coded) {
            const std::size_t part = b / part_blocks;
            if (part != placed_part) {
                const auto bytes = k.dictionary.bytes();
                std::uint8_t *p = dictionary_page.hi() - bytes;
                std::memcpy(p, k.dictionary.plane, bytes);
                placed_part = part, placed_plane = p;
            }
            k.dictionary.plane = placed_plane, k.dictionary.grant = k.dictionary.bytes();
        }
        if (!admits(k))
            fail("admits", b);
        // The scan-grant copy: delta bodies outside the monotone law don't
        // read their checkpoints.
        std::optional<borrowed_block> scan;
        if (d.type == kind::delta && (delta_rbase(k) < 0 || mode == checkpoints::none)) {
            std::uint8_t *s = scan_page.hi() - x.scan_grant_bytes;
            std::memcpy(s, k.body, x.scan_grant_bytes);
            scan = k;
            scan->body = s, scan->grant = x.scan_grant_bytes;
        }
        const u64 *v = values.data() + b * block_rows;
        u64 lo = ~u64{0}, hi = 0;
        for (unsigned i = 0; i < block_rows; ++i)
            lo = std::min(lo, v[i]), hi = std::max(hi, v[i]);
        // A runs or packed descriptor's bound is the block's exact width (base
        // is the minimum), on byte-lane images too.
        if (!k.coded && (d.type == kind::runs || d.type == kind::packed) &&
            (d.base != lo || bound_width(k) != width_of(hi - lo)))
            fail("bound width is not the block's exact width", b);
        std::vector<u64> t{0, ~u64{0}, lo, hi, lo - 1, hi + 1, d.base, d.base - 1};
        if (const unsigned w = bound_width(k); w && w < 64)
            t.push_back(d.base + (u64{1} << w));  // the first unrepresentable residual
        if (k.exact_width)  // a byte-lane plane: the first residual its physical width can't hold
            t.push_back(d.base + (u64{1} << k.d.width) - 1), t.push_back(d.base + (u64{1} << k.d.width));
        for (unsigned r : {0u, 1u, 15u, 16u, 17u, 63u, 64u, 65u, 127u, 128u, 239u, 240u, 241u, 255u})
            t.push_back(v[r]), t.push_back(v[r] + 1);
        t.push_back(v[mix(b) % block_rows]);
        t.push_back(mix(b + 7));
        t.push_back(lo + (hi - lo) / 2);
        std::sort(t.begin(), t.end());
        t.erase(std::unique(t.begin(), t.end()), t.end());
        // A large dictionary is translated once per part per predicate: part
        // thresholds with one per-call and one cached evaluator each, reused
        // over the part's blocks. Other blocks take block thresholds with a
        // fresh evaluator per mask.
        const bool by_part = k.coded && k.dictionary.count > 256;
        if (by_part && part_ready != b / part_blocks) {
            part_ready = b / part_blocks;
            const std::size_t first = part_ready * part_blocks * block_rows;
            const std::size_t limit = std::min(first + part_rows, values.size());
            std::vector<u64> pv(values.begin() + first, values.begin() + limit);
            std::sort(pv.begin(), pv.end());
            std::vector<u64> pt{0, ~u64{0}, pv.front(), pv.back(), pv.front() - 1, pv.back() + 1};
            for (unsigned q = 1; q < 20; ++q)
                pt.push_back(pv[pv.size() * q / 20]), pt.push_back(pv[pv.size() * q / 20] + 1);
            for (std::size_t i = first; i < limit; i += 32 * block_rows)
                for (unsigned r : {0u, 16u, 64u, 255u})
                    pt.push_back(values[i + r]);
            pt.push_back(mix(b + 3));
            std::sort(pt.begin(), pt.end());
            pt.erase(std::unique(pt.begin(), pt.end()), pt.end());
            part_preds = predicates_at(pt, b);
            part_cold.clear(), part_warm.clear();
            for (const auto &p : part_preds)
                part_cold.emplace_back(p), part_warm.emplace_back(p, &shared);
        }
        const auto preds = by_part ? part_preds : predicates_at(t, b);
        for (std::size_t j = 0; j < preds.size(); ++j) {
            const auto &p = preds[j];
            for (const row_selection *s : {&every_row, &selections[(j + b) % nsel]}) {
                const row_selection want = oracle(v, p, *s);
                predicate_mask fresh(p), fresh_cached(p, &shared);
                predicate_mask &cold = by_part ? part_cold[j] : fresh;
                predicate_mask &warm = by_part ? part_warm[j] : fresh_cached;
                if (cold(k, *s) != want)
                    fail("block mask", b, predicate_name(p.op));
                if (warm(k, *s) != want)
                    fail("cached block mask", b, predicate_name(p.op));
                if (reference_predicate_rows(k, interval_of(p), *s) != want)
                    fail("reference mask", b, predicate_name(p.op));
                n.block += 1, n.cached += 1, n.reference += 1;
                if (scan) {
                    predicate_mask scan_fresh(p);
                    if ((by_part ? part_cold[j] : scan_fresh)(*scan, *s) != want)
                        fail("scan-grant mask", b, predicate_name(p.op));
                    ++n.scan_grant;
                }
            }
        }
        // An empty active selection reads nothing: body and dictionary inside
        // PROT_NONE.
        if (b % 16 == 0) {
            borrowed_block none = k;
            none.body = none_page.forbidden(), none.grant = 0;
            if (none.coded)
                none.dictionary.plane = none_page.forbidden();
            for (const auto &p : predicates_at(std::span(t).first(2), b)) {
                predicate_mask cached(p, &shared);
                if (!selection_empty(block_predicate_rows(none, p, {})) || !selection_empty(cached(none, {})))
                    fail("empty selection", b);
                ++n.empty_guard;
            }
        }
    }

    // Columns: coarse masks x fine inputs x column predicates.
    const auto plane = mixed_plane(nblocks, 11);
    const auto validity = validity_plane(nblocks, 23);
    block_mask all(nblocks), sparse(nblocks), none(nblocks);
    for (std::size_t b = 0; b < nblocks; ++b) {
        all.set(b);
        if (b % 7 == 3 || mix(b + 99) % 10 == 0)
            sparse.set(b);
    }
    auto coarse_has = [](const block_mask &m, std::size_t b) { return m.words[b / 64] >> (b % 64) & 1; };
    const auto col_t = column_thresholds(values);
    auto col_preds = predicates_at(col_t, 5);
    constexpr u64 sentinel = 0xa5a5a5a5a5a5a5a5u;
    std::vector<u64> out(4 * nblocks), ref(4 * nblocks);
    std::vector<std::uint16_t> counts(nblocks);
    auto check_column = [&](const column &image, std::span<const u64> truth, dictionary_cache *cache,
                            const char *what) {
        const std::pair<const block_mask *, unsigned> combos[] = {{&all, 0}, {&all, 1}, {&all, 2}, {&all, 3},
                                                                   {&sparse, 0}, {&sparse, 3}, {&none, 3}};
        for (auto [m, inputs] : combos) {
                predicate_inputs in;
                if (inputs & 1)
                    in.selection = plane;
                if (inputs & 2)
                    in.validity = validity;
                for (std::size_t j = 0; j < col_preds.size(); ++j) {
                    const auto &p = col_preds[j];
                    std::fill(out.begin(), out.end(), sentinel);
                    std::fill(counts.begin(), counts.end(), 0xa5a5);
                    // The block source is never asked for a skipped or empty block.
                    predicate_mask eval(p, cache);
                    const u64 total = emit_predicate_masks(
                        *m,
                        [&](std::size_t b) {
                            if (!coarse_has(*m, b) || selection_empty(active_rows(in, b)))
                                fail("source asked for a skipped block", b, what);
                            return borrow(image, b);
                        },
                        eval, out, in, counts);
                    u64 want_total = 0;
                    for (std::size_t b = 0; b < nblocks; ++b) {
                        const row_selection got = selection_at(out, b);
                        if (!coarse_has(*m, b)) {
                            if (got != row_selection{sentinel, sentinel, sentinel, sentinel} || counts[b] != 0xa5a5)
                                fail("skipped block written", b, what);
                            ++n.skipped_words;
                            continue;
                        }
                        const row_selection want = oracle(truth.data() + b * block_rows, p, active_rows(in, b));
                        if (got != want || counts[b] != selected_count(want))
                            fail("column mask", b, what);
                        want_total += selected_count(want);
                    }
                    if (total != want_total)
                        fail("column total", 0, what);
                    // The column wrapper and the control agree on every visited block.
                    if (j % 3 == 0) {
                        std::fill(ref.begin(), ref.end(), sentinel);
                        if (predicate_masks(image, *m, p, ref, in, cache) != total)
                            fail("wrapper total", 0, what);
                        if (ref != out)
                            fail("wrapper mask", 0, what);
                        std::fill(ref.begin(), ref.end(), sentinel);
                        if (reference_predicate_masks(image, *m, p, ref, in) != total || ref != out)
                            fail("control mask", 0, what);
                    }
                    ++n.column;
                }
            }
    };
    check_column(c, values, nullptr, "per-call");
    check_column(c, values, &shared, "cached");

    // Editions: a mirrored image through the same cache, old/new/old/copy/new.
    {
        std::vector<u64> mirrored(values);
        const auto [mn, mx] = std::minmax_element(values.begin(), values.end());
        for (auto &y : mirrored)
            y = *mn + (*mx - y);
        const column next = encode(mirrored, options);
        const column copy = c;
        dictionary_cache editions;
        const auto saved = col_preds;
        col_preds.resize(std::min<std::size_t>(col_preds.size(), 24));
        for (const column *image : {&c, &next, &c, &copy, &next}) {
            check_column(*image, image == &next ? std::span<const u64>(mirrored) : std::span<const u64>(values),
                         &editions, "edition");
            ++n.edition;
        }
        col_preds = saved;
    }

    // Duplicate-valued projected dictionary views over the coded blocks: the
    // same codes, a hand-built plane with repeated values and a fresh identity.
    if (c.options.dictionary != dictionary_order::none && c.coded_blocks()) {
        const auto codes = stored_oracle(c, values);
        struct projection {
            dictionary_order order;
            unsigned shift, modulus;
        };
        const auto unordered = c.options.dictionary == dictionary_order::arrival ? dictionary_order::arrival
                                                                                 : dictionary_order::frequency;
        std::vector<projection> projections{{unordered, 0, 7}};  // arrival blocks need an arrival view
        if (c.options.dictionary == dictionary_order::sorted)
            projections.push_back({dictionary_order::sorted, 3, 0});  // D >> 3 stays non-decreasing
        for (std::size_t pi = 0; pi < projections.size(); ++pi) {
            const auto &pr = projections[pi];
            dictionary_cache cache;
            for (std::size_t part = 0; part < c.parts.size(); ++part) {
                const auto &dp = c.parts[part];
                if (!dp.count)
                    continue;
                std::vector<u64> entries(dp.count), projected(dp.count);
                read_dictionary(c, part, 0, dp.count, entries.data());
                u64 top = 0;
                for (std::size_t e = 0; e < dp.count; ++e) {
                    projected[e] = pr.modulus ? entries[e] % pr.modulus : entries[e] >> pr.shift;
                    top = std::max(top, projected[e]);
                }
                const unsigned width = std::max(1u, width_of(top));
                guarded page(plane_bytes(width, dp.count) + 64);
                std::uint8_t *plane_at = page.hi() - plane_bytes(width, dp.count);
                pack(width, projected, plane_at);
                const dictionary_view view{dp.min,   dp.count, static_cast<std::uint8_t>(width), pr.order, plane_at,
                                           plane_bytes(width, dp.count), {~c.image - pi, part}};
                // Values, and thresholds at duplicated entries and their neighbours.
                std::vector<u64> t{0, ~u64{0}, dp.min, dp.min - 1};
                for (std::size_t e = 0; e < dp.count; e += std::max<std::size_t>(1, dp.count / 9))
                    t.push_back(dp.min + projected[e]), t.push_back(dp.min + projected[e] + 1);
                t.push_back(dp.min + top), t.push_back(dp.min + top + 1);
                const auto preds = predicates_at(t, part);
                std::vector<predicate_mask> cold, warm;
                for (const auto &p : preds)
                    cold.emplace_back(p), warm.emplace_back(p, &cache);
                const std::size_t first = part * part_blocks, limit = std::min(first + part_blocks, nblocks);
                for (std::size_t b = first; b < limit; ++b) {
                    if (!is_coded(c, b))
                        continue;
                    borrowed_block k = borrow(c, b);
                    k.dictionary = view;
                    if (!admits(k))
                        fail("projected admits", b);
                    u64 v[block_rows];
                    for (unsigned i = 0; i < block_rows; ++i)
                        v[i] = dp.min + projected[codes[b * block_rows + i]];
                    for (std::size_t j = 0; j < preds.size(); ++j)
                        for (const row_selection *s : {&every_row, &selections[(j + b) % nsel]}) {
                            const row_selection want = oracle(v, preds[j], *s);
                            if (cold[j](k, *s) != want || warm[j](k, *s) != want ||
                                reference_predicate_rows(k, interval_of(preds[j]), *s) != want)
                                fail("projected mask", b, predicate_name(preds[j].op));
                            ++n.projected;
                        }
                }
            }
        }
    }

    // filter(A) -> SUM/COUNT(B): A's mask (A's validity in it) ANDed with B's
    // validity is B's row plane.
    if (with) {
        std::vector<u64> bvalues;
        load(with, bvalues);
        if (bvalues.size() != values.size())
            fail("pair length", 0);
        const column bc = encode(bvalues, options);
        const auto bvalid = validity_plane(nblocks, 77);
        std::vector<u64> bs(bvalues);
        std::nth_element(bs.begin(), bs.begin() + bs.size() / 2, bs.end());
        const u64 bmedian = bs[bs.size() / 2], bpoint = bvalues[bvalues.size() / 3];
        // A's predicates: the column set plus EventTime-style windows.
        std::vector<u64> sorted(values);
        std::sort(sorted.begin(), sorted.end());
        auto preds = col_preds;
        for (unsigned q : {0, 5, 40, 80, 95})
            preds.push_back({predicate_op::between, sorted[sorted.size() * q / 100],
                             sorted[std::min(sorted.size() - 1, sorted.size() * (q + 3) / 100)]});
        dictionary_cache acache, bcache;
        for (const block_mask *m : {&all, &sparse})
            for (const auto &p : preds) {
                predicate_inputs in{plane, validity};
                std::fill(out.begin(), out.end(), 0);
                predicate_masks(c, *m, p, out, in, &acache);
                for (std::size_t w = 0; w < out.size(); ++w)
                    out[w] &= bvalid[w];
                u128 sum = 0;
                u64 rows = 0, ge = 0, eq = 0;
                for (std::size_t b = 0; b < nblocks; ++b) {
                    if (!coarse_has(*m, b))
                        continue;
                    const row_selection s = and_rows(active_rows(in, b), selection_at(bvalid, b));
                    for (unsigned i = 0; i < block_rows; ++i)
                        if (selected(s, i) && holds(p, values[b * block_rows + i])) {
                            const u64 y = bvalues[b * block_rows + i];
                            sum += y, ++rows, ge += y >= bmedian, eq += y == bpoint;
                        }
                }
                if (masked_encoded_sum(bc, *m, out, &bcache) != sum || masked_selected_rows(*m, out) != rows ||
                    masked_encoded_count_ge(bc, *m, out, bmedian, &bcache) != ge ||
                    masked_encoded_count_eq(bc, *m, out, bpoint, &bcache) != eq ||
                    masked_encoded_sum(bc, *m, out) != sum)
                    fail("pair aggregate", 0, predicate_name(p.op));
                ++n.pair;
            }
    }

    std::printf("{\"name\":\"%s\",\"with\":\"%s\",\"checkpoints\":\"%s\",\"delta_exceptions\":%s,\"dictionary\":\"%s\","
                "\"blocks\":%zu,\"coded_blocks\":%zu,\"block_masks\":%zu,\"cached_masks\":%zu,\"reference_masks\":%zu,"
                "\"scan_grant_masks\":%zu,\"empty_guards\":%zu,\"column_masks\":%zu,\"skipped_blocks\":%zu,"
                "\"edition_images\":%zu,\"projected_masks\":%zu,\"pair_aggregates\":%zu,\"kinds\":{",
                argv[1], with ? with : "", checkpoint_name(mode), options.delta_exceptions ? "true" : "false",
                dictionary_name(options.dictionary), nblocks, c.coded_blocks(), n.block, n.cached, n.reference,
                n.scan_grant, n.empty_guard, n.column, n.skipped_words, n.edition, n.projected, n.pair);
    bool first = true;
    for (const auto &[k, v] : kinds) {
        std::printf("%s\"%s\":%zu", first ? "" : ",", k.c_str(), v);
        first = false;
    }
    std::printf("}}\n");
}
