#include <ikea/bec256/author/write.h>
#include <ikea/bec256/author/chain.h>
#include "../../../ikea/test/bec256/reference.h"
#include <benchmark/benchmark.h>
#include <random>

namespace {
namespace bc = ikea::bec256;
enum class path { ordinary, native_checked, native_admitted, native_shared, native_cps };
struct result {
    unsigned pa, pb, na, nb;
};

struct context {
    const bc::source &a, &b;
    const bc::byte *mask;
    bc::destination &target;
    ikea::source_write_journal &effects;
    result output{};
};
using chain = bc::chain<2>;
[[gnu::always_inline]] static chain::result
read_filter(void *raw, std::size_t, std::uint64_t active, bc::pipeline_bits, unsigned) {
    auto &c = *static_cast<context *>(raw);
    return {bc::pipeline_bits::from(bc::native::intersection(bc::native::read_pair(c.a, c.b),
                                                             bc::native::load_pair(c.mask))),
            active};
}
[[gnu::always_inline]] static void write_result(void *raw, std::size_t, std::uint64_t,
                                                bc::pipeline_bits values) {
    auto &c = *static_cast<context *>(raw);
    const auto bits = values.get();
    const auto pa = bc::native::population(bc::native::part<0>(bits));
    const auto pb = bc::native::population(bc::native::part<1>(bits));
    const auto sizes =
        bc::native::encode_pair_exact_unchecked(bits, pa, c.target, 0, pb, c.target, 64, c.effects);
    c.output = {pa, pb, sizes.first, sizes.second};
}
static const auto pipeline =
    *chain::prepare(std::array{&chain::stage<read_filter>}, &chain::completion<write_result>);
[[gnu::always_inline]] static result inline_mutation(context &c) {
    const auto filtered = read_filter(&c, 0, 3, {}, 0);
    write_result(&c, 0, filtered.active, filtered.values);
    return c.output;
}
[[gnu::noinline]] static result shared_mutation(context &c) { return inline_mutation(c); }

template <path P>
[[gnu::always_inline]] result replace_pair(const bc::source &a, const bc::source &b,
                                           const bc::byte *mask, bc::destination &target,
                                           ikea::source_write_journal &effects) {
    if constexpr (P == path::native_admitted || P == path::native_shared || P == path::native_cps) {
        context c{a, b, mask, target, effects};
        if constexpr (P == path::native_admitted)
            return inline_mutation(c);
        if constexpr (P == path::native_shared)
            return shared_mutation(c);
        if constexpr (P == path::native_cps) {
            pipeline.run(&c, 0, 3, {});
            return c.output;
        }
    } else if constexpr (P == path::ordinary) {
        std::array<bc::byte, 64> plain;
        bc::decode_pair(a, b, plain);
        for (unsigned i = 0; i < 64; ++i)
            plain[i] &= mask[i];
        const auto pa = bec_reference::population(plain.data());
        const auto pb = bec_reference::population(plain.data() + 32);
        auto na =
            bc::encode(std::span<const bc::byte, 32>(plain.data(), 32), pa, target, 0, effects);
        auto nb = bc::encode(std::span<const bc::byte, 32>(plain.data() + 32, 32), pb, target, 64,
                             effects);
        assert(na && nb);
        return {pa, pb, *na, *nb};
    } else {
        auto bits =
            bc::native::intersection(bc::native::read_pair(a, b), bc::native::load_pair(mask));
        const auto pa = bc::native::population(bc::native::part<0>(bits));
        const auto pb = bc::native::population(bc::native::part<1>(bits));
        auto na = bc::native::encode(bc::native::part<0>(bits), pa, target, 0, effects);
        auto nb = bc::native::encode(bc::native::part<1>(bits), pb, target, 64, effects);
        assert(na && nb);
        return {pa, pb, *na, *nb};
    }
}

template <path P> void mutation(benchmark::State &state, unsigned shape) {
    std::mt19937_64 rng(0xbecc0de);
    std::array<bc::plain_block, 256> plain;
    std::array<std::array<bc::byte, 64>, 256> bodies{};
    std::vector<bc::source> sources;
    std::array<bc::byte, 64> mask;
    for (auto &b : mask)
        b = bc::byte(rng());
    for (unsigned i = 0; i < 256; ++i) {
        for (unsigned j = 0; j < 32; ++j)
            plain[i][j] = shape == 0   ? bc::byte(rng())
                          : shape == 1 ? bc::byte(j < i % 33 ? 255 : 0)
                                       : bc::byte(i % 2 ? 255 : 0);
        auto p = bec_reference::population(plain[i].data());
        auto body = bc::prepare(plain[i], p);
        assert(body);
        std::copy(body->body().begin(), body->body().end(), bodies[i].begin());
        sources.push_back(
            *bc::source::admit(std::span(bodies[i]).first(body->bytes()), body->bytes(), p));
    }
    std::array<bc::byte, 128> output;
    bc::destination target{output};
    std::array<ikea::owner_write, 2> entries;
    ikea::source_write_journal effects{entries};
    benchmark::DoNotOptimize(output.data());
    benchmark::DoNotOptimize(entries.data());
    benchmark::DoNotOptimize(&effects);
    unsigned total_body_bytes = 0;
    for (unsigned i = 0; i < 256; ++i) {
        output.fill(bc::byte{0xa5});
        effects.used = 0;
        auto got =
            replace_pair<P>(sources[i], sources[(i + 73) % 256], mask.data(), target, effects);
        total_body_bytes += got.na + got.nb;
        for (unsigned half = 0; half < 2; ++half) {
            bc::plain_block expected;
            for (unsigned j = 0; j < 32; ++j)
                expected[j] = plain[(i + half * 73) % 256][j] & mask[half * 32 + j];
            std::array<bc::byte, 47> wire;
            const auto n = bec_reference::encode(expected.data(), wire.data());
            assert(n == (half ? got.nb : got.na));
            assert(bec_reference::population(expected.data()) == (half ? got.pb : got.pa));
            assert(std::equal(wire.begin(), wire.begin() + n, output.begin() + half * 64));
            for (unsigned j = n; j < 64; ++j)
                assert(output[half * 64 + j] == bc::byte{0xa5});
        }
        assert(effects.used == unsigned(got.na != 0) + unsigned(got.nb != 0));
    }
    unsigned i = 0;
    for (auto _ : state) {
        effects.used = 0;
        auto got =
            replace_pair<P>(sources[i], sources[(i + 73) % 256], mask.data(), target, effects);
        benchmark::DoNotOptimize(got);
        benchmark::ClobberMemory();
        i = (i + 1) % 256;
    }
    state.counters["blocks_per_call"] = 2;
    state.counters["output_body_bytes"] = total_body_bytes / 256.0;
}
} // namespace

void register_mutation_cases() {
    const std::array names{"ordinary", "native_checked", "native_admitted", "native_shared",
                           "native_cps"};
    const std::array callbacks{&mutation<path::ordinary>, &mutation<path::native_checked>,
                               &mutation<path::native_admitted>, &mutation<path::native_shared>,
                               &mutation<path::native_cps>};
    for (unsigned shape = 0; shape < 3; ++shape)
        for (unsigned p = 0; p < names.size(); ++p) {
            auto name = std::string("pair_mutation/") +
                        std::array{"random", "runs", "terminal"}[shape] + "/" + names[p];
            benchmark::RegisterBenchmark(name.c_str(), callbacks[p], shape);
        }
}
