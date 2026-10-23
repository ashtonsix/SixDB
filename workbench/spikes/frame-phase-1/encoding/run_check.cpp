// Run-value programs (runs.h) over hand-built runs blocks at every plane width
// K = 1..64 and run counts from 1 to 256, each body copied to end exactly at a
// PROT_NONE page. Every program (wide, narrow, in_place) must match an oracle
// computed from the run values and lengths that built the block, never from
// the operations: decode, SUM, COUNT(>=) and COUNT(==) at probes around every
// run value and the width bound base + 2^K - 1, the row-selected forms under
// adversarial and random selections, predicate rows for each interval, and
// fused SUM + COUNT + EQ + MIN + MAX.
//
// Byte-lane twins: wherever byte_lane_width(K) > K (K < 32), the same runs,
// and 256 packed values below 2^K, stored at K' = byte_lane_width(K) under a
// flagged descriptor that keeps the exact K (material.h, byte_lane_flag),
// pass the same oracle. Their bound law: in a forked child, with the body
// pointed at a PROT_NONE page and no grant, every decision the exact bound
// settles (COUNT >= and == above base + 2^K - 1, their masked forms, predicate
// rows above it and for [base, base + 2^K - 1], fused counts) must answer
// without reading a byte. Descriptors whose flag is inconsistent are refused.
// Predicate rows are checked under both mask fills (per_run, toggle).
//
// Layout: at K = 8, 16 and 32 the packed plane must equal the row-major
// little-endian array of the values, byte for byte (the premise of the
// in_place program), and at K = 1 a row-major LSB-first bit array with zero
// padding (the premise of both narrow programs' K = 1 bit read). In-place
// guard: the plane alone, granted exactly plane_bytes(K, n) bytes ending at
// the guard, read through with_run_values; and, in a forked child, the same
// plane granted one byte short, which must fault when n is a multiple of 8 (no
// padding in the last tile).
#include "material.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace frame_encoding;

[[noreturn]] void fail(const char *what, unsigned k, unsigned n, const char *program) {
    std::fprintf(stderr, "run check: %s at K=%u runs=%u program=%s\n", what, k, n, program);
    std::exit(1);
}
[[noreturn]] void fail(const char *block, const char *what, unsigned k, unsigned n, const char *program) {
    std::fprintf(stderr, "run check: %s block: %s at K=%u runs=%u program=%s fill=%s\n", block, what, k, n, program,
                 run_fill_name(run_mask_fill));
    std::exit(1);
}

struct guarded {
    std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)), n;
    std::uint8_t *base;
    explicit guarded(std::size_t bytes) : n((bytes + page - 1) / page + 1) {
        void *m = ::mmap(nullptr, (n + 2) * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED)
            std::abort();
        base = static_cast<std::uint8_t *>(m);
        if (::mprotect(base, page, PROT_NONE) || ::mprotect(base + (n + 1) * page, page, PROT_NONE))
            std::abort();
    }
    ~guarded() { ::munmap(base, (n + 2) * page); }
    std::uint8_t *end_at(const std::uint8_t *src, std::size_t bytes) const {
        std::uint8_t *at = base + (n + 1) * page - bytes;
        std::memcpy(at, src, bytes);
        return at;
    }
};

template <class F> bool faults(F &&read) {
    std::fflush(nullptr);
    const auto child = ::fork();
    if (child == 0) {
        const rlimit no_core{0, 0};
        ::setrlimit(RLIMIT_CORE, &no_core);
        read();
        ::_exit(0);
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return false;
    if (WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS))
        return true;
    std::fprintf(stderr, "run check: child neither completed nor faulted\n");
    std::exit(1);
}

std::size_t bound_laws = 0, refusals = 0, twins = 0;
const std::uint8_t *unreadable = nullptr;  // a PROT_NONE page

u64 state = 0x853c49e6748fea9bu;
u64 next() {
    state = state * 6364136223846793005u + 1442695040888963407u;
    u64 x = state;
    x ^= x >> 31;
    return x * 0x9e3779b97f4a7c15u;
}

