// Prototype 5: can a description rank presets? (../README.md, idea 5)
//
// Calibrate: time each preset's SUM, COUNT(x >= t) and points on synthetic
// columns of one width K and E exceptions per block, giving cost tables per
// (preset, operation, K, exception bucket). COUNT is timed twice: with t
// inside every block's window (partial blocks reach the leaf) and outside
// (settled by descriptors).
//
// Predict: for each real column and preset, the census (blocks per width and
// exception bucket, a by-product of fitting) and the blocks a predicate
// leaves partial (from block bounds, which an owner keeps as statistics)
// weight the tables. Compare with the measured time, and compare the preset
// the prediction picks with the one the measurement picks.
//
// usage: frame3_calibrate COLS_DIR ROWS ROUNDS COLUMN...
#include "trees.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <string>

using namespace frame3;

namespace {
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}
std::vector<u64> load(const std::string &dir, const std::string &name, std::size_t rows) {
    std::ifstream f(dir + "/" + name + ".u64", std::ios::binary | std::ios::ate);
    if (!f)
        std::abort();
    rows = std::min(rows, static_cast<std::size_t>(f.tellg()) / 8) / block_rows * block_rows;
    std::vector<u64> v(rows);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}

constexpr std::array<unsigned, 17> widths{1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64};
constexpr std::array<unsigned, 5> exceptions{0, 2, 6, 20, 48};  // one per census bucket

struct costs {
    // ns per block (points: per point), [width index][bucket]
    std::array<std::array<double, 5>, widths.size()> sum{}, partial{}, settled{}, point{};
};
unsigned width_index(unsigned K) {
    unsigned i = 0;
    while (i + 1 < widths.size() && widths[i + 1] <= K)
        ++i;
    // nearest calibrated width at or below K, or above when closer
    if (i + 1 < widths.size() && widths[i + 1] - K < K - widths[i])
        ++i;
    return i;
}

struct preset {
    std::string name;
    std::function<std::unique_ptr<stage>()> make;
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
#if defined(__x86_64__)
    constexpr auto bulk = sp::preset::bulk_x86;
#else
    constexpr auto bulk = sp::preset::bulk_arm;
#endif
    const std::vector<preset> presets{
        {"compact", [] { return bound_pack<sp::preset::compact>(); }},
        {"bulk", [] { return bound_pack<bulk>(); }},
        {"compact_groups", [] { return std::make_unique<bound>(std::make_unique<pack<sp::preset::compact>>(group_blocks)); }},
    };
    std::mt19937_64 rng(3);
    std::vector<u32> rows(16384);
    std::map<std::string, costs> table;
    constexpr std::size_t cal_blocks = 1024;
    for (const auto &pr : presets) {
        costs &c = table[pr.name];
        for (unsigned wi = 0; wi < widths.size(); ++wi)
            for (unsigned ei = 0; ei < exceptions.size(); ++ei) {
                const unsigned K = widths[wi], E = exceptions[ei];
                const u64 lo = u64{1} << 40, span = mask(K);
                std::vector<u64> v(cal_blocks * block_rows);
                for (std::size_t b = 0; b < cal_blocks; ++b) {
                    u64 *x = v.data() + b * block_rows;
                    for (unsigned r = 0; r < block_rows; ++r)
                        x[r] = K == 64 ? rng() : lo + (rng() & span);
                    if (K < 56)
                        for (unsigned e = 0; e < E; ++e)
                            x[rng() % block_rows] = lo + (span + 1) * 64 + (rng() & span);
                }
                column col(pr.make(), v);
                for (auto &r : rows)
                    r = static_cast<u32>(rng() % v.size());
                const u64 inside = K == 64 ? u64{1} << 63 : lo + (span >> 1) + 1, outside = 0;
                std::vector<double> ts, tp, tq, tpt;
                volatile u64 sink = 0;
                for (int r = 0; r < rounds; ++r) {
                    ts.push_back(time_ns([&] { sink = static_cast<u64>(col.sum()); }));
                    tp.push_back(time_ns([&] { sink = col.count(comparison::greater_equal, inside); }));
                    tq.push_back(time_ns([&] { sink = col.count(comparison::greater_equal, outside); }));
                    tpt.push_back(time_ns([&] {
                        u64 s = 0;
                        for (u32 row : rows)
                            s += col.get(row);
                        sink = s;
                    }));
                }
                c.sum[wi][ei] = median(ts) / cal_blocks, c.partial[wi][ei] = median(tp) / cal_blocks;
                c.settled[wi][ei] = median(tq) / cal_blocks, c.point[wi][ei] = median(tpt) / rows.size();
                std::printf("{\"calibration\":\"%s\",\"K\":%u,\"E\":%u,\"sum\":%.2f,\"count_partial\":%.2f,\"count_settled\":%.2f,\"point\":%.2f}\n",
                            pr.name.c_str(), K, E, c.sum[wi][ei], c.partial[wi][ei], c.settled[wi][ei], c.point[wi][ei]);
            }
    }

