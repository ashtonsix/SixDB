#pragma once
#include <bit>
#include <utility>
#include <ikea/tuplepack/detail/packet_plan.h>
#include <ikea/tuplepack/detail/native/types.h>
#include <ikea/tuplepack/detail/native/memory.h>
#include <ikea/tuplepack/detail/native/neon/shuffle.h>
#include <ikea/tuplepack/detail/native/avx2/shuffle.h>
#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack::native {
[[gnu::always_inline]] inline native_packet load_packet(const byte* p) {
    using namespace native_detail;
    return join(load16(p, 16), load16(p + 16, 16), load16(p + 32, 16), load16(p + 48, 16));
}
/// Trusted 0..64-byte physical unit load, zero-filling the rest. No allocation
/// padding or next tuple is read. The caller owns this raw-byte interpretation.
[[gnu::always_inline]] inline native_packet load_unit(const byte* p, unsigned bytes) {
    using namespace native_detail;
    return join(row_part<0>(p, bytes), row_part<1>(p, bytes), row_part<2>(p, bytes),
                row_part<3>(p, bytes));
}
[[gnu::always_inline]] inline void store_packet(byte* p, native_packet value) {
#if defined(__aarch64__)
    vst1q_u8(p, value.a);
    vst1q_u8(p + 16, value.b);
    vst1q_u8(p + 32, value.c);
    vst1q_u8(p + 48, value.d);
#elif defined(__AVX512VBMI__)
    _mm512_storeu_si512(p, value);
#else
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), value.a);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(p + 32), value.b);
#endif
}
template <bool Left>
[[gnu::always_inline]] inline native_packet transform(native_packet source,
                                                      const detail::shuffle& p) {
#if defined(__aarch64__)
    return {native_detail::apply16<0>(source, p), native_detail::apply16<1>(source, p),
            native_detail::apply16<2>(source, p), native_detail::apply16<3>(source, p)};
#elif defined(__AVX512VBMI__)
    auto result = _mm512_permutexvar_epi8(_mm512_loadu_si512(p.index.data()), source);
    if (p.shifting)
        result = _mm512_multishift_epi64_epi8(_mm512_loadu_si512(p.bit_index.data()), result);
    if (p.masking)
        result = _mm512_and_si512(result, _mm512_loadu_si512(p.mask.data()));
    return result;
#else
    return {native_detail::apply32<0, Left>(source, p), native_detail::apply32<1, Left>(source, p)};
#endif
}
[[gnu::always_inline]] inline native_packet read_body(const detail::read64& p, const byte* row) {
    using namespace native_detail;
    return transform<false>(join(load_chunk<0>(p, row), load_chunk<1>(p, row),
                                 load_chunk<2>(p, row), load_chunk<3>(p, row)),
                            p.operation);
}

