// Run-block point reads: where should a group's run ends live?
//   block     per-block body [values][u8 end-1], descriptor {offset,count}
//   slice     every run end of the group in one u16 buffer (group-absolute
//             row of each run's last row) plus per-block first-run offsets;
//             search only the block's slice
//   group     the same buffer searched whole: no per-block step at all
//   relative  ikea relative_end_source<u16> over the group buffer (G16 checkpoints)
//   sizes     ikea delta_source<u16> over run lengths (G16 checkpoints)
//   bitmap    per-block 32 B run-start bitmap beside the body; popcount rank
// Each read returns the run's value byte; checksums match an oracle first.
//   intervals_search_groups [trials]
#include "candidates.h"
#include "control/search_before.h"
#include <ikea/intervals/sparse.h>
#include <ikea/intervals/projected.h>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {
using namespace probe;
using u64 = std::uint64_t;
constexpr unsigned rows = 256, queries = 1u << 14;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
constexpr unsigned NW = 64;
#elif defined(__AVX2__)
constexpr unsigned NW = 32;
#else
constexpr unsigned NW = 16;
#endif

struct column {
    unsigned blocks, group;
    // block layout
    std::vector<std::uint8_t> payload;
    std::vector<std::uint32_t> offset;
    std::vector<std::uint16_t> count;
    // group layout
    std::vector<std::vector<std::uint16_t>> last; // group-absolute last row per run
    std::vector<std::vector<std::uint8_t>> value;
    std::vector<std::uint32_t> first_run;          // per block, within its group
    std::vector<std::vector<std::uint16_t>> sizes; // run lengths (delta form)
    std::vector<std::vector<std::uint16_t>> local; // relative ends, G16
    std::vector<std::vector<std::uint32_t>> checkpoints, size_checkpoints;
    std::vector<std::uint8_t> truth;
    std::vector<std::uint64_t> starts; // four words per block, bit r = run starts at row r
    // Flat copies: groups are contiguous, first_run is absolute with a sentinel.
    struct span { std::uint32_t base, pbase, cbase, n; };
    std::vector<span> spans;
    std::vector<std::uint16_t> last_flat, local_flat, sizes_flat;
    std::vector<std::uint8_t> value_flat;
    std::vector<std::uint32_t> ck_flat, run_start;

    column(unsigned blocks, unsigned group, double mean_runs, std::mt19937_64 &rng)
        : blocks(blocks), group(group) {
        const unsigned groups = blocks / group;
        last.resize(groups);
        value.resize(groups);
        sizes.resize(groups);
        local.resize(groups);
        checkpoints.resize(groups);
        size_checkpoints.resize(groups);
        for (unsigned b = 0; b < blocks; ++b) {
            // Block-local runs; each block starts a run (Frame blocks are independent).
            std::vector<unsigned> ends;
            // Per-block run density varies: a block-level scale in [0.25, 4].
            const double scale = std::exp2(double(rng() % 1000) / 1000.0 * 4 - 2);
            std::geometric_distribution<unsigned> blen(std::min(1.0, mean_runs * scale / rows));
            unsigned row = 0;
            while (row < rows) {
                row = std::min(rows, row + 1 + blen(rng));
                ends.push_back(row);
            }
            const unsigned g = b / group, base = (b % group) * rows;
            first_run.push_back(unsigned(last[g].size()));
            offset.push_back(unsigned(payload.size()));
            count.push_back(std::uint16_t(ends.size()));
            unsigned start = 0;
            starts.resize(starts.size() + 4, 0);
            for (unsigned s0 = 0, k = 0; k < ends.size(); s0 = ends[k++])
                starts[4 * b + s0 / 64] |= std::uint64_t{1} << (s0 % 64);
            for (const unsigned e : ends) {
                const auto v = std::uint8_t(rng());
                payload.push_back(v);
                for (unsigned r = start; r < e; ++r)
                    truth.push_back(v);
                last[g].push_back(std::uint16_t(base + e - 1));
                value[g].push_back(v);
                sizes[g].push_back(std::uint16_t(e - start));
                start = e;
            }
            for (const unsigned e : ends)
                payload.push_back(std::uint8_t(e - 1));
        }
        for (unsigned g = 0; g < groups; ++g) {
            const auto &l = last[g];
            const unsigned n = unsigned(l.size()), padded = (n + 15) / 16 * 16;
            local[g].assign(padded, 0);
            sizes[g].resize(padded, 0);
            for (unsigned i = 0; i < n; ++i) {
                const unsigned origin = i / 16 ? unsigned(l[i / 16 * 16 - 1]) + 1 : 0;
                local[g][i] = std::uint16_t(unsigned(l[i]) + 1 - origin);
            }
            for (unsigned k = 0; k < padded / 16; ++k)
                checkpoints[g].push_back(k ? unsigned(l[k * 16 - 1]) + 1 : 0);
            size_checkpoints[g] = checkpoints[g];
        }
        payload.resize(payload.size() + 64); // image suffix: the halo for the last body
        for (unsigned g = 0; g < groups; ++g) {
            spans.push_back({std::uint32_t(last_flat.size()), std::uint32_t(local_flat.size()),
                             std::uint32_t(ck_flat.size()), std::uint32_t(last[g].size())});
            for (unsigned b = g * group; b < (g + 1) * group; ++b)
                run_start.push_back(std::uint32_t(last_flat.size()) + first_run[b]);
            last_flat.insert(last_flat.end(), last[g].begin(), last[g].end());
            value_flat.insert(value_flat.end(), value[g].begin(), value[g].end());
            local_flat.insert(local_flat.end(), local[g].begin(), local[g].end());
            sizes_flat.insert(sizes_flat.end(), sizes[g].begin(), sizes[g].end());
            ck_flat.insert(ck_flat.end(), checkpoints[g].begin(), checkpoints[g].end());
        }
        run_start.push_back(std::uint32_t(last_flat.size()));
        last_flat.resize(last_flat.size() + 64);
        local_flat.resize(local_flat.size() + 64);
        sizes_flat.resize(sizes_flat.size() + 64);
    }
};

