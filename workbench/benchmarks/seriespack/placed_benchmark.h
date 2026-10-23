#pragma once
#include "fixture.h"
#include <ikea/seriespack/writer.h>
namespace cp = sp::author;

struct placed_bytes {
    std::unique_ptr<std::uint8_t, free_bytes> data;
    std::size_t size;
    explicit placed_bytes(std::size_t n)
        : data(static_cast<std::uint8_t*>(std::aligned_alloc(64, (n + 63) & ~std::size_t{63}))),
          size(n) {
        if (!data)
            std::abort();
        std::memset(data.get(), 0, n);
    }
    std::span<std::byte> span() {
        return std::as_writable_bytes(std::span{data.get(), size});
    }
};
enum class placed_mode { sum_native, sum_materialized, mutation_bound, mutation_materialized };
template <class F, unsigned Placement, placed_mode Mode, bool Masked>
void placed_benchmark(benchmark::State& state) {
    using U = sp::uint_for<F::width>;
    using T = sp::layout<F::tail, sp::geometry::striped>;
    constexpr std::size_t count = 8192, tiles = count / F::tile_rows;
    constexpr auto stride =
        Placement ? ((F::tile_bytes + F::heads / 8 * F::tile_rows + 63) & ~std::size_t{63})
                  : F::tile_bytes;
    placed_bytes packed(tiles * stride), heads0(count), heads1(count), tails(count * T::width / 8);
    std::array<sp::plane<std::byte>, 3> planes{};
    planes[0] = {packed.span(), stride};
    if constexpr (F::heads >= 8)
        planes[1] = Placement
                        ? sp::plane<std::byte>{packed.span().subspan(F::tile_bytes), stride}
                        : sp::plane<std::byte>{heads0.span(), F::tile_rows};
    if constexpr (F::heads == 16)
        planes[2] =
            Placement ? sp::plane<std::byte>{packed.span().subspan(F::tile_bytes + F::tile_rows),
                                                stride}
                      : sp::plane<std::byte>{heads1.span(), F::tile_rows};
    const auto original = *sp::view<F>::attach(count, planes);
    const auto tail =
        *sp::view<T>::attach(count, {{{std::as_writable_bytes(std::span{tails.span()}), T::tile_bytes},{},{}}});
    std::vector<U> input(count), scratch(count), truth(count);
    std::vector<std::uint8_t> low(count);
    std::mt19937_64 random(42);
    constexpr std::uint64_t domain = ~std::uint64_t{0} >> (64 - F::width),
                            tail_mask = (1u << F::tail) - 1;
    for (std::size_t i = 0; i < count; ++i) {
        truth[i] = random() & domain;
        input[i] = truth[i] ^ domain;
        low[i] = (truth[i] ^ tail_mask) & tail_mask;
    }
    ikea::no_effects none;
    sp::author::initialize_unchecked(original, truth.data(), none);
    sp::author::initialize_unchecked(tail, low.data(), none);
    const auto base = cp::expression(original);
    const auto replacement = cp::expression(tail);
    const auto expression = [&] {
        if constexpr (Placement == 2) {
            using P = cp::payload_expression<F::payload, decltype(base.payload.body),
                                             decltype(replacement.payload.tail)>;
            using E = cp::value_expression<F::width, F::heads, P, decltype(base.head0),
                                           decltype(base.head1)>;
            return E{{base.payload.body, replacement.payload.tail}, base.head0, base.head1};
        } else
            return base;
    }();
    if constexpr (Placement == 2)
        for (std::size_t i = 0; i < count; ++i)
            truth[i] = (truth[i] & ~tail_mask) | low[i];
    const auto prepared = sp::bind_writer(expression, count);
    if (!prepared)
        std::abort();
    const auto operation = prepared->template erase<U, sp::author::sum_change>();
    std::vector<ikea::journal::entry> records(count * 2);
    ikea::journal effects{records};
    sp::author::sum_change summary;
    constexpr std::uint16_t selected = Masked ? 0xb6db : 0xffff;
    auto mask = [](std::size_t) { return selected; };
    std::array<std::uint16_t, count / 16> words;
    words.fill(selected);
    const auto selection = Masked ? sp::selection::bits(0, words) : sp::selection::all();
    const std::uint64_t cutoff = domain - domain / 3;
    auto run = [&]() {
        effects.used = 0;
        summary = {};
        if constexpr (Mode == placed_mode::sum_native)
            return cp::sum_regions(expression, 0, count, cutoff, mask);
        else if constexpr (Mode == placed_mode::mutation_bound) {
            operation.replace_unchecked(0, count, input.data(), effects, selection, summary);
            return summary.finish();
        } else {
            // Materialize the same admitted expression using its native reads.
            // No scalar decoder handicap and no source-format mismatch.
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
            cp::wide_ops<16> reader;
            for (std::size_t row = 0; row < count; row += 16)
                sp::native_group::store(scratch.data() + row,
                                        cp::read(reader, expression, row, std::uint64_t{0xffff}));
#else
            cp::native_ops reader;
            for (std::size_t row = 0; row < count; row += 16)
                sp::native::store16(scratch.data() + row,
                                    cp::read(reader, expression, row, std::uint16_t{0xffff}));
#endif
            std::uint64_t result = 0;
            for (std::size_t i = 0; i < count; ++i)
                if (selected & (1u << (i % 16))) {
                    if constexpr (Mode == placed_mode::sum_materialized) {
                        if (scratch[i] < cutoff)
                            result += scratch[i];
                    } else {
                        result += std::uint64_t(input[i]) - scratch[i];
                        scratch[i] = input[i];
                    }
                }
            if constexpr (Mode == placed_mode::mutation_materialized) {
                sp::author::no_summary no_summary;
                prepared->replace_unchecked(0, count, scratch.data(), effects,
                                            sp::selection::all(), no_summary);
            }
            return result;
        }
    };
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        std::uint64_t wanted = 0;
        for (std::size_t i = 0; i < count; ++i)
            if (selected & (1u << (i % 16))) {
                if constexpr (Mode == placed_mode::sum_native ||
                              Mode == placed_mode::sum_materialized) {
                    if (truth[i] < cutoff)
                        wanted += truth[i];
                } else {
                    wanted += std::uint64_t(input[i]) - truth[i];
                    truth[i] = input[i];
                }
            }
        if (run() != wanted)
            std::abort();
        cp::scalar_ops reader;
        for (std::size_t i = 0; i < count; ++i)
            if (cp::read(reader, expression, i, true).value != truth[i])
                std::abort();
    }
    for (auto _ : state) {
        auto result = run();
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * count);
    state.counters["logical_values"] = count;
}
template <class F, unsigned Placement, bool Masked> void register_placed_case() {
    const auto name = std::string("placed/k") + std::to_string(F::width) + "h" +
                      std::to_string(F::heads) +
                      (Placement == 0   ? "/separated"
                       : Placement == 1 ? "/interleaved"
                                        : "/substituted") +
                      (Masked ? "/partial" : "/all");
    benchmark::RegisterBenchmark((name + "/sum-native").c_str(),
                                 placed_benchmark<F, Placement, placed_mode::sum_native, Masked>);
    benchmark::RegisterBenchmark(
        (name + "/sum-materialized").c_str(),
        placed_benchmark<F, Placement, placed_mode::sum_materialized, Masked>);
    benchmark::RegisterBenchmark(
        (name + "/mutation-bound").c_str(),
        placed_benchmark<F, Placement, placed_mode::mutation_bound, Masked>);
    benchmark::RegisterBenchmark(
        (name + "/mutation-materialized").c_str(),
        placed_benchmark<F, Placement, placed_mode::mutation_materialized, Masked>);
}
template <class F> void register_placed_format() {
    sp::detail::each<3>([](auto placement) {
        register_placed_case<F, placement, false>();
        register_placed_case<F, placement, true>();
    });
}
