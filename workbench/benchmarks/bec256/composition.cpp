#include "fixture.h"
#include <ikea/bec256/author/chain.h>
#include <benchmark/benchmark.h>

namespace bec_bench {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
namespace {
using chain = bc::chain<4>;
struct context {
    const bc::source &a, &b;
    const bc::byte *qa, *qb;
    std::uint64_t count = 0;
};
[[gnu::always_inline]] chain::result read_body(void *raw, std::size_t, std::uint64_t active,
                                               bc::pipeline_bits, unsigned) {
    auto &c = *static_cast<context *>(raw);
    return {bc::pipeline_bits::from(bc::native::read_pair(c.a, c.b)), active};
}
[[gnu::always_inline]] chain::result intersect_body(void *raw, std::size_t, std::uint64_t active,
                                                    bc::pipeline_bits bits, unsigned) {
    auto &c = *static_cast<context *>(raw);
    return {bc::pipeline_bits::from(bc::native::intersection(
                bits.get(), bc::native::join(bc::native::load(c.qa), bc::native::load(c.qb)))),
            active};
}
[[gnu::always_inline]] void finish(void *raw, std::size_t, std::uint64_t, bc::pipeline_bits bits) {
    static_cast<context *>(raw)->count = bc::native::population(bits.get());
}
const chain pipeline =
    *chain::prepare(std::array{&chain::stage<read_body>, &chain::stage<intersect_body>},
                    &chain::completion<finish>);
[[gnu::always_inline]] void inline_pair(context &c) {
    const auto read = read_body(&c, 0, 3, {}, 0);
    const auto filtered = intersect_body(&c, 0, read.active, read.values, 1);
    finish(&c, 0, filtered.active, filtered.values);
}
[[gnu::noinline]] void compiled_pair(context &c) { inline_pair(c); }
enum class execution { inlined, compiled, cps, materialized };
template <execution E> std::uint64_t consume(context &c) {
    if constexpr (E == execution::inlined)
        inline_pair(c);
    if constexpr (E == execution::compiled)
        compiled_pair(c);
    if constexpr (E == execution::cps)
        pipeline.run(&c, 0, 3, {});
    if constexpr (E == execution::materialized) {
        std::array<bc::byte, 64> values;
        bc::decode_pair(c.a, c.b, values);
        c.count = bc::native::population(bc::native::intersection(
            bc::native::load_pair(values.data()),
            bc::native::join(bc::native::load(c.qa), bc::native::load(c.qb))));
    }
    return c.count;
}
template <execution E> void run(benchmark::State &state, unsigned shape, bool padded) {
    fixture data(shape);
    const auto &sources = padded ? data.padded : data.exact;
    // Compare every pair with a bytewise consumer outside timing. This catches
    // drift in the benchmark adapters; independent wire checks live in Ikea.
    for (unsigned i = 0; i < block_count; ++i) {
        const auto j = (i + 73) % block_count;
        context c{sources[i], sources[j], data.query[i].data(), data.query[j].data()};
        unsigned expected = 0;
        for (unsigned k = 0; k < 32; ++k) {
            expected +=
                std::popcount(std::to_integer<unsigned>(data.plain[i][k] & data.query[i][k]));
            expected +=
                std::popcount(std::to_integer<unsigned>(data.plain[j][k] & data.query[j][k]));
        }
        assert(consume<E>(c) == expected);
    }
    unsigned first = 0;
    for (auto _ : state) {
        std::uint64_t result = 0;
        for (unsigned n = 0; n < unsigned(state.range(0)); ++n) {
            const auto i = (first + n * 2) % block_count, j = (i + 73) % block_count;
            context c{sources[i], sources[j], data.query[i].data(), data.query[j].data()};
            result += consume<E>(c);
        }
        benchmark::DoNotOptimize(result);
        first = (first + 1) % block_count;
    }
    state.counters["blocks_per_iteration"] = 2 * state.range(0);
    state.SetItemsProcessed(state.iterations() * 2 * state.range(0));
}
} // namespace
#endif
void register_composition_cases(unsigned shapes) {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
    const std::array functions{&run<execution::inlined>, &run<execution::compiled>,
                               &run<execution::cps>, &run<execution::materialized>};
    constexpr std::array names{"inline", "compiled", "cps", "materialized"};
    for (unsigned shape = 0; shape < shapes; ++shape)
        for (bool padded : {false, true})
            for (unsigned e = 0; e < names.size(); ++e)
                benchmark::RegisterBenchmark(("intersection_count/" + shape_name(shape) + "/" +
                                              (padded ? "padded/" : "exact/") + names[e])
                                                 .c_str(),
                                             functions[e], shape, padded)
                    ->Arg(1)
                    ->Arg(16);
#else
    (void)shapes;
#endif
}
} // namespace bec_bench
