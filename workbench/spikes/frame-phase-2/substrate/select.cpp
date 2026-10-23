// SUM(B) WHERE A >= t: staged (emit A's row masks, then a masked SUM over B)
// against fused per block (emit, then consume B's block while the mask is in
// registers), with native narrow-FOR staged and fused controls. Original
// block order and a whole-block permutation of A and B together select the
// same rows; every answer is checked against an exact oracle.
//
// usage: frame_substrate_select COLS_DIR ROWS ROUNDS A:B...
#include "material.h"
#include "predicate.h"
#include "selected.h"

#include <algorithm>
#include <bit>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fe = frame_encoding;
using u64 = std::uint64_t;
using u128 = unsigned __int128;

namespace {
std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(in.tellg()) / 8) / fe::block_rows * fe::block_rows;
    std::vector<u64> v(rows);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}

// Global FOR in the narrowest byte lane.
struct native {
    u64 min = 0;
    unsigned lane = 8;
    std::vector<std::uint8_t> bytes;
    explicit native(const std::vector<u64> &v) {
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        min = *lo;
        const u64 span = *hi - *lo;
        lane = span < (u64{1} << 8) ? 1 : span < (u64{1} << 16) ? 2 : span < (u64{1} << 32) ? 4 : 8;
        bytes.resize(v.size() * lane);
        for (std::size_t i = 0; i < v.size(); ++i) {
            const u64 r = v[i] - min;
            std::memcpy(bytes.data() + i * lane, &r, lane);
        }
    }
    template <class T> const T *as() const { return reinterpret_cast<const T *>(bytes.data()); }
};

template <class F> decltype(auto) with_lane(const native &x, F &&f) {
    switch (x.lane) {
    case 1: return f(x.as<std::uint8_t>());
    case 2: return f(x.as<std::uint16_t>());
    case 4: return f(x.as<std::uint32_t>());
    default: return f(x.as<u64>());
    }
}

// Fused native: SUM(b - bmin) over rows with a >= t, branch-free, split halves
// so wide b stays exact; then add bmin per selected row.
u128 native_fused(const native &a, const native &b, u64 t, std::size_t n) {
    if (t <= a.min)
        t = a.min;
    const u64 rhs = t - a.min;
    return with_lane(a, [&](const auto *x) {
        return with_lane(b, [&](const auto *y) -> u128 {
            u64 lo = 0, hi = 0, c = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const u64 keep = -static_cast<u64>(u64{x[i]} >= rhs);
                const u64 v = u64{y[i]} & keep;
                lo += v & 0xffffffffu, hi += v >> 32, c += keep & 1;
            }
            return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32) + static_cast<u128>(b.min) * c;
        });
    });
}

// Staged native: a bitmap of A's matches, then a masked SUM over B.
u128 native_staged(const native &a, const native &b, u64 t, std::size_t n, std::vector<u64> &bits) {
    const u64 rhs = t <= a.min ? 0 : t - a.min;
    with_lane(a, [&](const auto *x) {
        for (std::size_t w = 0; w < n / 64; ++w) {
            u64 m = 0;
            for (unsigned j = 0; j < 64; ++j)
                m |= static_cast<u64>(u64{x[w * 64 + j]} >= rhs) << j;
            bits[w] = m;
        }
    });
    return with_lane(b, [&](const auto *y) -> u128 {
        u64 lo = 0, hi = 0, c = 0;
        for (std::size_t w = 0; w < n / 64; ++w) {
            u64 m = bits[w];
            if (!m)
                continue;
            c += std::popcount(m);
            for (; m; m &= m - 1) {
                const u64 v = y[w * 64 + std::countr_zero(m)];
                lo += v & 0xffffffffu, hi += v >> 32;
            }
        }
        return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32) + static_cast<u128>(b.min) * c;
    });
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

