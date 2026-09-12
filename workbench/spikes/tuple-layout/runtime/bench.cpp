#include "cases.h"
#include "fusion.h"
#include "consumer.h"
#include "describe.h"
#include <benchmark/benchmark.h>
#include <bit>
#include <cstring>

using namespace tuple_runtime;
namespace {
enum class mode { read_inline, read_bound, scalar64, scalar8, weighted_bound,
                  write_scalar, write_native, prepare_read, prepare_write };
constexpr unsigned batch = 256;
std::uint64_t mix(std::uint64_t value) {
    value ^= value >> 30; value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27; value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
void prepare_bench(benchmark::State& state, const case_spec& c, mode kind) {
    for (auto _ : state) {
        if (kind == mode::prepare_read) {
            auto plan = prepare_read(c.physical(), c.map);
            benchmark::DoNotOptimize(plan);
        } else {
            auto plan = prepare_write(c.physical(), c.map);
            benchmark::DoNotOptimize(plan);
        }
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["items_per_iteration"] = 1;
    state.counters["invocations"] = 0;
    state.counters["read_preparations"] = kind == mode::prepare_read ? state.iterations() : 0;
    state.counters["write_preparations"] = kind == mode::prepare_write ? state.iterations() : 0;
    state.counters["prepared_read_bytes"] = sizeof(read_plan);
    state.counters["prepared_write_bytes"] = sizeof(write_plan);
}
template <mode Mode> void invoke_bench(benchmark::State& state, const case_spec& c) {
    const auto r = prepare_read(c.physical(), c.map);
    const auto w = prepare_write(c.physical(), c.map);
    if (!r || !w) { state.SkipWithError("invalid case binding"); return; }
    const unsigned rows = state.range(0);
    std::vector<byte> data(std::size_t(rows) * c.stride);
    std::array<unsigned, 8192> trace;
    for (unsigned i = 0; i < trace.size(); ++i) trace[i] = mix(i + 873) & (rows - 1);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = mix(i + 714);
    std::array<byte, 64> input{};
    for (unsigned i = 0; i < 64; ++i) input[i] = mix(i + 371) & byte(~w->invalid_bits[i]);
    auto native_input = load_packet(input.data());
    native_reader read = read_native;
    auto write = write_native;
    // Preserve an actual erased call without introducing a per-row memory
    // barrier or obscuring the data pointer's alias relationships.
    asm volatile("" : "+r"(read), "+r"(write));
    std::array<byte, 64> output{};
    for (unsigned i = 0; i < 16; ++i) {
        const auto* row = data.data() + std::size_t(trace[i]) * c.stride;
        const auto expected = reference_read(c.physical(), c.map, row);
        auto decoded = read(*r, row);
        store_packet(output.data(), decoded);
        if (output != expected) { state.SkipWithError("read oracle mismatch"); return; }
        std::uint64_t expected_sum = 0;
        for (unsigned j = 0; j < 64; ++j) expected_sum += std::uint64_t(expected[j]) * (j + 1);
        if (weighted(decoded) != expected_sum) { state.SkipWithError("ordered consumer mismatch"); return; }
    }
    unsigned cursor = 0;
    for (auto _ : state) {
        std::uint64_t checksum = 0;
        for (unsigned i = 0; i < batch; ++i) {
            const unsigned row_id = trace[(cursor + i) & (trace.size() - 1)];
            auto* row = data.data() + std::size_t(row_id) * c.stride;
            if constexpr (Mode == mode::read_inline) store_packet(output.data(), read_body(*r, row));
            if constexpr (Mode == mode::read_bound) store_packet(output.data(), read(*r, row));
            if constexpr (Mode == mode::scalar64) output = read_scalar(*r, row);
            if constexpr (Mode == mode::scalar8) checksum += read8(*r, row);
            if constexpr (Mode == mode::weighted_bound) checksum += weighted(read(*r, row));
            if constexpr (Mode == mode::write_scalar || Mode == mode::write_native) {
                std::uint64_t effects = 0;
                if constexpr (Mode == mode::write_scalar) {
                    if (!write_scalar(*w, row, input, effects)) { state.SkipWithError("write rejected admitted input"); return; }
                } else {
                    if (write(*w, row, native_input, effects) != mutation_status::ok) {
                        state.SkipWithError("native write rejected admitted input"); return;
                    }
                }
                // Keep tuple identity alongside tuple-relative coverage in the
                // observer. This checksum is not an owner journal or publication.
                checksum += effects ^ row_id;
            }
            if constexpr (Mode == mode::read_inline || Mode == mode::read_bound || Mode == mode::scalar64)
                benchmark::DoNotOptimize(output);
        }
        cursor = (cursor + batch) & (trace.size() - 1);
        asm volatile("" : "+r"(checksum));
        benchmark::ClobberMemory();
    }
    if constexpr (Mode == mode::write_scalar || Mode == mode::write_native) {
        for (unsigned i = 0; i < batch; ++i) {
            const unsigned row_id = trace[i];
            std::vector<byte> expected(c.stride);
            for (unsigned j = 0; j < c.stride; ++j)
                expected[j] = mix(std::size_t(row_id) * c.stride + j + 714);
            reference_write(c.physical(), c.map, expected.data(), input);
            if (std::memcmp(data.data() + std::size_t(row_id) * c.stride, expected.data(), c.stride)) {
                state.SkipWithError("post-write bytes/padding mismatch"); return;
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * batch);
    state.counters["items_per_iteration"] = batch;
    state.counters["invocations"] = state.iterations() * batch;
    state.counters["read_preparations"] = 1;
    state.counters["write_preparations"] = 1;
    state.counters["prepared_read_bytes"] = sizeof(read_plan);
    state.counters["prepared_write_bytes"] = sizeof(write_plan);
    state.counters["tuple_bytes"] = c.bytes;
    state.counters["stride"] = c.stride;
    state.counters["write_bytes"] = std::popcount(w->issued_writes);
    state.counters["native_read_bytes"] = std::popcount(r->issued_reads);
}
template <mode Mode> void add(const case_spec& c, const char* name) {
    benchmark::RegisterBenchmark(("runtime/" + c.name + "/" + name).c_str(),
        [&c](benchmark::State& state) { invoke_bench<Mode>(state, c); })
        ->Arg(1024)->Arg(65536);
}
} // namespace

void check_tuple_scan();
void register_tuple_scan();
void register_tuple_point();
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--check-extra") { check_tuple_scan(); register_tuple_point(); return 0; }
    const auto all = cases();
    if (argc == 3 && std::string(argv[1]) == "--describe") { describe(all, argv[2]); return 0; }
    for (const auto& c : all) {
        add<mode::read_inline>(c, "read_inline");
        add<mode::read_bound>(c, "read_bound");
        add<mode::weighted_bound>(c, "weighted_bound");
        add<mode::scalar64>(c, "scalar64");
        add<mode::scalar8>(c, "scalar8_projection");
        add<mode::write_scalar>(c, "write_scalar");
        if (prepare_write(c.physical(), c.map)->dense_native) add<mode::write_native>(c, "write_native");
        benchmark::RegisterBenchmark(("runtime/" + c.name + "/prepare_read").c_str(),
            [&c](benchmark::State& state) { prepare_bench(state, c, mode::prepare_read); });
        benchmark::RegisterBenchmark(("runtime/" + c.name + "/prepare_write").c_str(),
            [&c](benchmark::State& state) { prepare_bench(state, c, mode::prepare_write); });
    }
    check_tuple_scan();
    register_tuple_scan();
    register_tuple_point();
    tuple_composition_probe::register_fusion_benchmarks();
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
