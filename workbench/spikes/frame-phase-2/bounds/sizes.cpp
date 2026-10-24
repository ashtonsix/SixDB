// The bounds stage on the runs spike's second consumer: run sizes minus one
// (runs/README.md), per block (2-128 fields, 8-row Local tiles, exceptions as
// u8 rank and value) and per 16-block group (one series, preset tiles, u16
// ranks). Today's stage there fixes lo = 0 and tries every width
// (runs/read.cpp choose_bounds); here lo is free. Bytes only, against the
// exact oracle; values decode identically in every arm.
//
// usage: frame_bounds_sizes COLS_DIR ROWS COLUMN...
#include "fast.h"
#include "stage.h"

#include <ikea/seriespack.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>

namespace sp = ikea::seriespack;
using namespace frame_bounds;

namespace {
constexpr std::size_t block_rows = 256, max_runs = 128, group_blocks = 16;

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

// Bodies for n fields: 8-row Local tiles (the per-block form), or the
// preset's own tiles (the group form, where Bulk fills its tiles).
template <sp::preset P> cost_model tile_cost(std::size_t n, bool eight_rows, unsigned position_bytes) {
    cost_model c;
    [&]<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        ((c.body[I + 1] = static_cast<u32>(eight_rows ? (n + 7) / 8 * (I + 1)
                                                      : (n + sp::preset_layout<I + 1, P>::tile_rows - 1) / sp::preset_layout<I + 1, P>::tile_rows *
                                                            sp::preset_layout<I + 1, P>::tile_bytes)),
         ...);
    }(std::make_integer_sequence<unsigned, 64>{});
    c.position_bytes = position_bytes;
    return c;
}

// lo = 0, every width (runs/read.cpp).
choice zero_exhaustive(const std::vector<u64> &f, const cost_model &c) {
    const u64 mx = *std::max_element(f.begin(), f.end());
    const unsigned vbits = bits(mx), full = std::max(c.min_width, vbits);
    choice best{0, full, 0, 0, c(full, 0, 0, vbits)};
    for (unsigned K = c.min_width; K < vbits; ++K) {
        std::size_t ex = 0;
        for (u64 x : f)
            ex += x >> K != 0;
        const choice t{0, K, 0, static_cast<u32>(ex), c(K, 0, ex, vbits)};
        if (better(t, best))
            best = t;
    }
    return best;
}

struct tally {
    std::size_t series = 0, fields = 0, zero = 0, exact = 0, guess = 0, low = 0;
};

template <sp::preset P> void run(const char *column, const char *form, const std::vector<std::vector<u64>> &series, bool eight_rows, unsigned position_bytes) {
    tally t;
    std::vector<u64> scratch;
    choice prior;
    bool have = false;
    for (const auto &f : series) {
        const cost_model c = tile_cost<P>(f.size(), eight_rows, position_bytes);
        const cost_model sc = sample_cost(c, std::max<std::size_t>(1, f.size() / 32));
        const auto [mn, mx] = minmax(f.data(), f.size());
        const unsigned vbits = bits(mx - mn);
        const choice z = zero_exhaustive(f, c), e = exact(f.data(), f.size(), c, scratch);
        const choice q = fast::choose<32>(f.data(), f.size(), c, sc, have ? &prior : nullptr);
        cost_model pinned = c;
        pinned.pin_lo = true;
        const choice zp = fast::choose<32>(f.data(), f.size(), pinned, sc, nullptr);
        if (zp.bytes != z.bytes)
            std::abort();  // the pinned stage is today's lo = 0 search
        const choice g = realise(f.data(), f.size(), q.lo, q.width, vbits, c);
        // lo = 0 prices exceptions by the series' maximum, not its span.
        const choice zr = realise(f.data(), f.size(), 0, z.width, bits(mx), c);
        if (g.bytes < e.bytes || g.bytes > q.bytes)
            std::abort();
        prior = g, have = true;
        t.series += 1, t.fields += f.size(), t.zero += zr.bytes, t.exact += e.bytes, t.guess += g.bytes, t.low += g.low;
    }
    std::printf("{\"column\":\"%s\",\"form\":\"%s\",\"preset\":%d,\"series\":%zu,\"fields\":%zu,\"lo_zero_bytes\":%zu,\"exact_bytes\":%zu,\"guess_bytes\":%zu,"
                "\"guess_low_exceptions\":%zu}\n",
                column, form, static_cast<int>(P), t.series, t.fields, t.zero, t.exact, t.guess, t.low);
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 4)
        return 2;
    const std::string dir = argv[1];
    const std::size_t rows = std::strtoull(argv[2], nullptr, 10);
    for (int a = 3; a < argc; ++a) {
        const auto v = load(dir + "/" + argv[a] + ".u64", rows);
        std::vector<std::vector<u64>> per_block, per_group;
        std::vector<u64> group;
        std::size_t in_group = 0;
        for (std::size_t b = 0; b < v.size() / block_rows; ++b) {
            const u64 *x = v.data() + b * block_rows;
            std::vector<u64> sizes;
            std::size_t start = 0;
            for (std::size_t i = 1; i <= block_rows; ++i)
                if (i == block_rows || x[i] != x[i - 1])
                    sizes.push_back(i - start - 1), start = i;
            if (sizes.size() < 2 || sizes.size() > max_runs)
                continue;  // constant or not a run block
            group.insert(group.end(), sizes.begin(), sizes.end());
            per_block.push_back(std::move(sizes));
            if (++in_group == group_blocks)
                per_group.push_back(std::move(group)), group.clear(), in_group = 0;
        }
        if (!group.empty())
            per_group.push_back(std::move(group));
        run<sp::preset::compact>(argv[a], "block", per_block, true, 1);
        run<sp::preset::compact>(argv[a], "group", per_group, false, 2);
        run<sp::preset::bulk_x86>(argv[a], "group", per_group, false, 2);
    }
}