std::vector<u64> permute_blocks(const std::vector<u64> &v, const std::vector<std::size_t> &order) {
    std::vector<u64> out(v.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        std::memcpy(out.data() + i * fe::block_rows, v.data() + order[i] * fe::block_rows, fe::block_rows * 8);
    return out;
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS A:B...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    for (int arg = 4; arg < argc; ++arg) {
        const std::string pair = argv[arg];
        const auto colon = pair.find(':');
        const std::string an = pair.substr(0, colon), bn = pair.substr(colon + 1);
        const auto a0 = load(dir + "/" + an + ".u64", want), b0 = load(dir + "/" + bn + ".u64", want);
        const std::size_t n = std::min(a0.size(), b0.size()), blocks = n / fe::block_rows;
        std::vector<std::size_t> order(blocks);
        for (std::size_t i = 0; i < blocks; ++i)
            order[i] = i;
        std::mt19937_64 rng(0x5eed);
        std::shuffle(order.begin(), order.end(), rng);
        for (const char *layout : {"original", "scattered"}) {
            const bool scattered = layout[0] == 's';
            const auto av = scattered ? permute_blocks(a0, order) : std::vector<u64>(a0.begin(), a0.begin() + n);
            const auto bv = scattered ? permute_blocks(b0, order) : std::vector<u64>(b0.begin(), b0.begin() + n);
            fe::encode_options o;
            o.bind_decoders = false;
            const fe::column ac = fe::encode(av, o), bc = fe::encode(bv, o);
            const native an_(av), bn_(bv);
            fe::block_mask all(blocks);
            for (std::size_t b = 0; b < blocks; ++b)
                all.set(b);
            std::vector<u64> sorted = av;
            std::sort(sorted.begin(), sorted.end());
            for (double q : {0.001, 0.01, 0.1, 0.5}) {
                const u64 t = sorted[static_cast<std::size_t>((1.0 - q) * (n - 1))];
                u128 oracle = 0;
                std::size_t hits = 0, touched = 0;
                for (std::size_t b = 0; b < blocks; ++b) {
                    bool any = false;
                    for (std::size_t i = b * fe::block_rows; i < (b + 1) * fe::block_rows; ++i)
                        if (av[i] >= t)
                            oracle += bv[i], ++hits, any = true;
                    touched += any;
                }
                const fe::value_predicate p{fe::predicate_op::ge, t};
                std::vector<u64> masks(4 * blocks), bits(n / 64);
                std::vector<std::uint16_t> counts(blocks);
                auto staged = [&] {
                    fe::predicate_masks(ac, all, p, masks, {}, nullptr, counts);
                    // B visits only blocks with a match: the coarse mask comes from A's counts.
                    fe::block_mask coarse(blocks);
                    for (std::size_t b = 0; b < blocks; ++b)
                        if (counts[b])
                            coarse.set(b);
                    return fe::masked_encoded_sum(bc, coarse, masks);
                };
                auto fused = [&] {
                    fe::predicate_mask eval(p);
                    const fe::row_selection every{~u64{0}, ~u64{0}, ~u64{0}, ~u64{0}};
                    u128 total = 0;
                    for (std::size_t b = 0; b < blocks; ++b) {
                        const fe::row_selection r = eval(fe::borrow(ac, b), every);
                        if (fe::selection_empty(r))
                            continue;
                        const auto k = fe::borrow(bc, b);
                        total += fe::selection_full(r) ? fe::stored_sum(k) : fe::masked_stored_sum_mixed(k, r);
                    }
                    return total;
                };
                struct arm {
                    const char *name;
                    std::function<u128()> run;
                };
                const arm arms[] = {
                    {"frame_staged", staged},
                    {"frame_fused", fused},
                    {"native_staged", [&] { return native_staged(an_, bn_, t, n, bits); }},
                    {"native_fused", [&] { return native_fused(an_, bn_, t, n); }},
                };
                constexpr std::size_t narms = sizeof arms / sizeof arms[0];
                std::vector<std::vector<double>> samples(narms);
                for (const auto &a : arms)
                    if (a.run() != oracle) {
                        std::fprintf(stderr, "%s %s %s q=%g: wrong answer\n", pair.c_str(), layout, a.name, q);
                        return 1;
                    }
                for (int r = 0; r < rounds; ++r)
                    for (std::size_t j = 0; j < narms; ++j) {
                        const std::size_t i = (j + static_cast<std::size_t>(r)) % narms;
                        arms[i].run();
                        bool ok = true;
                        samples[i].push_back(time_ns([&] { ok = arms[i].run() == oracle; }));
                        if (!ok)
                            return 1;
                    }
                for (std::size_t i = 0; i < narms; ++i) {
                    const double ns = median(samples[i]);
                    std::printf("{\"pair\":\"%s\",\"layout\":\"%s\",\"rows\":%zu,\"q\":%g,\"density\":%.5f,\"blocks_touched\":%.4f,"
                                "\"arm\":\"%s\",\"ns\":%.0f,\"ns_per_row\":%.4f,\"a_bytes\":%zu,\"b_bytes\":%zu}\n",
                                pair.c_str(), layout, n, q, double(hits) / n, double(touched) / blocks, arms[i].name, ns,
                                ns / n, ac.payload.size() + ac.descriptor_bytes(), bc.payload.size() + bc.descriptor_bytes());
                }
            }
        }
    }
}
