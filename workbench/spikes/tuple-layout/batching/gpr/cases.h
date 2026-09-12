#pragma once
#include "probe.h"
#include <benchmark/benchmark.h>
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
namespace tp = ikea::tuplepack;
using gpr_probe::word;
constexpr std::size_t count = 1024;
word sum(word value) {
    value = (value & 0x00ff00ff00ff00ffull) + ((value >> 8) & 0x00ff00ff00ff00ffull);
    value = (value & 0x0000ffff0000ffffull) + ((value >> 16) & 0x0000ffff0000ffffull);
    return std::uint32_t(value) + (value >> 32);
}
word hash(word value) { return value ^ std::rotl(value, 27); }
word toggle(word value) { return (value & 0x0101010101010101ull) ^ 0x0101010101010101ull; }
#if defined(__aarch64__) || defined(__AVX2__)
[[gnu::always_inline]] inline tp::native::packet toggle(tp::native::packet value) {
#if defined(__aarch64__)
    const auto one = vdupq_n_u8(1);
    return {veorq_u8(vandq_u8(value.a, one), one), veorq_u8(vandq_u8(value.b, one), one),
            veorq_u8(vandq_u8(value.c, one), one), veorq_u8(vandq_u8(value.d, one), one)};
#elif defined(__AVX512VBMI__)
    const auto one = _mm512_set1_epi8(1);
    return _mm512_xor_si512(_mm512_and_si512(value, one), one);
#else
    const auto one = _mm256_set1_epi8(1);
    return {_mm256_xor_si256(_mm256_and_si256(value.a, one), one),
            _mm256_xor_si256(_mm256_and_si256(value.b, one), one)};
#endif
}
word sum(tp::native::packet value) {
    auto part = [](tp::native::vector16 v) {
#if defined(__aarch64__)
        return word(vaddlvq_u8(v));
#else
        const auto x = _mm_sad_epu8(v, _mm_setzero_si128());
        return word(_mm_cvtsi128_si64(x)) + word(_mm_extract_epi64(x, 1));
#endif
    };
    using tp::native::native_detail::split;
    return part(split<0>(value)) + part(split<1>(value)) + part(split<2>(value)) + part(split<3>(value));
}
template <unsigned Rows, std::size_t... R>
word compact(tp::native::packet value, std::index_sequence<R...>) {
    constexpr unsigned B = 8 / Rows;
    constexpr word mask = ~word(0) >> (64 - 8 * B);
    return (((tp::native::word<R * B>(value) & mask) << (R * B * 8)) | ...);
}
template <std::size_t... I>
word hash_full(tp::native::packet value, std::index_sequence<I...>) {
    return (hash(tp::native::word<I>(value)) + ...);
}

