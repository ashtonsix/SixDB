// Encode-side cost of each bounds method: ns per 256-row block over a whole
// column, warm, carrying each method's prior from block to block. Constant
// blocks are skipped (they exit before bounds). Each method's total bytes
// are checked against an untimed recomputation after the timers stop.
//
// usage: frame_bounds_time COLS_DIR ROWS ROUNDS COLUMN...
#include "fast.h"

#include <ikea/seriespack.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
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

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// A method maps (block, its index, its prior) to a choice; the harness
// carries the prior. Returns total bytes so the work cannot be dropped.
using method = std::function<choice(const u64 *, std::size_t, const window &)>;

std::size_t run(const std::vector<const u64 *> &blocks, const method &m) {
    window prior;
    std::size_t total = 0;
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        const choice r = m(blocks[b], b, prior);
        total += r.bytes;
        prior = {r.lo, r.width, true, r.low, r.high};
    }
    return total;
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5)
        return 2;
    const std::string dir = argv[1];
    const std::size_t rows = std::strtoull(argv[2], nullptr, 10);
    const unsigned rounds = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
    const cost_model c = seriespack_cost<sp::preset::compact>(block_rows);
    const cost_model s16 = sample_cost(c, 16), s32 = sample_cost(c, 8);
    std::vector<u64> scratch;
    const std::pair<const char *, method> methods[] = {
        {"minmax",
         [&](const u64 *x, std::size_t, const window &) {
             const auto [mn, mx] = minmax(x, block_rows);
             return choice{mn, bits(mx - mn), 0, 0, mx - mn};
         }},
        {"min_exhaustive", [&](const u64 *x, std::size_t, const window &) { return min_exhaustive(x, block_rows, c); }},
        {"min_prior_r16",
         [&](const u64 *x, std::size_t b, const window &p) { return min_prior(x, block_rows, c, b % 16 && p.valid ? static_cast<int>(p.width) : -1); }},
        {"exact", [&](const u64 *x, std::size_t, const window &) { return exact(x, block_rows, c, scratch); }},
        {"narrow_min_exhaustive", [&](const u64 *x, std::size_t, const window &) { return fast::min_exhaustive(x, block_rows, c); }},
        {"fast_t7_s32",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             return fast::guess<7, 32>(x, block_rows, c, s32, p.valid ? &pc : nullptr);
         }},
        {"fast_t5_s32",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             return fast::guess<5, 32>(x, block_rows, c, s32, p.valid ? &pc : nullptr);
         }},
        {"fast_t7_s32_e7",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             return fast::guess<7, 32>(x, block_rows, c, s32, p.valid ? &pc : nullptr, {1, true});
         }},
        {"quant_s32",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             fast::options o;
             o.quantiles = true;
             return fast::guess<7, 32>(x, block_rows, c, s32, p.valid ? &pc : nullptr, o);
         }},
        {"quant_s16",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             fast::options o;
             o.quantiles = true;
             return fast::guess<7, 16>(x, block_rows, c, s16, p.valid ? &pc : nullptr, o);
         }},
        {"ladder_s32",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             fast::options o;
             o.ladder = true;
             return fast::guess<7, 32>(x, block_rows, c, s32, p.valid ? &pc : nullptr, o);
         }},
        {"ladder_s16",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             fast::options o;
             o.ladder = true;
             return fast::guess<7, 16>(x, block_rows, c, s16, p.valid ? &pc : nullptr, o);
         }},
        {"lean_s32",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             fast::options o;
             o.lean = true;
             return fast::guess<7, 32>(x, block_rows, c, s32, p.valid ? &pc : nullptr, o);
         }},
        {"lean_s16",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             fast::options o;
             o.lean = true;
             return fast::guess<7, 16>(x, block_rows, c, s16, p.valid ? &pc : nullptr, o);
         }},
        {"local_s32",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             return fast::guess_local<32>(x, block_rows, c, s32, p.valid ? &pc : nullptr);
         }},
        {"local_s16",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             return fast::guess_local<16>(x, block_rows, c, s16, p.valid ? &pc : nullptr);
         }},
        {"quant_s16_noref",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             fast::options o;
             o.quantiles = true;
             o.refine_rounds = 0;
             return fast::guess<7, 16>(x, block_rows, c, s16, p.valid ? &pc : nullptr, o);
         }},
        {"fast_prior_ref2",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             return fast::guess<5, 16>(x, block_rows, c, s16, p.valid ? &pc : nullptr, {2, false, false});
         }},
        {"fast_s16_ref1",
         [&](const u64 *x, std::size_t, const window &p) {
             const choice pc{p.lo, p.width, p.low, p.high, 0};
             return fast::guess<5, 16>(x, block_rows, c, s16, p.valid ? &pc : nullptr);
         }},
    };
    for (int a = 4; a < argc; ++a) {
        const std::string col = argv[a];
        const auto v = load(dir + "/" + col + ".u64", rows);
        std::vector<const u64 *> blocks;
        for (std::size_t b = 0; b < v.size() / block_rows; ++b) {
            const auto [mn, mx] = minmax(v.data() + b * block_rows, block_rows);
            if (mn != mx)
                blocks.push_back(v.data() + b * block_rows);
        }
        if (blocks.empty())
            continue;
        std::size_t exact_bytes = 0;
        for (const u64 *x : blocks)
            exact_bytes += exact(x, block_rows, c, scratch).bytes;
        for (const auto &[name, m] : methods) {
            std::vector<double> ns;
            std::size_t sink = 0;
            for (unsigned r = 0; r < rounds; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                sink = run(blocks, m);
                ns.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / blocks.size());
            }
            if (sink != run(blocks, m))
                std::abort();
            std::printf("{\"column\":\"%s\",\"method\":\"%s\",\"blocks\":%zu,\"ns_per_block\":%.1f,\"bytes\":%zu,\"exact_bytes\":%zu}\n", col.c_str(), name,
                        blocks.size(), median(ns), sink, exact_bytes);
            std::fflush(stdout);
        }
    }
}