template <class Search> [[gnu::always_inline]] inline unsigned block_read(const column &c, unsigned row, Search search) {
    const unsigned b = row / rows;
    const std::uint8_t *body = c.payload.data() + c.offset[b];
    const unsigned n = c.count[b];
    const unsigned r = search(body + n, n, std::uint8_t(row % rows));
    return body[r];
}

template <int Arm> [[gnu::always_inline]] inline unsigned read(const column &c, unsigned row) {
    const unsigned g = row / (rows * c.group), in = row % (rows * c.group);
    if constexpr (Arm == 0)
        return block_read(c, row, [](const std::uint8_t *p, unsigned n, std::uint8_t q) {
            return iv::lower_bound(iv::direct_source<std::uint8_t>{p, n}, q);
        });
    else if constexpr (Arm == 1)
        return block_read(c, row, [](const std::uint8_t *p, unsigned n, std::uint8_t q) {
            return hybrid<edge::at_or_after, 64, NW, small::page, fit::clamp>(p, n, q);
        });
    else if constexpr (Arm == 2) {
        const unsigned b = row / rows, first = c.run_start[b], limit = c.run_start[b + 1];
        return c.value_flat[first + hybrid<edge::at_or_after, 64, NW, small::page, fit::clamp>(
                                        c.last_flat.data() + first, limit - first, std::uint16_t(in))];
    } else if constexpr (Arm == 3) {
        const auto s = c.spans[g];
        return c.value_flat[s.base + hybrid<edge::at_or_after, 64, NW, small::page, fit::clamp>(
                                         c.last_flat.data() + s.base, s.n, std::uint16_t(in))];
    } else if constexpr (Arm == 4) {
        const auto sp = c.spans[g];
        const iv::relative_end_source<std::uint16_t, std::uint32_t> s{
            c.local_flat.data() + sp.pbase, c.ck_flat.data() + sp.cbase, sp.n, 16,
            std::uint32_t(c.group * rows)};
        return c.value_flat[sp.base + iv::upper_bound(s, in)];
    } else if constexpr (Arm == 10) {
        // Frame's maintained unpredictable-reader source (projection below 16 runs).
        const unsigned b = row / rows;
        const std::uint8_t *body = c.payload.data() + c.offset[b];
        const unsigned n = c.count[b];
        return body[iv::upper_bound(iv::run_end_source{body + n, n}, row % rows)];
    } else if constexpr (Arm == 9) {
        return block_read(c, row, [](const std::uint8_t *p, unsigned n, std::uint8_t q) {
            return ikea::intervals::frozen::hybrid_array<edge::at_or_after, true>(p, n, q);
        });
    } else if constexpr (Arm == 8) {
        return block_read(c, row, [](const std::uint8_t *p, unsigned n, std::uint8_t q) {
            return proposed<edge::at_or_after, 32>(p, n, q);
        });
    } else if constexpr (Arm == 7) {
        return block_read(c, row, [](const std::uint8_t *p, unsigned n, std::uint8_t q) {
            return proposed<edge::at_or_after, 0>(p, n, q);
        });
    } else if constexpr (Arm == 6) {
        const unsigned b = row / rows, r = row % rows, w = r / 64;
        const std::uint64_t *s = c.starts.data() + 4 * b;
        unsigned rank = std::popcount(s[w] & iv::detail::low_bits(r % 64 + 1));
#pragma clang loop unroll(full)
        for (unsigned i = 0; i < 4; ++i)
            rank += i < w ? std::popcount(s[i]) : 0;
        return c.payload[c.offset[b] + rank - 1];
    } else {
        const auto sp = c.spans[g];
        const iv::delta_source<std::uint16_t, std::uint32_t> s{
            c.sizes_flat.data() + sp.pbase, c.ck_flat.data() + sp.cbase, sp.n, 16,
            std::uint32_t(c.group * rows)};
        return c.value_flat[sp.base + iv::upper_bound(s, in)];
    }
}
const char *names[] = {"block_current", "block_hyb64", "slice_hyb64", "group_hyb64", "group_relative",
                       "group_sizes", "block_bitmap", "block_proposed", "block_halo", "block_before", "block_run_source"};

