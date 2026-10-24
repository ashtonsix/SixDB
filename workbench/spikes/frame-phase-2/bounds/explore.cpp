// Exploration for a cheaper bounds search: what one pass seeded by the
// previous block's window can find, and which in-pass statistics flag the
// blocks where it misses. One CSV row per block (not timed):
//
//   column,block,exact,full,sample_path,prior_path,feature...
//
// The prior is the previous block's sample-path choice, so the features
// describe the realistic case of a policy that mostly trusts its prior.
//
// usage: frame_bounds_explore COLS_DIR ROWS COLUMN... > blocks.csv
#include "fast.h"

#include <ikea/seriespack.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>

namespace sp = ikea::seriespack;
using namespace frame_bounds;

namespace {
constexpr std::size_t block_rows = 256;

std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(f.tellg()) / 8) / block_rows * block_rows;
    std::vector<u64> v(rows);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}

cost_model seriespack_cost() {
    cost_model c;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((c.body[I + 1] = static_cast<u32>((block_rows + sp::preset_layout<I + 1, sp::preset::compact>::tile_rows - 1) /
                                           sp::preset_layout<I + 1, sp::preset::compact>::tile_rows *
                                           sp::preset_layout<I + 1, sp::preset::compact>::tile_bytes)),
         ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    return c;
}

u64 sat_add(u64 a, u64 b) { return a > ~u64{0} - b ? ~u64{0} : a + b; }
u64 sat_sub(u64 a, u64 b) { return a < b ? 0 : a - b; }

// The prior-seeded candidate set: the prior window; widths K-2..K+1 at the
// prior's lo and at its top; the same widths at the block's minimum and
// maximum; the full width. All priced in one pass (here by realise()).
choice prior_path(const u64 *x, const cost_model &c, const choice &p, unsigned vbits, u64 mn, u64 mx, bool tighten_refine) {
    choice best = realise(x, block_rows, mn, std::max(c.min_width, vbits), vbits, c);
    const u64 ptop = sat_add(p.lo, mask(p.width));
    for (int d = -2; d <= 1; ++d) {
        const int Ki = static_cast<int>(p.width) + d;
        if (Ki < static_cast<int>(c.min_width) || Ki > 64)
            continue;
        const unsigned K = static_cast<unsigned>(Ki);
        for (u64 lo : {p.lo, sat_sub(ptop, mask(K)), mn, sat_sub(mx, mask(K))}) {
            const choice t = realise(x, block_rows, lo, K, vbits, c);
            if (better(t, best))
                best = t;
        }
    }
    if (!tighten_refine)
        return best;
    const choice t = evaluate(x, block_rows, best.lo, best.width, vbits, c);
    const choice r = realise(x, block_rows, t.lo, t.width, vbits, c);
    return refine(x, block_rows, c, vbits, better(r, best) ? r : best, 1);
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 4)
        return 2;
    const std::string dir = argv[1];
    const std::size_t rows = std::strtoull(argv[2], nullptr, 10);
    const cost_model c = seriespack_cost(), sc = sample_cost(c, 8);
    std::printf("column,block,exact,full,sample_path,prior_path,prior_plain,vbits,prev_vbits,prior_width,prior_bytes,pp_width,pp_low,pp_high,"
                "prior_low,prior_high,sample_bytes,distinct16,top_half,top_quarter,top_eighth,bottom_half,bottom_quarter,bottom_eighth,"
                "sample_out_low,sample_out_high,prev_low,prev_high\n");
    std::vector<u64> scratch;
    for (int a = 3; a < argc; ++a) {
        const auto v = load(dir + "/" + argv[a] + ".u64", rows);
        choice prior;
        bool have = false;
        unsigned prev_vbits = 0;
        for (std::size_t b = 0; b < v.size() / block_rows; ++b) {
            const u64 *x = v.data() + b * block_rows;
            const auto [mn, mx] = minmax(x, block_rows);
            if (mn == mx)
                continue;
            const unsigned vbits = bits(mx - mn);
            const choice e = exact(x, block_rows, c, scratch);
            const choice full = realise(x, block_rows, mn, std::max(c.min_width, vbits), vbits, c);
            const choice q = fast::guess<7, 32>(x, block_rows, c, sc, have ? &prior : nullptr);
            const choice f = realise(x, block_rows, q.lo, q.width, vbits, c);
            if (have) {
                const choice pp = prior_path(x, c, prior, vbits, mn, mx, true);
                const choice plain = prior_path(x, c, prior, vbits, mn, mx, false);
                const choice pw = realise(x, block_rows, prior.lo, prior.width, vbits, c);
                // In-pass statistics: tail masses relative to [min, max]
                // (leading zeros of x - min and max - x), and how many
                // distinct values a 16-value sample holds.
                const u64 span = mx - mn;
                unsigned th = 0, tq = 0, te = 0, bh = 0, bq = 0, be = 0;
                for (unsigned i = 0; i < block_rows; ++i) {
                    const u64 up = x[i] - mn, down = mx - x[i];
                    th += up > span / 2, tq += up > span - span / 4, te += up > span - span / 8;
                    bh += down > span / 2, bq += down > span - span / 4, be += down > span - span / 8;
                }
                std::array<u64, 16> s16;
                for (unsigned i = 0; i < 16; ++i)
                    s16[i] = x[i * 16];
                std::sort(s16.begin(), s16.end());
                const unsigned d16 = static_cast<unsigned>(std::unique(s16.begin(), s16.end()) - s16.begin());
                // The prior window checked on a 32-value sample (32 compares).
                const u64 ptop = sat_add(prior.lo, mask(prior.width));
                unsigned so_lo = 0, so_hi = 0;
                for (unsigned i = 0; i < 32; ++i)
                    so_lo += x[i * 8] < prior.lo, so_hi += x[i * 8] > ptop;
                std::printf("%s,%zu,%zu,%zu,%zu,%zu,%zu,%u,%u,%u,%zu,%u,%u,%u,%u,%u,%zu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", argv[a], b, e.bytes, full.bytes, f.bytes,
                            pp.bytes, plain.bytes, vbits, prev_vbits, prior.width, prior.bytes, pp.width, pp.low, pp.high, pw.low, pw.high, f.bytes, d16, th,
                            tq, te, bh, bq, be, so_lo, so_hi, prior.low, prior.high);
            }
            prior = f, have = true, prev_vbits = vbits;
        }
    }
}
