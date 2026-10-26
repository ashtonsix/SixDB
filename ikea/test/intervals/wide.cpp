// wide_sizes and wide_ends against the series they reconstruct: every end,
// every start and a carried traversal, including signed differences that
// wrap modulo 2^64, spacings that are not multiples of 16 and a partial
// final region; then lower and upper bound over nondecreasing series.
#include <ikea/intervals/search.h>
#include <ikea/intervals/wide.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace in = ikea::intervals;
using u64 = std::uint64_t;

namespace {
int failures = 0;
void expect(bool ok, const char *what, unsigned spacing, unsigned n) {
    if (!ok && failures++ < 20)
        std::fprintf(stderr, "FAIL %s spacing %u n %u\n", what, spacing, n);
}

struct encoded {
    std::vector<u64> sizes, ends, checkpoints;
};
encoded encode(const std::vector<u64> &x, unsigned spacing) {
    encoded e{std::vector<u64>(x.size(), 12345), std::vector<u64>(x.size(), 6789), {}};
    for (unsigned r = 0; r < x.size(); ++r) {
        if (r % spacing == 0)
            e.checkpoints.push_back(x[r]); // the fields here stay arbitrary: they are ignored
        else
            e.sizes[r] = x[r] - x[r - 1], e.ends[r] = x[r] - e.checkpoints.back();
    }
    return e;
}

template <class Source> void check_source(const Source &s, const std::vector<u64> &x, bool ordered, unsigned spacing) {
    const unsigned n = static_cast<unsigned>(x.size());
    bool ends = true, starts = true, visited = true;
    for (unsigned r = 0; r < n; ++r)
        ends &= s.at(r) == x[r], starts &= s.start(r) == (r ? x[r - 1] : x[0]);
    unsigned seen = 0;
    s.visit_intervals(0, n, [&](unsigned r, in::interval<u64> v) {
        visited &= r == seen++ && v.limit == x[r] && v.first == (r ? x[r - 1] : x[0]);
    });
    // Every range starting and ending inside, at and across region edges.
    for (unsigned first : {0u, 1u, spacing - 1, spacing, spacing + 1, n / 3, n - 1})
        for (unsigned take : {1u, 2u, spacing - 1, spacing, spacing + 1, n})
            if (first < n && take && take <= n - first) {
                unsigned next = first;
                s.visit_intervals(first, take, [&](unsigned r, in::interval<u64> v) {
                    visited &= r == next++ && v.limit == x[r] && v.first == (r ? x[r - 1] : x[0]);
                });
                visited &= next == first + take;
            }
    expect(ends, "at", spacing, n);
    expect(starts, "start", spacing, n);
    expect(visited && seen == n, "visit_intervals", spacing, n);
    if (!ordered || !n)
        return;
    std::mt19937_64 rng(5);
    bool searched = true;
    for (int i = 0; i < 200; ++i) {
        const u64 q = i < 4 ? std::array<u64, 4>{0, x.front(), x.back(), x.back() + 1}[i] : x[rng() % n] + (rng() % 3) - 1;
        searched &= in::lower_bound(s, q) == std::lower_bound(x.begin(), x.end(), q) - x.begin();
        searched &= in::upper_bound(s, q) == std::upper_bound(x.begin(), x.end(), q) - x.begin();
    }
    expect(searched, "search", spacing, n);
}
} // namespace

int main() {
    std::mt19937_64 rng(3);
    for (unsigned spacing : {1u, 3u, 16u, 64u, 256u})
        for (unsigned n : {1u, 2u, 17u, 255u, 256u, 700u}) {
            std::vector<u64> walk(n), rising(n);
            u64 a = ~u64{0} - 500, b = u64{1} << 60;
            for (unsigned r = 0; r < n; ++r) {
                walk[r] = a += (rng() % 2001) - 1000; // wraps past 2^64
                rising[r] = b += rng() % 4 ? rng() % 1000 : 0;
            }
            for (const auto *x : {&walk, &rising}) {
                const auto e = encode(*x, spacing);
                const bool ordered = x == &rising;
                check_source(in::wide_sizes{e.sizes.data(), e.checkpoints.data(), n, spacing}, *x, ordered, spacing);
                check_source(in::wide_ends{e.ends.data(), e.checkpoints.data(), n, spacing}, *x, ordered, spacing);
            }
        }
    // Fields in another encoding (xor), reconstructed in place over them.
    for (unsigned spacing : {1u, 3u, 16u, 64u})
        for (unsigned n : {1u, 17u, 256u, 700u}) {
            std::vector<u64> x(n);
            u64 a = 12345;
            for (unsigned r = 0; r < n; ++r)
                x[r] = a += (rng() % 2001) - 1000;
            const auto e = encode(x, spacing);
            constexpr u64 key = u64{1} << 63;
            for (int which = 0; which < 2; ++which)
                for (unsigned first : {0u, 1u, spacing, n / 2})
                    if (first < n) {
                        std::vector<u64> fields = which ? e.ends : e.sizes;
                        for (auto &f : fields)
                            f ^= key;
                        const unsigned take = n - first;
                        auto write = [&](unsigned r, in::interval<u64> v) { fields[r] = v.limit; };
                        if (which)
                            in::wide_ends{fields.data(), e.checkpoints.data(), n, spacing, key}.visit_intervals(first, take, write);
                        else
                            in::wide_sizes{fields.data(), e.checkpoints.data(), n, spacing, key}.visit_intervals(first, take, write);
                        // A range from first reads ranks before it through start(),
                        // which still sees the encoded fields: compare the range only.
                        expect(std::equal(fields.begin() + first, fields.end(), x.begin() + first), "in place", spacing, n);
                    }
        }
    // Boundary arithmetic at the largest counts: a final region of one rank
    // answers from checkpoints alone, without reading a field.
    {
        const u64 checkpoints[2] = {1, 10};
        const unsigned n = ~0u - 1, spacing = ~0u - 2;
        expect(in::lower_bound(in::wide_sizes{nullptr, checkpoints, n, spacing}, u64{11}) == n, "wide_sizes limit", spacing, n);
        expect(in::lower_bound(in::wide_ends{nullptr, checkpoints, n, spacing}, u64{11}) == n, "wide_ends limit", spacing, n);
    }
    if (failures) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::printf("intervals wide: reconstruction and search exact\n");
}
