// Independent random point reads: does staging the descriptor -> body chain
// close the gap to a native array? Frame direct calls phase 1's getter per
// request; Frame staged prefetches request i + 2D's descriptor and request
// i + D's body (its first two lines) before reading request i. The native
// control is the narrow global-FOR array with and without a prefetch D ahead.
// Every batch's exact sum is checked.
//
// usage: frame_substrate_points COLS_DIR ROWS ROUNDS DISTANCE COLUMN...
#include "material.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
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
template <class F> double time_ns(F &&f) {
    const auto a = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count();
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s COLS_DIR ROWS ROUNDS DISTANCE COLUMN...\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const std::size_t want = std::strtoull(argv[2], nullptr, 10);
    const int rounds = std::atoi(argv[3]);
    const unsigned d = static_cast<unsigned>(std::atoi(argv[4]));
    constexpr std::size_t requests = 65536;
    for (int arg = 5; arg < argc; ++arg) {
        const std::string name = argv[arg];
        const auto v = load(dir + "/" + name + ".u64", want);
        const std::size_t n = v.size();
        fe::encode_options o;
        o.bind_decoders = false;
        const fe::column c = fe::encode(v, o);
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        const u64 min = *lo, span = *hi - *lo;
        const unsigned lane = span < (u64{1} << 8) ? 1 : span < (u64{1} << 16) ? 2 : span < (u64{1} << 32) ? 4 : 8;
        std::vector<std::uint8_t> nat(n * lane + 8);
        for (std::size_t i = 0; i < n; ++i) {
            const u64 r = v[i] - min;
            std::memcpy(nat.data() + i * lane, &r, lane);
        }
        std::mt19937_64 rng(713);
        std::vector<std::uint32_t> idx(requests + 2 * d);
        for (auto &x : idx)
            x = static_cast<std::uint32_t>(rng() % n);
        u128 oracle = 0;
        for (std::size_t i = 0; i < requests; ++i)
            oracle += v[idx[i]];
        const std::uint8_t *payload = c.payload.data();
        const fe::descriptor *desc = c.blocks.data();
        auto native_read = [&](std::uint32_t i) {
            u64 x = 0;
            std::memcpy(&x, nat.data() + std::size_t{i} * lane, 8);
            return lane == 8 ? x : x & ((u64{1} << (8 * lane)) - 1);
        };
        struct arm {
            const char *name;
            std::function<u128()> run;
        };
        const arm arms[] = {
            {"native", [&] {
                 u128 s = 0;
                 for (std::size_t i = 0; i < requests; ++i)
                     s += native_read(idx[i]) + min;
                 return s;
             }},
            {"native_prefetch", [&] {
                 u128 s = 0;
                 for (std::size_t i = 0; i < requests; ++i) {
                     __builtin_prefetch(nat.data() + std::size_t{idx[i + d]} * lane);
                     s += native_read(idx[i]) + min;
                 }
                 return s;
             }},
            {"frame_direct", [&] {
                 u128 s = 0;
                 for (std::size_t i = 0; i < requests; ++i)
                     s += fe::value_at(c, idx[i]);
                 return s;
             }},
            {"frame_staged", [&] {
                 u128 s = 0;
                 for (std::size_t i = 0; i < requests; ++i) {
                     __builtin_prefetch(desc + idx[i + 2 * d] / fe::block_rows);
                     const std::uint8_t *body = payload + desc[idx[i + d] / fe::block_rows].offset;
                     __builtin_prefetch(body);
                     __builtin_prefetch(body + 64);
                     s += fe::value_at(c, idx[i]);
                 }
                 return s;
             }},
        };
        constexpr std::size_t narms = sizeof arms / sizeof arms[0];
        std::vector<std::vector<double>> samples(narms);
        for (const auto &a : arms)
            if (a.run() != oracle) {
                std::fprintf(stderr, "%s %s: wrong answer\n", name.c_str(), a.name);
                return 1;
            }
        for (int r = 0; r < rounds; ++r)
            for (std::size_t j = 0; j < narms; ++j) {
                const std::size_t i = (j + static_cast<std::size_t>(r)) % narms;
                bool ok = true;
                samples[i].push_back(time_ns([&] { ok = arms[i].run() == oracle; }));
                if (!ok)
                    return 1;
            }
        for (std::size_t i = 0; i < narms; ++i)
            std::printf("{\"column\":\"%s\",\"rows\":%zu,\"distance\":%u,\"arm\":\"%s\",\"ns_per_point\":%.3f,"
                        "\"image_bytes\":%zu,\"native_bytes\":%zu}\n",
                        name.c_str(), n, d, arms[i].name, median(samples[i]) / requests,
                        c.payload.size() + c.descriptor_bytes(), n * lane);
    }
}
