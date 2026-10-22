// Borrowed-block correctness and grant check for the private Frame material.
// Every block of a real column is copied out of the image into a guarded
// mapping and read through borrowed_block alone, with its dictionary (when
// coded) copied into a second guarded mapping and no other part's dictionary
// present. Results are compared with the original input values and with codes
// derived independently from the input, never with the column operations; the
// column-indexed wrappers are then checked equal to the borrowed results.
// Placements: body ending exactly at a PROT_NONE page (the reported read grant
// is the whole readable span), starting right after one, and starting 1 and 3
// bytes after one (unaligned starts). Injected one-byte-short grants run in
// forked children: a body starting one byte inside a guard must fault, and so
// must one ending a byte short where its last byte is necessarily read; each
// operation's last-byte faults are reported per block class. A delta body
// is also read with only its scan grant (no checkpoints) before the guard.
// Page placement is only for access checking, never for timing.
#include "material.h"

#include <algorithm>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include <csignal>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace frame_encoding;

[[noreturn]] void fail(const char *what, std::size_t b, const char *detail = "") {
    std::fprintf(stderr, "borrowed check: %s at block %zu %s\n", what, b, detail);
    std::exit(1);
}

// [PROT_NONE page][n readable pages][PROT_NONE page], as in Codex's
// masked_access_check (mmap + mprotect, faults observed in a forked child).
struct guarded {
    std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)), n;
    std::uint8_t *base;
    explicit guarded(std::size_t bytes) : n((bytes + page - 1) / page + 1) {
        void *m = ::mmap(nullptr, (n + 2) * page, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
};

// Where a span of `bytes` goes: ending at the high guard, or `shift` bytes
// after the low guard. Shift -1 starts one byte inside the low guard (the
// copy skips that byte): a start one byte short.
struct placement {
    const char *name;
    bool at_end;
    long shift;
};
std::uint8_t *place(const guarded &g, const placement &p, const std::uint8_t *src, std::size_t bytes) {
    std::uint8_t *at = p.at_end ? g.hi() - bytes : g.lo() + p.shift;
    if (p.shift < 0)
        std::memcpy(g.lo(), src + 1, bytes - 1);
    else
        std::memcpy(at, src, bytes);
    return at;
}

// Stored values per block as the oracle sees them: the input, or for a coded
// block its code, derived here from the input by the dictionary laws in
// material.h (sorted: rank of the value among the part's distinct values;
// frequency: rank by descending frequency, ties by ascending value; arrival:
// order of first occurrence within the part).
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
            std::stable_sort(order.begin(), order.end(),
                             [](auto a, auto b) { return a.second > b.second; });
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

struct expected {
    const u64 *values = nullptr, *stored = nullptr;
    u128 sum = 0, stored_sum = 0;
    std::vector<u64> thresholds;  // logical
    std::vector<u64> stored_thresholds;
    u64 count(u64 t) const {
        u64 n = 0;
        for (std::size_t i = 0; i < block_rows; ++i)
            n += values[i] >= t;
        return n;
    }
    u64 stored_count(u64 t) const {
        u64 n = 0;
        for (std::size_t i = 0; i < block_rows; ++i)
            n += stored[i] >= t;
        return n;
    }
    u64 equal(u64 v) const {
        u64 n = 0;
        for (std::size_t i = 0; i < block_rows; ++i)
            n += values[i] == v;
        return n;
    }
    u64 stored_equal(u64 s) const {
        u64 n = 0;
        for (std::size_t i = 0; i < block_rows; ++i)
            n += stored[i] == s;
        return n;
    }
};

expected expect(const u64 *values, const u64 *stored, u64 column_median) {
    expected e;
    e.values = values, e.stored = stored;
    u64 lo = UINT64_MAX, hi = 0, slo = UINT64_MAX, shi = 0;
    for (std::size_t i = 0; i < block_rows; ++i) {
        e.sum += values[i];
        e.stored_sum += stored[i];
        lo = std::min(lo, values[i]), hi = std::max(hi, values[i]);
        slo = std::min(slo, stored[i]), shi = std::max(shi, stored[i]);
    }
    e.thresholds = {0, lo, values[100], values[255], hi, column_median};
    // Rows either side of checkpoint spacings, for the non-decreasing delta
    // COUNT's search, and one past a value (a strict step).
    for (unsigned row : {1u, 15u, 16u, 17u, 63u, 64u, 65u, 129u, 240u, 254u})
        e.thresholds.push_back(values[row]);
    if (values[64] < UINT64_MAX)
        e.thresholds.push_back(values[64] + 1);
    if (hi < UINT64_MAX)
        e.thresholds.push_back(hi + 1);
    if (lo)
        e.thresholds.push_back(lo - 1);
    e.stored_thresholds = {0, slo, stored[37], shi, shi + 1};
    return e;
}

// Every borrowed operation against the oracle. all_points: every row (else a
// sample including checkpoint edges).
void check_block(const borrowed_block &k, const expected &e, std::size_t b, bool all_points) {
    alignas(64) u64 out[block_rows];
    decode_block(k, out);
    if (std::memcmp(out, e.values, sizeof out))
        fail("decode_block", b);
    decode_stored(k, out);
    if (std::memcmp(out, e.stored, sizeof out))
        fail("decode_stored", b);
    static constexpr unsigned sample[] = {0, 1, 2, 15, 16, 17, 31, 32, 63, 64, 65, 127, 128,
                                          191, 192, 193, 239, 240, 241, 254, 255};
    auto point = [&](unsigned row) {
        if (stored_at(k, row) != e.stored[row])
            fail("stored_at", b);
        if (value_at(k, row) != e.values[row])
            fail("value_at", b);
    };
    if (all_points)
        for (unsigned row = 0; row < block_rows; ++row)
            point(row);
    else
        for (unsigned row : sample)
            point(row);
    if (stored_sum(k) != e.stored_sum || narrow_stored_sum(k) != e.stored_sum ||
        catalogue_stored_sum(k) != e.stored_sum)
        fail("stored SUM", b);
    if (block_sum(k) != e.sum)
        fail("block_sum", b);
    for (u64 t : e.stored_thresholds)
        if (stored_count_ge(k, t) != e.stored_count(t) || catalogue_stored_count_ge(k, t) != e.stored_count(t) ||
            stored_count_eq(k, t) != e.stored_equal(t))
            fail("stored COUNT", b);
    // Equality at every threshold (present and absent values) and at stored
    // values of rows either side of the arrival frontier's movement.
    for (u64 t : e.thresholds)
        if (block_count_eq(k, t) != e.equal(t))
            fail("block_count_eq", b);
    for (unsigned row : {0u, 7u, 99u, 200u, 255u})
        if (stored_count_eq(k, e.stored[row]) != e.stored_equal(e.stored[row]) ||
            stored_count_eq(k, e.stored[row] + 1) != e.stored_equal(e.stored[row] + 1))
            fail("stored equality COUNT", b);
    for (u64 t : e.thresholds) {
        if (block_count_ge(k, t) != e.count(t))
            fail("block_count_ge", b);
        code_count codes(t);
        if (catalogue_block_count_ge(codes, k) != e.count(t))
            fail("catalogue COUNT", b);
    }
    if (k.coded) {
        code_sum codes;
        if (codes(k) != e.sum)
            fail("code_sum", b);
    }
}

// Run read in a child; true when it was killed by SIGSEGV/SIGBUS.
template <class F> bool faults(F &&read) {
    std::fflush(nullptr);
    const auto child = ::fork();
    if (child < 0)
        fail("fork", 0);
    if (child == 0) {
        const rlimit no_core{0, 0};
        if (::setrlimit(RLIMIT_CORE, &no_core) != 0)
            ::_exit(120);
        read();
        ::_exit(0);
    }
    int status = 0;
    if (::waitpid(child, &status, 0) != child)
        fail("waitpid", 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return false;
    if (WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS))
        return true;
    fail("child neither completed nor faulted", 0);
}

// The operations probed one at a time with a one-byte-short grant.
struct probe {
    const char *name;
    void (*run)(const borrowed_block &);
};
const probe probes[] = {
    {"decode", [](const borrowed_block &k) { alignas(64) u64 o[block_rows]; decode_block(k, o); sink = o[255]; }},
    {"points", [](const borrowed_block &k) { for (unsigned r = 0; r < block_rows; ++r) sink = value_at(k, r); }},
    {"sum", [](const borrowed_block &k) { consume(block_sum(k)); }},
    {"narrow_sum", [](const borrowed_block &k) { consume(k.coded ? block_sum(k) : narrow_stored_sum(k)); }},
    {"catalogue_sum", [](const borrowed_block &k) { consume(k.coded ? block_sum(k) : catalogue_stored_sum(k)); }},
    {"count", [](const borrowed_block &k) { sink = block_count_ge(k, k.d.base + 1); }},
    {"catalogue_count", [](const borrowed_block &k) { code_count c(k.d.base + 1); sink = catalogue_block_count_ge(c, k); }},
    {"code_sum", [](const borrowed_block &k) { if (k.coded) { code_sum c; consume(c(k)); } }},
    {"count_eq", [](const borrowed_block &k) { sink = block_count_eq(k, value_at(k, 0)); }},
};
void battery(const borrowed_block &k) {
    for (const auto &p : probes)
        p.run(k);
}

std::string block_class(const borrowed_block &k) {
    static const char *kinds[] = {"constant", "runs", "packed", "delta", "patched", "delta_patched", "arrival"};
    return std::string(k.coded ? "coded_" : "") + kinds[static_cast<int>(k.d.type)];
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s NAME FILE.u64 [--checkpoints=native|packed|every64|none] "
                     "[--delta-exceptions] [--dictionary=none|sorted|frequency|arrival]\n", argv[0]);
        return 2;
    }
    encode_options options;
    options.bind_decoders = false;  // the borrowed path reads none of them
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
        else return 2;
    }
    // Inputs load as in bodies.cpp: whole 256-row blocks of u64.
    std::vector<u64> values;
    {
        std::FILE *f = std::fopen(argv[2], "rb");
        if (!f)
            return 2;
        std::fseek(f, 0, SEEK_END);
        const long end = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (end <= 0 || end % 8 || (end / 8) % block_rows)
            return 2;
        values.resize(static_cast<std::size_t>(end) / 8);
        if (std::fread(values.data(), 8, values.size(), f) != values.size())
            return 2;
        std::fclose(f);
    }
    std::vector<u64> sorted(values);
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const u64 median = sorted[sorted.size() / 2];

    const column c = encode(values, options);
    const std::vector<u64> stored = stored_oracle(c, values);
    const std::size_t nblocks = c.blocks.size();
    const checkpoints mode = c.options.delta_checkpoints;

    // Reported extents account for every payload byte: each body ends where the
    // next body, or the next part's dictionary plane, begins.
    std::size_t max_body = 0, max_dictionary = 0, gaps = 0;
    for (std::size_t b = 0; b < nblocks; ++b) {
        const auto &d = c.blocks[b];
        const auto x = extent_of(d, mode, c.payload.data() + d.offset, c.payload.size() - d.offset);
        if (!x)
            fail("extent refused", b);
        std::size_t next = c.payload.size();
        if (b + 1 < nblocks) {
            next = c.blocks[b + 1].offset;
            if ((b + 1) % part_blocks == 0 && !c.parts.empty() && c.parts[(b + 1) / part_blocks].count)
                next = c.parts[(b + 1) / part_blocks].offset;
        }
        if (d.offset + x->body_bytes != next) {
            char detail[96];
            std::snprintf(detail, sizeof detail, "(extent %zu, gap %zu)", x->body_bytes,
                          static_cast<std::size_t>(next - d.offset));
            fail("extent differs from the gap to the next region", b, detail);
        }
        if (!admits(borrow(c, b)))
            fail("admits refused a column block", b);
        max_body = std::max(max_body, x->read_grant_bytes);
        ++gaps;
    }
    for (std::size_t part = 0; part < c.parts.size(); ++part) {
        const auto v = dictionary_of(c, part);
        if (!v.count)
            continue;
        if (v.plane + v.bytes() != c.payload.data() + c.blocks[part * part_blocks].offset)
            fail("dictionary extent differs from the gap to its first body", part * part_blocks);
        max_dictionary = std::max(max_dictionary, v.bytes());
        ++gaps;
    }

    guarded body_pages(max_body + 8), dictionary_pages(std::max<std::size_t>(max_dictionary, 1) + 8);
    const placement placements[] = {{"end", true, 0}, {"start", false, 0}, {"start+1", false, 1},
                                    {"start+3", false, 3}};
    std::size_t start_alignment[8] = {};
    std::size_t checked = 0, scan_checked = 0;
    for (const auto &where : placements) {
        std::size_t placed_part = SIZE_MAX;
        dictionary_view dictionary{};
        for (std::size_t b = 0; b < nblocks; ++b) {
            borrowed_block k = borrow(c, b);
            const auto grant = extent_of(k.d, mode, k.body, k.grant)->read_grant_bytes;
            k.d.offset = 0xdeadbeef;  // ignored
            k.body = place(body_pages, where, k.body, grant);
            k.grant = grant;
            if (where.at_end && grant)
                ++start_alignment[reinterpret_cast<std::uintptr_t>(k.body) % 8];
            if (k.coded) {
                if (b / part_blocks != placed_part) {
                    placed_part = b / part_blocks;
                    dictionary = k.dictionary;
                    dictionary.plane = place(dictionary_pages, where, dictionary.plane, dictionary.bytes());
                    dictionary.grant = dictionary.bytes();
                }
                k.dictionary = dictionary;
            }
            if (!admits(k))
                fail("admits refused an exact grant", b);
            if (grant) {
                borrowed_block short_k = k;
                --short_k.grant;
                if (admits(short_k))
                    fail("admits accepted a grant one byte short", b);
            }
            const auto e = expect(values.data() + b * block_rows, stored.data() + b * block_rows, median);
            check_block(k, e, b, !where.shift);
            // Column wrappers construct the same view and must agree.
            const borrowed_block in_place = borrow(c, b);
            alignas(64) u64 a[block_rows], w[block_rows];
            decode_block(k, a);
            decode_block(c, b, w);
            if (std::memcmp(a, w, sizeof a) || block_sum(c, b) != block_sum(k) ||
                stored_sum(c, b) != stored_sum(k) || block_count_ge(c, b, median) != block_count_ge(k, median) ||
                stored_count_ge(c, b, e.stored_thresholds[2]) != stored_count_ge(k, e.stored_thresholds[2]) ||
                value_at(c, b * block_rows + 200) != value_at(k, 200) ||
                stored_at(in_place, 77) != stored_at(k, 77) ||
                block_count_eq(c, b, e.values[9]) != block_count_eq(k, e.values[9]) ||
                stored_count_eq(c, b, e.stored[9]) != stored_count_eq(k, e.stored[9]))
                fail("column wrapper differs from the borrowed result", b);
            // A delta body granted only its scan prefix, ending at the guard:
            // decode, SUM and the rbase < 0 COUNT must not need the
            // checkpoints (a non-decreasing block's COUNT searches them).
            const auto x = *extent_of(in_place.d, mode, in_place.body, in_place.grant);
            if (where.at_end && x.scan_grant_bytes < x.read_grant_bytes) {
                borrowed_block scan = k;
                scan.body = place(body_pages, where, in_place.body, x.scan_grant_bytes);
                scan.grant = x.scan_grant_bytes;
                decode_block(scan, a);
                if (std::memcmp(a, e.values, sizeof a) || block_sum(scan) != e.sum ||
                    narrow_stored_sum(scan) != e.stored_sum || catalogue_stored_sum(scan) != e.stored_sum)
                    fail("scan grant SUM/decode", b);
                for (u64 t : e.thresholds) {
                    code_count codes(t);
                    if (delta_rbase(scan) < 0 &&
                        (block_count_ge(scan, t) != e.count(t) || catalogue_block_count_ge(codes, scan) != e.count(t)))
                        fail("scan grant COUNT", b);
                }
                ++scan_checked;
            }
            ++checked;
        }
    }

    // Mask aggregates over the column wrappers against the input.
    {
        block_mask all(nblocks), sparse(nblocks);
        u128 total = 0, sparse_total = 0;
        u64 count = 0, sparse_count = 0;
        for (std::size_t b = 0; b < nblocks; ++b) {
            all.set(b);
            if (b % 10 == 3)
                sparse.set(b);
            for (std::size_t i = b * block_rows; i < (b + 1) * block_rows; ++i) {
                total += values[i];
                count += values[i] >= median;
                if (b % 10 == 3)
                    sparse_total += values[i], sparse_count += values[i] >= median;
            }
        }
        for (auto [mask, sum, n] : {std::tuple{&all, total, count}, std::tuple{&sparse, sparse_total, sparse_count}})
            if (encoded_sum(c, *mask) != sum || encoded_sum_narrow(c, *mask) != sum ||
                catalogue_sum(c, *mask) != sum || encoded_count_ge(c, *mask, median) != n ||
                catalogue_count_ge(c, *mask, median) != n)
                fail("mask aggregate differs from the input", 0);
        // Equality over all blocks at present values from several parts and
        // at one absent value, against the input.
        for (std::size_t row : {std::size_t{0}, values.size() / 3, values.size() / 2, values.size() - 1}) {
            for (u64 v : {values[row], values[row] + 1}) {
                u64 n = 0;
                for (std::size_t i = 0; i < nblocks * block_rows; ++i)
                    n += values[i] == v;
                if (encoded_count_eq(c, all, v) != n)
                    fail("mask equality COUNT differs from the input", row / block_rows);
            }
        }
    }

    // Injected one-byte-short grants, on the first eight non-empty blocks of each
    // class: the whole battery must fault at either edge, and each operation's
    // faults show which reads reach the body's last byte.
    std::map<std::string, std::size_t> seen;
    std::map<std::string, std::map<std::string, std::pair<int, int>>> last_byte;  // class -> op -> (faulted, probed)
    std::map<std::string, std::pair<int, int>> end_short;  // class -> (faulted, probed)
    int injected = 0, dictionary_injected = 0, dictionary_faulted = 0;
    for (std::size_t b = 0; b < nblocks; ++b) {
        borrowed_block k = borrow(c, b);
        const auto grant = extent_of(k.d, mode, k.body, k.grant)->read_grant_bytes;
        const std::string cls = block_class(k);
        if (!grant || seen[cls]++ >= 8)
            continue;
        const std::uint8_t *src = k.body;
        k.grant = grant - 1;
        if (k.coded) {
            k.dictionary.plane = place(dictionary_pages, placements[0], k.dictionary.plane, k.dictionary.bytes());
            k.dictionary.grant = k.dictionary.bytes();
        }
        k.body = place(body_pages, placements[0], src, grant - 1);  // last byte beyond the guard edge
        for (const auto &p : probes) {
            auto &cell = last_byte[cls][p.name];
            cell.second++;
            cell.first += faults([&] { p.run(k); });
        }
        // The body's last byte is always read when the body ends in a full
        // 256-row plane, run ends or fixed-width delta fields. A partially
        // occupied final tile (exception values, packed checkpoints) can leave
        // its unoccupied rows' bytes unread, so there the outcome is recorded.
        const bool ends_read = k.d.type == kind::packed || k.d.type == kind::runs ||
                               (k.d.type == kind::delta && mode != checkpoints::packed);
        const bool end_fault = faults([&] { battery(k); });
        auto &end_cell = end_short[cls];
        end_cell.first += end_fault, end_cell.second++;
        if (ends_read && !end_fault)
            fail("battery read within a grant one byte short at the end", b);
        k.body = place(body_pages, {"start-1", false, -1}, src, grant);  // first byte inside the guard
        if (!faults([&] { battery(k); }))
            fail("battery read within a grant one byte short at the start", b);
        injected += 2;
        if (k.coded) {
            k.body = place(body_pages, placements[0], src, grant);
            k.grant = grant;
            const auto *dsrc = borrow(c, b).dictionary.plane;
            k.dictionary.plane = place(dictionary_pages, placements[0], dsrc, k.dictionary.bytes() - 1);
            k.dictionary.grant = k.dictionary.bytes() - 1;
            dictionary_faulted += faults([&] { battery(k); });
            ++dictionary_injected;
        }
    }

    std::size_t kinds[7] = {}, coded = 0;
    for (std::size_t b = 0; b < nblocks; ++b)
        ++kinds[static_cast<int>(c.blocks[b].type)], coded += is_coded(c, b);
    // Dictionary identity across images (Ikea LEAD's case): one cache shared
    // over borrow(old), borrow(new), borrow(old), and an unchanged copy, must
    // answer each from its own dictionary. new mirrors every value within the
    // column's range, so its dictionaries differ in content at the same part
    // indices. A
    // negative control aliases new's image id to old's: the shared cache
    // should then go stale, which shows the check can see a collision.
    std::size_t identity_checks = 0;
    int alias_stale = -1;  // -1: no coded block to try
    if (options.dictionary != dictionary_order::none && coded) {
        // Mirror within the column's range: same shape, different relative
        // dictionary content (a plain shift would leave min-relative entries
        // unchanged).
        const u64 lo = *std::min_element(values.begin(), values.end());
        const u64 hi = *std::max_element(values.begin(), values.end());
        std::vector<u64> shifted(values);
        for (auto &v : shifted)
            v = lo + (hi - v);
        column next = encode(shifted, options);
        const column copy = c;
        for (std::size_t b = 0; b < nblocks; ++b) {
            if (!is_coded(c, b) || !is_coded(next, b))
                continue;
            u128 old_sum = 0, new_sum = 0;
            u64 old_ge = 0, new_ge = 0, old_eq = 0, new_eq = 0;
            const u64 probe = values[b * block_rows], shifted_probe = shifted[b * block_rows];
            for (std::size_t i = 0; i < block_rows; ++i) {
                old_sum += values[b * block_rows + i], new_sum += shifted[b * block_rows + i];
                old_ge += values[b * block_rows + i] >= median, new_ge += shifted[b * block_rows + i] >= median;
                old_eq += values[b * block_rows + i] == probe, new_eq += shifted[b * block_rows + i] == probe;
            }
            (void)shifted_probe;
            code_sum sums;
            code_count counts(median);
            code_count_eq equal(probe);
            for (const column *image : {&c, static_cast<const column *>(&next), &c, &copy, static_cast<const column *>(&next)}) {
                const auto k = borrow(*image, b);
                const bool is_old = image != &next;
                if (sums(k) != (is_old ? old_sum : new_sum) || counts.count(k) != (is_old ? old_ge : new_ge) ||
                    equal.count(k) != (is_old ? old_eq : new_eq))
                    fail("dictionary cache reused across images", b);
                ++identity_checks;
            }
            next.image = c.image;  // negative control
            code_sum aliased;
            aliased(borrow(c, b));
            alias_stale = aliased(borrow(next, b)) != new_sum;
            break;
        }
    }

    std::printf("{\"column\":\"%s\",\"delta_checkpoints\":\"%s\",\"delta_exceptions\":%s,\"dictionary\":\"%s\","
                "\"blocks\":%zu,\"coded\":%zu,\"kinds\":[%zu,%zu,%zu,%zu,%zu,%zu,%zu],\"extent_gaps_checked\":%zu,"
                "\"max_body\":%zu,\"max_dictionary\":%zu,\"page\":%zu,\"placements_checked\":%zu,\"scan_grants_checked\":%zu,"
                "\"end_start_mod8\":[%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu],\"injected\":%d,"
                "\"dictionary_injected\":%d,\"dictionary_short_faulted\":%d,\"end_short\":{",
                argv[1], checkpoint_name(mode), options.delta_exceptions ? "true" : "false",
                dictionary_name(options.dictionary), nblocks, coded, kinds[0], kinds[1], kinds[2], kinds[3],
                kinds[4], kinds[5], kinds[6], gaps, max_body, max_dictionary, body_pages.page, checked, scan_checked,
                start_alignment[0], start_alignment[1], start_alignment[2], start_alignment[3],
                start_alignment[4], start_alignment[5], start_alignment[6], start_alignment[7], injected,
                dictionary_injected, dictionary_faulted);
    bool first_end = true;
    for (const auto &[cls, cell] : end_short) {
        std::printf("%s\"%s\":\"%d/%d\"", first_end ? "" : ",", cls.c_str(), cell.first, cell.second);
        first_end = false;
    }
    std::printf("},\"last_byte\":{");
    bool first_class = true;
    for (const auto &[cls, ops] : last_byte) {
        std::printf("%s\"%s\":{", first_class ? "" : ",", cls.c_str());
        first_class = false;
        bool first_op = true;
        for (const auto &[op, cell] : ops) {
            std::printf("%s\"%s\":\"%d/%d\"", first_op ? "" : ",", op.c_str(), cell.first, cell.second);
            first_op = false;
        }
        std::printf("}");
    }
    std::printf("},\"identity_checks\":%zu,\"identity_alias_stale\":%d}\n", identity_checks, alias_stale);
}