// Point: repeated prepared scalar calls. GPR: provisional shared body, either
// inlined or behind one compiled packet endpoint. SIMD: current native operation
// with the same row count. Full SIMD: eight times as many rows, for scans only.
enum class method { point, gpr, compiled, simd, full_simd, gpr_points };
template <unsigned Rows, method Method>
void run(benchmark::State& state, unsigned extent, unsigned stride, unsigned pattern,
         bool sparse, bool random, unsigned consumer) {
    constexpr unsigned B = 8 / Rows;
    constexpr unsigned Step = Method == method::full_simd ? Rows * 8 : Rows;
    std::vector<tp::code> codes(extent == 1 ? 8 : extent);
    for (unsigned i = 0; i < codes.size(); ++i)
        codes[i] = extent == 1 ? tp::code{0, tp::byte(i), 1}
                              : tp::code{tp::byte(i), tp::byte(pattern ? i % 2 : 0), 7};
    const auto format = *tp::layout::make(extent, codes);
    const unsigned drawn = std::min(B, unsigned(codes.size()));
    std::vector<tp::byte> map(drawn);
    for (unsigned i = 0; i < drawn; ++i)
        map[i] = pattern == 2 && drawn > 1 ? i * (codes.size() - 1) / (drawn - 1)
                 : pattern == 1 ? drawn - 1 - i : i;
    gpr_probe::plan<Rows> gp(format, map);
    const auto rp = *tp::reader<64, Step>::make(format, map);
    const auto wp = *tp::writer<64, Step>::make(format, map);
    std::vector<tp::byte> bytes((count - 1) * stride + extent);
    for (unsigned i = 0; i < bytes.size(); ++i)
        bytes[i] = tp::byte(i * 113 + i / 7 + 91);
    const auto initial = bytes;
    auto view = *tp::view::bind(format, bytes, count, stride);
    const auto pr = *tp::bind_reader(gp.read, view);
    const auto pw = *tp::bind_writer(gp.write, view);
    const auto br = *tp::bind_reader(rp, view);
    const auto bw = *tp::bind_writer(wp, view);
    const auto nr = tp::native_reader(br);
    const auto nw = tp::native_writer(bw);
    const gpr_probe::mutation<Rows, Method == method::compiled> gw(gp, view);
    std::array<ikea::owner_write, 512> entries;
    ikea::source_write_journal effects{entries};
    word active = tp::detail::all_rows<Rows> & (sparse ? 0x55 : ~word(0));
    if constexpr (Method == method::full_simd) {
        const auto part = active;
        for (unsigned i = 1; i < 8; ++i)
            active |= part << (i * Rows);
    }
    std::vector<std::size_t> trace(count / Step);
    for (std::size_t i = 0; i < trace.size(); ++i)
        trace[i] = (random ? (i * 389 + 53) % trace.size() : i) * Step;

    auto read_word = [&](std::size_t first) __attribute__((always_inline)) -> word {
        if constexpr (Method == method::point || Method == method::gpr_points) {
            word result = 0;
            for (unsigned r = 0; r < Rows; ++r)
                if (active & (word(1) << r)) {
                    const auto value = [&]() __attribute__((always_inline)) {
                        if constexpr (Method == method::point)
                            return pr.get_unchecked(first + r);
                        else
                            return gpr_probe::read(gp, view, first + r, 1);
                    }();
                    result |= value << (r * B * 8);
                }
            return result;
        } else if constexpr (Method == method::compiled)
            return gpr_probe::endpoint_read(gp, view, first, active);
        else
            return gpr_probe::read(gp, view, first, active);
    };
    auto consume = [&](std::size_t first) __attribute__((always_inline)) -> word {
        if constexpr (Method == method::simd || Method == method::full_simd) {
            const auto value = nr.get_unchecked(first, active);
            if (consumer == 1)
                return sum(value);
            if constexpr (Method == method::full_simd)
                return hash_full(value, std::make_index_sequence<8>{});
            else
                return hash(compact<Rows>(value, std::make_index_sequence<Rows>{}));
        } else {
            const auto value = read_word(first);
            return consumer == 1 ? sum(value) : hash(value);
        }
    };
    auto update = [&](std::size_t first) __attribute__((always_inline)) -> bool {
        effects.used = 0;
        if constexpr (Method == method::point || Method == method::gpr_points) {
            bool okay = true;
            for (unsigned r = 0; r < Rows; ++r)
                if (active & (word(1) << r)) {
                    if constexpr (Method == method::point)
                        okay &= bool(pw.set(first + r, toggle(pr.get_unchecked(first + r)), effects));
                    else
                        okay &= bool(gw.set(first + r,
                            toggle(gpr_probe::read(gp, view, first + r, 1)), effects, 1));
                }
            return okay;
        } else if constexpr (Method == method::simd || Method == method::full_simd)
            return bool(nw.set(first, toggle(nr.get_unchecked(first, active)), effects, active));
        else
            return bool(gw.set(first, toggle(read_word(first)), effects, active));
    };

    // Independently derive decoded slots and replacement bits, including inactive
    // original rows. Each actual timed competitor is checked before measurement.
    word expected_result = 0, actual_result = 0;
    auto expected = bytes;
    for (auto first : trace) {
        for (unsigned group = 0; group < Step / Rows; ++group) {
            word value = 0;
            for (unsigned r = 0; r < Rows; ++r) {
                const auto local = group * Rows + r;
                if (!(active & (word(1) << local)))
                    continue;
                for (unsigned i = 0; i < drawn; ++i) {
                    const auto c = codes[map[i]];
                    const auto offset = (first + local) * stride + c.offset;
                    tp::byte decoded = 0;
                    for (unsigned bit = 0; bit < c.width; ++bit)
                        decoded |= ((initial[offset] >> (c.shift + bit)) & 1) << bit;
                    value |= word(decoded) << (8 * (r * B + i));
                    const unsigned replacement = (decoded & 1) ^ 1;
                    for (unsigned bit = 0; bit < c.width; ++bit)
                        expected[offset] = tp::byte((expected[offset] & ~(1u << (c.shift + bit))) |
                                                   (((replacement >> bit) & 1) << (c.shift + bit)));
                }
            }
            expected_result += consumer == 1 ? sum(value) : hash(value);
        }
        actual_result += consume(first);
    }
    if (actual_result != expected_result) {
        state.SkipWithError("independent read consumer mismatch");
        return;
    }
    for (auto first : trace)
        if (!update(first)) {
            state.SkipWithError("checked update rejected");
            return;
        }
    if (bytes != expected) {
        state.SkipWithError("independent update mismatch");
        return;
    }
    std::copy(initial.begin(), initial.end(), bytes.begin());
    bool okay = true;
    for (auto _ : state) {
        word result = 0;
        if (consumer == 2) {
            for (auto first : trace)
                okay &= update(first);
        } else
            for (auto first : trace)
                result += consume(first);
        benchmark::DoNotOptimize(result);
        benchmark::DoNotOptimize(okay);
        benchmark::ClobberMemory();
    }
    if (!okay)
        state.SkipWithError("timed update rejected");
    state.SetItemsProcessed(state.iterations() * count);
    state.counters["items_per_iteration"] = count;
    state.counters["rows_per_call"] = Step;
    state.counters["drawn_codes"] = drawn;
    state.counters["stride"] = stride;
}
template <unsigned Rows, method Method> void cases() {
    constexpr const char* name[] = {"point", "gpr_inline", "gpr_compiled", "simd", "simd_full", "gpr_points"};
    for (unsigned extent : {1u, 4u, 8u, 16u, 64u})
        for (unsigned stride : {extent, extent == 64 ? 0u : 64u}) {
            if (!stride)
                continue;
            for (unsigned pattern = 0; pattern < (extent == 1 ? 1u : 3u); ++pattern)
                for (bool random : {false, true}) {
                    if (random && Method == method::full_simd)
                        continue;
                    for (bool sparse : {false, true}) {
                        if constexpr (Rows == 1)
                            if (sparse)
                                continue;
                        for (unsigned consumer = 0; consumer < 3; ++consumer) {
                            constexpr const char* consumers[] = {"word_hash", "sum", "update"};
                            const auto label = "gpr/" + std::to_string(Rows) + "/unit" +
                                std::to_string(extent) + "/stride" + std::to_string(stride) +
                                "/map" + std::to_string(pattern) + (random ? "/random" : "/scan") +
                                (sparse ? "/half/" : "/all/") + consumers[consumer] + "/" +
                                name[unsigned(Method)];
                            benchmark::RegisterBenchmark(label.c_str(), [=](auto& s) {
                                run<Rows, Method>(s, extent, stride, pattern, sparse, random, consumer);
                            });
                        }
                    }
                }
        }
}
template <unsigned Rows> void methods() {
    cases<Rows, method::point>();
    cases<Rows, method::gpr>();
    cases<Rows, method::compiled>();
    cases<Rows, method::simd>();
    cases<Rows, method::full_simd>();
    cases<Rows, method::gpr_points>();
}
#endif
} // namespace
