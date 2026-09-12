#include "byte_route.h"
#include <benchmark/benchmark.h>
#include <random>
#include <string>

// Fixed 16-code projection, over a 16-byte plane or a 64-byte row. Compare
// native 16-byte point results with native 16x4 packets, through opaque calls.
// The selected bytes are first in each row only to isolate placement/gather;
// projection permutations and byte-contained extraction remain runtime controls.
namespace tuple_scan_probe {
using namespace tuple_runtime;
using namespace tuple_composition_probe;
struct plan { shuffle single, four; };
plan prepare(bool permuted) {
    plan p;
    for (unsigned i = 0; i < 64; ++i) {
        const unsigned index = permuted ? (5 * (i % 16) + 3) % 16 : i % 16;
        const unsigned width = 1 + index % 7, shift = index % (9 - width);
        p.four.index[i] = i / 16 * 16 + index;
        p.four.shift[i] = -int(shift); p.four.mask[i] = (1u << width) - 1;
        if (i < 16) {
            p.single.index[i] = index; p.single.shift[i] = -int(shift); p.single.mask[i] = (1u << width) - 1;
        }
    }
    finish_controls(p.single, false); finish_controls(p.four, false);
    return p;
}
[[gnu::noinline]] vector16 one(const plan& p, const byte* row) {
#if defined(__aarch64__)
    auto v = vqtbl1q_u8(vld1q_u8(row), vld1q_u8(p.single.index.data()));
    return vandq_u8(vshlq_u8(v, vld1q_s8(p.single.shift.data())), vld1q_u8(p.single.mask.data()));
#else
    auto v = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(row)),
                             _mm_loadu_si128(reinterpret_cast<const __m128i*>(p.single.index.data())));
#if defined(__AVX512VBMI__)
    v = _mm_multishift_epi64_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p.single.bit_index.data())), v);
#else
    const auto even = _mm_srli_epi16(_mm_mullo_epi16(_mm_and_si128(v, _mm_set1_epi16(255)),
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(p.single.even_factor.data()))), 8);
    const auto odd = _mm_and_si128(_mm_mullo_epi16(_mm_srli_epi16(v, 8),
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(p.single.odd_factor.data()))), _mm_set1_epi16(short(0xff00)));
    v = _mm_or_si128(even, odd);
#endif
    return _mm_and_si128(v, _mm_loadu_si128(reinterpret_cast<const __m128i*>(p.single.mask.data())));
#endif
}
[[gnu::always_inline]] native_packet four_body(const plan& p, native_packet data) {
    // Each output lane selects from its own tuple; a lane-local permutation is
    // stronger than a generic 64-byte shuffle. Shape is bound, map stays runtime.
#if defined(__aarch64__)
    auto part = [&](auto v) {
        return vandq_u8(vshlq_u8(vqtbl1q_u8(v, vld1q_u8(p.single.index.data())),
                               vld1q_s8(p.single.shift.data())), vld1q_u8(p.single.mask.data()));
    };
    return {part(data.a), part(data.b), part(data.c), part(data.d)};
#elif defined(__AVX512VBMI__)
    auto v = _mm512_shuffle_epi8(data, _mm512_loadu_si512(p.four.index.data()));
    return _mm512_and_si512(_mm512_multishift_epi64_epi8(_mm512_loadu_si512(p.four.bit_index.data()), v),
                           _mm512_loadu_si512(p.four.mask.data()));
#else
    native_packet v{_mm256_shuffle_epi8(data.a, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.four.index.data()))),
                    _mm256_shuffle_epi8(data.b, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p.four.index.data() + 32)))};
    return shift_mask<false>(v, p.four);
