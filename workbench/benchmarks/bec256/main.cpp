#include "fixture.h"
#include <ikea/bec256/author/write.h>
#include <benchmark/benchmark.h>
#include <cstdlib>

namespace bec_bench {
enum class method {
    read_exact,
    read_padded,
    encode,
    prepare_write,
    estimate,
    enum_cost,
    gate,
    native_checked,
    native_exact,
    native_wide
};
template <method M> void run(benchmark::State &state, unsigned shape) {
    fixture data(shape);
    std::array<bc::byte, 64> output{};
    bc::destination target{output};
    std::array<ikea::owner_write, 1> entries;
    ikea::source_write_journal effects{entries};
    // The pointer escape plus the loop barrier makes issued writes observable;
    // consuming just a returned length can silently turn an encoder into sizing.
    benchmark::DoNotOptimize(output.data());
    benchmark::DoNotOptimize(entries.data());
    benchmark::DoNotOptimize(&effects);
    unsigned i = 0;
    for (auto _ : state) {
        effects.used = 0;
        if constexpr (M == method::read_exact || M == method::read_padded)
            bc::decode(M == method::read_exact ? data.exact[i] : data.padded[i],
                       std::span<bc::byte, 32>(output.data(), 32));
        if constexpr (M == method::encode)
            benchmark::DoNotOptimize(bc::encode(data.plain[i], data.pop[i], target, 0, effects));
        if constexpr (M == method::prepare_write) {
            auto candidate = bc::prepare(data.plain[i], data.pop[i]);
            benchmark::DoNotOptimize(candidate->write(target, 0, effects));
        }
        if constexpr (M == method::estimate)
            benchmark::DoNotOptimize(bc::estimate_bytes(data.plain[i]));
        if constexpr (M == method::enum_cost)
            benchmark::DoNotOptimize(bc::enum_bits(data.plain[i]));
        if constexpr (M == method::gate)
            benchmark::DoNotOptimize(
                bc::encode_if_promising(data.plain[i], data.pop[i], 144, target, 0, effects));
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
        if constexpr (M == method::native_checked)
            benchmark::DoNotOptimize(bc::native::encode(bc::native::load(data.plain[i].data()),
                                                        data.pop[i], target, 0, effects));
        if constexpr (M == method::native_exact)
            benchmark::DoNotOptimize(bc::native::encode_exact_unchecked(
                bc::native::load(data.plain[i].data()), data.pop[i], target, 0, effects));
        if constexpr (M == method::native_wide)
            benchmark::DoNotOptimize(bc::native::encode_unchecked(
                bc::native::load(data.plain[i].data()), data.pop[i], output.data()));
#endif
        benchmark::ClobberMemory();
        i = (i + 1) % block_count;
    }
    state.counters["blocks_per_iteration"] = 1;
    state.counters["body_bytes_per_block"] = data.mean_bytes;
    if constexpr (M == method::gate) {
        unsigned declines = 0;
        for (unsigned j = 0; j < block_count; ++j)
            declines += data.pop[j] > 0 && data.pop[j] < 256 && bc::enum_bits(data.plain[j]) >= 144;
        state.counters["declined_fraction"] = declines / double(block_count);
    }
    state.SetItemsProcessed(state.iterations());
}
template <std::size_t... I> auto callbacks(std::index_sequence<I...>) {
    return std::array{&run<static_cast<method>(I)>...};
}
} // namespace bec_bench

int main(int argc, char **argv) {
    using namespace bec_bench;
    constexpr std::array names{"read_exact",
                               "read_padded",
                               "encode_checked_exact",
                               "prepare_write",
                               "estimate_bytes",
                               "enum_bits",
                               "encode_gate144",
                               "native_encode_checked_exact",
                               "native_encode_admitted_exact",
                               "native_encode_wide"};
    const unsigned shapes = std::getenv("BEC_BROAD") ? 3 + populations.size() : 3;
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    constexpr unsigned methods = names.size();
#else
    constexpr unsigned methods = 7;
#endif
    const auto functions = callbacks(std::make_index_sequence<methods>{});
    for (unsigned shape = 0; shape < shapes; ++shape)
        for (unsigned m = 0; m < methods; ++m)
            benchmark::RegisterBenchmark(("codec/" + shape_name(shape) + "/" + names[m]).c_str(),
                                         functions[m], shape);
    register_composition_cases(shapes);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
