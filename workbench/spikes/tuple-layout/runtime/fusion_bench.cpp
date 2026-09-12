#include "fusion.h"
#include "byte_route.h"
#include <benchmark/benchmark.h>
#include <string>

namespace tuple_composition_probe {
namespace {
constexpr unsigned batch = 256;
std::uint64_t mix(std::uint64_t v) {
    v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ULL;
    v ^= v >> 27; v *= 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}
[[gnu::always_inline]] bool invalid(native_packet v) {
#if defined(__aarch64__)
    return vmaxvq_u8(vorrq_u8(vorrq_u8(v.a, v.b), vorrq_u8(v.c, v.d))) != 0;
#elif defined(__AVX512VBMI__)
    return _mm512_test_epi8_mask(v, v) != 0;
#else
    const auto x = _mm256_or_si256(v.a, v.b);
    return !_mm256_testz_si256(x, x);
#endif
}
struct issued_span { std::uint64_t region, offset, bytes; };
void invoke(benchmark::State& state, execution e, bool reordered, bool partial, bool planes, bool effects) {
    const unsigned rows = state.range(0), bindings = state.range(1);
    std::vector<recipe> plans;
    for (unsigned i = 0; i < bindings; ++i) plans.push_back(prepare(reordered, partial));
    operation call = select(e, reordered, partial);
    asm volatile("" : "+r"(call));
    std::vector<byte> first(std::size_t(rows) * (planes ? 32 : 64));
    std::vector<byte> second(planes ? std::size_t(rows) * 32 : 0);
    auto view = [&](unsigned row) -> raw_view {
        if (planes) return {first.data() + row * 32, second.data() + row * 32};
        return {first.data() + row * 64, first.data() + row * 64 + 32};
    };
    std::vector<std::uint64_t> summaries(rows);
    bytes64 values;
    for (unsigned row = 0; row < rows; ++row) {
        for (unsigned i = 0; i < 64; ++i) values[i] = mix(row * 64 + i + 7);
        summaries[row] = sum(values);
        bytes64 physical{};
        auto input = split(values);
        for (unsigned p = 0; p < 2; ++p)
            reference_write({64, plans[0].codes}, plans[0].maps[p], physical.data(), input[p]);
        store(view(row), load_packet(physical.data()));
    }
    std::array<packets, 2> input;
    for (unsigned n = 0; n < 2; ++n) {
        for (unsigned i = 0; i < 64; ++i) values[i] = mix(n * 64 + i + 178);
        input[n] = split(values);
    }
    std::array<unsigned, 8192> trace;
    for (unsigned i = 0; i < trace.size(); ++i) trace[i] = mix(i + 873) & (rows - 1);
    std::array<issued_span, batch * 2> journal;
    unsigned cursor = 0, phase = 0;
    for (auto _ : state) {
        std::uint64_t checksum = 0;
        const auto low = load_packet(input[phase].at(0).data());
        const auto high = load_packet(input[phase].at(1).data());
        for (unsigned i = 0; i < batch; ++i) {
            const auto row = trace[(cursor + i) & 8191];
            const auto& p = plans[(cursor + i) & (bindings - 1)];
            if (effects) {
                // This bound batch already owns both leases and journal capacity.
                // Per-value admission still precedes its no-fail before-write
                // hook. No publication, allocator, COW fault or scheduler timed.
                if (invalid(either(both(low, load_packet(p.write[0].invalid_bits.data())),
                                   both(high, load_packet(p.write[1].invalid_bits.data()))))) {
                    state.SkipWithError("admission rejected valid input"); return;
                }
                journal[2 * i] = {1, std::uint64_t(row) * (planes ? 32 : 64), 32};
                journal[2 * i + 1] = {planes ? 2u : 1u, std::uint64_t(row) * (planes ? 32 : 64) + (planes ? 0u : 32u), 32};
            }
            const auto delta = call(p, view(row), low, high);
            if (effects) summaries[row] += delta;
            checksum += delta ^ row;
        }
        asm volatile("" : "+r"(checksum));
        if (effects) benchmark::DoNotOptimize(journal);
        benchmark::ClobberMemory();
        cursor = (cursor + batch) & 8191; phase ^= 1;
    }
    if (effects) for (unsigned row = 0; row < rows; ++row) {
        bytes64 physical; store_packet(physical.data(), load(view(row)));
        packets out;
        for (unsigned p = 0; p < 2; ++p) out[p] = reference_read({64, plans[0].codes}, plans[0].maps[p], physical.data());
        if (sum(assemble(out)) != summaries[row]) { state.SkipWithError("post-mutation summary mismatch"); return; }
    }
    state.SetItemsProcessed(state.iterations() * batch);
    state.counters["items_per_iteration"] = batch;
    state.counters["live_bindings"] = bindings;
    state.counters["allocated_binding_bytes"] = sizeof(recipe) + plans[0].decoded.size() * sizeof(route_term) + sizeof(byte_route);
    state.counters["lowered_decode_terms"] = plans[0].decoded.size();
    state.counters["issued_write_bytes"] = 64;
    state.counters["input_bytes"] = 128;
}
} // namespace
void register_fusion_benchmarks() {
    for (auto e : {execution::separate, execution::generic, execution::constants, execution::algebraic, execution::normalized})
    for (bool reordered : {false, true}) for (bool partial : {false, true})
    for (bool planes : {false, true}) for (bool effects : {false, true}) {
        const auto label = std::string("fusion/") + name(e) + (reordered ? "/reordered" : "/ordered") +
            (partial ? "/partial" : "/full") + (planes ? "/planes" : "/contiguous") + (effects ? "/effects" : "/body");
        benchmark::RegisterBenchmark(label.c_str(), [=](benchmark::State& s) { invoke(s, e, reordered, partial, planes, effects); })
            ->Args({1024, 1})->Args({1024, 32})->Args({65536, 1});
    }
    benchmark::RegisterBenchmark("fusion/prepare/reordered", [](benchmark::State& state) {
        for (auto _ : state) { auto p = prepare(true, true); benchmark::DoNotOptimize(p); }
        state.SetItemsProcessed(state.iterations()); state.counters["items_per_iteration"] = 1;
    });
}
} // namespace tuple_composition_probe
