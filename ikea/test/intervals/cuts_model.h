#pragma once
#include <ikea/intervals.h>
#include <cassert>
#include <vector>
#include <bit>
namespace cuts_model {
namespace bl = ikea::intervals;
template <class Geometry>
void check(const Geometry &g, const std::vector<unsigned> &cuts, unsigned dead) {
    const unsigned n = cuts.size() - 1;
    assert(g.size() == n);
    unsigned visited = 0;
    assert(bl::range(g, 0, n, [&](unsigned r, auto bounds) {
        assert(!(dead & (1u << r)) && bounds.first == cuts[r] && bounds.limit == cuts[r + 1]);
        ++visited;
    }));
    assert(visited == n - std::popcount(dead));
    unsigned refused = 0;
    assert(!bl::range(g, n, 1, [&](auto, auto) { ++refused; }) && !refused);
    for (unsigned r = 0; r < n; ++r) {
        const auto bounds = bl::locate(g, r);
        assert(bool(bounds) == !(dead & (1u << r)));
        if (bounds) assert(bounds->first == cuts[r] && bounds->limit == cuts[r + 1]);
    }
    assert(!bl::locate(g, n));
    // Widen independently; neither signed queries nor final-cut clipping may wrap.
    for (std::int64_t lo = -1; lo <= std::int64_t(cuts.back()) + 1; ++lo) {
        unsigned owner = n;
        for (unsigned r = 0; r < n; ++r)
            if (std::int64_t(cuts[r]) <= lo && lo < std::int64_t(cuts[r + 1]) && !(dead & (1u << r)))
                owner = r;
        assert(bl::reverse(g, lo) == owner);
        for (std::int64_t hi = -1; hi <= std::int64_t(cuts.back()) + 1; ++hi) {
            bl::rank_window expected{n, n};
            if (lo < hi)
                for (unsigned r = 0; r < n; ++r)
                    if (!(dead & (1u << r)) && cuts[r] < cuts[r + 1] &&
                        std::int64_t(cuts[r]) < hi && std::int64_t(cuts[r + 1]) > lo) {
                        if (expected.first == n) expected.first = r;
                        expected.limit = r + 1;
                    }
            assert(bl::overlap(g, lo, hi) == expected);
        }
    }
    assert(bl::reverse(g, UINT64_MAX) == n);
    const auto all = bl::overlap(g, 0u, UINT64_MAX);
    const auto clipped = bl::overlap(g, 0u, cuts.back());
    assert(all == clipped);
}
}
