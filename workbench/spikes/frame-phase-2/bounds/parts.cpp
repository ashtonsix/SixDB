// Cost of each part of the fast bounds path, per 256-row block, on one
// column's residuals at each lane width that holds them. Warm; the sink
// keeps every result live.
//
// usage: frame_bounds_parts COLS_DIR COLUMN [BLOCKS]
#include "fast.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace frame_bounds;

namespace {
template <class F> double per_call(F &&f, std::size_t reps) {
    const auto a = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < reps; ++i)
        f(i);
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - a).count() / reps;
}
volatile u64 sink;

template <class V> void parts(const char *col, const std::vector<u64> &v, std::size_t blocks) {
    std::vector<V> r(v.size());
    std::vector<unsigned> vb(blocks);
    for (std::size_t b = 0; b < blocks; ++b) {
        const auto [mn, mx] = minmax(&v[b * 256], 256);
        vb[b] = bits(mx - mn);
        for (std::size_t i = 0; i < 256; ++i)
            r[b * 256 + i] = static_cast<V>(v[b * 256 + i] - mn);
    }
    cost_model c;
    for (unsigned K = 1; K <= 64; ++K)
        c.body[K] = 32 * K;
    const cost_model s8 = sample_cost(c, 8), s16 = sample_cost(c, 16);
    const std::size_t reps = blocks * 16;
    auto R = [&](std::size_t i) { return &r[(i % blocks) * 256]; };
    auto report = [&](const char *part, double ns) { std::printf("{\"column\":\"%s\",\"lane_bits\":%zu,\"part\":\"%s\",\"ns\":%.1f}\n", col, 8 * sizeof(V), part, ns); };
    report("count1", per_call([&](std::size_t i) {
               std::array<u32, 1> lo, hi;
               fast::count<V, 1>(R(i), 256, {fast::clip<V>(3, 4)}, lo, hi);
               sink = sink + lo[0] + hi[0];
           }, reps));
    report("count7", per_call([&](std::size_t i) {
               std::array<u32, 7> lo, hi;
               std::array<fast::win<V>, 7> w;
               for (unsigned k = 0; k < 7; ++k)
                   w[k] = fast::clip<V>(k, 4);
               fast::count<V, 7>(R(i), 256, w, lo, hi);
               sink = sink + lo[0] + hi[6];
           }, reps));
    report("tighten", per_call([&](std::size_t i) { sink = sink + fast::tighten<V>(R(i), 256, choice{1, 4, 0, 0, 0}, 8, c).lo; }, reps));
    report("network_sort32", per_call([&](std::size_t i) {
               std::array<V, 32> s;
               for (unsigned j = 0; j < 32; ++j)
                   s[j] = R(i)[8 * j];
               fast::network_sort(s);
               sink = sink + s[3];
           }, reps));
    report("vector_sort32", per_call([&](std::size_t i) {
               std::array<V, 32> s;
               for (unsigned j = 0; j < 32; ++j)
                   s[j] = R(i)[8 * j];
               fast::vector_sort(s);
               sink = sink + s[3];
           }, reps));
    report("std_sort32", per_call([&](std::size_t i) {
               std::array<V, 32> s;
               for (unsigned j = 0; j < 32; ++j)
                   s[j] = R(i)[8 * j];
               std::sort(s.begin(), s.end());
               sink = sink + s[3];
           }, reps));
    report("quantiles32", per_call([&](std::size_t i) { sink = sink + fast::sample_quantiles<V, 32>(R(i), 256, s8, vb[i % blocks], 3, 5).best.lo; }, reps));
    report("quantiles16", per_call([&](std::size_t i) { sink = sink + fast::sample_quantiles<V, 16>(R(i), 256, s16, vb[i % blocks], 3, 5).best.lo; }, reps));
    report("ladder32", per_call([&](std::size_t i) { sink = sink + fast::sample_ladder<V, 32>(R(i), 256, s8, vb[i % blocks]).first.lo; }, reps));
    report("ladder16", per_call([&](std::size_t i) { sink = sink + fast::sample_ladder<V, 16>(R(i), 256, s16, vb[i % blocks]).first.lo; }, reps));
    report("guess_narrow_ladder32", per_call([&](std::size_t i) {
               fast::options o;
               o.ladder = true;
               sink = sink + fast::guess_narrow<7, 32, V>(R(i), 256, vb[i % blocks], c, s8, nullptr, o).lo;
           }, reps));
    report("guess_lean32", per_call([&](std::size_t i) { sink = sink + fast::guess_lean<V, 32>(R(i), 256, vb[i % blocks], c, s8, nullptr).lo; }, reps));
    report("guess_lean16", per_call([&](std::size_t i) { sink = sink + fast::guess_lean<V, 16>(R(i), 256, vb[i % blocks], c, s16, nullptr).lo; }, reps));
    report("sample32_t9", per_call([&](std::size_t i) { sink = sink + fast::sample_guess<9, V, 32>(R(i), 256, s8, vb[i % blocks]).best.lo; }, reps));
    report("sample16_t5", per_call([&](std::size_t i) { sink = sink + fast::sample_guess<5, V, 16>(R(i), 256, s16, vb[i % blocks]).best.lo; }, reps));
    report("guess_narrow_quant32", per_call([&](std::size_t i) {
               fast::options o;
               o.quantiles = true;
               sink = sink + fast::guess_narrow<7, 32, V>(R(i), 256, vb[i % blocks], c, s8, nullptr, o).lo;
           }, reps));
    report("guess_narrow_quant16", per_call([&](std::size_t i) {
               fast::options o;
               o.quantiles = true;
               sink = sink + fast::guess_narrow<7, 16, V>(R(i), 256, vb[i % blocks], c, s16, nullptr, o).lo;
           }, reps));
    report("guess_narrow_s32",
           per_call([&](std::size_t i) { sink = sink + fast::guess_narrow<5, 32, V>(R(i), 256, vb[i % blocks], c, s8, nullptr, {}).lo; }, reps));
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    const std::string col = argv[2];
    const std::size_t blocks = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 4096;
    std::ifstream f(std::string(argv[1]) + "/" + col + ".u64", std::ios::binary);
    std::vector<u64> v(blocks * 256);
    f.read(reinterpret_cast<char *>(v.data()), static_cast<std::streamsize>(v.size() * 8));
    unsigned widest = 0;
    for (std::size_t b = 0; b < blocks; ++b) {
        const auto [mn, mx] = minmax(&v[b * 256], 256);
        widest = std::max(widest, bits(mx - mn));
    }
    std::printf("{\"column\":\"%s\",\"part\":\"minmax\",\"ns\":%.1f}\n", col.c_str(), per_call([&](std::size_t i) {
                    const auto m = fast::minmax(&v[(i % blocks) * 256], 256);
                    sink = sink + m.min + m.max;
                }, blocks * 16));
    // Every lane width that holds the column's widest block.
    if (widest <= 8)
        parts<std::uint8_t>(col.c_str(), v, blocks);
    if (widest <= 16)
        parts<std::uint16_t>(col.c_str(), v, blocks);
    if (widest <= 32)
        parts<std::uint32_t>(col.c_str(), v, blocks);
    parts<u64>(col.c_str(), v, blocks);
}
