// Bytes census for the bounds stage: every method against the exact oracle,
// on real columns (256-row blocks, FOR's no-RLE path in runs/pipelines.cpp)
// and generated cases. Physical cost is SeriesPack's tile bytes at the
// chosen width (compact preset) plus exceptions, as in the runs spike.
//
// usage: frame_bounds_census COLS_DIR ROWS COLUMN...   (one JSON line per column and method)
#include "fast.h"

#include <ikea/seriespack.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <random>
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

template <sp::preset P> cost_model seriespack_cost(std::size_t n) {
    cost_model c;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((c.body[I + 1] = static_cast<u32>((n + sp::preset_layout<I + 1, P>::tile_rows - 1) / sp::preset_layout<I + 1, P>::tile_rows *
                                           sp::preset_layout<I + 1, P>::tile_bytes)),
         ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    return c;
}

// Generated columns: each targets one failure the notebook or Ashton named.
std::vector<u64> generate(const std::string &name, std::size_t rows) {
    std::mt19937_64 g(0x5eed ^ std::hash<std::string>{}(name));
    std::uniform_real_distribution<double> u(0, 1);
    std::vector<u64> v(rows);
    auto U = [&](u64 a, u64 b) { return a + g() % (b - a + 1); };
    for (std::size_t i = 0; i < rows; ++i) {
        const std::size_t b = i / block_rows;
        if (name == "gen_low")  // Ashton's {1004, 999, 1000, 9, 996, ...}
            v[i] = u(g) < 1.0 / 128 ? U(0, 20) : U(992, 1008);
        else if (name == "gen_two")  // rare outliers on both sides
            v[i] = u(g) < 0.01 ? U(0, 500000) : u(g) < 0.01 ? U(2000000, 4000000) : U(1000000, 1000255);
        else if (name == "gen_heavy")  // one-sided heavy tail
            v[i] = static_cast<u64>(std::exp(5 + 2 * std::sqrt(-2 * std::log(u(g) + 1e-300)) * std::cos(6.283185307 * u(g))));
        else if (name == "gen_bimodal")  // a 20% second cluster far above
            v[i] = u(g) < 0.2 ? U(1u << 20, (1u << 20) + 63) : U(0, 63);
        else if (name == "gen_shift") {  // level and spread change every 37 blocks
            const u64 seg = b / 37, level = (seg * 0x9e3779b97f4a7c15ull) >> 24, spread = u64{1} << (2 + seg % 20);
            v[i] = level + U(0, spread) + (u(g) < 0.005 ? U(0, u64{1} << 40) : 0);
        } else if (name == "gen_ts")  // increasing timestamps, 1% late arrivals
            v[i] = 1700000000000ull + 10 * i + U(0, 5) - (u(g) < 0.01 ? U(1000, 100000) : 0);
        else if (name == "gen_rand64")
            v[i] = g();
        else
            std::abort();
    }
    return v;
}

struct prior {
    bool valid = false;
    u64 lo = 0;
    unsigned K = 0;
};

// Guess, verify, refine. Guesses: the given proposals, the previous block's
// window, and windows at the previous block's width +-1 anchored at the
// minimum, the maximum and both ends of the first proposal. One counting pass
// prices them all (realise() per guess here); the cheapest is tightened.
struct guessed {
    choice c;
    int source;  // index of the winning proposal, or -1 prior window, -2 prior width
    int dK;      // chosen width minus the prior width
};
guessed guess_set(const u64 *x, std::size_t n, const cost_model &c, const prior &p, const std::vector<choice> &proposals, bool anchors = true) {
    const auto [mn, mx] = minmax(x, n);
    const unsigned vbits = bits(mx - mn);
    guessed g{realise(x, n, mn, std::max(c.min_width, vbits), vbits, c), -3, 0};
    for (std::size_t i = 0; i < proposals.size(); ++i) {
        const choice t = realise(x, n, proposals[i].lo, proposals[i].width, vbits, c);
        if (better(t, g.c))
            g.c = t, g.source = static_cast<int>(i);
    }
    if (p.valid) {
        const choice &first = proposals.empty() ? g.c : proposals[0];
        const u64 top = first.lo + mask(first.width);
        std::vector<std::pair<std::pair<u64, unsigned>, int>> w{{{p.lo, p.K}, -1}};
        for (int d = -1; d <= 1 && anchors; ++d) {
            const int K = static_cast<int>(p.K) + d;
            if (K < static_cast<int>(c.min_width) || K >= static_cast<int>(vbits))
                continue;
            const u64 m = mask(K);
            for (u64 lo : {mn, mx - std::min(mx, m), first.lo, top - std::min(top, m)})
                w.push_back({{lo, static_cast<unsigned>(K)}, -2});
        }
        for (auto [win, src] : w) {
            const choice t = realise(x, n, win.first, win.second, vbits, c);
            if (better(t, g.c))
                g.c = t, g.source = src;
        }
        g.dK = static_cast<int>(g.c.width) - static_cast<int>(p.K);
    }
    const choice t = evaluate(x, n, g.c.lo, g.c.width, vbits, c);
    const choice r = realise(x, n, t.lo, t.width, vbits, c);
    if (better(r, g.c))
        g.c = r;
    return g;
}

// Both one-sided searches: lo at the minimum, or hi at the maximum, every width.
choice two_anchor(const u64 *x, std::size_t n, const cost_model &c) {
    const auto [mn, mx] = minmax(x, n);
    const unsigned vbits = bits(mx - mn);
    choice best = min_exhaustive(x, n, c);
    for (unsigned K = c.min_width; K < vbits; ++K) {
        const choice t = realise(x, n, mx - mask(K), K, vbits, c);
        if (better(t, best))
            best = t;
    }
    return best;
}

struct tally {
    std::size_t bytes = 0, regret_blocks = 0, regret_bytes = 0, worst = 0, low_ex = 0, high_ex = 0;
};

std::FILE *features = nullptr;

void census(const std::string &column, const std::vector<u64> &v) {
    const std::size_t blocks = v.size() / block_rows;
    const cost_model c = seriespack_cost<sp::preset::compact>(block_rows);
    const char *names[] = {"exact",        "min_exhaustive", "min_prior",     "two_anchor",   "extremes5",    "distinct5",
                           "sample32",     "e5_s32",         "e5_s32_pw",     "e5_s32_pw_anchors", "e3_s32_pw",  "e5_s16_pw",
                           "s32_pw",       "e5_pw_anchors",  "e9_s32_pw",     "e5_s32_pw_r16", "guess_e5_s32", "guess_e3_s16", "guess_e5_s32_ref1", "guess_e5_s32_ref4", "guess_e3_s16_ref4", "guess_e2_s16_ref4", "guess_e1_s32_ref1", "guess_e2_s32_ref1", "guess_e3_s32_ref1", "guess_e1_s32_ref2", "guess_e1_s16_ref2", "fast_s32_ref1", "fast_s32_ref1_e5", "fast_s16_ref1", "fast_prior_ref2", "narrow_min_exhaustive", "fast_fb_e5", "fast_fb_exact", "gated_j1.00_r16", "gated_j1.03_r16", "gated_j1.10_r16", "gated_j1.03_r8", "gated_j1.03_r64", "fast_t9_s32", "fast_t7_s32", "fast_t5_s32", "fast_t4_s32", "fast_t5_s16", "quant_s32", "quant_s16", "quant_s32_r16", "quant_s16_r8", "ladder_s32", "ladder_s16", "lean_s32", "lean_s16", "local_s32", "local_s16"};
    constexpr std::size_t M = std::size(names);
    std::array<tally, M> t{};
    std::array<int, M> kp;
    kp.fill(-1);
    std::array<prior, M> pr{};
    std::array<std::pair<u32, u32>, M> prq{};  // the prior's exceptions below and above
    std::vector<u64> scratch;
    std::size_t constant = 0;
    std::array<std::size_t, M> fallback{};
    std::array<fast::gate_state, M> gs{};
    const std::array<fast::gate, 5> gates{{{1.00, 16}, {1.03, 16}, {1.10, 16}, {1.03, 8}, {1.03, 64}}};
    for (std::size_t b = 0; b < blocks; ++b) {
        const u64 *x = v.data() + b * block_rows;
        const auto [mn, mx] = minmax(x, block_rows);
        if (mn == mx) {
            ++constant;  // constant blocks exit before bounds in every pipeline
            continue;
        }
        const unsigned vbits = bits(mx - mn);
        std::array<choice, M> ch;
        ch[0] = exact(x, block_rows, c, scratch);
        ch[1] = min_exhaustive(x, block_rows, c);
        ch[2] = min_prior(x, block_rows, c, b % 16 ? kp[2] : -1);
        ch[3] = two_anchor(x, block_rows, c);
        auto ext = [&]<unsigned T>(std::integral_constant<unsigned, T>) {
            const choice o = from_extremes<T>(order_stats<T>(x, block_rows), block_rows, vbits, c);
            return realise(x, block_rows, o.lo, o.width, vbits, c);
        };
        ch[4] = ext(std::integral_constant<unsigned, 5>{});
        std::vector<u64> sorted(x, x + block_rows);
        std::sort(sorted.begin(), sorted.end());
        const choice d5 = from_distinct<5>(distinct_stats_sorted<5>(sorted), vbits, c);
        const choice e3 = from_extremes<3>(order_stats<3>(x, block_rows), block_rows, vbits, c);
        const choice e5 = from_extremes<5>(order_stats<5>(x, block_rows), block_rows, vbits, c);
        const choice e9 = from_extremes<9>(order_stats<9>(x, block_rows), block_rows, vbits, c);
        const choice s32 = sample_guess<32>(x, block_rows, sample_cost(c, 8), vbits);
        const choice s16 = sample_guess<16>(x, block_rows, sample_cost(c, 16), vbits);
        const prior none{};
        ch[5] = realise(x, block_rows, d5.lo, d5.width, vbits, c);
        ch[6] = guess_set(x, block_rows, c, none, {s32}).c;
        ch[7] = guess_set(x, block_rows, c, none, {e5, s32}).c;
        const guessed g = guess_set(x, block_rows, c, pr[8], {e5, s32}, false);
        ch[8] = g.c;
        ch[9] = guess_set(x, block_rows, c, pr[9], {e5, s32}).c;
        ch[10] = guess_set(x, block_rows, c, pr[10], {e3, s32}, false).c;
        ch[11] = guess_set(x, block_rows, c, pr[11], {e5, s16}, false).c;
        ch[12] = guess_set(x, block_rows, c, pr[12], {s32}, false).c;
        ch[13] = guess_set(x, block_rows, c, pr[13], {e5}).c;
        ch[14] = guess_set(x, block_rows, c, pr[14], {e9, s32}, false).c;
        ch[15] = b % 16 ? guess_set(x, block_rows, c, pr[15], {e5, s32}, false).c : ch[0];
        for (std::size_t m : {16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26}) {
            const window w{pr[m].lo, pr[m].K, pr[m].valid};
            const choice q = m == 16   ? guess<5, 32>(x, block_rows, c, sample_cost(c, 8), w)
                             : m == 17 ? guess<3, 16>(x, block_rows, c, sample_cost(c, 16), w)
                             : m == 18 ? guess<5, 32>(x, block_rows, c, sample_cost(c, 8), w, 1)
                             : m == 19 ? guess<5, 32>(x, block_rows, c, sample_cost(c, 8), w, 4)
                             : m == 20 ? guess<3, 16>(x, block_rows, c, sample_cost(c, 16), w, 4)
                             : m == 21 ? guess<2, 16>(x, block_rows, c, sample_cost(c, 16), w, 4)
                             : m == 22 ? guess<1, 32>(x, block_rows, c, sample_cost(c, 8), w, 1)
                             : m == 23 ? guess<2, 32>(x, block_rows, c, sample_cost(c, 8), w, 1)
                             : m == 24 ? guess<3, 32>(x, block_rows, c, sample_cost(c, 8), w, 1)
                             : m == 25 ? guess<1, 32>(x, block_rows, c, sample_cost(c, 8), w, 2)
                                       : guess<1, 16>(x, block_rows, c, sample_cost(c, 16), w, 2);
            ch[m] = realise(x, block_rows, q.lo, q.width, vbits, c);
            if (ch[m].bytes > q.bytes)
                std::abort();  // tightening never adds exceptions
        }
        fast::trace tr;
        for (std::size_t m : {27, 28, 29, 30, 31, 32, 33}) {
            const choice pc{pr[m].lo, pr[m].K, 0, 0, 0};
            const choice *pp = pr[m].valid ? &pc : nullptr;
            const choice q = m == 27   ? fast::guess<5, 32>(x, block_rows, c, sample_cost(c, 8), pp, [&] { fast::options o; o.t = &tr; return o; }())
                             : m == 28 ? fast::guess<5, 32>(x, block_rows, c, sample_cost(c, 8), pp, {1, true})
                             : m == 29 ? fast::guess<5, 16>(x, block_rows, c, sample_cost(c, 16), pp)
                             : m == 30 ? fast::guess<5, 16>(x, block_rows, c, sample_cost(c, 16), pp, {2, false, false})
                             : m == 31 ? fast::min_exhaustive(x, block_rows, c)
                                       : [&] {
                                             // Bad-guess test: exceptions on both sides, which the
                                             // one-ended refinement cannot trade off.
                                             const choice g = fast::guess<5, 32>(x, block_rows, c, sample_cost(c, 8), pp);
                                             const choice gr = realise(x, block_rows, g.lo, g.width, vbits, c);
                                             if (!(gr.low && gr.high))
                                                 return gr;
                                             fallback[m]++;
                                             const choice f = m == 32 ? fast::guess<5, 32>(x, block_rows, c, sample_cost(c, 8), pp, {2, true})
                                                                      : exact(x, block_rows, c, scratch);
                                             const choice fr = realise(x, block_rows, f.lo, f.width, vbits, c);
                                             return better(fr, gr) ? fr : gr;
                                         }();
            ch[m] = realise(x, block_rows, q.lo, q.width, vbits, c);
            if (ch[m].bytes > q.bytes)
                std::abort();
        }
        for (std::size_t m = 34; m < 39; ++m) {
            const choice q = fast::gated<5, 32>(x, block_rows, c, sample_cost(c, 8), gs[m], gates[m - 34]);
            ch[m] = realise(x, block_rows, q.lo, q.width, vbits, c);
            gs[m].prior.bytes = ch[m].bytes;  // the emission pass knows the exact bytes
            fallback[m] = gs[m].fallbacks + gs[m].refreshes;
        }
        for (std::size_t m = 52; m < 54; ++m) {
            const choice pc{pr[m].lo, pr[m].K, prq[m].first, prq[m].second, 0};
            const choice q = m == 52 ? fast::guess_local<32>(x, block_rows, c, sample_cost(c, 8), pr[m].valid ? &pc : nullptr)
                                     : fast::guess_local<16>(x, block_rows, c, sample_cost(c, 16), pr[m].valid ? &pc : nullptr);
            ch[m] = realise(x, block_rows, q.lo, q.width, vbits, c);
            if (ch[m].bytes > q.bytes) {
                std::fprintf(stderr, "%s b%zu m%zu: claimed lo %llu K %u %u/%u %zuB, realised %u/%u %zuB, min %llu vbits %u\n", column.c_str(), b, m,
                             (unsigned long long)q.lo, q.width, q.low, q.high, q.bytes, ch[m].low, ch[m].high, ch[m].bytes, (unsigned long long)mn, vbits);
                std::abort();
            }
        }
        for (std::size_t m = 50; m < 52; ++m) {
            const choice pc{pr[m].lo, pr[m].K, prq[m].first, prq[m].second, 0};
            fast::options o;
            o.lean = true;
            const choice q = m == 50 ? fast::guess<7, 32>(x, block_rows, c, sample_cost(c, 8), pr[m].valid ? &pc : nullptr, o)
                                     : fast::guess<7, 16>(x, block_rows, c, sample_cost(c, 16), pr[m].valid ? &pc : nullptr, o);
            ch[m] = realise(x, block_rows, q.lo, q.width, vbits, c);
        }
        for (std::size_t m = 48; m < 50; ++m) {
            const choice pc{pr[m].lo, pr[m].K, prq[m].first, prq[m].second, 0};
            const choice *pp = pr[m].valid ? &pc : nullptr;
            fast::options o;
            o.ladder = true;
            const choice q = m == 48 ? fast::guess<7, 32>(x, block_rows, c, sample_cost(c, 8), pp, o) : fast::guess<7, 16>(x, block_rows, c, sample_cost(c, 16), pp, o);
            ch[m] = realise(x, block_rows, q.lo, q.width, vbits, c);
        }
        for (std::size_t m = 44; m < 48; ++m) {
            // Quantile reads of a sorted sample, seeded by the prior's
            // exception counts; _rN runs the full sample oracle every N blocks.
            const cost_model s8 = sample_cost(c, 8), s16c = sample_cost(c, 16);
            const unsigned every = m == 46 ? 16 : m == 47 ? 8 : 0;
            const bool full_now = !pr[m].valid || (every && b % every == 0);
            const choice pc{pr[m].lo, pr[m].K, prq[m].first, prq[m].second, 0};
            const choice *pp = pr[m].valid ? &pc : nullptr;
            fast::options o;
            o.quantiles = !full_now;
            const choice q = (m == 44 || m == 46) ? fast::guess<7, 32>(x, block_rows, c, s8, pp, o) : fast::guess<7, 16>(x, block_rows, c, s16c, pp, o);
            ch[m] = realise(x, block_rows, q.lo, q.width, vbits, c);
        }
        if (std::getenv("BOUNDS_DEBUG") && ch[44].bytes > ch[0].bytes + 40) {
            static int shown = 0;
            if (shown++ < 12)
                std::fprintf(stderr, "%s b%zu vbits %u exact K%u lo-ex %u hi-ex %u %zuB | quant K%u %u %u %zuB | prior counts %u %u K%u | t7 %zuB\n", column.c_str(), b,
                             vbits, ch[0].width, ch[0].low, ch[0].high, ch[0].bytes, ch[44].width, ch[44].low, ch[44].high, ch[44].bytes, prq[44].first,
                             prq[44].second, pr[44].K, ch[40].bytes);
        }
        for (std::size_t m = 39; m < 44; ++m) {
            const choice pc{pr[m].lo, pr[m].K, 0, 0, 0};
            const choice *pp = pr[m].valid ? &pc : nullptr;
            const cost_model s8 = sample_cost(c, 8), s16c = sample_cost(c, 16);
            const choice q = m == 39   ? fast::guess<9, 32>(x, block_rows, c, s8, pp)
                             : m == 40 ? fast::guess<7, 32>(x, block_rows, c, s8, pp)
                             : m == 41 ? fast::guess<5, 32>(x, block_rows, c, s8, pp)
                             : m == 42 ? fast::guess<4, 32>(x, block_rows, c, s8, pp)
                                       : fast::guess<5, 16>(x, block_rows, c, s16c, pp);
            ch[m] = realise(x, block_rows, q.lo, q.width, vbits, c);
        }
        if (features)
            std::fprintf(features, "%s %zu %zu %zu %d %zu %zu %zu %u %u %u %u %u\n", column.c_str(), b, ch[0].bytes, ch[27].bytes, tr.winner, tr.guessed,
                         tr.sample, tr.refined, tr.rounds, ch[27].low, ch[27].high, ch[27].width, vbits);
        for (std::size_t m = 0; m < M; ++m) {
            // Every reported cost is the window's real cost.
            const choice r = realise(x, block_rows, ch[m].lo, ch[m].width, vbits, c);
            if (r.bytes != ch[m].bytes || r.bytes < ch[0].bytes) {
                std::fprintf(stderr, "%s block %zu method %s: %zu vs realised %zu, exact %zu\n", column.c_str(), b, names[m], ch[m].bytes, r.bytes,
                             ch[0].bytes);
                std::abort();
            }
            const std::size_t d = r.bytes - ch[0].bytes;
            t[m].bytes += r.bytes, t[m].regret_blocks += d != 0, t[m].regret_bytes += d, t[m].worst = std::max(t[m].worst, d);
            t[m].low_ex += r.low, t[m].high_ex += r.high;
            kp[m] = static_cast<int>(r.width);
            pr[m] = {true, r.lo, r.width};
            prq[m] = {r.low, r.high};
        }
    }
    for (std::size_t m = 0; m < M; ++m)
        std::printf("{\"column\":\"%s\",\"method\":\"%s\",\"blocks\":%zu,\"constant\":%zu,\"bytes\":%zu,\"exact_bytes\":%zu,\"regret_pct\":%.3f,"
                    "\"regret_blocks\":%zu,\"worst_block_bytes\":%zu,\"low_exceptions\":%zu,\"high_exceptions\":%zu,\"fallbacks\":%zu}\n",
                    column.c_str(), names[m], blocks, constant, t[m].bytes, t[0].bytes, 100.0 * t[m].regret_bytes / std::max<std::size_t>(1, t[0].bytes),
                    t[m].regret_blocks, t[m].worst, t[m].low_ex, t[m].high_ex, fallback[m]);
    std::fflush(stdout);
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 4)
        return 2;
    const std::string dir = argv[1];
    const std::size_t rows = std::strtoull(argv[2], nullptr, 10);
    if (const char *f = std::getenv("BOUNDS_FEATURES"))
        features = std::fopen(f, "w");
    for (int a = 3; a < argc; ++a) {
        const std::string col = argv[a];
        census(col, col.rfind("gen_", 0) == 0 ? generate(col, rows / block_rows * block_rows) : load(dir + "/" + col + ".u64", rows));
    }
}