std::string u128_digits(u128 v) {
    std::string s;
    do {
        s.insert(s.begin(), static_cast<char>('0' + static_cast<int>(v % 10)));
        v /= 10;
    } while (v);
    return s;
}

row_selection random_rows(unsigned per_mille) {
    row_selection s{};
    for (unsigned i = 0; i < block_rows; ++i)
        if (next() % 1000 < per_mille)
            s[i / 64] |= u64{1} << (i % 64);
    return s;
}

// Every program (and both mask fills for predicate rows) against the oracle
// rows; returns the probes checked.
std::size_t check_block(const borrowed_block &blk, const u64 *rows, const std::vector<u64> &probes,
                        const std::vector<row_selection> &sels, u64 top, unsigned k, unsigned n, const char *label) {
    const run_program programs[] = {run_program::wide, run_program::narrow, run_program::in_place};
    std::size_t checks = 0;
    for (run_program program : programs) {
        run_values_program = program;
        const char *pn = run_program_name(program);
        u64 decoded[block_rows];
        decode_block(blk, decoded);
        if (std::memcmp(decoded, rows, sizeof decoded))
            fail(label, "decode", k, n, pn);
        for (const auto &s : sels) {
            u128 sum = 0;
            u64 m = 0;
            for (unsigned i = 0; i < block_rows; ++i)
                if (selected(s, i))
                    sum += rows[i], ++m;
            const bool all = selection_full(s);
            if ((all ? stored_sum(blk) : masked_stored_sum_mixed(blk, s)) != sum)
                fail(label, "SUM", k, n, pn);
            for (u64 t : probes) {
                u64 ge = 0, eq = 0;
                for (unsigned i = 0; i < block_rows; ++i)
                    ge += selected(s, i) && rows[i] >= t, eq += selected(s, i) && rows[i] == t;
                if ((all ? stored_count_ge(blk, t) : masked_stored_count_ge_mixed(blk, s, t)) != ge ||
                    (all ? stored_count_eq(blk, t) : masked_stored_count_eq_mixed(blk, s, t)) != eq)
                    fail(label, "COUNT", k, n, pn);
                // Predicate rows: x >= t, x == t, and [t, t + 2^K / 4].
                for (const value_predicate &pr :
                     {value_predicate{predicate_op::ge, t}, value_predicate{predicate_op::eq, t},
                      value_predicate{predicate_op::between, t, t + (top >> 2) + 1}}) {
                    const value_interval iv = interval_of(pr);
                    row_selection want{};
                    for (unsigned i = 0; i < block_rows; ++i)
                        if (selected(s, i) && iv.contains(rows[i]) != iv.negate)
                            want[i / 64] |= u64{1} << (i % 64);
                    for (run_fill fill : {run_fill::per_run, run_fill::toggle}) {
                        run_mask_fill = fill;
                        if (block_predicate_rows(blk, pr, s) != want)
                            fail(label, "predicate rows", k, n, pn);
                    }
                }
                fused_request q;
                q.sum = q.min = q.max = q.eq = true, q.ge_count = 1, q.ge[0] = t, q.eq_value = t;
                fused_request counts_only = q;
                counts_only.sum = counts_only.min = counts_only.max = false;
                for (const auto &fq : {q, counts_only}) {
                    fused_result want;
                    want.rows = m;
                    for (unsigned i = 0; i < block_rows; ++i)
                        if (selected(s, i)) {
                            if (fq.sum)
                                want.sum += rows[i], want.min = std::min(want.min, rows[i]),
                                    want.max = std::max(want.max, rows[i]);
                            want.ge[0] += rows[i] >= t, want.eq += rows[i] == t;
                        }
                    if (const auto got = fused_stored(blk, s, fq); got != want) {
                        std::fprintf(stderr, "t=%llu base=%llu sum %s/%s ge %llu/%llu eq %llu/%llu rows %llu/%llu min %llu/%llu max %llu/%llu\n",
                                     (unsigned long long)t, (unsigned long long)blk.d.base,
                                     u128_digits(got.sum).c_str(), u128_digits(want.sum).c_str(),
                                     (unsigned long long)got.ge[0], (unsigned long long)want.ge[0],
                                     (unsigned long long)got.eq, (unsigned long long)want.eq,
                                     (unsigned long long)got.rows, (unsigned long long)want.rows,
                                     (unsigned long long)got.min, (unsigned long long)want.min,
                                     (unsigned long long)got.max, (unsigned long long)want.max);
                        fail(label, "fused", k, n, pn);
                    }
                }
                ++checks;
            }
        }
    }
    return checks;
}