#endif
}
[[gnu::noinline]] TUPLE_CC native_packet four(const plan& p, const byte* row, unsigned stride) {
    using namespace native_detail;
    const auto data = stride == 16 ? load_packet(row) :
        join(load16(row,16),load16(row+stride,16),load16(row+stride*2,16),load16(row+stride*3,16));
    return four_body(p,data);
}
[[gnu::noinline]] TUPLE_CC native_packet masked_four(const plan& p, const std::array<const byte*,4>& rows, unsigned active) {
    using namespace native_detail;
    // Mask coordinates are tuple lanes, separate from the native payload ABI.
    // Inactive lanes require no readable address and return zero code bytes.
    const auto a = active & 1 ? load16(rows[0],16) : zero16();
    const auto b = active & 2 ? load16(rows[1],16) : zero16();
    const auto c = active & 4 ? load16(rows[2],16) : zero16();
    const auto d = active & 8 ? load16(rows[3],16) : zero16();
    return four_body(p,join(a,b,c,d));
}
[[gnu::always_inline]] std::uint64_t sum16(vector16 v) {
#if defined(__aarch64__)
    return vaddlvq_u8(v);
#else
    const auto s = _mm_sad_epu8(v, _mm_setzero_si128());
    return std::uint64_t(_mm_cvtsi128_si64(s)) + _mm_extract_epi64(s, 1);
#endif
}
[[gnu::always_inline]] std::uint64_t sum64(native_packet v) {
    return sum16(native_detail::split<0>(v)) + sum16(native_detail::split<1>(v)) +
           sum16(native_detail::split<2>(v)) + sum16(native_detail::split<3>(v));
}
std::uint64_t reference(const plan& p, const byte* row) {
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < 16; ++i) sum += (row[p.single.index[i]] >> -p.single.shift[i]) & p.single.mask[i];
    return sum;
}
void check() {
    std::mt19937 random(0x164);
    for (bool permuted : {false, true}) for (unsigned stride : {16, 64}) {
        auto p = prepare(permuted);
        for (unsigned count = 1; count <= 129; ++count) {
            // Last row has only the selected 16 bytes: no implicit row padding.
            std::vector<byte> data((count - 1) * stride + 16);
            for (auto& b : data) b = random();
            std::uint64_t expected = 0, actual = 0;
            unsigned i = 0;
            std::array<code,16> codes;
            mapping map; map.fill(255);
            for (unsigned j = 0; j < 16; ++j) {
                const unsigned width = 1 + j % 7;
                codes[j] = {byte(j),byte(j % (9-width)),byte(width)};
                map[j] = permuted ? (5*j+3)%16 : j;
            }
            for (; i + 4 <= count; i += 4) {
                const auto packet = four(p,data.data() + i*stride,stride);
                actual += sum64(packet);
                std::array<byte,64> lanes{}; store_packet(lanes.data(),packet);
                for (unsigned r = 0; r < 4; ++r) {
                    const auto wanted = reference_read({16,codes},map,data.data() + (i+r)*stride);
                    for (unsigned j = 0; j < 16; ++j)
                        require(lanes[r*16+j] == wanted[j], "ordered row/lane coordinates in 16x4 packet");
                }
            }
            for (; i < count; ++i) {
                const auto packet = one(p,data.data()+i*stride); actual += sum16(packet);
                std::array<byte,16> lanes{}; native_detail::store16(lanes.data(),16,packet);
                const auto wanted = reference_read({16,codes},map,data.data()+i*stride);
                for (unsigned j = 0; j < 16; ++j)
                    require(lanes[j] == wanted[j], "ordered coordinates in 16x1 tail");
            }
            for (i = 0; i < count; ++i) expected += reference(p, data.data() + i * stride);
            require(actual == expected, "16x4 native projection and exact tail");
        }
    }
    for (bool permuted : {false,true}) for (unsigned active = 0; active < 16; ++active) {
        auto p = prepare(permuted);
        std::array<byte,64> data{},actual{},wanted{};
        std::array<const byte*,4> pointers{};
        for (auto& b : data) b = random();
        for (unsigned r = 0; r < 4; ++r) if (active & (1u<<r)) {
            pointers[r] = data.data()+16*r;
            native_detail::store16(wanted.data()+16*r,16,one(p,pointers[r]));
        }
        store_packet(actual.data(),masked_four(p,pointers,active));
        require(actual == wanted, "prefilter skips null inactive lanes and preserves output coordinates");
    }
    std::puts("32 prefiltered native packets passed with null inactive-lane pointers");
    std::puts("516 native scan projection/placement/tail cases passed");
}
void bench(benchmark::State& state, bool group, bool permuted, unsigned stride) {
    const unsigned rows = state.range(0);
    auto p = prepare(permuted);
    std::vector<byte> data(std::size_t(rows) * stride);
    for (unsigned row = 0; row < rows; ++row) for (unsigned i = 0; i < stride; ++i)
        data[std::size_t(row) * stride + i] = byte(row * 17 + i * 13);
    auto one_call = one; auto four_call = four;
    asm volatile("" : "+r"(one_call), "+r"(four_call));
    for (auto _ : state) {
        std::uint64_t total = 0;
        if (group) for (unsigned i = 0; i < rows; i += 4) total += sum64(four_call(p, data.data() + std::size_t(i) * stride, stride));
        else for (unsigned i = 0; i < rows; ++i) total += sum16(one_call(p, data.data() + std::size_t(i) * stride));
        asm volatile("" : "+r"(total));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * rows);
    state.counters["items_per_iteration"] = rows;
    state.counters["stride"] = stride; state.counters["projected_codes"] = 16;
    state.counters["tuples_per_call"] = group ? 4 : 1;
}
void add() {
    for (bool group : {false, true}) for (bool permuted : {false, true}) for (unsigned stride : {16, 64}) {
        auto name = std::string("scan/") + (group ? "packet4" : "packet1") + (permuted ? "/permuted/" : "/ordered/") + std::to_string(stride);
        benchmark::RegisterBenchmark(name.c_str(), [=](benchmark::State& s) { bench(s, group, permuted, stride); })
            ->Arg(1024)->Arg(65536)->Arg(1048576);
    }
}
}
void check_tuple_scan() { tuple_scan_probe::check(); }
void register_tuple_scan() { tuple_scan_probe::add(); }
