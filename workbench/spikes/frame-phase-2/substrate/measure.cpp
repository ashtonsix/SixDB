// Per-block cost of phase 1's owner versus kind-batched execution on the same
// image, with a native narrow-FOR array as the floor. Every call's answer is
// checked against an exact oracle over the original values.
//
// usage: frame_substrate_measure COLS_DIR ROWS ROUNDS COLUMN...
// Prints one JSON line per (column, operation, arm).
#include "plan.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fe = frame_encoding;
namespace fs = frame_substrate;
using u64 = std::uint64_t;
using u128 = unsigned __int128;

namespace {
std::vector<u64> load(const std::string &path, std::size_t rows) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        std::abort();
    const std::size_t have = static_cast<std::size_t>(in.tellg()) / 8;
    rows = std::min(rows, have) / fe::block_rows * fe::block_rows;
    std::vector<u64> v(rows);
    in.seekg(0);
    in.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(rows * 8));
    return v;
}

// Global FOR in the narrowest byte lane: the strong native control.
struct native {
    u64 min = 0;
    unsigned lane = 8;
    std::vector<std::uint8_t> bytes;
    std::size_t n = 0;
    explicit native(const std::vector<u64> &v) : n(v.size()) {
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        min = *lo;
        const u64 span = *hi - *lo;
        lane = span < (u64{1} << 8) ? 1 : span < (u64{1} << 16) ? 2 : span < (u64{1} << 32) ? 4 : 8;
        bytes.resize(n * lane);
        for (std::size_t i = 0; i < n; ++i) {
            const u64 r = v[i] - min;
            std::memcpy(bytes.data() + i * lane, &r, lane);
        }
    }
    template <class T> const T *as() const { return reinterpret_cast<const T *>(bytes.data()); }
    template <class T> u128 sum_lane() const {
        const T *x = as<T>();
        if constexpr (sizeof(T) <= 2) {
            // u32 lanes over 65,536-row chunks (65,536 (2^16 - 1) < 2^32), so
            // the loop vectorises at four times the u64 density.
            u64 s = 0;
            for (std::size_t c = 0; c < n; c += 65536) {
                const std::size_t e = std::min(n, c + 65536);
                std::uint32_t t = 0;
                for (std::size_t i = c; i < e; ++i)
                    t += x[i];
                s += t;
            }
            return s;
        } else if constexpr (sizeof(T) == 4) {
            u64 s = 0;
            for (std::size_t i = 0; i < n; ++i)
                s += x[i];
            return s;
        } else {
            u64 lo = 0, hi = 0;
            for (std::size_t i = 0; i < n; ++i)
                lo += x[i] & 0xffffffffu, hi += x[i] >> 32;
            return static_cast<u128>(lo) + (static_cast<u128>(hi) << 32);
        }
    }
    template <class T> u64 count_lane(u64 rhs) const {
        const T *x = as<T>();
        const T r = static_cast<T>(rhs);
        u64 c = 0;
        for (std::size_t k = 0; k < n; k += 65536) {  // lane-width counters, as for SUM
            const std::size_t e = std::min(n, k + 65536);
            std::uint32_t t = 0;
            for (std::size_t i = k; i < e; ++i)
                t += x[i] >= r;
            c += t;
        }
        return c;
    }
    u128 sum() const {
        u128 r = lane == 1 ? sum_lane<std::uint8_t>() : lane == 2 ? sum_lane<std::uint16_t>()
               : lane == 4 ? sum_lane<std::uint32_t>() : sum_lane<u64>();
        return r + static_cast<u128>(min) * n;
    }
    u64 count_ge(u64 t) const {
        if (t <= min)
            return n;
        const u64 rhs = t - min;
        if (lane < 8 && rhs >> (lane * 8))
            return 0;
        return lane == 1 ? count_lane<std::uint8_t>(rhs) : lane == 2 ? count_lane<std::uint16_t>(rhs)
             : lane == 4 ? count_lane<std::uint32_t>(rhs) : count_lane<u64>(rhs);
    }
};

template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    const auto b = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(b - a).count();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