    for (int a = 4; a < argc; ++a) {
        const std::string name = argv[a];
        const auto v = load(dir, name, want);
        const std::size_t n = v.size(), blocks = n / block_rows;
        std::vector<u64> sorted = v;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const u64 t = sorted[n / 2];
        for (auto &r : rows)
            r = static_cast<u32>(rng() % n);
        for (const auto &pr : presets) {
            column col(pr.make(), v);
            const census cs = col.describe();
            const costs &c = table[pr.name];
            // Blocks the predicate leaves partial: t inside (min, max], from block bounds.
            std::size_t partial_blocks = 0;
            for (std::size_t b = 0; b < blocks; ++b) {
                const auto [mn, mx] = std::minmax_element(v.begin() + b * block_rows, v.begin() + (b + 1) * block_rows);
                partial_blocks += t > *mn && t <= *mx;
            }
            const double partial_share = static_cast<double>(partial_blocks) / static_cast<double>(blocks);
            double sum = 0, count = 0, point = 0;
            for (unsigned K = 0; K <= 64; ++K)
                for (unsigned e = 0; e < 5; ++e)
                    if (const double k = cs.blocks[K][e]) {
                        const unsigned wi = K ? width_index(K) : 0;
                        const double s = K ? c.sum[wi][e] : c.settled[0][e];
                        sum += k * s;
                        count += k * (partial_share * (K ? c.partial[wi][e] : c.settled[0][e]) + (1 - partial_share) * c.settled[wi][e]);
                        point += k * (K ? c.point[wi][e] : c.point[0][0]);
                    }
            point /= static_cast<double>(blocks);
            std::vector<double> ts, tc, tpt;
            volatile u64 sink = 0;
            for (int r = 0; r < rounds; ++r) {
                ts.push_back(time_ns([&] { sink = static_cast<u64>(col.sum()); }));
                tc.push_back(time_ns([&] { sink = col.count(comparison::greater_equal, t); }));
                tpt.push_back(time_ns([&] {
                    u64 s = 0;
                    for (u32 row : rows)
                        s += col.get(row);
                    sink = s;
                }));
            }
            std::printf("{\"column\":\"%s\",\"preset\":\"%s\",\"bits_per_value\":%.3f,\"partial_share\":%.3f,"
                        "\"sum_predicted\":%.2f,\"sum_measured\":%.2f,\"count_predicted\":%.2f,\"count_measured\":%.2f,"
                        "\"point_predicted\":%.2f,\"point_measured\":%.2f}\n",
                        name.c_str(), pr.name.c_str(), 8.0 * static_cast<double>(col.bytes()) / static_cast<double>(n), partial_share,
                        sum / static_cast<double>(blocks), median(ts) / static_cast<double>(blocks), count / static_cast<double>(blocks),
                        median(tc) / static_cast<double>(blocks), point, median(tpt) / static_cast<double>(rows.size()));
        }
    }
}
