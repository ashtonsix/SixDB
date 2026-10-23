// Every leaf against an oracle over the original residuals: every layout and
// width 1..32, several value distributions, comparison constants at and
// around the edges and around present values, empty/full/sparse selections,
// every row as a point. local8 bytes must equal SeriesPack Local's own
// encoding (phase 1's pack()), so the phase 1 arm reads identical bytes.
#include "catalogue.h"
#include "exceptions.h"
#include "runs.h"
#include "packing.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace bodies;

namespace {
int failures = 0;
long blocks_checked = 0;
void fail(const char *what, layout l, unsigned k, u64 got, u64 want) {
    if (++failures <= 20)
        std::fprintf(stderr, "FAIL %s %s k=%u got=%llu want=%llu\n", what, layout_name(l), k,
                     static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
}

std::vector<u64> values(unsigned k, unsigned shape, std::mt19937_64 &rng) {
    const u64 top = k == 64 ? ~u64{0} : (u64{1} << k) - 1;
    std::vector<u64> v(block_rows);
    for (unsigned i = 0; i < block_rows; ++i) {
        switch (shape) {
        case 0: v[i] = rng() & top; break;                           // uniform
        case 1: v[i] = (rng() & 1) ? top : 0; break;                 // edges
        case 2: v[i] = (rng() & 3) == 0 ? rng() & top : top / 2; break;  // mostly one value
        default: v[i] = (rng() % 5) & top; break;                    // small
        }
    }
    return v;
}

std::vector<u64> constants(const std::vector<u64> &v, unsigned k, std::mt19937_64 &rng) {
    const u64 top = k == 64 ? ~u64{0} : (u64{1} << k) - 1;
    std::vector<u64> c = {0, 1 & top, top, top / 2, rng() & top};
    for (unsigned i = 0; i < 4; ++i) {
        const u64 x = v[rng() % block_rows];
        c.push_back(x);
        if (x < top)
            c.push_back(x + 1);
        if (x)
            c.push_back(x - 1);
    }
    return c;
}

std::vector<std::array<u64, 4>> selections(std::mt19937_64 &rng) {
    std::vector<std::array<u64, 4>> s = {{0, 0, 0, 0}, {~0ull, ~0ull, ~0ull, ~0ull}, {1, 0, 0, 1ull << 63}};
    s.push_back({rng(), rng(), rng(), rng()});
    s.push_back({rng() & rng() & rng(), 0, rng(), ~0ull});
    return s;
}

void check_packed(layout l, unsigned k, std::mt19937_64 &rng) {
    const packed_leaf &f = packed_leaves(l, k);
    for (unsigned shape = 0; shape < 4; ++shape)
        for (unsigned rep = 0; rep < 3; ++rep) {
            const auto v = values(k, shape, rng);
            ++blocks_checked;
            std::vector<u8> img(block_bytes(l, k) + 256);
            encode(l, k, v.data(), img.data());
            if (l == layout::local8) {
                std::vector<u8> sp(frame_encoding::plane_bytes(k, block_rows) + 256);
                frame_encoding::pack(k, v, sp.data());
                if (std::memcmp(sp.data(), img.data(), block_bytes(l, k)))
                    fail("local8-bytes", l, k, 0, 1);
            }
            u64 s = 0;
            for (u64 x : v)
                s += x;
            if (f.sum(img.data()) != s)
                fail("sum", l, k, f.sum(img.data()), s);
            for (u64 c : constants(v, k, rng)) {
                u64 m[4], want[4] = {0, 0, 0, 0};
                unsigned ge = 0, eq = 0;
                for (unsigned i = 0; i < block_rows; ++i) {
                    ge += v[i] >= c, eq += v[i] == c;
                    want[i / 64] |= u64{v[i] >= c} << (i % 64);
                }
                if (f.count_ge(img.data(), c) != ge)
                    fail("count_ge", l, k, f.count_ge(img.data(), c), ge);
                if (f.count_eq(img.data(), c) != eq)
                    fail("count_eq", l, k, f.count_eq(img.data(), c), eq);
                f.mask_ge(img.data(), c, m);
                for (unsigned w = 0; w < 4; ++w)
                    if (m[w] != want[w])
                        fail("mask_ge", l, k, m[w], want[w]);
            }
            for (const auto &sel : selections(rng)) {
                u64 want = 0;
                for (unsigned i = 0; i < block_rows; ++i)
                    if (sel[i / 64] >> (i % 64) & 1)
                        want += v[i];
                if (f.masked_sum(img.data(), sel.data()) != want)
                    fail("masked_sum", l, k, f.masked_sum(img.data(), sel.data()), want);
            }
            for (unsigned i = 0; i < block_rows; ++i)
                if (f.point(img.data(), i) != v[i])
                    fail("point", l, k, f.point(img.data(), i), v[i]);
            const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
            if (f.max(img.data()) != *hi)
                fail("max", l, k, f.max(img.data()), *hi);
            if (f.min(img.data()) != *lo)
                fail("min", l, k, f.min(img.data()), *lo);
            for (u64 c : constants(v, k, rng)) {
                unsigned first = block_rows;
                for (unsigned i = 0; i < block_rows && first == block_rows; ++i)
                    if (v[i] >= c)
                        first = i;
                if (f.first_ge(img.data(), c) != first)
                    fail("first_ge", l, k, f.first_ge(img.data(), c), first);
            }
            if (k <= 32) {
                u32 out[block_rows];
                f.unpack(img.data(), out);
                for (unsigned i = 0; i < block_rows; ++i)
                    if (out[i] != v[i])
                        fail("unpack", l, k, out[i], v[i]);
            }
        }
}

void check_runs(unsigned w, std::mt19937_64 &rng) {
    const unsigned lane = run_lane(w);
    const runs_leaf &f = runs_leaves(lane);
    const u64 top = (u64{1} << w) - 1;
    for (unsigned target : {2u, 3u, 7u, 16u, 17u, 24u, 33u, 64u, 100u, 200u, 256u})
        for (unsigned rep = 0; rep < 3; ++rep) {
            // target runs with random boundaries and values (adjacent runs differ).
            std::vector<unsigned> cut;
            for (unsigned i = 1; i < block_rows; ++i)
                cut.push_back(i);
            std::shuffle(cut.begin(), cut.end(), rng);
            cut.resize(target - 1);
            std::sort(cut.begin(), cut.end());
            std::vector<u64> v(block_rows);
            u64 x = rng() & top;
            unsigned at = 0;
            for (unsigned i = 0; i < block_rows; ++i) {
                if (at < cut.size() && cut[at] == i) {
                    u64 y;
                    do
                        y = (rep == 1 ? (rng() & 1) * top : rng() & top);
                    while (y == x && top);
                    x = y;
                    ++at;
                }
                v[i] = x;
            }
            const unsigned n = count_runs(v.data());
            if (n < 2)
                continue;
            ++blocks_checked;
            std::vector<u8> img(runs_bytes(n, w) + 256);
            encode_runs(v.data(), w, img.data());
            const layout tag = layout::lane;
            u64 s = 0;
            for (u64 y : v)
                s += y;
            if (f.sum(img.data(), n) != s)
                fail("runs-sum", tag, w, f.sum(img.data(), n), s);
            for (u64 c : constants(v, w, rng)) {
                unsigned ge = 0;
                u64 m[4], want[4] = {0, 0, 0, 0};
                for (unsigned i = 0; i < block_rows; ++i) {
                    ge += v[i] >= c;
                    want[i / 64] |= u64{v[i] >= c} << (i % 64);
                }
                if (f.count_ge(img.data(), n, c) != ge)
                    fail("runs-count_ge", tag, w, f.count_ge(img.data(), n, c), ge);
                f.mask_ge(img.data(), n, c, m);
                for (unsigned q = 0; q < 4; ++q)
                    if (m[q] != want[q])
                        fail("runs-mask_ge", tag, w, m[q], want[q]);
                for (auto *g : {f.mask_ge_every_run, f.mask_ge_fill, f.mask_ge_scatter}) {
                    g(img.data(), n, c, m);
                    for (unsigned q = 0; q < 4; ++q)
                        if (m[q] != want[q])
                            fail(g == f.mask_ge_fill ? "runs-mask_ge_fill" : g == f.mask_ge_scatter ? "runs-mask_ge_scatter" : "runs-mask_ge_every_run", tag, w, m[q], want[q]);
                }
            }
            for (const auto &sel : selections(rng)) {
                u64 want = 0;
                for (unsigned i = 0; i < block_rows; ++i)
                    if (sel[i / 64] >> (i % 64) & 1)
                        want += v[i];
                if (f.masked_sum(img.data(), n, sel.data()) != want)
                    fail("runs-masked_sum", tag, w, f.masked_sum(img.data(), n, sel.data()), want);
                if (f.masked_sum_scalar(img.data(), n, sel.data()) != want)
                    fail("runs-masked_sum_scalar", tag, w, f.masked_sum_scalar(img.data(), n, sel.data()), want);
                if (f.masked_sum_fill(img.data(), n, sel.data()) != want)
                    fail("runs-masked_sum_fill", tag, w, f.masked_sum_fill(img.data(), n, sel.data()), want);
            }
            for (unsigned i = 0; i < block_rows; ++i)
                if (f.point(img.data(), n, i) != v[i])
                    fail("runs-point", tag, w, f.point(img.data(), n, i), v[i]);
            const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
            if (f.max(img.data(), n) != *hi)
                fail("runs-max", tag, w, f.max(img.data(), n), *hi);
            if (f.min(img.data(), n) != *lo)
                fail("runs-min", tag, w, f.min(img.data(), n), *lo);
            for (u64 c : constants(v, w, rng)) {
                unsigned first = block_rows;
                for (unsigned i = 0; i < block_rows && first == block_rows; ++i)
                    if (v[i] >= c)
                        first = i;
                if (f.first_ge(img.data(), n, c) != first)
                    fail("runs-first_ge", tag, w, f.first_ge(img.data(), n, c), first);
            }
            if (lane == 1) {
                u64 starts[4], m[4];
                runs<u8>::run_starts(img.data(), n, starts);
                for (u64 c : constants(v, w, rng)) {
                    u64 want[4] = {0, 0, 0, 0};
                    for (unsigned i = 0; i < block_rows; ++i)
                        want[i / 64] |= u64{v[i] >= c} << (i % 64);
                    runs<u8>::mask_ge_starts(img.data(), n, starts, c, m);
                    for (unsigned q = 0; q < 4; ++q)
                        if (m[q] != want[q])
                            fail("runs-mask_ge_starts", tag, w, m[q], want[q]);
                }
                for (unsigned i = 0; i < block_rows; ++i)
                    if (runs<u8>::point_starts(img.data(), n, starts, i) != v[i])
                        fail("runs-point_starts", tag, w, runs<u8>::point_starts(img.data(), n, starts, i), v[i]);
            }
            u32 out[block_rows + 32];
            f.unpack(img.data(), n, out);
            for (unsigned i = 0; i < block_rows; ++i)
                if (out[i] != v[i])
                    fail("runs-unpack", tag, w, out[i], v[i]);
        }
}
template <unsigned K> void check_patched_k(std::mt19937_64 &rng) {
    using X = patched<K>;
    const u64 top = (u64{1} << K) - 1;
    for (unsigned e : {1u, 3u, 16u, 17u, 64u})
        for (unsigned rep = 0; rep < 3; ++rep) {
            std::vector<u64> v(block_rows);
            for (auto &x : v)
                x = rng() & top;
            std::vector<unsigned> rows(block_rows);
            for (unsigned i = 0; i < block_rows; ++i)
                rows[i] = i;
            std::shuffle(rows.begin(), rows.end(), rng);
            for (unsigned i = 0; i < e; ++i)
                v[rows[i]] = top + 1 + rng() % (0xffffffffull - top);
            std::vector<u8> img(patched_bytes(K, e) + 256);
            unsigned n = 0;
            if (!encode_patched(K, v.data(), img.data(), n) || n != e)
                fail("patched-encode", layout::plane256, K, n, e);
            ++blocks_checked;
            u64 s = 0;
            for (u64 x : v)
                s += x;
            if (X::sum(img.data()) != s)
                fail("patched-sum", layout::plane256, K, X::sum(img.data()), s);
            std::vector<u64> cs = constants(v, K, rng);
            cs.push_back(v[rows[0]]), cs.push_back(v[rows[0]] + 1), cs.push_back(top + 1), cs.push_back(u64{1} << 33);
            for (u64 c : cs) {
                unsigned ge = 0, eq = 0;
                u64 want[4] = {0, 0, 0, 0}, m[4];
                for (unsigned i = 0; i < block_rows; ++i) {
                    ge += v[i] >= c, eq += v[i] == c;
                    want[i / 64] |= u64{v[i] >= c} << (i % 64);
                }
                if (X::count_ge(img.data(), c) != ge)
                    fail("patched-count_ge", layout::plane256, K, X::count_ge(img.data(), c), ge);
                if (X::count_eq(img.data(), c) != eq)
                    fail("patched-count_eq", layout::plane256, K, X::count_eq(img.data(), c), eq);
                X::mask_ge(img.data(), c, m);
                for (unsigned w = 0; w < 4; ++w)
                    if (m[w] != want[w])
                        fail("patched-mask_ge", layout::plane256, K, m[w], want[w]);
            }
            for (const auto &sel : selections(rng)) {
                u64 want = 0;
                for (unsigned i = 0; i < block_rows; ++i)
                    if (sel[i / 64] >> (i % 64) & 1)
                        want += v[i];
                if (X::masked_sum(img.data(), sel.data()) != want)
                    fail("patched-masked_sum", layout::plane256, K, X::masked_sum(img.data(), sel.data()), want);
            }
            const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
            if (X::max(img.data()) != *hi)
                fail("patched-max", layout::plane256, K, X::max(img.data()), *hi);
            if (X::min(img.data()) != *lo)
                fail("patched-min", layout::plane256, K, X::min(img.data()), *lo);
            for (u64 c : cs) {
                unsigned first = block_rows;
                for (unsigned i = 0; i < block_rows && first == block_rows; ++i)
                    if (v[i] >= c)
                        first = i;
                if (X::first_ge(img.data(), c) != first)
                    fail("patched-first_ge", layout::plane256, K, X::first_ge(img.data(), c), first);
            }
            u32 out[block_rows];
            X::unpack(img.data(), out);
            for (unsigned i = 0; i < block_rows; ++i) {
                if (X::point(img.data(), i) != v[i])
                    fail("patched-point", layout::plane256, K, X::point(img.data(), i), v[i]);
                if (out[i] != v[i])
                    fail("patched-unpack", layout::plane256, K, out[i], v[i]);
            }
        }
}
template <std::size_t... I> void check_patched(std::mt19937_64 &rng, std::index_sequence<I...>) {
    (check_patched_k<I + 1>(rng), ...);
}
}  // namespace

int main() {
    std::mt19937_64 rng(42);
    for (layout l : {layout::local8, layout::plane64, layout::plane256})
        for (unsigned k = 1; k <= 32; ++k)
            check_packed(l, k, rng);
    for (unsigned k : {8u, 16u, 32u, 64u})
        check_packed(layout::lane, k, rng);
    for (unsigned w = 1; w <= 32; ++w)
        check_runs(w, rng);
    check_patched(rng, std::make_index_sequence<31>{});
    std::printf("%s bodies check: %ld blocks, %d failures\n", isa_name(), blocks_checked, failures);
    return failures != 0;
}
