#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <ikea/tuplepack/author/batch.h>
#include <benchmark/benchmark.h>
#include <cassert>
#include <string>
#include <vector>

using namespace ikea::tuplepack;
namespace {
// Same-wire explicit control for the four-code fixture; mask/shift controls are
// runtime, as in the point-layout experiment. It has the same width admission
// and spare-bit preservation, but no range/effect shell. Comparison labels make
// that boundary difference explicit rather than calling it a full-operation win.
struct control {
    std::array<byte, 4> shift{}, mask{}, offset{};
    std::uint64_t invalid = 0;
};
template <unsigned N>
[[gnu::noinline]] std::uint64_t control_read(const control& p, const byte* row) {
    std::uint64_t out = 0;
    for (unsigned i = 0; i < N; ++i)
        out |= std::uint64_t((row[p.offset[i]] >> p.shift[i]) & p.mask[i]) << (8 * i);
    return out;
}
template <unsigned N>
[[gnu::noinline]] void control_write(const control& p, byte* row, std::uint64_t input) {
    if (input & p.invalid)
        return;
    if constexpr (N == 1) {
        row[p.offset[0]] =
            (row[p.offset[0]] & byte(~(p.mask[0] << p.shift[0]))) | (byte(input) << p.shift[0]);
    } else {
        for (unsigned b = 0; b < 2; ++b) {
            byte mask = 0, value = 0;
            for (unsigned i = 0; i < N; ++i)
                if (p.offset[i] == b) {
                    mask |= p.mask[i] << p.shift[i];
                    value |= byte(input >> (8 * i)) << p.shift[i];
                }
            if (mask)
                row[b] = (row[b] & byte(~mask)) | value;
        }
    }
}
template <unsigned N> void point(benchmark::State& state, bool writing, unsigned boundary) {
    const std::array<code, 4> codes{{{0, 0, 1}, {0, 1, 7}, {1, 0, 3}, {1, 3, 5}}};
    const std::array<byte, 4> map{0, 2, 1, 3};
    auto format = *layout::make(2, codes);
    auto r = *reader<8>::make(format, std::span(map).first(N));
    auto w = *writer<8>::make(format, std::span(map).first(N));
    std::vector<byte> bytes(1024 * 16, 0x5a);
    auto v = *view::bind(format, bytes, 1024, 16);
    auto read = *bind_reader(r, v);
    auto write = *bind_writer(w, v);
    std::array<ikea::owner_write, 2> events;
    ikea::source_write_journal effects{events};
    control p;
    for (unsigned i = 0; i < N; ++i) {
        auto c = codes[map[i]];
        p.shift[i] = c.shift;
        p.mask[i] = (1u << c.width) - 1;
        p.offset[i] = c.offset;
        p.invalid |= std::uint64_t(byte(~p.mask[i])) << (8 * i);
    }
    std::array<unsigned, 8192> trace;
    std::uint64_t random = 317;
    for (auto& row : trace) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        row = random % 1024;
    }
    auto cr = control_read<N>;
    auto cw = control_write<N>;
    benchmark::DoNotOptimize(cr);
    benchmark::DoNotOptimize(cw);
    for (auto _ : state) {
        std::uint64_t result = 0;
        for (unsigned i = 0; i < trace.size(); ++i) {
            const auto row = trace[i];
            const std::uint64_t value = (i & 1 ? 0x01030201ULL : 0x02040300ULL) & ~p.invalid;
            if (writing) {
                if (boundary == 0)
                    cw(p, bytes.data() + row * 16, value);
                else if (boundary == 1) {
                    if (w.accepts(value))
                        w.set_unchecked(bytes.data() + row * 16, value);
                } else if (boundary == 3) {
                    effects.used = 0;
                    // Same range/domain/capacity and issued-source journal
                    // obligations as the ordinary command, around the control.
                    bool valid = row < v.size() && !(value & p.invalid) && effects.remaining() >= 1;
                    if (valid) {
                        effects.before(v, ikea::byte_write{0, row * 16, N == 1 ? 1u : 2u});
                        cw(p, bytes.data() + row * 16, value);
                    }
                    benchmark::DoNotOptimize(valid);
                } else {
                    effects.used = 0;
                    auto ok = write.set(row, value, effects);
                    benchmark::DoNotOptimize(ok);
                }
            } else {
                if (boundary == 0)
                    result += cr(p, bytes.data() + row * 16);
                else if (boundary == 1)
                    result += r.get_unchecked(bytes.data() + row * 16);
                else
                    result += read.get_unchecked(row);
            }
        }
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * trace.size());
    state.counters["items_per_iteration"] = trace.size();
}
void wide(benchmark::State& state, bool writing, unsigned boundary) {
    std::array<code, 64> codes;
    std::array<byte, 64> map, input;
    for (unsigned i = 0; i < 64; ++i) {
        codes[i] = {byte(i), byte(i % 2), 7};
        map[i] = (5 * i + 3) % 64;
        input[i] = i;
    }
    auto format = *layout::make(64, codes);
    auto r = *reader<64>::make(format, map);
    auto w = *writer<64>::make(format, map);
    std::vector<byte> bytes(1024 * 64, 0x5a);
    auto v = *view::bind(format, bytes, 1024, 64);
    auto read = *bind_reader(r, v);
    auto write = *bind_writer(w, v);
    std::array<ikea::owner_write, 1> events;
    ikea::source_write_journal effects{events};
#if defined(__aarch64__) || defined(__AVX2__)
    auto value = native::load_packet(input.data());
#endif
    for (auto _ : state) {
        for (unsigned row = 0; row < 1024; ++row) {
            auto* data = bytes.data() + row * 64;
            if (writing) {
#if defined(__aarch64__) || defined(__AVX2__)
                if (boundary == 0)
                    native::write(w.controls(), data, value);
                else
#endif
                    if (boundary == 1)
                    w.set_unchecked(data, input);
                else {
                    effects.used = 0;
                    auto ok = write.set(row, input, effects);
                    benchmark::DoNotOptimize(ok);
                }
            } else {
#if defined(__aarch64__) || defined(__AVX2__)
                if (boundary == 0) {
                    auto out = native::read(r.controls(), data);
                    benchmark::DoNotOptimize(out);
                } else
#endif
                    if (boundary == 1) {
                    auto out = r.get_unchecked(data);
                    benchmark::DoNotOptimize(out);
                } else {
                    auto out = read.get_unchecked(row);
                    benchmark::DoNotOptimize(out);
                }
            }
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 1024);
    state.counters["items_per_iteration"] = 1024;
}
#if defined(__aarch64__) || defined(__AVX2__)
std::uint64_t sum(native::packet values) {
    using namespace native::native_detail;
    auto part = [](native::vector16 v) {
#if defined(__aarch64__)
        return std::uint64_t(vaddlvq_u8(v));
#else
        const auto s = _mm_sad_epu8(v, _mm_setzero_si128());
        return std::uint64_t(_mm_cvtsi128_si64(s)) + std::uint64_t(_mm_extract_epi64(s, 1));
#endif
    };
    return part(split<0>(values)) + part(split<1>(values)) + part(split<2>(values)) +
           part(split<3>(values));
}
void scan(benchmark::State& state, unsigned stride) {
    std::array<code, 16> codes;
    std::array<byte, 16> map;
    for (unsigned i = 0; i < 16; ++i) {
        const auto width = 1 + i % 7;
        codes[i] = {byte(i), byte(i % (9 - width)), byte(width)};
        map[i] = (5 * i + 3) % 16;
    }
    auto format = *layout::make(16, codes);
    auto plan = *batch_reader<4>::make(format, map);
    const std::size_t rows = state.range(0);
    std::vector<byte> data(rows * stride, 0x5a);
    auto source = *const_view::bind(format, data, rows, stride);
    for (auto _ : state) {
        std::uint64_t result = 0;
        for (std::size_t row = 0; row < rows; row += 4)
            result += sum(plan.read_unchecked(source, row));
        benchmark::DoNotOptimize(result);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * rows);
    state.counters["items_per_iteration"] = rows;
}
using pipeline = chain<8>;
struct pipeline_context {
    const reader<64>& reader;
    const writer<64>& writer;
    byte* data;
    std::uint64_t checksum = 0;
};
pipeline::result decode_stage(void* opaque, std::size_t row, std::uint64_t active,
                              native::pipeline_values, unsigned) {
    auto& c = *static_cast<pipeline_context*>(opaque);
    return {
        native::pipeline_values::from(native::read_body(c.reader.controls(), c.data + row * 64)),
        active};
}
pipeline::result update_stage(void* opaque, std::size_t row, std::uint64_t active,
                              native::pipeline_values values, unsigned) {
    auto& c = *static_cast<pipeline_context*>(opaque);
    // Real decode/repack with a caller-owned reduction, rather than the tiny
    // mask-only stage stress case. No concurrent publication or effects timed.
    auto value = values.get();
    c.checksum += sum(value);
    native::write_body(c.writer.controls(), c.data + row * 64, value);
    return {values, active};
}
void complete_stage(void*, std::size_t, std::uint64_t, native::pipeline_values) {}
void pipeline_bench(benchmark::State& state, bool cps) {
    std::array<code, 64> codes;
    std::array<byte, 64> map;
    for (unsigned i = 0; i < 64; ++i) {
        codes[i] = {byte(i), byte(i % 2), 7};
        map[i] = (5 * i + 3) % 64;
    }
    auto format = *layout::make(64, codes);
    auto r = *reader<64>::make(format, map);
    auto w = *writer<64>::make(format, map);
    std::vector<byte> data(1024 * 64, 0x5a);
    pipeline_context c{r, w, data.data()};
    std::array<pipeline::function, 2> stages{pipeline::stage<decode_stage>,
                                             pipeline::stage<update_stage>};
    auto plan = *pipeline::prepare(stages, pipeline::completion<complete_stage>);
    std::array<byte, 64> zeros{};
    auto zero = native::pipeline_values::from(native::load_packet(zeros.data()));
    for (auto _ : state) {
        c.checksum = 0;
        for (unsigned row = 0; row < 1024; ++row) {
            if (cps)
                plan.run(&c, row, 1, zero);
            else {
                auto decoded = decode_stage(&c, row, 1, zero, 0);
                update_stage(&c, row, 1, decoded.values, 1);
            }
        }
        benchmark::DoNotOptimize(c.checksum);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 1024);
    state.counters["items_per_iteration"] = 1024;
}
#endif
} // namespace
void register_tuple_composition();
int main(int argc, char** argv) {
    register_tuple_composition();
    for (bool write : {false, true})
        for (unsigned boundary = 0; boundary < 3; ++boundary) {
            const std::string suffix =
                std::string(write ? "/write/" : "/read/") + (boundary == 0   ? "control"
                                                             : boundary == 1 ? "body"
                                                                             : "ordinary");
            benchmark::RegisterBenchmark(("point/1" + suffix).c_str(),
                                         [=](auto& s) { point<1>(s, write, boundary); });
            benchmark::RegisterBenchmark(("point/2" + suffix).c_str(),
                                         [=](auto& s) { point<2>(s, write, boundary); });
            benchmark::RegisterBenchmark(("point/4" + suffix).c_str(),
                                         [=](auto& s) { point<4>(s, write, boundary); });
            benchmark::RegisterBenchmark(("wide/64" + suffix).c_str(),
                                         [=](auto& s) { wide(s, write, boundary); });
        }
    benchmark::RegisterBenchmark("point/1/write/control_effects",
                                 [](auto& s) { point<1>(s, true, 3); });
    benchmark::RegisterBenchmark("point/2/write/control_effects",
                                 [](auto& s) { point<2>(s, true, 3); });
    benchmark::RegisterBenchmark("point/4/write/control_effects",
                                 [](auto& s) { point<4>(s, true, 3); });
#if defined(__aarch64__) || defined(__AVX2__)
    for (unsigned stride : {16, 64})
        benchmark::RegisterBenchmark(("scan/packet4/" + std::to_string(stride)).c_str(),
                                     [=](auto& s) { scan(s, stride); })
            ->Arg(1024)
            ->Arg(65536)
            ->Arg(1048576);
    benchmark::RegisterBenchmark("pipeline/decode_repack/inline",
                                 [](auto& s) { pipeline_bench(s, false); });
    benchmark::RegisterBenchmark("pipeline/decode_repack/cps",
                                 [](auto& s) { pipeline_bench(s, true); });
#endif
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;
    benchmark::RunSpecifiedBenchmarks();
}
