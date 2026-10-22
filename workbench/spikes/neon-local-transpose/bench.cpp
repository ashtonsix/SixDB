#include "../../benchmarks/seriespack/fixture.h"
#include <ikea/seriespack/detail/dense_read.h>
#include <ikea/seriespack/write.h>

// Concrete SeriesPack operations whose NEON bodies may use transpose128, plus
// controls that do not. Each binary is built against one transpose variant; the
// driver and data are identical.
//   read:  dense_range over 8192 rows (64-row groups for pure tails)
//   read16: 8192 random aligned 16-row ranges (packet path)
//   write: prepared maintained replacement of all rows with sum and coverage
template <unsigned K, unsigned Operation> void transpose_case(benchmark::State& state) {
    using F = sp::format<K, sp::geometry::local>;
    using U = sp::uint_for<K>;
    fixture<K, sp::geometry::local> data(8192);
    const auto destination = *sp::view<F, std::uint8_t>::attach(
        data.count, {{{{data.storage.get(), data.bytes}, F::tile_bytes}, {}, {}}});
    const auto prepared = *sp::bind_mutation(destination);
    std::vector<U> output(data.count), input(data.values.begin(), data.values.end());
    for (auto& value : input)
        value ^= U(~std::uint64_t{0} >> (64 - K));
    std::vector<std::size_t> packets(data.queries.size());
    for (std::size_t i = 0; i < packets.size(); ++i)
        packets[i] = data.queries[i] / 16 * 16;
    std::vector<sp::composition::owner_write> records(
        *prepared.effect_capacity(0, data.count, sp::row_selection::all()));
    sp::composition::write_journal effects{records};
    sp::sum_change summary;
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
        } else {
            effects.used = 0;
            summary = {};
            prepared.replace_unchecked(0, data.count, input.data(), sp::row_selection::all(),
                                       summary, effects);
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
        std::uint64_t expected = 0;
        for (auto first : packets)
            expected += data.values[first + first % 16];
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
        benchmark::DoNotOptimize(run());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * data.count);
    state.SetLabel(NEON_LOCAL_TRANSPOSE_VARIANT);
}
template <unsigned K> void register_width() {
    const auto name = std::string("local/k") + std::to_string(K);
    benchmark::RegisterBenchmark((name + "/read").c_str(), transpose_case<K, 0>);
    benchmark::RegisterBenchmark((name + "/read16").c_str(), transpose_case<K, 1>);
    benchmark::RegisterBenchmark((name + "/write").c_str(), transpose_case<K, 2>);
}
int main(int argc, char** argv) {
    // 1..7: pure residual tails. 9..31 mixed: body plus vector-transposed
    // residual on write, integer-register residual on read. 8/16/64: no residual.
    sp::detail::each<7>([](auto k) { register_width<k + 1>(); });
    register_width<8>();
    register_width<11>();
    register_width<12>();
    register_width<20>();
    register_width<28>();
    register_width<31>();
    register_width<64>();
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
}