[[gnu::always_inline]] inline native_packet bit_or(native_packet a, native_packet b) {
#if defined(__aarch64__)
    return {vorrq_u8(a.a, b.a), vorrq_u8(a.b, b.b), vorrq_u8(a.c, b.c), vorrq_u8(a.d, b.d)};
#elif defined(__AVX512VBMI__)
    return _mm512_or_si512(a, b);
#else
    return {_mm256_or_si256(a.a, b.a), _mm256_or_si256(a.b, b.b)};
#endif
}
[[gnu::always_inline]] inline native_packet bit_and(native_packet a, native_packet b) {
#if defined(__aarch64__)
    return {vandq_u8(a.a, b.a), vandq_u8(a.b, b.b), vandq_u8(a.c, b.c), vandq_u8(a.d, b.d)};
#elif defined(__AVX512VBMI__)
    return _mm512_and_si512(a, b);
#else
    return {_mm256_and_si256(a.a, b.a), _mm256_and_si256(a.b, b.b)};
#endif
}
[[gnu::always_inline]] inline bool nonzero(native_packet p) {
#if defined(__aarch64__)
    return vmaxvq_u8(vorrq_u8(vorrq_u8(p.a, p.b), vorrq_u8(p.c, p.d))) != 0;
#elif defined(__AVX512VBMI__)
    return _mm512_test_epi8_mask(p, p) != 0;
#else
    const auto value = _mm256_or_si256(p.a, p.b);
    return !_mm256_testz_si256(value, value);
#endif
}
namespace native_detail {
/// Encoded physical bytes remain in registers through sparse stores. Bit masks
/// describe issued bytes; they never authorize writing an inactive neighbor.
template <unsigned W>
[[gnu::always_inline]] inline void store_selected_word(byte* row, packet encoded,
                                                       std::uint64_t written) {
    const auto part = split<W / 2>(encoded);
#if defined(__aarch64__)
    const auto word = vgetq_lane_u64(vreinterpretq_u64_u8(part), W % 2);
#elif defined(__AVX2__)
    const auto word = std::uint64_t(_mm_extract_epi64(part, W % 2));
#endif
    unsigned mask = (written >> (8 * W)) & 255;
    while (mask) {
        const unsigned offset = std::countr_zero(mask);
        row[8 * W + offset] = byte(word >> (8 * offset));
        mask &= mask - 1;
    }
}
[[gnu::always_inline]] inline void store_selected(byte* row, packet encoded,
                                                  std::uint64_t written) {
#if defined(__AVX512VBMI__)
    _mm512_mask_storeu_epi8(row, written, encoded);
#else
    store_selected_word<0>(row, encoded, written);
    store_selected_word<1>(row, encoded, written);
    store_selected_word<2>(row, encoded, written);
    store_selected_word<3>(row, encoded, written);
    store_selected_word<4>(row, encoded, written);
    store_selected_word<5>(row, encoded, written);
    store_selected_word<6>(row, encoded, written);
    store_selected_word<7>(row, encoded, written);
#endif
}
template <class Plan>
[[gnu::always_inline]] inline packet encode(const Plan& p, const byte* row, packet input) {
    auto encoded = join(zero16(), zero16(), zero16(), zero16());
    if (p.needs_old)
        encoded = bit_and(load_unit(row, p.read.bytes), load_packet(p.preserve.data()));
    for (unsigned i = 0; i < p.round_count; ++i)
        encoded = bit_or(encoded, transform<true>(input, p.rounds[i]));
    return encoded;
}
} // namespace native_detail
[[gnu::always_inline]] inline void write_body(const detail::write64& p, byte* row, packet input) {
    if (!p.count)
        return;
    using namespace native_detail;
    auto encoded = encode(p, row, input);
    if (!p.dense) {
        store_selected(row, encoded, p.writes);
        return;
    }
#if defined(__AVX512VBMI__)
    const auto mask =
        p.read.bytes == 64 ? ~std::uint64_t(0) : (std::uint64_t(1) << p.read.bytes) - 1;
    _mm512_mask_storeu_epi8(row, mask, encoded);
#else
    store_part<0>(row, p.read.bytes, encoded);
    store_part<1>(row, p.read.bytes, encoded);
    store_part<2>(row, p.read.bytes, encoded);
    store_part<3>(row, p.read.bytes, encoded);
#endif
}
/// Trusted in-register endpoints. Input widths, extents, isolation and effects
/// are admitted by the operation shell. No allocation, checks or suspension.
IKEA_TUPLE_CC packet read(const detail::read64&, const byte* row);
IKEA_TUPLE_CC void write(const detail::write64&, byte* row, packet input);
/// Packet-shaped compiled endpoints. Rows must match preparation (2..64,
/// powers of two). Bit r selects first+r; inactive rows issue no payload access.
IKEA_TUPLE_CC packet read(const detail::packet_read&, unsigned rows, const byte* first,
                          std::size_t stride, std::uint64_t active);
IKEA_TUPLE_CC void write(const detail::packet_write&, unsigned rows, byte* first,
                         std::size_t stride, packet input, std::uint64_t active);
} // namespace ikea::tuplepack::native
#endif

#include <ikea/tuplepack/detail/native/packet.h>
#include <ikea/tuplepack/detail/native/selection.h>
