#include <ikea/bec256/author/analysis.h>
#include <ikea/bec256/author/write.h>
#include "../../../ikea/test/bec256/reference.h"
#include <benchmark/benchmark.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {
namespace bc = ikea::bec256;
enum class method {
    estimate,
    native_estimate,
    exact_size,
    encode,
    checked_encode,
    split_gate,
    gate,
    native_gate,
    statistic
};
struct sample {
    bc::plain_block input;
    unsigned population, bytes, cost;
};
struct data_set {
    std::string name;
    std::vector<sample> values;
};
std::vector<data_set> inputs;
// Exact no-emission control. Dead value/rank/packing work can be eliminated
// from the shared encoder body; supplied population is trusted in this control.
struct size_sink {
    [[gnu::always_inline]] unsigned operator()(const std::uint64_t *,
                                               const std::uint64_t *n) const {
        return bc::detail::encoded_bytes(n);
    }
    unsigned singleton(std::uint8_t) const { return 1; }
};
[[gnu::always_inline]] unsigned exact_size(bc::native::block value, unsigned population) {
#if defined(IKEA_BEC256_AVX512)
    return bc::detail::avx512::encode_to(value, population, size_sink{});
#else
    return bc::detail::neon::encode_to(value, population, size_sink{});
#endif
}
void add(std::string name, std::vector<bc::plain_block> blocks) {
    data_set d{std::move(name), {}};
    for (auto &b : blocks) {
        std::array<bc::byte, 64> reference;
        const auto p = bec_reference::population(b.data());
        const auto n = bec_reference::encode(b.data(), reference.data());
        assert(exact_size(bc::native::load(b.data()), p) == n);
        d.values.push_back({b, p, n, bc::enum_bits(b)});
    }
    inputs.push_back(std::move(d));
}
template <method M> void run(benchmark::State &state, unsigned index, unsigned cutoff) {
    const auto &d = inputs[index];
    std::array<bc::byte, 64> output;
    bc::destination destination{output};
    std::array<ikea::owner_write, 1> entries;
    ikea::source_write_journal effects{entries};
    // ClobberMemory observes emitted bytes/events only after their addresses
    // escape; this matters for fully inlined native write controls.
    benchmark::DoNotOptimize(output.data());
    benchmark::DoNotOptimize(entries.data());
    benchmark::DoNotOptimize(&effects);
    unsigned i = 0;
    for (auto _ : state) {
        const auto &s = d.values[i];
        effects.used = 0;
        if constexpr (M == method::estimate)
            benchmark::DoNotOptimize(bc::estimate_bytes(s.input));
        if constexpr (M == method::native_estimate)
            benchmark::DoNotOptimize(bc::native::estimate_bytes(bc::native::load(s.input.data())));
        if constexpr (M == method::statistic)
            benchmark::DoNotOptimize(bc::native::enum_bits(bc::native::load(s.input.data())));
        if constexpr (M == method::exact_size)
            benchmark::DoNotOptimize(exact_size(bc::native::load(s.input.data()), s.population));
        if constexpr (M == method::encode)
            benchmark::DoNotOptimize(bc::native::encode_unchecked(bc::native::load(s.input.data()),
                                                                  s.population, output.data()));
        if constexpr (M == method::checked_encode)
            benchmark::DoNotOptimize(bc::encode(s.input, s.population, destination, 0, effects));
        if constexpr (M == method::gate)
            benchmark::DoNotOptimize(
                bc::encode_if_promising(s.input, s.population, cutoff, destination, 0, effects));
        if constexpr (M == method::native_gate)
            benchmark::DoNotOptimize(bc::native::encode_if_promising(
                bc::native::load(s.input.data()), s.population, cutoff, destination, 0, effects));
        if constexpr (M == method::split_gate) {
            const auto value = bc::native::load(s.input.data());
            // Prior style: separate prediction then encode. Supplied population
            // is trusted on declines; the fused checked API has a stronger contract.
            if (!s.population || s.population == 256 || bc::native::enum_bits(value) < cutoff)
                benchmark::DoNotOptimize(
                    bc::native::encode(value, s.population, destination, 0, effects));
        }
        benchmark::ClobberMemory();
        if (++i == d.values.size())
            i = 0;
    }
    unsigned partial = 0, declines = 0, missed = 0, extra = 0;
    std::vector<unsigned> errors;
    unsigned total_error = 0;
    for (auto &s : d.values) {
        if (!s.population || s.population == 256)
            continue;
        ++partial;
        const unsigned estimate = bc::estimate_bytes(s.input);
        const unsigned error = estimate > s.bytes ? estimate - s.bytes : s.bytes - estimate;
        errors.push_back(error);
        total_error += error;
        if (s.cost >= cutoff) {
            ++declines;
            if (s.bytes < 32) {
                ++missed;
                extra += 32 - s.bytes;
            }
        }
    }
    std::sort(errors.begin(), errors.end());
    const double denominator = std::max(partial, 1u);
    state.counters["blocks"] = d.values.size();
    state.counters["partial_blocks"] = partial;
    state.counters["declined_fraction_partial"] = declines / denominator;
    state.counters["missed_compress_fraction_declined"] = double(missed) / std::max(declines, 1u);
    state.counters["extra_raw_bytes_per_partial"] = extra / denominator;
    state.counters["estimate_mae_partial"] = total_error / denominator;
    state.counters["estimate_p95_partial"] =
        errors.empty() ? 0 : errors[(errors.size() - 1) * 95 / 100];
    state.counters["estimate_max_partial"] = errors.empty() ? 0 : errors.back();
}
template <std::size_t... I> auto callbacks(std::index_sequence<I...>) {
    return std::array{&run<static_cast<method>(I)>...};
}
} // namespace
void register_analysis_cases() {
    std::mt19937_64 rng(0xbec0512);
    for (unsigned shape = 0; shape < 7; ++shape) {
        std::vector<bc::plain_block> blocks(256);
        for (auto &b : blocks) {
            const bool dense =
                shape == 0 || (shape >= 3 && shape <= 5 &&
                               rng() % 100 < std::array<unsigned, 3>{10, 50, 90}[shape - 3]);
            const unsigned end = rng() % 33;
            for (unsigned j = 0; j < 32; ++j)
                b[j] = dense ? bc::byte(rng()) : bc::byte(j < end ? 255 : 0);
            if (shape == 2)
                std::shuffle(b.begin(), b.end(), rng);
            if (shape == 6)
                b.fill(bc::byte((rng() & 1) ? 255 : 0));
        }
        add(std::array<const char *, 7>{"random", "runs", "permuted_bytes", "dense10", "dense50",
                                        "dense90", "terminal"}[shape],
            std::move(blocks));
    }
    if (const char *root = std::getenv("BEC_WINDOWS"))
        for (const char *name : {"census-income", "weather_sept_85", "wikileaks-noquotes"}) {
            std::ifstream f(std::filesystem::path(root) / (std::string(name) + ".windows"),
                            std::ios::binary);
            std::vector<bc::plain_block> blocks(2048);
            if (!f.read(reinterpret_cast<char *>(blocks.data()), blocks.size() * 32))
                throw std::runtime_error("missing analysis windows");
            add(name, std::move(blocks));
        }
    const auto functions = callbacks(std::make_index_sequence<9>{});
    constexpr std::array names{"estimate",    "native_estimate", "exact_size",
                               "encode_wide", "encode_checked",  "split_gate",
                               "gate",        "native_gate",     "enum_statistic"};
    for (unsigned d = 0; d < inputs.size(); ++d)
        for (unsigned m = 0; m < names.size(); ++m)
            for (unsigned cutoff : {0u, 100u, 144u, 180u, 225u}) {
                if ((m < 5 || m > 7) && cutoff != 225)
                    continue;
                auto name =
                    "analysis/" + inputs[d].name + "/" + names[m] + "/" + std::to_string(cutoff);
                benchmark::RegisterBenchmark(name.c_str(), functions[m], d, cutoff);
            }
}
