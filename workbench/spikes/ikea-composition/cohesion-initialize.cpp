// Whole checked/trusted initialization of 1,024 strided records. The baseline
// and candidate differ only at the public plan/binding/byte/effects adapters.
// The oracle derives every occupied byte directly from the code schema and
// verifies spare bits, gaps and the complete source-qualified store coverage.
#include <ikea/tuplepack.h>
#include <benchmark/benchmark.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace tp = ikea::tuplepack;
namespace {
constexpr std::size_t rows = 1024, unit = 64, stride = 80, origin = 7;
using input = std::array<std::uint8_t, 128>;
#if COHESION_BEFORE
using plan = tp::constructor;
using operation = tp::construction_operation;
using event = ikea::owner_write;
using journal = ikea::source_write_journal;
auto attach(const tp::layout& law, std::vector<std::uint8_t>& bytes) {
    return tp::view::bind(law, bytes, rows, stride, origin);
}
auto bind_initialization(const plan& p, const tp::view& v) { return tp::bind_constructor(p, v); }
unsigned plane(const event& e) { return e.bytes.plane; }
std::size_t offset(const event& e) { return e.bytes.offset; }
std::size_t size(const event& e) { return e.bytes.size; }
#else
using plan = tp::write_plan<128>;
using operation = tp::writer<128>;
using event = ikea::journal::entry;
using journal = ikea::journal;
auto attach(const tp::layout& law, std::vector<std::uint8_t>& bytes) {
    return tp::view::attach(law, std::as_writable_bytes(std::span(bytes)), rows, stride, origin);
}
auto bind_initialization(const plan& p, const tp::view& v) { return tp::bind_writer(p, v); }
unsigned plane(const event& e) { return e.plane; }
std::size_t offset(const event& e) { return e.offset; }
std::size_t size(const event& e) { return e.size; }
#endif

template <class T> T require(std::expected<T, tp::error> result) {
    if (!result)
        std::abort();
    return *result;
}
struct fixture {
    unsigned code_count;
    std::array<tp::code, 128> codes;
    tp::layout law;
    plan prepared;
    std::vector<std::uint8_t> bytes;
    tp::view view;
    operation writer;
    std::array<event, rows> events;
    journal effects{events};
    std::array<std::vector<input>, 2> inputs;
    static auto schema() {
        std::array<tp::code, 128> result;
        for (unsigned i = 0; i < 64; ++i) {
            result[i] = {std::uint8_t(i), 1, 3};
            result[64 + i] = {std::uint8_t(i), 5, 3};
        }
        return result;
    }
    explicit fixture(unsigned count)
        : code_count(count), codes(schema()),
          law(require(tp::layout::make(unit, std::span(codes).first(count)))),
          prepared(require(plan::make(law))), bytes(rows * stride + origin + 11, 0xa7),
          view(require(attach(law, bytes))), writer(require(bind_initialization(prepared, view))) {
        for (unsigned bank = 0; bank < 2; ++bank) {
            inputs[bank].resize(rows);
            for (std::size_t row = 0; row < rows; ++row)
                for (unsigned code = 0; code < 128; ++code)
                    inputs[bank][row][code] = code < count
                        ? std::uint8_t((row * 13 + code * 5 + bank * 3) & 7)
                        : 0xe3; // ignored slots need not satisfy a code's width
        }
    }
    fixture(const fixture&) = delete;
    fixture(fixture&&) = delete;
    void verify(unsigned bank, std::uint8_t canary) {
        auto expected = std::vector<std::uint8_t>(bytes.size(), canary);
        for (std::size_t row = 0; row < rows; ++row) {
            const auto base = origin + row * stride;
            std::fill_n(expected.begin() + base, unit, 0);
            for (unsigned rank = 0; rank < code_count; ++rank)
                for (unsigned bit = 0; bit < 3; ++bit)
                    expected[base + rank % 64] |=
                        ((inputs[bank][row][rank] >> bit) & 1) << ((rank < 64 ? 1 : 5) + bit);
        }
        if (bytes != expected || effects.used != rows)
            std::abort();
        for (std::size_t row = 0; row < rows; ++row) {
            const auto& e = effects.entries()[row];
            if (e.source != &view || plane(e) != 0 || offset(e) != origin + row * stride || size(e) != unit)
                std::abort();
        }
    }
};

template <bool Checked> void initialize(benchmark::State& state) {
    fixture data(unsigned(state.range(0)));
    for (unsigned bank = 0; bank < 2; ++bank) {
        const std::uint8_t canary = bank ? 0x5c : 0xa7;
        std::fill(data.bytes.begin(), data.bytes.end(), canary);
        data.effects.used = 0;
        if constexpr (Checked) {
            if (!data.writer.initialize(0, std::span<const input>(data.inputs[bank]), data.effects))
                std::abort();
        } else
            data.writer.initialize_unchecked(0, std::span<const input>(data.inputs[bank]), data.effects);
        data.verify(bank, canary);
    }
    benchmark::DoNotOptimize(data.bytes.data());
    unsigned bank = 0;
    for (auto _ : state) {
        bank ^= 1;
        data.effects.used = 0;
        if constexpr (Checked) {
            auto result = data.writer.initialize(0, std::span<const input>(data.inputs[bank]), data.effects);
            benchmark::DoNotOptimize(result);
        } else
            data.writer.initialize_unchecked(0, std::span<const input>(data.inputs[bank]), data.effects);
        benchmark::DoNotOptimize(data.effects.used);
        benchmark::ClobberMemory();
    }
    data.verify(bank, 0x5c);
    state.SetItemsProcessed(state.iterations() * rows);
    state.counters["items_per_iteration"] = rows;
    state.counters["physical_unit_bytes"] = unit;
    state.counters["stride_bytes"] = stride;
}
BENCHMARK_TEMPLATE(initialize, true)->Arg(64)->Arg(65)->Arg(128);
BENCHMARK_TEMPLATE(initialize, false)->Arg(64)->Arg(65)->Arg(128);
} // namespace
BENCHMARK_MAIN();
