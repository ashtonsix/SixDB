// Bytes of run-boundary forms on real run blocks (Ashton's composition,
// 2026-10-24): Intervals geometry (absolute ends, sizes, relative ends) with
// its checkpoints moved to the group's checkpoint column, the fields packed by
// SeriesPack, and optionally the bounds stage pulling outlier fields out so
// the rest packs narrower (notebook/pfor-bounds.md). Phase 1's u8
// ends-minus-one plane (no checkpoints) is the baseline it must beat.
//
// A run block here is a 256-row block with 2 <= runs <= MAX_RUNS (default
// 128). Every form is checked by decoding it back to the block's run ends.
// Bytes count fields at SeriesPack's tile grain, checkpoints at their stored
// width, and exceptions as one count byte plus (u8 position, u8 value) each.
// Run values are the same in every form and are left out.
//
// usage: frame_runs_census COLS_DIR ROWS [MAX_RUNS] COLUMN...
#include <ikea/seriespack.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace sp = ikea::seriespack;
using u8 = std::uint8_t;
using u64 = std::uint64_t;

namespace {
constexpr unsigned block_rows = 256;

std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(in.tellg()) / 8) / block_rows * block_rows;
    std::vector<u64> v(rows);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}

// SeriesPack bytes for n fields of width K under a preset (whole tiles).
template <sp::preset P, unsigned K> std::size_t tiles(std::size_t n) {
    using L = sp::preset_layout<K, P>;
    return (n + L::tile_rows - 1) / L::tile_rows * L::tile_bytes;
}
template <sp::preset P> std::size_t packed_bytes(unsigned K, std::size_t n) {
    static constexpr auto table = []<unsigned... I>(std::integer_sequence<unsigned, I...>) {
        return std::array<std::size_t (*)(std::size_t), sizeof...(I)>{&tiles<P, I + 1>...};
    }(std::make_integer_sequence<unsigned, 16>{});
    return table[K - 1](n);
}
unsigned bits(u64 x) { return std::max(1u, static_cast<unsigned>(std::bit_width(x))); }

// Fields packed at the cheapest width with up to |fields| exceptions: a field
// above 2^K - 1 stores 0 and goes to the exception list. Returns bytes and
// the chosen width; exhaustive over K.
template <sp::preset P> std::pair<std::size_t, unsigned> with_bounds(const std::vector<unsigned> &f) {
    std::size_t best = ~std::size_t{0};
    unsigned at = 0;
    const unsigned full = bits(*std::max_element(f.begin(), f.end()));
    for (unsigned K = 1; K <= full; ++K) {
        std::size_t ex = 0;
        for (unsigned x : f)
            ex += x >> K != 0;
        const std::size_t b = packed_bytes<P>(K, f.size()) + (ex ? 1 + 2 * ex : 0);
        if (b < best)
            best = b, at = K;
    }
    return {best, at};
}