// Decisions the exact bound settles, with the body unreadable: a forked child
// answers each one with body at a PROT_NONE page and grant 0.
void bound_law(const borrowed_block &view, unsigned k, unsigned n) {
    const u64 base = view.d.base, top = (u64{1} << k) - 1;
    if (base + top + 1 < base)
        return;  // no residual above the bound fits u64
    const run_program programs[] = {run_program::wide, run_program::narrow, run_program::in_place};
    const row_selection full{~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}}, mixed = {0x00ff00ff00ff00ffu, 0, ~u64{0}, 1};
    borrowed_block blind = view;
    blind.body = unreadable;
    blind.grant = 0;
    const bool faulted = faults([&] {
        for (run_program program : programs) {
            run_values_program = program;
            for (u64 t : {base + top + 1, base + top + 2, ~u64{0}}) {
                if (stored_count_ge(blind, t) || stored_count_eq(blind, t) ||
                    masked_stored_count_ge_mixed(blind, mixed, t) || masked_stored_count_eq_mixed(blind, mixed, t))
                    ::_exit(3);
                for (const auto &s : {full, mixed}) {
                    if (block_predicate_rows(blind, {predicate_op::ge, t}, s) != row_selection{} ||
                        block_predicate_rows(blind, {predicate_op::eq, t}, s) != row_selection{})
                        ::_exit(3);
                    fused_request q;
                    q.eq = true, q.eq_value = t, q.ge_count = 1, q.ge[0] = t;
                    const fused_result r = fused_stored(blind, s, q);
                    if (r.ge[0] || r.eq || r.rows != selected_count(s))
                        ::_exit(3);
                }
            }
            // Every value lies in [base, base + 2^K - 1]: the whole selection passes.
            for (const auto &s : {full, mixed})
                if (block_predicate_rows(blind, {predicate_op::between, base, base + top + 1}, s) != s ||
                    block_predicate_rows(blind, {predicate_op::lt, base + top + 1}, s) != s)
                    ::_exit(3);
        }
        ::_exit(0);
    });
    if (faulted)
        fail("a bound decision read the body", k, n, "-");
    ++bound_laws;
}
}  // namespace

