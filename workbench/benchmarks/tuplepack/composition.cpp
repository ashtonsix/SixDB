#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/execution.h>
#include <benchmark/benchmark.h>
#include <cassert>
#include <vector>

using namespace ikea::tuplepack;
using namespace ikea::tuplepack::author;
#if defined(__aarch64__) || defined(__AVX2__)
namespace {
using operation = author::mutation_group<native_writer<>, native_writer<>>;
[[gnu::noinline]] IKEA_TUPLE_CC bool checked_pair(const operation& op, std::size_t row,
                                                  native::packet low, native::packet high,
                                                  ikea::journal& effects) {
    // Register arguments reach the authored command directly. The tuple is a
    // compile-time grouping, not a required materialized payload interface.
    return bool(op.set(row, {low, high}, effects));
}
[[gnu::noinline]] IKEA_TUPLE_CC void body_pair(const write_plan<64>& low, const write_plan<64>& high,
                                               std::uint8_t* row, native::packet a, native::packet b) {
    native::write_body(low.controls(), row, a);
    native::write_body(high.controls(), row, b);
}
void run(benchmark::State& state, bool ordinary) {
    std::array<code, 128> codes;
    std::array<std::uint8_t, 64> lm, hm, lv, hv;
    for (unsigned i = 0; i < 64; ++i) {
        codes[i] = {std::uint8_t(i), 0, 4};
        codes[64 + i] = {std::uint8_t(i), 4, 4};
        lm[i] = (5 * i + 3) % 64;
        hm[i] = 64 + (5 * i + 3) % 64;
        lv[i] = i % 16;
        hv[i] = 15 - i % 16;
    }
    auto format = *layout::make(64, codes);
    auto low = *write_plan<64>::make(format, lm), high = *write_plan<64>::make(format, hm);
    std::vector<std::byte> bytes(1024 * 64, std::byte{0});
    auto source = *view::attach(format, bytes, 1024, 64);
    auto op = *author::bind_group(native_writer(*bind_writer(low, source)),
                                       native_writer(*bind_writer(high, source)));
    std::array<ikea::journal::entry, 2> records;
    ikea::journal effects{records};
    auto a = native::load_packet(lv.data()), b = native::load_packet(hv.data());
    assert(checked_pair(op, 0, a, b, effects));
    for (unsigned i = 0; i < 64; ++i)
        assert(std::to_integer<unsigned>(bytes[lm[i]]) == (lv[i] | (hv[i] << 4)));
    for (auto _ : state) {
        for (unsigned row = 0; row < 1024; ++row) {
            if (ordinary) {
                effects.used = 0;
                auto ok = checked_pair(op, row, a, b, effects);
                benchmark::DoNotOptimize(ok);
            } else
                body_pair(low, high, ikea::to_u8(bytes.data() + row * 64), a, b);
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 1024);
    state.counters["items_per_iteration"] = 1024;
}
} // namespace
#endif
void register_tuple_composition() {
#if defined(__aarch64__) || defined(__AVX2__)
    benchmark::RegisterBenchmark("composition/128/body", [](auto& s) { run(s, false); });
    benchmark::RegisterBenchmark("composition/128/ordinary", [](auto& s) { run(s, true); });
#endif
}