struct total {
    std::size_t bytes = 0;
    std::map<unsigned, std::size_t> widths;
};
}  // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS [MAX_RUNS] COLUMN...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    int a = 3;
    unsigned max_runs = 128;
    if (std::isdigit(static_cast<unsigned char>(argv[3][0])))
        max_runs = static_cast<unsigned>(std::atoi(argv[a++]));
    for (; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v = load(dir + "/" + name + ".u64", want);
        const std::size_t blocks = v.size() / block_rows;
        std::map<std::string, total> forms;
        std::size_t run_blocks = 0, runs = 0;
        std::vector<std::size_t> run_hist(9, 0);  // log2 buckets of runs per block
        for (std::size_t b = 0; b < blocks; ++b) {
            const u64 *x = v.data() + b * block_rows;
            std::vector<unsigned> ends;  // exclusive ends 1..256
            for (unsigned i = 1; i < block_rows; ++i)
                if (x[i] != x[i - 1])
                    ends.push_back(i);
            ends.push_back(block_rows);
            const std::size_t n = ends.size();
            if (n < 2 || n > max_runs)
                continue;
            ++run_blocks, runs += n, ++run_hist[std::bit_width(n) - 1];
            std::vector<unsigned> sizes(n), rel16(n), rel64(n);
            for (std::size_t r = 0; r < n; ++r) {
                const unsigned start = r ? ends[r - 1] : 0;
                sizes[r] = ends[r] - start - 1;  // sizes >= 1 stored minus one
                rel16[r] = ends[r] - (r >= 16 ? ends[r / 16 * 16 - 1] : 0) - 1;
                rel64[r] = ends[r] - (r >= 64 ? ends[r / 64 * 64 - 1] : 0) - 1;
            }
            // Check the forms reconstruct the ends (sizes by prefix, relative
            // ends from their checkpoints).
            for (std::size_t r = 0, e = 0; r < n; ++r) {
                e += sizes[r] + 1;
                const unsigned c16 = r >= 16 ? ends[r / 16 * 16 - 1] : 0, c64 = r >= 64 ? ends[r / 64 * 64 - 1] : 0;
                if (e != ends[r] || c16 + rel16[r] + 1 != ends[r] || c64 + rel64[r] + 1 != ends[r]) {
                    std::fprintf(stderr, "%s block %zu: form mismatch\n", name.c_str(), b);
                    return 1;
                }
            }
            const std::size_t cp16 = (n - 1) / 16, cp64 = (n - 1) / 64;  // u8 block-relative starts, first is 0
            auto add = [&](const char *form, std::size_t bytes, unsigned K) {
                forms[form].bytes += bytes;
                ++forms[form].widths[K];
            };
            add("ends_u8 (baseline)", n, 8);
            const unsigned ks = bits(*std::max_element(sizes.begin(), sizes.end()));
            const unsigned k16 = bits(*std::max_element(rel16.begin(), rel16.end()));
            const unsigned k64 = bits(*std::max_element(rel64.begin(), rel64.end()));
            add("ends_compact", packed_bytes<sp::preset::compact>(8, n), 8);
            add("sizes_g16_compact", packed_bytes<sp::preset::compact>(ks, n) + cp16, ks);
            add("sizes_g16_bulk_x86", packed_bytes<sp::preset::bulk_x86>(ks, n) + cp16, ks);
            add("sizes_g64_compact", packed_bytes<sp::preset::compact>(ks, n) + cp64, ks);
            add("rel_ends_g16_compact", packed_bytes<sp::preset::compact>(k16, n) + cp16, k16);
            add("rel_ends_g64_compact", packed_bytes<sp::preset::compact>(k64, n) + cp64, k64);
            {
                const auto [bb, K] = with_bounds<sp::preset::compact>(sizes);
                add("sizes_g16_compact_bounds", bb + cp16, K);
                add("sizes_g64_compact_bounds", bb + cp64, K);
            }
            {
                const auto [bb, K] = with_bounds<sp::preset::bulk_x86>(sizes);
                add("sizes_g16_bulk_x86_bounds", bb + cp16, K);
            }
            {
                const auto [bb, K] = with_bounds<sp::preset::compact>(rel16);
                add("rel_ends_g16_compact_bounds", bb + cp16, K);
            }
        }
        std::printf("{\"column\":\"%s\",\"rows\":%zu,\"blocks\":%zu,\"run_blocks\":%zu,\"mean_runs\":%.1f,\"max_runs\":%u,\"runs_log2_hist\":[",
                    name.c_str(), v.size(), blocks, run_blocks, run_blocks ? double(runs) / run_blocks : 0.0, max_runs);
        for (std::size_t i = 0; i < run_hist.size(); ++i)
            std::printf("%s%zu", i ? "," : "", run_hist[i]);
        std::printf("]}\n");
        for (const auto &[form, t] : forms) {
            std::printf("{\"column\":\"%s\",\"form\":\"%s\",\"bytes\":%zu,\"bits_per_run_block_row\":%.3f,\"bytes_per_run\":%.3f,\"widths\":{",
                        name.c_str(), form.c_str(), t.bytes, run_blocks ? 8.0 * t.bytes / (run_blocks * block_rows) : 0.0,
                        runs ? double(t.bytes) / runs : 0.0);
            bool first = true;
            for (const auto &[k, c] : t.widths)
                std::printf("%s\"%u\":%zu", first ? "" : ",", k, c), first = false;
            std::printf("}}\n");
        }
    }
}