int main() {
    const unsigned counts[] = {1, 2, 3, 7, 8, 9, 16, 24, 31, 41, 64, 100, 128, 200, 255, 256};
    std::size_t blocks = 0, checks = 0, layouts = 0, guards = 0, short_faults = 0, short_probes = 0;
    guarded body_page(4096 + 512), plane_page(4096), packed_page(4096);
    unreadable = body_page.base;  // the low guard page
    for (unsigned k = 1; k <= 64; ++k) {
        const u64 top = k == 64 ? ~u64{0} : (u64{1} << k) - 1;
        for (unsigned n : counts) {
            // Bases keep base + 2^K - 1 within u64, as an encoded block does.
            for (u64 base : {u64{0}, std::min<u64>(1000, ~u64{0} - top), ~u64{0} - top}) {
                // Run values below 2^K (top, 0 and 1 included when there is
                // room) and n run lengths summing to 256.
                std::vector<u64> v(n);
                for (unsigned r = 0; r < n; ++r)
                    v[r] = r == 0 ? top : r == 1 ? 0 : r == 2 ? 1 : next() & top;
                if (n > 3 && k > 1)
                    v[3] = v[n - 1];  // a repeated value
                std::vector<unsigned> ends(n);
                {
                    std::vector<unsigned> cuts;
                    for (unsigned c = 1; c < block_rows && cuts.size() + 1 < n; ++c)
                        cuts.push_back(c);
                    for (std::size_t i = cuts.size(); i > 1; --i)  // a random subset of n - 1 cuts
                        std::swap(cuts[i - 1], cuts[next() % i]);
                    cuts.resize(n - 1);
                    std::sort(cuts.begin(), cuts.end());
                    for (unsigned r = 0; r + 1 < n; ++r)
                        ends[r] = cuts[r];
                    ends[n - 1] = block_rows;
                }
                const std::size_t plane = plane_bytes(k, n);
                std::vector<std::uint8_t> body(plane + n);
                pack(k, v, body.data());
                for (unsigned r = 0; r < n; ++r)
                    body[plane + r] = static_cast<std::uint8_t>(ends[r] - 1);
                if (k % 8 == 0 && k <= 32) {
                    // Layout premise of the in_place program.
                    std::vector<std::uint8_t> le(n * (k / 8));
                    for (unsigned r = 0; r < n; ++r)
                        for (unsigned byte = 0; byte < k / 8; ++byte)
                            le[r * (k / 8) + byte] = static_cast<std::uint8_t>(v[r] >> (8 * byte));
                    if (std::memcmp(le.data(), body.data(), le.size()))
                        fail("byte-lane plane is not a row-major little-endian array", k, n, "-");
                    for (std::size_t i = le.size(); i < plane; ++i)
                        if (body[i])
                            fail("byte-lane tile padding is not zero", k, n, "-");
                    ++layouts;
                }
                if (k == 1) {
                    // Layout premise of the K = 1 bit read: bit r % 8 of byte r / 8.
                    for (unsigned r = 0; r < n; ++r)
                        if ((body[r / 8] >> (r % 8) & 1) != v[r])
                            fail("a K = 1 plane is not a row-major LSB-first bit array", k, n, "-");
                    for (unsigned r = n; r < 8 * plane; ++r)
                        if (body[r / 8] >> (r % 8) & 1)
                            fail("K = 1 tile padding is not zero", k, n, "-");
                    ++layouts;
                }
                // Row values by the oracle.
                u64 rows[block_rows];
                for (unsigned r = 0, row = 0; r < n; ++r)
                    for (; row < ends[r]; ++row)
                        rows[row] = base + v[r];
                borrowed_block blk;
                blk.d = {base, 0, static_cast<std::uint16_t>(n), kind::runs, static_cast<std::uint8_t>(k)};
                blk.body = body_page.end_at(body.data(), body.size());
                blk.grant = body.size();
                blk.delta_checkpoints = checkpoints::native;
                if (!admits(blk))
                    fail("admits refused", k, n, "-");
                std::vector<u64> probes{0, base, base + top, ~u64{0}};
                if (base)
                    probes.push_back(base - 1);
                if (base + top < ~u64{0})
                    probes.push_back(base + top + 1);
                for (unsigned r = 0; r < std::min(n, 6u); ++r)
                    for (u64 d : {u64{0}, u64{1}, ~u64{0}})
                        probes.push_back(base + v[r] + d);
                const row_selection full{~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}};
                std::vector<row_selection> sels{full, {1, 0, 0, 0}, {0, 0, 0, u64{1} << 63}, random_rows(10),
                                                random_rows(500), random_rows(990),
                                                {~u64{0}, (u64{1} << 36) - 1, 0, 0}};
                checks += check_block(blk, rows, probes, sels, top, k, n, "exact");
                if (byte_lane_width(k) > k) {
                    // Byte-lane twins under a flagged descriptor, viewed as borrow() views them.
                    const unsigned lane = byte_lane_width(k);
                    const std::size_t lane_plane = plane_bytes(lane, n);
                    std::vector<std::uint8_t> wide_body(lane_plane + n);
                    pack(lane, v, wide_body.data());
                    for (unsigned r = 0; r < n; ++r)
                        wide_body[lane_plane + r] = static_cast<std::uint8_t>(ends[r] - 1);
                    const auto flagged = static_cast<std::uint8_t>(k | byte_lane_flag);
                    borrowed_block twin = blk;
                    view_descriptor(twin, {base, 0, static_cast<std::uint16_t>(n), kind::runs, flagged});
                    twin.body = body_page.end_at(wide_body.data(), wide_body.size());
                    twin.grant = wide_body.size();
                    if (twin.d.width != lane || bound_width(twin) != k || !admits(twin))
                        fail("byte-lane runs view", k, n, "-");
                    checks += check_block(twin, rows, probes, sels, top, k, n, "byte_lane_runs");
                    bound_law(twin, k, n);
                    // 256 packed values below 2^K (top and 0 included).
                    std::vector<u64> pv(block_rows);
                    for (unsigned i = 0; i < block_rows; ++i)
                        pv[i] = i == 0 ? 0 : i == 1 ? top : next() & top;
                    std::vector<std::uint8_t> packed_body(plane_bytes(lane, block_rows));
                    pack(lane, pv, packed_body.data());
                    u64 prow[block_rows];
                    for (unsigned i = 0; i < block_rows; ++i)
                        prow[i] = base + pv[i];
                    borrowed_block pk = blk;
                    view_descriptor(pk, {base, 0, block_rows, kind::packed, flagged});
                    pk.body = packed_page.end_at(packed_body.data(), packed_body.size());
                    pk.grant = packed_body.size();
                    if (pk.d.width != lane || bound_width(pk) != k || !admits(pk))
                        fail("byte-lane packed view", k, n, "-");
                    std::vector<u64> pprobes{0, base, base + top, ~u64{0}, base + pv[2], base + pv[3] + 1};
                    if (base + top < ~u64{0})
                        pprobes.push_back(base + top + 1);
                    if (n == 1) {  // once per width and base
                        checks += check_block(pk, prow, pprobes, sels, top, k, n, "byte_lane_packed");
                        bound_law(pk, k, n);
                    }
                    // Inconsistent flags: the exact width must lie below its lane.
                    borrowed_block bad = twin;
                    bad.exact_width = static_cast<std::uint8_t>(lane);
                    if (admits(bad) || (lane > 8 && (bad.exact_width = static_cast<std::uint8_t>(lane / 2 - 1), admits(bad))))
                        fail("an inconsistent byte-lane view was admitted", k, n, "-");
                    for (const descriptor &d : {descriptor{base, 0, static_cast<std::uint16_t>(n), kind::runs,
                                                           static_cast<std::uint8_t>(lane | byte_lane_flag)},
                                                descriptor{base, 0, block_rows, kind::delta, flagged},
                                                descriptor{base, 0, block_rows, kind::patched, flagged}})
                        if (extent_of(d, checkpoints::native, wide_body.data(), 1 << 16))
                            fail("an inconsistent byte-lane descriptor has an extent", k, n, "-");
                    ++refusals;
                    ++twins;
                }
                // In-place guard: the plane alone, exactly granted, at the
                // guard (K = 1 through both narrow programs' bit read).
                if ((k % 8 == 0 && k <= 32) || k == 1) {
                    borrowed_block lone = blk;
                    lone.body = plane_page.end_at(body.data(), plane);
                    lone.grant = plane;
                    with_run_values(lone, run_program::in_place, [&](const auto &x) {
                        for (unsigned r = 0; r < n; ++r)
                            if (u64{x[r]} != v[r])
                                fail("in-place read", k, n, "in_place");
                        return 0;
                    });
                    ++guards;
                    if (n % 8 == 0) {  // one byte short reaches back into the last tile
                        borrowed_block cut = blk;
                        cut.body = plane_page.end_at(body.data(), plane - 1);
                        ++short_probes;
                        short_faults += faults([&] {
                            with_run_values(cut, run_program::in_place, [&](const auto &x) {
                                for (unsigned r = 0; r < n; ++r)
                                    sink = x[r];
                                return 0;
                            });
                        });
                    }
                }
                ++blocks;
            }
        }
    }
    if (short_faults != short_probes)
        fail("an in-place read within a plane one byte short did not fault", 0, 0, "in_place");
    std::printf("{\"blocks\":%zu,\"byte_lane_twins\":%zu,\"checks\":%zu,\"layouts\":%zu,\"in_place_guards\":%zu,"
                "\"short_faults\":\"%zu/%zu\",\"bound_laws\":%zu,\"flag_refusals\":%zu}\n",
                blocks, twins, checks, layouts, guards, short_faults, short_probes, bound_laws, refusals);
}
