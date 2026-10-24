// Checks for the bounds stage against an independent brute force: every lo
// among the series' values (and the minimum minus nothing else; a window's
// best lo is always one of its inliers) and every width, priced by the same
// cost table with exceptions counted by mathematical containment. Includes
// Codex Ikea LEAD's two witnesses (build/frame-collab/bounds-current-review):
// a 17-value series where a wider width is cheaper, and values at the top
// of the u64 domain.
#include "stage.h"

#include <cstdio>
#include <cstdlib>
#include <random>

using namespace frame_bounds;

namespace {
int failures = 0;
void expect(bool ok, const char *what) {
    if (!ok)
        std::printf("FAIL %s\n", what), ++failures;
}

// Mathematical containment: lo <= x and x - lo <= 2^K - 1.
choice brute(const std::vector<u64> &x, const cost_model &c) {
    u64 mn = ~u64{0}, mx = 0;
    for (u64 v : x)
        mn = std::min(mn, v), mx = std::max(mx, v);
    const unsigned vbits = bits(mx - mn);
    choice best;
    for (u64 lo : x)
        for (unsigned K = c.min_width; K <= 64; ++K) {
            std::size_t low = 0, high = 0;
            for (u64 v : x)
                low += v < lo, high += v >= lo && v - lo > mask(K);
            const choice t{lo, K, static_cast<u32>(low), static_cast<u32>(high), c(K, low, high, vbits)};
            if (better(t, best))
                best = t;
        }
    return best;
}
}  // namespace

int main() {
    // Witness 1: 17 values of 0 and 1; K1 costs 32 bytes, K8 costs 24.
    {
        cost_model c;
        for (unsigned K = 1; K <= 64; ++K)
            c.body[K] = 1000;
        c.body[1] = 32, c.body[8] = 24;
        std::vector<u64> x(17);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = i % 3 == 0;
        std::vector<u64> s;
        const choice e = exact(x.data(), x.size(), c, s), q = fast::choose<16>(x.data(), x.size(), c, sample_cost(c, 1), nullptr);
        expect(e.width == 8 && e.bytes == 24, "ragged n17 oracle takes the cheaper wider width");
        expect(q.width == 8 && q.bytes == 24, "ragged n17 choose takes the cheaper wider width");
    }
    // Witness 2: values UINT64_MAX-9 .. UINT64_MAX in a 256-row block.
    {
        cost_model c;
        for (unsigned K = 1; K <= 64; ++K)
            c.body[K] = 32 * K;
        std::vector<u64> x(256);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = ~u64{0} - i % 10;
        const choice q = fast::choose<32>(x.data(), x.size(), c, sample_cost(c, 8), nullptr);
        const choice r = realise(x.data(), x.size(), q.lo, q.width, bits(9), c);
        expect(q.width == 4 && r.low + r.high == 0, "top of domain: K4, no exceptions");
    }
    // Against brute force: small random series under a nonmonotone table.
    std::mt19937_64 g(17);
    for (int trial = 0; trial < 3000; ++trial) {
        cost_model c;
        const std::size_t n = 1 + g() % 40;
        for (unsigned K = 1; K <= 64; ++K)
            c.body[K] = static_cast<u32>((n + 7) / 8 * K + (g() % 4 == 0 ? g() % 16 : 0));
        std::vector<u64> x(n);
        const u64 base = g() >> (g() % 64), spread = u64{1} << (g() % 20);
        for (auto &v : x)
            v = g() % 8 == 0 ? g() : base + g() % spread;
        std::vector<u64> s;
        const choice b = brute(x, c), e = exact(x.data(), n, c, s);
        if (e.bytes != b.bytes) {
            std::printf("FAIL oracle trial %d n %zu: exact %zu brute %zu\n", trial, n, e.bytes, b.bytes);
            ++failures;
        }
        const choice q = fast::choose<16>(x.data(), n, c, sample_cost(c, std::max<std::size_t>(1, n / 16)), nullptr);
        u64 mn = ~u64{0}, mx = 0;
        for (u64 v : x)
            mn = std::min(mn, v), mx = std::max(mx, v);
        const choice r = realise(x.data(), n, q.lo, q.width, bits(mx - mn), c);
        if (r.bytes < b.bytes || r.bytes > q.bytes) {
            std::printf("FAIL choose trial %d n %zu: realised %zu claimed %zu brute %zu\n", trial, n, r.bytes, q.bytes, b.bytes);
            ++failures;
        }
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures != 0;
}
