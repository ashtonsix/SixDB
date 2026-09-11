#include "fixtures/runtime.h"
#include <benchmark/benchmark.h>

namespace seriespack_runtime_ranges {
template<class U>
void run(benchmark::State& state, shape s, regime r, sp::execution_target target) {
    fixture f(s, r);
    const auto admitted = sp::bind_reader(f.view(), target);
    require(admitted.has_value() && admitted->target() == target, "runtime target binding");
    const auto& reader = *admitted;
    f.verify<U>(reader); // Independent full materialization, excluded from timing.
    const auto source_before = f.storage;
    std::array<U, max_count> output; // Natural carrier alignment, no over-alignment admission.
    // Verify the actual timed output pointer as well as the offset canary view.
    for (auto query : f.queries) {
        output.fill(U(0xd3));
        const auto count = query.size();
        reader.decode(query, sp::output_values{std::span(output.data(), count)});
        for (std::size_t j = 0; j < count; ++j)
            require(output[j] == value_at(query.begin + j, s.width), "timed pointer oracle");
        for (auto j = count; j < output.size(); ++j)
            require(output[j] == U(0xd3), "timed pointer exact suffix");
    }
    const auto* queries = f.queries.data();
    std::size_t cursor = 0;
    word sum = 0;
    for (auto _ : state) {
        for (std::size_t i = 0; i < batch; ++i) {
            const auto query = queries[cursor++];
            if (cursor == query_count) cursor = 0;
            const auto count = query.end - query.begin;
            reader.decode(query, sp::output_values{std::span(output.data(), count)});
            sum += word(output[0]) + word(output[count - 1]);
        }
        benchmark::DoNotOptimize(sum);
    }
    require(f.storage == source_before, "runtime input changed");
    state.SetItemsProcessed(state.iterations() * batch);
    state.counters["logical_values"] = f.n;
    state.counters["encoded_bytes"] = f.n * s.width / 8;
    state.counters["placement_bytes"] = f.envelopes[0] + f.envelopes[1] + f.envelopes[2];
    state.counters["payload_stride"] = f.strides[0];
    state.counters["head0_stride"] = f.strides[1];
    state.counters["head1_stride"] = f.strides[2];
    state.counters["values_per_item"] = double(f.output_values) / query_count;
    state.counters["io_bytes_per_value"] = sizeof(U);
    state.counters["query_bytes"] = query_count * sizeof(sp::index_range);
    state.counters["queries_per_iteration"] = batch;
    state.counters["sink_bytes_per_iteration"] = f.output_values / (query_count / batch) * sizeof(U);
    state.counters["query_hash_lo"] = std::uint32_t(f.query_hash);
    state.counters["query_hash_hi"] = std::uint32_t(f.query_hash >> 32);
    state.counters["checksum"] = static_cast<double>(sum);
}
}
namespace seriespack_measurement {
void register_ordinary_runtime_benchmarks() {
    using namespace seriespack_runtime_ranges;
    benchmark::AddCustomContext("ordinary_runtime_contract",
        "4096 independent runtime ranges; 256 queries per iteration; one bound callback; "
        "u32/u64 exact outputs; first+last uint64 checksum; full oracle and timed-pointer canaries outside timing");
    for (auto target : {sp::execution_target::avx2, sp::execution_target::avx512,
                       sp::execution_target::neon}) {
        const auto empty = sp::const_view::assume_valid({1,0,sp::geometry::local8}, 0, {});
        if (!sp::bind_reader(empty, target)) continue;
        for (auto s : shapes) for (auto r : regimes(s.geometry)) {
            benchmark::RegisterBenchmark(name(s, 4, r, target).c_str(),
                [=](benchmark::State& state) { run<std::uint32_t>(state, s, r, target); });
            benchmark::RegisterBenchmark(name(s, 8, r, target).c_str(),
                [=](benchmark::State& state) { run<std::uint64_t>(state, s, r, target); });
        }
    }
}
}
