#pragma once
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/execution.h>
#include <benchmark/benchmark.h>
#include <cstdlib>
#include <vector>

using namespace ikea::tuplepack;
#if defined(__aarch64__) || defined(__AVX2__)
namespace {
[[gnu::always_inline]] inline native::packet toggle(native::packet v) {
#if defined(__aarch64__)
    auto one = vdupq_n_u8(1);
    return {veorq_u8(vandq_u8(v.a, one), one), veorq_u8(vandq_u8(v.b, one), one),
            veorq_u8(vandq_u8(v.c, one), one), veorq_u8(vandq_u8(v.d, one), one)};
#elif defined(__AVX512VBMI__)
    auto one = _mm512_set1_epi8(1);
    return _mm512_xor_si512(_mm512_and_si512(v, one), one);
#else
    auto one = _mm256_set1_epi8(1);
    return {_mm256_xor_si256(_mm256_and_si256(v.a, one), one),
            _mm256_xor_si256(_mm256_and_si256(v.b, one), one)};
#endif
}
std::uint64_t reduce(native::packet v) {
    using namespace native::native_detail;
    auto part = [](native::vector16 p) {
#if defined(__aarch64__)
        return std::uint64_t(vaddlvq_u8(p));
#else
        auto sums = _mm_sad_epu8(p, _mm_setzero_si128());
        return std::uint64_t(_mm_cvtsi128_si64(sums)) + std::uint64_t(_mm_extract_epi64(sums, 1));
#endif
    };
    return part(split<0>(v)) + part(split<1>(v)) + part(split<2>(v)) + part(split<3>(v));
}
using pipeline = chain<8>;
template <unsigned Rows> struct operation {
    native_reader<byte, Rows> read;
    native_writer<Rows> write;
    ikea::source_write_journal& effects;
    bool okay = true;
};
template <unsigned Rows>
[[gnu::always_inline]] inline pipeline::result
decode(void* context, std::size_t row, std::uint64_t active, native::pipeline_values, unsigned) {
    auto& c = *static_cast<operation<Rows>*>(context);
    return {native::pipeline_values::from(c.read.get_unchecked(row, active)), active, !active};
}
template <unsigned Rows>
[[gnu::always_inline]] inline pipeline::result update(void* context, std::size_t row,
                                                      std::uint64_t active,
                                                      native::pipeline_values values, unsigned) {
    auto& c = *static_cast<operation<Rows>*>(context);
    c.okay &= bool(c.write.set(row, toggle(values.get()), c.effects, active));
    return {values, active, !c.okay};
}
void complete(void*, std::size_t, std::uint64_t, native::pipeline_values) {}

template <unsigned Rows>
void run(benchmark::State& state, unsigned layout_kind, bool sparse, unsigned mode, bool mutate) {
    constexpr unsigned B = 64 / Rows;
    const unsigned extent = layout_kind >= 2 ? 1 : 64;
    std::vector<code> codes(layout_kind >= 2 ? 8 : 64);
    for (unsigned i = 0; i < codes.size(); ++i)
        codes[i] = layout_kind >= 2 ? code{0, byte(i), 1} : code{byte(i), byte(i % 2), 7};
    auto format = *layout::make(extent, codes);
    std::array<byte, B> map;
    map.fill(hole);
    for (unsigned i = 0; i < std::min(B, unsigned(codes.size())); ++i)
        map[i] = layout_kind == 1 ? (i * 13 + 17) % 64
                                  : (i * 5 + 3) % std::min(B, unsigned(codes.size()));
    auto rp = *reader<64, Rows>::make(format, map);
    auto wp = *writer<64, Rows>::make(format, map);
    const std::size_t count = 1024, stride = layout_kind == 2 ? 1 : 64;
    std::vector<byte> bytes((count - 1) * stride + extent, 0x55);
    auto source = *view::bind(format, bytes, count, stride);
    auto read = *bind_reader(rp, source);
    auto write = *bind_writer(wp, source);
    std::array<ikea::owner_write, 4096> entries;
    ikea::source_write_journal effects{entries};
    operation<Rows> c{native_reader(read), native_writer(write), effects};
    std::array<pipeline::function, 2> stages{pipeline::stage<decode<Rows>>,
                                             pipeline::stage<update<Rows>>};
    auto chain = *pipeline::prepare(stages, pipeline::completion<complete>);
    const auto empty = native::pipeline_values::from(native::row_mask<Rows>(0));
    const std::uint64_t active = detail::all_rows<Rows> & (sparse ? 0x5555555555555555ull : ~0ull);
    for (auto _ : state) {
        std::uint64_t result = 0;
        for (std::size_t first = 0; first < count; first += Rows) {
            effects.used = 0;
            if (!mutate) {
                auto value = c.read.get_unchecked(first, active);
                result += reduce(value);
            } else if (mode == 2)
                chain.run(&c, first, active, empty);
            else if (mode == 1) {
                const auto decoded = decode<Rows>(&c, first, active, empty, 0);
                update<Rows>(&c, first, active, decoded.values, 1);
            } else {
                auto value = c.read.get_unchecked(first, active);
                auto address = [&](unsigned r) { return source.row_unchecked(first + r); };
                if constexpr (Rows == 1)
                    native::write_body(wp.controls(), address(0), toggle(value));
                else
                    native::write_body<Rows>(wp.controls(), address, toggle(value), active, stride);
            }
        }
        benchmark::DoNotOptimize(result);
        benchmark::DoNotOptimize(c.okay);
        benchmark::ClobberMemory();
    }
    if (!c.okay)
        state.SkipWithError("checked packet mutation failed");
    state.SetItemsProcessed(state.iterations() * count);
    state.counters["items_per_iteration"] = count;
}
template <unsigned Rows> void cases(bool broad) {
    constexpr const char* layouts[] = {"compact", "spread", "one_byte", "one_byte_stride64"};
    for (unsigned layout = 0; layout < 4; ++layout) {
        if (!broad && !((Rows == 64 && layout == 2) || (Rows == 8 && layout == 2) ||
                        (Rows == 4 && layout == 1)))
            continue;
        for (bool sparse : {false, true}) {
            const auto prefix = "packets/" + std::to_string(Rows) + "/" + layouts[layout] +
                                (sparse ? "/half" : "/all");
            benchmark::RegisterBenchmark((prefix + "/sum").c_str(),
                                         [=](auto& s) { run<Rows>(s, layout, sparse, 0, false); });
            for (unsigned mode = 0; mode < 3; ++mode) {
                constexpr const char* names[] = {"body", "ordinary", "cps"};
                benchmark::RegisterBenchmark(
                    (prefix + "/update/" + names[mode]).c_str(),
                    [=](auto& s) { run<Rows>(s, layout, sparse, mode, true); });
            }
        }
    }
}
} // namespace
#endif
