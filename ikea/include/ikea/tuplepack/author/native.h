#pragma once
#include <ikea/tuplepack/detail/plan.h>
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
[[gnu::always_inline]] inline void write_body(const detail::write64& p, byte* row, packet input) {
    if (!p.dense) {
        std::array<byte, 64> values;
        store_packet(values.data(), input);
        detail::write_body<64>(p, row, values);
        return;
    }
    using namespace native_detail;
    const auto z = zero16();
    auto updated = join(z, z, z, z);
    if (p.needs_old) {
        updated = join(row_part<0>(row, p.read.bytes), row_part<1>(row, p.read.bytes),
                       row_part<2>(row, p.read.bytes), row_part<3>(row, p.read.bytes));
        updated = bit_and(updated, load_packet(p.preserve.data()));
    }
    for (unsigned i = 0; i < p.round_count; ++i)
        updated = bit_or(updated, transform<true>(input, p.rounds[i]));
#if defined(__AVX512VBMI__)
    const auto mask =
        p.read.bytes == 64 ? ~std::uint64_t(0) : (std::uint64_t(1) << p.read.bytes) - 1;
    _mm512_mask_storeu_epi8(row, mask, updated);
#else
    store_part<0>(row, p.read.bytes, updated);
    store_part<1>(row, p.read.bytes, updated);
    store_part<2>(row, p.read.bytes, updated);
    store_part<3>(row, p.read.bytes, updated);
#endif
}
/// Trusted in-register endpoints. Input widths, extents, isolation and effects
/// are admitted by the operation shell. No allocation, checks or suspension.
IKEA_TUPLE_CC packet read(const detail::read64&, const byte* row);
IKEA_TUPLE_CC void write(const detail::write64&, byte* row, packet input);
} // namespace ikea::tuplepack::native
#endif