const char *kind_census(const fe::column &c, std::string &out) {
    std::size_t k[8] = {};
    for (const auto &d : c.blocks)
        ++k[static_cast<unsigned>(d.type)];
    char buf[160];
    std::snprintf(buf, sizeof buf, "{\"constant\":%zu,\"runs\":%zu,\"packed\":%zu,\"delta\":%zu,\"patched\":%zu,\"delta_patched\":%zu,\"arrival\":%zu}",
                  k[0], k[1], k[2], k[3], k[4], k[5], k[6]);
    out = buf;
    return out.c_str();
}
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
        const auto values = load(dir + "/" + name + ".u64", want);
        const std::size_t n = values.size(), blocks = n / fe::block_rows;
        fe::encode_options o;
        o.bind_decoders = false;
        const fe::column c = fe::encode(values, o);
        const fs::plan p = fs::build(c);
        const native nat(values);
        fe::block_mask all(blocks);
        for (std::size_t b = 0; b < blocks; ++b)
            all.set(b);

        u128 oracle_sum = 0;
        for (u64 x : values)
            oracle_sum += x;
        std::vector<u64> sorted = values;
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        const u64 t = sorted[n / 2];
        u64 oracle_count = 0;
        for (u64 x : values)
            oracle_count += x >= t;

        struct arm {
            const char *op, *name;
            bool (*run)(const void *);
        };
        struct ctx {
            const fe::column *c;
            const fs::plan *p;
            const native *nat;
            const fe::block_mask *all;
            u128 sum;
            u64 count, t;
        } x{&c, &p, &nat, &all, oracle_sum, oracle_count, t};
        static const arm arms[] = {
            {"sum", "phase1_owner", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fe::encoded_sum(*x.c, *x.all) == x.sum; }},
            {"sum", "phase1_catalogue", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fe::catalogue_sum(*x.c, *x.all) == x.sum; }},
            {"sum", "per_block", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fs::sum_per_block(*x.p) == x.sum; }},
            {"sum", "segments", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fs::sum_segments(*x.p) == x.sum; }},
            {"sum", "batched", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fs::sum(*x.p) == x.sum; }},
            {"sum", "native_for", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return x.nat->sum() == x.sum; }},
            {"count_ge", "phase1_owner", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fe::encoded_count_ge(*x.c, *x.all, x.t) == x.count; }},
            {"count_ge", "phase1_catalogue", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fe::catalogue_count_ge(*x.c, *x.all, x.t) == x.count; }},
            {"count_ge", "per_block", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fs::count_ge_per_block(*x.p, x.t) == x.count; }},
            {"count_ge", "segments", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fs::count_ge_segments(*x.p, x.t) == x.count; }},
            {"count_ge", "batched", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return fs::count_ge(*x.p, x.t) == x.count; }},
            {"count_ge", "native_for", [](const void *v) { auto &x = *static_cast<const ctx *>(v); return x.nat->count_ge(x.t) == x.count; }},
        };
        constexpr std::size_t narms = sizeof arms / sizeof arms[0];
        std::vector<std::vector<double>> samples(narms);
        for (std::size_t i = 0; i < narms; ++i)
            if (!arms[i].run(&x)) {
                std::fprintf(stderr, "%s %s %s: wrong answer\n", name.c_str(), arms[i].op, arms[i].name);
                return 1;
            }
        // Rotate arm order each round so drift and predecessor effects spread
        // evenly; every arm repeats its call immediately (warm-repeat).
        for (int r = 0; r < rounds; ++r)
            for (std::size_t j = 0; j < narms; ++j) {
                const std::size_t i = (j + static_cast<std::size_t>(r)) % narms;
                bool ok = true;
                arms[i].run(&x);
                samples[i].push_back(time_ns([&] { ok = arms[i].run(&x); }));
                if (!ok)
                    return 1;
            }
        std::string census;
        kind_census(c, census);
        for (std::size_t i = 0; i < narms; ++i) {
            const double ns = median(samples[i]);
            std::printf("{\"column\":\"%s\",\"rows\":%zu,\"op\":\"%s\",\"arm\":\"%s\",\"ns\":%.0f,\"ns_per_row\":%.4f,"
                        "\"ns_per_block\":%.2f,\"image_bytes\":%zu,\"native_bytes\":%zu,\"plan_bytes\":%zu,\"census\":%s}\n",
                        name.c_str(), n, arms[i].op, arms[i].name, ns, ns / n, ns / blocks,
                        c.payload.size() + c.descriptor_bytes(), nat.bytes.size(), p.bytes(), census.c_str());
        }
    }
}
