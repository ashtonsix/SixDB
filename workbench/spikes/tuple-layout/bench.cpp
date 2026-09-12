#include "edge.h"
#include <benchmark/benchmark.h>
#include <cstdio>
#include <string>
#include <vector>

using namespace tuple_probe;
namespace {
enum class path { direct, bound, unpacked };
enum class operation { read_sum, materialize, replace };
constexpr std::size_t batch = 256;

std::uint64_t mix(std::uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

template <unsigned Selected>
[[gnu::always_inline]] packet unpacked_read(const byte* row) {
    auto zero = vdupq_n_u8(0);
    packet result{zero, zero, zero, zero};
    if constexpr (Selected & 1) result.a = vld1q_u8(row);
    if constexpr (Selected & 2) result.b = vld1q_u8(row + 16);
    if constexpr (Selected & 4) result.c = vld1q_u8(row + 32);
    return result;
}
template <unsigned Selected>
[[gnu::always_inline]] void unpacked_write(byte* row, packet input) {
    if constexpr (Selected & 1) vst1q_u8(row, input.a);
    if constexpr (Selected & 2) vst1q_u8(row + 16, input.b);
    if constexpr (Selected & 4) vst1q_u8(row + 32, input.c);
}

template <unsigned Layout, unsigned Selected, path Path, operation Op>
void run(benchmark::State& state) {
    const std::size_t rows = state.range(0);
    constexpr unsigned stride = Path == path::unpacked ? 48 : 16;
    std::vector<byte> data(rows * stride);
    std::vector<std::size_t> trace(8192);
    for (std::size_t i = 0; i < trace.size(); ++i)
        trace[i] = mix(i + 873) & (rows - 1);
    for (std::size_t row = 0; row < rows; ++row) {
        for (unsigned lane = 0; lane < 16; ++lane) {
            const auto value = mix(row * 16 + lane);
            std::array<byte, 3> codes{byte(value & 1), byte((value >> 1) & 15), byte((value >> 5) & 7)};
            if constexpr (Path == path::unpacked) {
                for (unsigned kind = 0; kind < 3; ++kind)
                    data[row * stride + kind * 16 + lane] = codes[kind];
            } else {
                for (unsigned kind = 0; kind < 3; ++kind)
                    data[row * stride + lane] |= codes[kind] << layouts[Layout].shift[kind];
            }
        }
    }
    std::array<byte, 64> replacement{};
    for (unsigned i = 0; i < 48; ++i)
        replacement[i] = mix(i + 371) & ((1u << widths[i / 16]) - 1);
    const auto value = load_packet(replacement.data());
    const auto endpoint = bind(Layout, Selected);
    alignas(64) std::array<byte, 64> output;
    std::size_t cursor = 0;
    for (auto _ : state) {
        std::uint64_t checksum = 0;
        for (std::size_t i = 0; i < batch; ++i) {
            auto* row = data.data() + trace[(cursor + i) & (trace.size() - 1)] * stride;
            if constexpr (Op == operation::replace) {
                if constexpr (Path == path::direct) write_body<Layout, Selected>(row, value);
                if constexpr (Path == path::bound) endpoint.write(row, value);
                if constexpr (Path == path::unpacked) unpacked_write<Selected>(row, value);
            } else {
                packet decoded;
                if constexpr (Path == path::direct) decoded = read_body<Layout, Selected>(row);
                if constexpr (Path == path::bound) decoded = endpoint.read(row);
                if constexpr (Path == path::unpacked) decoded = unpacked_read<Selected>(row);
                if constexpr (Op == operation::read_sum) {
                    checksum += sum(decoded);
                } else {
                    vst1q_u8(output.data(), decoded.a);
                    vst1q_u8(output.data() + 16, decoded.b);
                    vst1q_u8(output.data() + 32, decoded.c);
                    vst1q_u8(output.data() + 48, decoded.d);
                    benchmark::DoNotOptimize(output);
                }
            }
        }
        cursor = (cursor + batch) & (trace.size() - 1);
        // A memory-capable per-row barrier spilled the row pointer beside the
        // checksum and induced a store-forwarding hazard in some instantiations.
        // Keep this sink in a register. The data loads feed it and the memory
        // clobber below retains replacement stores without obscuring row aliases.
        asm volatile("" : "+r"(checksum));
        benchmark::ClobberMemory();
    }
    benchmark::DoNotOptimize(data.data());
    state.SetItemsProcessed(state.iterations() * batch);
    state.counters["seconds_per_tuple"] = benchmark::Counter(
        double(state.iterations()) * batch, benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
    state.counters["tuple_bytes"] = stride;
    state.counters["selected_codes"] = 16 * __builtin_popcount(Selected);
}

template <unsigned L, unsigned S, path P> void add_path(const std::string& prefix) {
    auto add = [&](const char* name, auto function) {
        benchmark::RegisterBenchmark((prefix + name).c_str(), function)
            ->Arg(1024)->Arg(65536)->MinTime(0.015)->Repetitions(5);
    };
    add("/read_sum", run<L, S, P, operation::read_sum>);
    add("/materialize", run<L, S, P, operation::materialize>);
    add("/replace", run<L, S, P, operation::replace>);
}
template <unsigned L, unsigned S> void add_selection() {
    auto prefix = std::string("edge/") + names[L] + "/select" + std::to_string(S);
    add_path<L, S, path::direct>(prefix + "/direct");
    add_path<L, S, path::bound>(prefix + "/bound");
    if constexpr (L == 0)
        add_path<L, S, path::unpacked>("edge/control/select" + std::to_string(S) + "/unpacked");
}
template <unsigned L> void add_layout() {
    add_selection<L, 1>(); add_selection<L, 2>(); add_selection<L, 3>();
    add_selection<L, 4>(); add_selection<L, 5>(); add_selection<L, 6>();
    add_selection<L, 7>();
}
} // namespace

int main(int argc, char** argv) {
    add_layout<0>(); add_layout<1>(); add_layout<2>();
    add_layout<3>(); add_layout<4>(); add_layout<5>();
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
