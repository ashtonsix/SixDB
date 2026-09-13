#include <ikea/bec256/author/write.h>
#include "casing.h"
#include "../../../ikea/test/bec256/reference.h"
#include "../ikea-composition/probes/ikea-blocks/codec.h"
#include <benchmark/benchmark.h>
#include <random>

namespace bc = ikea::bec256;
enum class method {
    spike_encode,
    native_encode,
    prepare,
    exact_write,
    staged_write,
    bound_write,
    native_checked_write,
    native_admitted_write,
    spike_decode,
    padded_decode,
    exact_decode,
    native_pair,
    exact_pair,
    spike_pair
};
static constexpr std::array<unsigned, 15> screened_populations{
    1, 2, 4, 8, 16, 32, 64, 128, 192, 224, 240, 248, 252, 254, 255};
template <method mode> static void codec(benchmark::State &state, unsigned shape) {
    std::mt19937_64 rng(0xbec256);
    std::array<bc::plain_block, 256> input;
    std::array<std::array<bc::byte, 64>, 256> body;
    std::array<unsigned, 256> populations, sizes;
    std::vector<bc::source> padded, exact;
    for (unsigned i = 0; i < 256; ++i) {
        for (unsigned j = 0; j < 32; ++j) {
            input[i][j] = shape == 0   ? bc::byte(rng())
                          : shape == 1 ? bc::byte(j < i % 33 ? 255 : 0)
                                       : bc::byte(i % 2 ? 255 : 0);
        }
        if (shape >= 3) {
            std::array<unsigned, 256> order;
            for (unsigned j = 0; j < 256; ++j)
                order[j] = j;
            std::shuffle(order.begin(), order.end(), rng);
            input[i].fill(bc::byte{0});
            for (unsigned j = 0; j < screened_populations[shape - 3]; ++j)
                input[i][order[j] / 8] |= bc::byte(1u << (order[j] % 8));
        }
        populations[i] = bec_reference::population(input[i].data());
        auto encoded = bc::prepare(input[i], populations[i]);
        assert(encoded);
        sizes[i] = encoded->bytes();
        body[i].fill(bc::byte{0});
        std::copy(encoded->body().begin(), encoded->body().end(), body[i].begin());
        padded.push_back(*bc::source::admit(body[i], sizes[i], populations[i]));
        exact.push_back(
            *bc::source::admit(std::span(body[i]).first(sizes[i]), sizes[i], populations[i]));
    }
    alignas(64) std::array<bc::byte, 128> output{};
    bc::destination target{output};
    std::array<ikea::owner_write, 1> entries;
    ikea::source_write_journal effects{entries};
    benchmark::DoNotOptimize(output.data());
    benchmark::DoNotOptimize(entries.data());
    benchmark::DoNotOptimize(&effects);
    auto bound = bec_study::bound_encoder::bind(target, effects);
    assert(bound);
    unsigned i = 0;
    for (auto _ : state) {
        switch (mode) {
        case method::spike_encode:
            benchmark::DoNotOptimize(ikea_probe::encode_native(
                reinterpret_cast<const std::uint8_t *>(input[i].data()), populations[i],
                reinterpret_cast<std::uint8_t *>(output.data())));
            break;
        case method::native_encode:
            benchmark::DoNotOptimize(bc::native::encode_unchecked(bc::native::load(input[i].data()),
                                                                  populations[i], output.data()));
            break;
        case method::prepare: {
            auto value = bc::prepare(input[i], populations[i]);
            benchmark::DoNotOptimize(value);
            break;
        }
        case method::exact_write:
            effects.used = 0;
            benchmark::DoNotOptimize(bc::encode(input[i], populations[i], target, 0, effects));
            break;
        case method::staged_write:
            effects.used = 0;
            benchmark::DoNotOptimize(
                bec_study::staged_encode(input[i], populations[i], target, 0, effects));
            break;
        case method::bound_write:
            effects.used = 0;
            benchmark::DoNotOptimize(bound->encode(input[i], populations[i]));
            break;
        case method::native_checked_write:
            effects.used = 0;
            benchmark::DoNotOptimize(bc::native::encode(bc::native::load(input[i].data()),
                                                        populations[i], target, 0, effects));
            break;
        case method::native_admitted_write:
            effects.used = 0;
            benchmark::DoNotOptimize(bc::native::encode_exact_unchecked(
                bc::native::load(input[i].data()), populations[i], target, 0, effects));
            break;
        case method::spike_decode:
            benchmark::DoNotOptimize(ikea_probe::decode_native(
                reinterpret_cast<const std::uint8_t *>(body[i].data()), populations[i],
                reinterpret_cast<std::uint8_t *>(output.data())));
            break;
        case method::padded_decode:
            bc::decode(padded[i], std::span<bc::byte, 32>(output.data(), 32));
            break;
        case method::exact_decode:
            bc::decode(exact[i], std::span<bc::byte, 32>(output.data(), 32));
            break;
        case method::native_pair: {
            auto value = bc::native::read_pair(padded[i], padded[(i + 73) % 256]);
            bc::native::store_pair(output.data(), value);
            break;
        }
        case method::exact_pair: {
            auto value = bc::native::read_pair(exact[i], exact[(i + 73) % 256]);
            bc::native::store_pair(output.data(), value);
            break;
        }
        case method::spike_pair:
            ikea_probe::decode_native2(
                reinterpret_cast<const std::uint8_t *>(body[i].data()), populations[i],
                reinterpret_cast<const std::uint8_t *>(body[(i + 73) % 256].data()),
                populations[(i + 73) % 256], reinterpret_cast<std::uint8_t *>(output.data()));
            break;
        }
        benchmark::ClobberMemory();
        i = (i + 1) % 256;
    }
    state.counters["blocks_per_call"] =
        (mode == method::native_pair || mode == method::exact_pair || mode == method::spike_pair)
            ? 2
            : 1;
    unsigned total_bytes = 0, total_population = 0;
    for (unsigned j = 0; j < 256; ++j) {
        total_bytes += sizes[j];
        total_population += populations[j];
    }
    state.counters["body_bytes_per_block"] = total_bytes / 256.0;
    state.counters["population_per_block"] = total_population / 256.0;
}
template <std::size_t... I> static auto callbacks(std::index_sequence<I...>) {
    return std::array{&codec<static_cast<method>(I)>...};
}
void register_codec_cases() {
    const std::array<const char *, 14> names{"spike_encode_wide",
                                             "native_encode_wide",
                                             "prepare_checked",
                                             "encode_checked_exact",
                                             "encode_staged_exact",
                                             "encode_bound_exact",
                                             "native_encode_checked_exact",
                                             "native_encode_admitted_exact",
                                             "spike_decode_padded",
                                             "decode_padded",
                                             "decode_exact",
                                             "native_pair_materialized",
                                             "native_pair_exact_materialized",
                                             "spike_pair_materialized"};
    const auto functions = callbacks(std::make_index_sequence<names.size()>{});
    for (unsigned shape = 0; shape < 3 + screened_populations.size(); ++shape)
        for (unsigned m = 0; m < names.size(); ++m) {
            const auto shape_name =
                shape < 3
                    ? std::string(std::array<const char *, 3>{"random", "runs", "terminal"}[shape])
                    : "population" + std::to_string(screened_populations[shape - 3]);
            const auto name = std::string("codec/") + shape_name + "/" + names[m];
            // Resolve the experiment choice before timing. A runtime switch in one
            // large benchmark body distorts inlining and register allocation.
            benchmark::RegisterBenchmark(name.c_str(), functions[m], shape);
        }
}
