// Every stage tree against a plain-array oracle: sum, count and filter under
// every comparison at boundary thresholds, every point, whole reads and
// selections, on generated columns that exercise the laws' edge cases
// (constant blocks, exceptions on both sides, 64-bit extremes, signed and
// monotone deltas, a partial last part).
#include "trees.h"

#include <cstdio>
#include <random>
#include <set>

using namespace frame3;

namespace {
int failures = 0;
void expect(bool ok, const std::string &what) {
    if (!ok && failures++ < 20)
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

std::vector<std::pair<std::string, std::vector<u64>>> columns() {
    std::mt19937_64 rng(7);
    const std::size_t n = part_rows * 2 + block_rows * 17 + 37;  // a partial last part and block
    std::vector<std::pair<std::string, std::vector<u64>>> out;
    auto add = [&](std::string name, auto f) {
        std::vector<u64> v(n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = f(i);
        out.emplace_back(std::move(name), std::move(v));
    };
    add("narrow", [&](std::size_t) { return rng() % 200; });
    add("constant_blocks", [&](std::size_t i) { return (i / block_rows) % 3 ? 77 + i / block_rows : rng() % 9; });
    add("low_outliers", [&](std::size_t) { return rng() % 50 == 0 ? rng() % 10 : 1000 + rng() % 64; });
    add("two_sided", [&](std::size_t) {
        const u64 r = rng() % 100;
        return r == 0 ? rng() % 5 : r == 1 ? (u64{1} << 40) + rng() : 5000 + rng() % 300;
    });
    add("extremes", [&](std::size_t) {
        const u64 r = rng() % 8;
        return r == 0 ? ~u64{0} : r == 1 ? 0 : ~u64{0} - rng() % 1000;
    });
    add("wide", [&](std::size_t) { return rng(); });
    add("monotone", [&, t = u64{1} << 50](std::size_t) mutable { return t += rng() % 1000; });
    add("signed_walk", [&, t = u64{1} << 33](std::size_t) mutable { return t += (rng() % 2001) - 1000; });
    add("sawtooth", [&](std::size_t i) { return (i % 700) * 3 + (rng() % 2); });
    return out;
}

void check_column(const std::string &what, const column &c, const std::vector<u64> &v) {
    const std::size_t n = v.size();
    u128 s = 0;
    for (u64 x : v)
        s += x;
    expect(c.sum() == s, what + " sum");

    std::set<u64> thresholds{0, 1, 2, ~u64{0}, ~u64{0} - 1, v[0], v[n / 2], v[n - 1]};
    std::mt19937_64 rng(11);
    for (int i = 0; i < 12; ++i)
        thresholds.insert(v[rng() % n]), thresholds.insert(v[rng() % n] + 1), thresholds.insert(v[rng() % n] - 1);
    std::vector<u64> words((n + 63) / 64), oracle((n + 63) / 64);
    for (comparison op : {comparison::equal, comparison::less, comparison::less_equal, comparison::greater, comparison::greater_equal})
        for (u64 th : thresholds) {
            u64 k = 0;
            std::fill(oracle.begin(), oracle.end(), 0);
            for (std::size_t i = 0; i < n; ++i)
                if (test(op, v[i], th))
                    ++k, oracle[i / 64] |= u64{1} << (i % 64);
            expect(c.count(op, th) == k, what + " count op " + std::to_string(int(op)) + " t " + std::to_string(th));
            std::fill(words.begin(), words.end(), 0);
            c.filter(op, th, words.data());
            expect(words == oracle, what + " filter op " + std::to_string(int(op)) + " t " + std::to_string(th));
        }
    bool points = true;
    for (std::size_t i = 0; i < n; ++i)
        points &= c.get(i) == v[i];
    expect(points, what + " get");
    std::vector<u64> all(n);
    c.read(all.data());
    expect(all == v, what + " read");
    for (unsigned density : {1u, 30u, 200u}) {
        std::fill(words.begin(), words.end(), 0);
        std::vector<u64> want;
        for (std::size_t i = 0; i < n; ++i)
            if (rng() % 256 < density)
                words[i / 64] |= u64{1} << (i % 64), want.push_back(v[i]);
        std::vector<u64> got(want.size() + 1);
        const std::size_t k = c.select(words.data(), got.data());
        got.resize(k);
        expect(got == want, what + " select density " + std::to_string(density));
    }
}
void check(const std::string &cname, const std::vector<u64> &v, const tree &t) {
    column c(t.make(), v);
    check_column(cname + " / " + t.name, c, v);
}
}  // namespace

int main() {
    for (const auto &[name, v] : columns())
        for (const auto &t : trees())
            check(name, v, t);
    for (const auto &[name, v] : columns())
        for (const auto &t : placeholder_trees())
            check(name, v, t);
    // Two columns sharing one TuplePack record leaf.
    {
        const auto cols = columns();
        const auto &a = cols[0].second, &b = cols[2].second;  // narrow, low_outliers
        auto shared = std::make_shared<record>(2);
        column ca(std::make_unique<bound>(std::make_unique<record_field>(shared, 0)), a);
        column cb(std::make_unique<bound>(std::make_unique<record_field>(shared, 1)), b);
        check_column("narrow / bound_record0", ca, a);
        check_column("low_outliers / bound_record1", cb, b);
    }
    if (failures) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::printf("frame3 check: all trees exact on %zu columns\n", columns().size());
}
