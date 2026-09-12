#pragma once
#include "native.h"
#include "routes.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <memory>

// A caller-owned experiment, not a TuplePack or Engine interface proposal.
// Thirty-two LE uint16 values are decomposed into 128 byte-contained codes.
// A bound operation consumes two code packets and returns a summary delta.
namespace tuple_composition_probe {
using namespace tuple_runtime;
using bytes64 = std::array<byte, 64>;
using packets = std::array<bytes64, 2>;

inline void require(bool value, const char* reason) {
    if (!value) { std::fprintf(stderr, "compound mutation: %s\n", reason); std::abort(); }
}
inline unsigned width(unsigned i) { return 1 + i % 7; }
inline std::uint64_t sum(const bytes64& values) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 64; i += 2)
        result += unsigned(values[i]) | (unsigned(values[i + 1]) << 8);
    return result;
}
inline packets split(const bytes64& values) {
    packets result{};
    for (unsigned i = 0; i < 64; ++i) {
        result[0][i] = values[i] & ((1u << width(i)) - 1);
        result[1][i] = values[i] >> width(i);
    }
    return result;
}
inline bytes64 assemble(const packets& values) {
    bytes64 result{};
    for (unsigned i = 0; i < 64; ++i)
        result[i] = values[0][i] | (values[1][i] << width(i));
    return result;
}
[[gnu::always_inline]] inline native_packet either(native_packet a, native_packet b) {
#if defined(__aarch64__)
    return {vorrq_u8(a.a, b.a), vorrq_u8(a.b, b.b), vorrq_u8(a.c, b.c), vorrq_u8(a.d, b.d)};
#elif defined(__AVX512VBMI__)
    return _mm512_or_si512(a, b);
#else
    return {_mm256_or_si256(a.a, b.a), _mm256_or_si256(a.b, b.b)};
#endif
}
[[gnu::always_inline]] inline native_packet both(native_packet a, native_packet b) {
#if defined(__aarch64__)
    return {vandq_u8(a.a, b.a), vandq_u8(a.b, b.b), vandq_u8(a.c, b.c), vandq_u8(a.d, b.d)};
#elif defined(__AVX512VBMI__)
    return _mm512_and_si512(a, b);
#else
    return {_mm256_and_si256(a.a, b.a), _mm256_and_si256(a.b, b.b)};
#endif
}
[[gnu::always_inline]] inline std::uint64_t sum_native(native_packet values) {
#if defined(__aarch64__)
    return std::uint64_t(vaddlvq_u16(vreinterpretq_u16_u8(values.a))) +
           vaddlvq_u16(vreinterpretq_u16_u8(values.b)) +
           vaddlvq_u16(vreinterpretq_u16_u8(values.c)) +
           vaddlvq_u16(vreinterpretq_u16_u8(values.d));
#elif defined(__AVX512VBMI__)
    // Widen before adding: uint16 values may exceed signed int16's range.
    auto lo = _mm512_cvtepu16_epi32(_mm512_castsi512_si256(values));
    auto hi = _mm512_cvtepu16_epi32(_mm512_extracti64x4_epi64(values, 1));
    return std::uint64_t(_mm512_reduce_add_epi32(_mm512_add_epi32(lo, hi)));
#else
    auto part = [](__m256i v) {
        auto lo = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(v));
        auto hi = _mm256_cvtepu16_epi32(_mm256_extracti128_si256(v, 1));
        auto totals = _mm256_add_epi32(lo, hi);
        auto halves = _mm_add_epi32(_mm256_castsi256_si128(totals), _mm256_extracti128_si256(totals, 1));
        halves = _mm_hadd_epi32(halves, halves);
        halves = _mm_hadd_epi32(halves, halves);
        return std::uint64_t(_mm_cvtsi128_si32(halves));
    };
    return part(values.a) + part(values.b);
#endif
}

struct byte_route;
struct recipe {
    bool reordered = false, partial = false;
    std::vector<route_term> decoded;
    // Cold-owned optional normal form; kept out of the generic recipe API.
    std::shared_ptr<const byte_route> byte_decoder;
    std::array<code, 128> codes;
    std::array<mapping, 2> maps;
    std::array<read_plan, 2> read;
    std::array<write_plan, 2> write;
    std::array<shuffle, 2> assembly;
};
recipe prepare(bool reordered, bool partial);
// Caller-owned assembly contract: two code fragments per semantic byte, widths
// fixed by that caller. A child can replace physical offsets/shifts, not widths.
std::expected<recipe, error> prepare_codes(const std::array<code,128>&, bool partial);

// Placement is supplied by the owner. Two 32-byte regions can be adjacent in
// one TuplePack unit or live in separately leased planes. Kernels see pointers;
// the outer operation retains ownership and validates the mapping generation.
struct raw_view { byte* first; byte* second; };
[[gnu::always_inline]] inline native_packet load(raw_view v) {
    using namespace native_detail;
    return join(load16(v.first, 16), load16(v.first + 16, 16),
                load16(v.second, 16), load16(v.second + 16, 16));
}
[[gnu::always_inline]] inline void store(raw_view v, native_packet data) {
    using namespace native_detail;
    store16(v.first, 16, native_detail::split<0>(data));
    store16(v.first + 16, 16, native_detail::split<1>(data));
    store16(v.second, 16, native_detail::split<2>(data));
    store16(v.second + 16, 16, native_detail::split<3>(data));
}

TUPLE_CC std::uint64_t replace_and_sum_delta(const recipe&, raw_view, native_packet, native_packet);
using operation = std::uint64_t (TUPLE_CC *)(const recipe&, raw_view, native_packet, native_packet);

} // namespace tuple_composition_probe
