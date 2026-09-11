#include "fixtures/wire.h"
#include <benchmark/benchmark.h>
#include <string>

namespace checked_point_probe {
constexpr std::uint64_t seed = 0x4383e2b23a4d106fULL;
constexpr std::size_t query_count = 4096, batch = 256, count = 8193;
std::uint64_t mix(std::uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
template<bool Checked>
void run(benchmark::State& state, sp::description d) {
    fixture f(d, count, true);
    for (std::size_t i = 0; i < count; ++i)
        f.oracle_set(f.bytes, i, mix(seed + i) & mask(d.width));
    const auto view = f.view().as_const();
    const auto bound = sp::bind_reader(view, sp::execution_target::scalar);
    if (!bound) fail("checked-point bind", d, count);
    const auto before = f.bytes;
    std::array<std::size_t, query_count> indices;
    std::uint64_t hash = 0;
    for (std::size_t i = 0; i < query_count; ++i) {
        indices[i] = mix(seed + i * 0x9e3779b97f4a7c15ULL) % count;
        hash = mix(hash ^ indices[i]);
        const auto expected = mix(seed + indices[i]) & mask(d.width);
        const auto checked = sp::get(view, indices[i]);
        if (!checked || *checked != expected || bound->get(indices[i]) != expected)
            fail("checked-point oracle", d, count, indices[i]);
    }
    if (sp::get(view, count)) fail("checked-point invalid index accepted", d, count);
    std::size_t cursor = 0;
    std::uint64_t checksum = 0;
    for (auto _ : state) {
        for (std::size_t j = 0; j < batch; ++j) {
            const auto index = indices[cursor++];
            if (cursor == query_count) cursor = 0;
            if constexpr (Checked) {
                const auto result = sp::get(view, index);
                if (!result) std::abort();
                checksum += *result;
            } else checksum += bound->get(index);
        }
        benchmark::DoNotOptimize(checksum);
    }
    f.compare(before, "checked-point source changed");
    state.SetItemsProcessed(state.iterations() * batch);
    state.counters["queries_per_iteration"] = batch;
    state.counters["query_bytes"] = sizeof(indices);
    state.counters["query_hash_lo"] = std::uint32_t(hash);
    state.counters["query_hash_hi"] = std::uint32_t(hash >> 32);
    state.counters["logical_values"] = count;
    state.counters["checksum"] = static_cast<double>(checksum);
    for (unsigned p = 0; p < 3; ++p) {
        state.counters["plane" + std::to_string(p) + "_stride"] = f.stride[p];
        state.counters["plane" + std::to_string(p) + "_extent"] = f.envelope[p];
    }
}
}
namespace seriespack_measurement {
void register_checked_point_benchmarks() {
    using namespace checked_point_probe;
    for (auto d : {sp::description{1,0,sp::geometry::local8},
                   sp::description{6,0,sp::geometry::local8},
                   sp::description{23,16,sp::geometry::local8},
                   sp::description{56,0,sp::geometry::local8},
                   sp::description{5,0,sp::geometry::striped},
                   sp::description{28,16,sp::geometry::striped}}) {
        const auto name = std::string("checked-point/") +
            (d.storage == sp::geometry::local8 ? "local/k" : "striped/k") +
            std::to_string(d.width) + "/h" + std::to_string(d.head_bits) + "/gapped/";
        benchmark::RegisterBenchmark((name + "checked").c_str(), &run<true>, d);
        benchmark::RegisterBenchmark((name + "bound").c_str(), &run<false>, d);
    }
}
}
