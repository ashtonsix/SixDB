#include "../../benchmarks/seriespack/fixture.h"
#include <ikea/seriespack/detail/dense_read.h>
#include <ikea/seriespack/filter.h>
#include <ikea/seriespack/write.h>
#include <bit>

// Local versus striped for the widths where `bulk_arm` chooses stripes, using
// the maintained implementation. Same values, 8,192 dense headless rows.
//   read:   dense_range of all rows
//   read16: 8,192 random aligned 16-row ranges
//   point:  8,192 random get_unchecked, summed
//   write:  prepared maintained replacement of all rows with sum and coverage
//   filter: 128 complete 64-row less-than windows (cutoff at half range), popcount
template <unsigned K, sp::geometry G, unsigned Operation> void preset_case(benchmark::State& state) {
    using F = sp::layout<K, G>;
    using U = sp::uint_for<K>;
    fixture<K, G> data(8192);
    const auto view = *sp::view<F>::attach(
        data.count, {{{std::as_writable_bytes(std::span{data.storage.get(), data.bytes}), F::tile_bytes},{},{}}});
    const auto prepared = *sp::bind_writer(view);
    std::vector<U> output(data.count), input(data.values.begin(), data.values.end());
    for (auto& value : input)
        value ^= U(~std::uint64_t{0} >> (64 - K));
    std::vector<std::size_t> packets(data.queries.size());
    for (std::size_t i = 0; i < packets.size(); ++i)
        packets[i] = data.queries[i] / 16 * 16;
    std::vector<ikea::journal::entry> records(
        *prepared.effect_capacity(0, data.count, sp::selection::all()));
    ikea::journal effects{records};
    sp::author::sum_change summary;
    const std::uint64_t cutoff = std::uint64_t{1} << (K - 1);
    auto run = [&]() -> std::uint64_t {
        if constexpr (Operation == 0)
            sp::detail::dense_range<F, U>(data.storage.get(), 0, data.count, output.data());
        else if constexpr (Operation == 1) {
            std::uint64_t result = 0;
            for (auto first : packets) {
                sp::detail::dense_range<F, U>(data.storage.get(), first, 16, output.data());
                result += output[first % 16];
            }
            return result;
        } else if constexpr (Operation == 2) {
            std::uint64_t result = 0;
            for (auto row : data.queries)
                result += sp::get_unchecked(view, row);
            return result;
        } else if constexpr (Operation == 3) {
            effects.used = 0;
            summary = {};
            prepared.replace_unchecked(0, data.count, input.data(), effects,
                                       sp::selection::all(), summary);
            benchmark::DoNotOptimize(effects.used);
            return summary.finish();
        } else {
            std::uint64_t result = 0;
            for (std::size_t first = 0; first < data.count; first += 64)
                result += std::popcount(
                    sp::filter_unchecked<ikea::comparison::less>(view, first, 64, cutoff));
            return result;
        }
        return 0;
    };
    const auto actual = run();
    std::uint64_t expected = 0;
    if constexpr (Operation == 0)
        for (std::size_t i = 0; i < data.count; ++i)
            if (output[i] != data.values[i])
                std::abort();
    if constexpr (Operation == 1)
        for (auto first : packets)
            expected += data.values[first + first % 16];
    if constexpr (Operation == 2)
        for (auto row : data.queries)
            expected += data.values[row];
    if constexpr (Operation == 3)
        for (std::size_t i = 0; i < data.count; ++i) {
            expected += std::uint64_t(input[i]) - data.values[i];
            if (sp::get_unchecked(view, i) != input[i])
                std::abort();
        }
    if constexpr (Operation == 4)
        for (auto x : data.values)
            expected += x < cutoff;
    if (Operation != 0 && actual != expected)
        std::abort();
    for (auto _ : state) {
        benchmark::DoNotOptimize(run());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * data.count);
    state.counters["tile_rows"] = F::tile_rows;
}
template <unsigned K> void register_preset_width() {
    static constexpr const char* operations[] = {"read", "read16", "point", "write", "filter"};
    sp::detail::each<5>([](auto op) {
        const auto name = std::string("k") + std::to_string(K) + "/" + operations[op] + "/";
        benchmark::RegisterBenchmark((name + "local").c_str(),
                                     preset_case<K, sp::geometry::local, op>);
        benchmark::RegisterBenchmark((name + "striped").c_str(),
                                     preset_case<K, sp::geometry::striped, op>);
    });
}
int main(int argc, char** argv) {
    sp::detail::each<7>([](auto k) { register_preset_width<k + 1>(); });
    register_preset_width<10>();
    register_preset_width<12>();
    register_preset_width<14>();
    register_preset_width<15>();
    register_preset_width<20>();
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
}