template <int Arm> [[gnu::noinline]] u64 independent(const column &c, const std::vector<unsigned> &q) {
    u64 s = 0;
    for (const unsigned row : q)
        s += read<Arm>(c, row);
    return s;
}
// The next row depends on this value; salts keep the walk from repeating.
std::vector<unsigned> salt;
template <int Arm> [[gnu::noinline]] u64 dependent(const column &c, const std::vector<unsigned> &q) {
    u64 s = 0;
    unsigned j = 0;
    for (unsigned i = 0; i < queries; ++i) {
        const unsigned v = read<Arm>(c, q[j]);
        s += v;
        j = (salt[i] + v) & (queries - 1);
    }
    return s;
}
double now() {
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

int main(int argc, char **argv) {
    const unsigned trials = argc > 1 ? unsigned(std::atoi(argv[1])) : 9;
    std::mt19937_64 rng(77);
    for (const unsigned blocks : {1024u, 16384u})
        for (const unsigned group : {16u, 64u, 256u})
            for (const double mean : {4.0, 36.0, 128.0}) {
                const column c(blocks, group, mean, rng);
                std::vector<unsigned> q(queries);
                for (auto &r : q)
                    r = unsigned(rng() % (blocks * rows));
                u64 want_i = 0, want_d = 0;
                for (const unsigned r : q)
                    want_i += c.truth[r];
                salt.resize(queries);
                for (auto &x : salt)
                    x = unsigned(rng());
                for (unsigned i = 0, j = 0; i < queries; ++i) {
                    const unsigned v = c.truth[q[j]];
                    want_d += v;
                    j = (salt[i] + v) & (queries - 1);
                }
                using fn = u64 (*)(const column &, const std::vector<unsigned> &);
                const fn fi[] = {independent<0>, independent<1>, independent<2>,
                                 independent<3>, independent<4>, independent<5>, independent<6>, independent<7>, independent<8>, independent<9>, independent<10>};
                const fn fd[] = {dependent<0>, dependent<1>, dependent<2>,
                                 dependent<3>, dependent<4>, dependent<5>, dependent<6>, dependent<7>, dependent<8>, dependent<9>, dependent<10>};
                constexpr unsigned arms = 11;
                double runs = 0;
                for (auto n : c.count)
                    runs += n;
                for (const int stream : {0, 1}) {
                    const fn *f = stream ? fd : fi;
                    const u64 want = stream ? want_d : want_i;
                    std::vector<std::vector<double>> samples(arms);
                    for (unsigned a = 0; a < arms; ++a)
                        if (f[a](c, q) != want) {
                            std::fprintf(stderr, "%s blocks %u group %u: wrong\n", names[a], blocks, group);
                            return 1;
                        }
                    for (unsigned t = 0; t < trials; ++t)
                        for (unsigned k = 0; k < arms; ++k) {
                            const unsigned a = (k + t) % arms;
                            const double t0 = now();
                            const u64 got = f[a](c, q) + f[a](c, q);
                            samples[a].push_back((now() - t0) / (2.0 * queries));
                            if (got != 2 * want)
                                return 1;
                        }
                    for (unsigned a = 0; a < arms; ++a) {
                        auto &s = samples[a];
                        std::sort(s.begin(), s.end());
                        std::printf("{\"blocks\":%u,\"group\":%u,\"mean_runs\":%.1f,\"stream\":\"%s\","
                                    "\"arm\":\"%s\",\"min\":%.3f,\"median\":%.3f,\"max\":%.3f}\n",
                                    blocks, group, runs / blocks, stream ? "dependent" : "random",
                                    names[a], s.front(), s[s.size() / 2], s.back());
                    }
                    std::fflush(stdout);
                }
            }
}
