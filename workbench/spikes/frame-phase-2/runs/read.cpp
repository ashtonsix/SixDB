// Reading run boundaries through composed stages (census.cpp measures their
// bytes). Each stage is a separate function over an existing Ikea module:
//
//   boundaries  run ends -> sizes (minus one) and region checkpoints, the
//               checkpoints going to the group's checkpoint column
//   bounds      choose the packed width; fields above it become exceptions
//               (u8 position, u8 value) and store 0 (notebook/pfor-bounds.md)
//   pack        ikea::seriespack Local (compact) at the chosen width; one
//               SeriesPack array per width, each block's fields at a tile-aligned offset
//   read        SeriesPack read of the block's fields to u8, +1, exceptions
//               patched, then an ikea::intervals sizes source (sparse_sizes at
//               G16 or G64) answers reverse(row)
//
// Baseline: phase 1's u8 ends-minus-one plane read by run_end_source. Work:
// 65,536 random (block, row) points answered as run ranks, and SUM over runs
// (sum of size times the run's value for every run block). Every answer is
// checked against the original column. Warm only (the column's run geometry
// is a few hundred KB).
//
// usage: frame_runs_read COLS_DIR ROWS ROUNDS COLUMN...
#include <ikea/intervals.h>
#include <ikea/seriespack.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <array>
#include <tuple>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace sp = ikea::seriespack;
namespace in = ikea::intervals;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

