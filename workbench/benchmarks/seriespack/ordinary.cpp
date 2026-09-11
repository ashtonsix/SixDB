#include "fixture.h"
#include <ikea/seriespack/write.h>

// Compare the ordinary erased call with its concrete driver, holding physical
// bytes, traversal, summary and effect work equal. Prior-wire speed is measured
// separately by the optional Workbench comparison target.
template <unsigned K, sp::geometry G, unsigned Operation, bool Bound>
void ordinary_benchmark(benchmark::State& state) {
    using F = sp::format<K, G>;
    using U = sp::uint_for<K>;
    fixture<K, G> data(8192);
    const auto destination = *sp::view<F, std::uint8_t>::attach(
        data.count, {{{{data.storage.get(), data.bytes}, F::tile_bytes}, {}, {}}});
    auto reader = data.template next<U>();
    const auto prepared = *sp::bind_mutation(destination);
    auto writer = prepared.template erase<U, sp::sum_change>();
    std::vector<U> output(data.count), input(data.values.begin(), data.values.end());
    for (auto& value : input)
        value ^= U(~std::uint64_t{0} >> (64 - K));
    std::vector<sp::composition::owner_write> records(
        *prepared.effect_capacity(0, data.count, sp::row_selection::all()));
    sp::composition::write_journal effects{records};
    sp::sum_change summary;
    benchmark::DoNotOptimize(reader);
    benchmark::DoNotOptimize(writer);
    auto run = [&]() -> std::uint64_t {
        if constexpr (Operation == 0) {
            if constexpr (Bound)
                reader.read_unchecked(0, data.count, output.data());
            else
                sp::detail::dense_range<F, U>(data.storage.get(), 0, data.count, output.data());
        } else if constexpr (Operation == 1) {
            U result = 0;
            for (auto row : data.queries) {
                if constexpr (Bound)
                    result += reader.get_unchecked(row);
                else
                    result += sp::get_unchecked(destination, row);
            }
            benchmark::DoNotOptimize(result);
            return result;
        } else {
            effects.used = 0;
            summary = {};
            if constexpr (Bound)
                writer.replace_unchecked(0, data.count, input.data(), sp::row_selection::all(),
                                         summary, effects);
            else
                prepared.replace_unchecked(0, data.count, input.data(), sp::row_selection::all(),
                                           summary, effects);
            benchmark::DoNotOptimize(summary.finish());
            benchmark::DoNotOptimize(effects.used);
            return summary.finish();
        }
        return 0;
    };
    const auto actual = run();
    if constexpr (Operation == 0)
        for (std::size_t i = 0; i < data.count; ++i)
            if (output[i] != data.values[i])
                std::abort();
    if constexpr (Operation == 1) {
        U expected = 0;
        for (auto row : data.queries)
            expected += data.values[row];
        if (actual != expected)
            std::abort();
    }
    if constexpr (Operation == 2) {
        std::uint64_t expected = 0;
        for (std::size_t i = 0; i < data.count; ++i) {
            expected += std::uint64_t(input[i]) - data.values[i];
            if (sp::get_unchecked(destination, i) != input[i])
                std::abort();
        }
        if (actual != expected || !effects.used)
            std::abort();
    }
    for (auto _ : state) {
        run();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * data.count);
    state.counters["logical_values"] = data.count;
}
template <unsigned K, sp::geometry G, unsigned Operation> void ordinary_case() {
    const auto name = std::string("ordinary/k") + std::to_string(K) +
                      (G == sp::geometry::local ? "/local/" : "/striped/") +
                      (Operation == 0   ? "read"
                       : Operation == 1 ? "point"
                                        : "maintained-write");
    benchmark::RegisterBenchmark((name + "/bound").c_str(),
                                 ordinary_benchmark<K, G, Operation, true>);
    benchmark::RegisterBenchmark((name + "/concrete").c_str(),
                                 ordinary_benchmark<K, G, Operation, false>);
}
void register_ordinary() {
    sp::detail::each<3>([](auto operation) {
        ordinary_case<7, sp::geometry::local, operation>();
        ordinary_case<12, sp::geometry::striped, operation>();
        ordinary_case<28, sp::geometry::local, operation>();
        ordinary_case<64, sp::geometry::local, operation>();
    });
}