namespace {
constexpr unsigned block_rows = 256, max_runs = 128;

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
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
unsigned bits(unsigned x) { return std::max(1u, static_cast<unsigned>(std::bit_width(x))); }

// A run block's logical content: exclusive ends 1..256 and run values.
struct run_block {
    std::vector<unsigned> ends;
    std::vector<u64> values;
};

// Stage: boundaries. Sizes minus one, and the absolute start of every
// G-run region (the first region starts at 0).
struct boundaries {
    std::vector<unsigned> sizes_minus_one;
    std::vector<u16> checkpoints;
};
boundaries to_sizes(const run_block &r, unsigned G) {
    boundaries out;
    for (std::size_t i = 0; i < r.ends.size(); ++i) {
        const unsigned start = i ? r.ends[i - 1] : 0;
        out.sizes_minus_one.push_back(r.ends[i] - start - 1);
        if (i % G == 0)
            out.checkpoints.push_back(static_cast<u16>(start));
    }
    return out;
}

// Stage: bounds. The width minimising Local bytes plus exceptions.
struct bounded {
    unsigned width;
    std::vector<std::pair<u8, u8>> exceptions;  // (field rank, value)
};
bounded choose_bounds(const std::vector<unsigned> &f, bool allow_exceptions) {
    const unsigned full = bits(*std::max_element(f.begin(), f.end()));
    unsigned best_k = full;
    std::size_t best = (f.size() + 7) / 8 * full;
    if (allow_exceptions)
        for (unsigned K = 1; K < full; ++K) {
            std::size_t ex = 0;
            for (unsigned x : f)
                ex += x >> K != 0;
            const std::size_t b = (f.size() + 7) / 8 * K + (ex ? 1 + 2 * ex : 0);
            if (b < best)
                best = b, best_k = K;
        }
    bounded out{best_k, {}};
    for (std::size_t i = 0; i < f.size(); ++i)
        if (f[i] >> best_k)
            out.exceptions.push_back({static_cast<u8>(i), static_cast<u8>(f[i])});
    return out;
}

// Stage: pack. One SeriesPack Local array per width; each block's fields
// start at a tile-aligned (8-row) offset.
struct packed_widths {
    struct plane {
        std::vector<u64> staged;  // fields before packing
        std::vector<ikea::byte> bytes;
        std::optional<sp::erased_reader<u8>> reader;
    };
    std::array<plane, 9> by_width;
    std::size_t bytes() const {
        std::size_t n = 0;
        for (const auto &p : by_width)
            n += p.bytes.size();
        return n;
    }
    // Returns the offset of the appended fields within their width's array.
    u32 append(unsigned K, const std::vector<unsigned> &f, const bounded &bd) {
        auto &s = by_width[K].staged;
        const u32 at = static_cast<u32>(s.size());
        for (std::size_t i = 0; i < f.size(); ++i)
            s.push_back(f[i] >> K ? 0 : f[i]);
        s.resize((s.size() + 7) / 8 * 8, 0);
        (void)bd;
        return at;
    }
    void seal() {
        for (unsigned K = 1; K <= 8; ++K) {
            auto &p = by_width[K];
            if (p.staged.empty())
                continue;
            p.bytes.assign(p.staged.size() / 8 * K, ikea::byte{0});
            const sp::dense_output target{K, sp::geometry::local, p.bytes, p.staged.size()};
            std::vector<ikea::journal::entry> records(*sp::effect_capacity_initialize(target));
            ikea::journal effects{records};
            if (!sp::initialize(target, std::span<const u64>(p.staged), effects))
                std::abort();
            auto r = sp::bind_reader<u8>(sp::dense_input{K, sp::geometry::local, p.bytes, p.staged.size()});
            if (!r)
                std::abort();
            p.reader.emplace(*r);
        }
    }
};

// One column's run geometry under the composed form.
struct composed {
    unsigned G;
    packed_widths widths;
    std::vector<u8> width, count;
    std::vector<u32> offset;
    std::vector<u32> cp_first, ex_first;  // per block, into the group columns below
    // Group checkpoint column (every block's regions). Values are < 256 and
    // are counted at one byte; stored u16 because Intervals' Point type also
    // carries the 256 extent.
    std::vector<u16> checkpoints;
    std::vector<std::pair<u8, u8>> exceptions;
    std::size_t bytes() const {
        // Packed fields, checkpoints except each block's leading 0, exceptions
        // with one count byte per block that has any, and a one-byte width.
        std::size_t ex_blocks = 0;
        for (std::size_t b = 0; b < width.size(); ++b)
            ex_blocks += ex_first[b + 1] != ex_first[b];
        return widths.bytes() + (checkpoints.size() - width.size()) + ex_blocks + 2 * exceptions.size();
    }
    composed(const std::vector<run_block> &blocks, unsigned g, bool with_exceptions) : G(g) {
        for (const auto &r : blocks) {
            const auto bnd = to_sizes(r, G);
            const auto bd = choose_bounds(bnd.sizes_minus_one, with_exceptions);
            width.push_back(static_cast<u8>(bd.width));
            count.push_back(static_cast<u8>(r.ends.size()));
            offset.push_back(widths.append(bd.width, bnd.sizes_minus_one, bd));
            cp_first.push_back(static_cast<u32>(checkpoints.size()));
            checkpoints.insert(checkpoints.end(), bnd.checkpoints.begin(), bnd.checkpoints.end());
            ex_first.push_back(static_cast<u32>(exceptions.size()));
            exceptions.insert(exceptions.end(), bd.exceptions.begin(), bd.exceptions.end());
        }
        cp_first.push_back(static_cast<u32>(checkpoints.size()));
        ex_first.push_back(static_cast<u32>(exceptions.size()));
        widths.seal();
    }
    // Stage: read. Decode a block's sizes into a zero-padded native window.
    unsigned decode(std::size_t b, u8 *sizes) const {
        const unsigned n = count[b];
        widths.by_width[width[b]].reader->read_unchecked(offset[b], n, sizes);
        for (unsigned i = 0; i < n; ++i)
            sizes[i] += 1;
        for (u32 e = ex_first[b]; e < ex_first[b + 1]; ++e)
            sizes[exceptions[e].first] = static_cast<u8>(exceptions[e].second + 1);
        std::fill(sizes + n, sizes + (n + 15) / 16 * 16, u8{0});
        return n;
    }
    unsigned rank(std::size_t b, unsigned row) const {
        alignas(64) u8 sizes[max_runs + 16];
        const unsigned n = decode(b, sizes);
        const u16 *cp = checkpoints.data() + cp_first[b];
        if (G == 16)
            return in::reverse(in::sparse_sizes<16, u8, u16>{sizes, cp, n, 0, 256}, row);
        return in::reverse(in::sparse_sizes<64, u8, u16>{sizes, cp, n, 0, 256}, row);
    }
    // Routed read: the checkpoint column picks the region (an Intervals
    // search over the block's checkpoints), SeriesPack decodes only that
    // region's fields, and a one-region sizes source finishes the search.
    unsigned rank_routed(std::size_t b, unsigned row) const {
        const unsigned n = count[b];
        const u16 *cp = checkpoints.data() + cp_first[b];
        const unsigned regions = cp_first[b + 1] - cp_first[b];
        const unsigned g = in::upper_bound(in::direct_source<u16>{cp, regions}, row) - 1;
        const unsigned first = g * G, m = std::min(G, n - first);
        alignas(64) u8 w[64 + 16];
        widths.by_width[width[b]].reader->read_unchecked(offset[b] + first, m, w);
        for (unsigned i = 0; i < m; ++i)
            w[i] += 1;
        for (u32 e = ex_first[b]; e < ex_first[b + 1]; ++e)
            if (exceptions[e].first - first < m)
                w[exceptions[e].first - first] = static_cast<u8>(exceptions[e].second + 1);
        std::fill(w + m, w + (m + 15) / 16 * 16, u8{0});
        const u16 limit = g + 1 < regions ? cp[g + 1] : u16(256);
        if (G == 16)
            return first + in::reverse(in::sparse_sizes<16, u8, u16>{w, cp + g, m, cp[g], limit}, row);
        return first + in::reverse(in::sparse_sizes<64, u8, u16>{w, cp + g, m, cp[g], limit}, row);
    }
};
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS COLUMN...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    for (int a = 4; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v = load(dir + "/" + name + ".u64", want);
        std::vector<run_block> blocks;
        for (std::size_t b = 0; b < v.size() / block_rows; ++b) {
            const u64 *x = v.data() + b * block_rows;
            run_block r;
            for (unsigned i = 1; i <= block_rows; ++i)
                if (i == block_rows || x[i] != x[i - 1])
                    r.ends.push_back(i), r.values.push_back(x[i - 1]);
            if (r.ends.size() >= 2 && r.ends.size() <= max_runs)
                blocks.push_back(std::move(r));
        }
        const std::size_t nb = blocks.size();
        std::mt19937_64 rng(11);
        std::vector<std::pair<u32, u8>> queries(65536);
        for (auto &q : queries)
            q = {static_cast<u32>(rng() % nb), static_cast<u8>(rng())};
        u64 rank_oracle = 0;
        unsigned __int128 sum_oracle = 0;
        for (const auto &[b, row] : queries) {
            const auto &e = blocks[b].ends;
            rank_oracle += std::upper_bound(e.begin(), e.end(), unsigned(row)) - e.begin();
        }
        for (const auto &r : blocks)
            for (std::size_t i = 0; i < r.ends.size(); ++i)
                sum_oracle += static_cast<unsigned __int128>(r.ends[i] - (i ? r.ends[i - 1] : 0)) * r.values[i];

        // Baseline: u8 ends-minus-one per block, concatenated.
        std::vector<u8> ends_m1;
        std::vector<u32> ends_at;
        for (const auto &r : blocks) {
            ends_at.push_back(static_cast<u32>(ends_m1.size()));
            for (unsigned e : r.ends)
                ends_m1.push_back(static_cast<u8>(e - 1));
        }
        ends_m1.resize(ends_m1.size() + 64, 255);  // native search's readable window
        ends_at.push_back(static_cast<u32>(ends_m1.size() - 64));
        std::vector<u64> run_values;
        for (const auto &r : blocks)
            run_values.insert(run_values.end(), r.values.begin(), r.values.end());

        struct arm {
            std::string name;
            std::size_t bytes;
            std::function<u64()> points;
            std::function<unsigned __int128()> sum;
        };
        std::vector<arm> arms;
        arms.push_back({"ends_u8_run_end_source", ends_m1.size() - 64,
                        [&] {
                            u64 s = 0;
                            for (const auto &[b, row] : queries)
                                s += in::reverse(in::run_end_source{ends_m1.data() + ends_at[b], ends_at[b + 1] - ends_at[b]}, unsigned(row));
                            return s;
                        },
                        [&] {
                            unsigned __int128 s = 0;
                            std::size_t k = 0;
                            for (std::size_t b = 0; b < nb; ++b) {
                                const u8 *e = ends_m1.data() + ends_at[b];
                                const unsigned n = ends_at[b + 1] - ends_at[b];
                                for (unsigned i = 0; i < n; ++i) {
                                    const u8 w = static_cast<u8>(unsigned(e[i]) - (i ? unsigned(e[i - 1]) : 255u));  // size, mod 256
                                    s += static_cast<unsigned __int128>(w) * run_values[k + i];
                                }
                                k += n;
                            }
                            return s;
                        }});
        std::vector<std::unique_ptr<composed>> keep;
        for (auto [label, G, ex] : {std::tuple{"sizes_g16", 16u, false}, std::tuple{"sizes_g16_bounds", 16u, true},
                                    std::tuple{"sizes_g64", 64u, false}, std::tuple{"sizes_g64_bounds", 64u, true}}) {
            keep.push_back(std::make_unique<composed>(blocks, G, ex));
            const composed *c = keep.back().get();
            arms.push_back({std::string(label) + "_routed", c->bytes(),
                            [&queries, c] {
                                u64 s = 0;
                                for (const auto &[b, row] : queries)
                                    s += c->rank_routed(b, row);
                                return s;
                            },
                            [&run_values, c, nb] {
                                unsigned __int128 s = 0;
                                std::size_t k = 0;
                                alignas(64) u8 sizes[max_runs + 16];
                                for (std::size_t b = 0; b < nb; ++b) {
                                    const unsigned n = c->decode(b, sizes);
                                    for (unsigned i = 0; i < n; ++i)
                                        s += static_cast<unsigned __int128>(sizes[i]) * run_values[k + i];
                                    k += n;
                                }
                                return s;
                            }});
            arms.push_back({label, c->bytes(),
                            [&queries, c] {
                                u64 s = 0;
                                for (const auto &[b, row] : queries)
                                    s += c->rank(b, row);
                                return s;
                            },
                            [&run_values, c, nb] {
                                unsigned __int128 s = 0;
                                std::size_t k = 0;
                                alignas(64) u8 sizes[max_runs + 16];
                                for (std::size_t b = 0; b < nb; ++b) {
                                    const unsigned n = c->decode(b, sizes);
                                    for (unsigned i = 0; i < n; ++i)
                                        s += static_cast<unsigned __int128>(sizes[i]) * run_values[k + i];
                                    k += n;
                                }
                                return s;
                            }});
        }
        for (const auto &x : arms)
            if (x.points() != rank_oracle || x.sum() != sum_oracle) {
                std::fprintf(stderr, "%s %s: wrong answer (points %llu want %llu; sum %llu:%llu want %llu:%llu)\n", name.c_str(), x.name.c_str(),
                             (unsigned long long)x.points(), (unsigned long long)rank_oracle, (unsigned long long)(x.sum() >> 64),
                             (unsigned long long)x.sum(), (unsigned long long)(sum_oracle >> 64), (unsigned long long)sum_oracle);
                return 1;
            }
        std::vector<std::vector<double>> tp(arms.size()), ts(arms.size());
        for (int r = 0; r < rounds; ++r)
            for (std::size_t j = 0; j < arms.size(); ++j) {
                const std::size_t i = (j + static_cast<std::size_t>(r)) % arms.size();
                bool ok = true;
                arms[i].points();
                tp[i].push_back(time_ns([&] { ok &= arms[i].points() == rank_oracle; }));
                ts[i].push_back(time_ns([&] { ok &= arms[i].sum() == sum_oracle; }));
                if (!ok)
                    return 1;
            }
        std::size_t runs = 0;
        for (const auto &r : blocks)
            runs += r.ends.size();
        for (std::size_t i = 0; i < arms.size(); ++i)
            std::printf("{\"column\":\"%s\",\"arm\":\"%s\",\"run_blocks\":%zu,\"runs\":%zu,\"bytes\":%zu,\"bits_per_run_block_row\":%.3f,"
                        "\"ns_per_point\":%.2f,\"sum_ns_per_block\":%.2f}\n",
                        name.c_str(), arms[i].name.c_str(), nb, runs, arms[i].bytes, 8.0 * arms[i].bytes / (nb * block_rows),
                        median(tp[i]) / queries.size(), median(ts[i]) / nb);
    }
}
