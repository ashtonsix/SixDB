#include "native.h"

namespace tuple_runtime {
TUPLE_CC native_packet read_native(const read_plan& p, const byte* row) {
    return read_body(p, row);
}
namespace {
[[gnu::always_inline]] native_packet bit_or(native_packet a, native_packet b) {
#if defined(__aarch64__)
    return {vorrq_u8(a.a, b.a), vorrq_u8(a.b, b.b), vorrq_u8(a.c, b.c), vorrq_u8(a.d, b.d)};
#elif defined(__AVX512VBMI__)
    return _mm512_or_si512(a, b);
#else
    return {_mm256_or_si256(a.a, b.a), _mm256_or_si256(a.b, b.b)};
#endif
}
[[gnu::always_inline]] native_packet bit_and(native_packet a, native_packet b) {
#if defined(__aarch64__)
    return {vandq_u8(a.a, b.a), vandq_u8(a.b, b.b), vandq_u8(a.c, b.c), vandq_u8(a.d, b.d)};
#elif defined(__AVX512VBMI__)
    return _mm512_and_si512(a, b);
#else
    return {_mm256_and_si256(a.a, b.a), _mm256_and_si256(a.b, b.b)};
#endif
}
[[gnu::always_inline]] bool nonzero(native_packet p) {
#if defined(__aarch64__)
    return vmaxvq_u8(vorrq_u8(vorrq_u8(p.a, p.b), vorrq_u8(p.c, p.d))) != 0;
#elif defined(__AVX512VBMI__)
    return _mm512_test_epi8_mask(p, p) != 0;
#else
    const auto value = _mm256_or_si256(p.a, p.b);
    return !_mm256_testz_si256(value, value);
#endif
}
} // namespace

TUPLE_CC mutation_status write_native(const write_plan& p, byte* row, native_packet input,
                                      std::uint64_t& effects) {
    if (!p.dense_native) return mutation_status::native_shape;
    if (nonzero(bit_and(input, load_packet(p.invalid_bits.data())))) return mutation_status::value;
    using namespace native_detail;
    const auto z = zero16();
    auto updated = join(z, z, z, z);
    if (p.needs_old) {
        updated = join(row_part<0>(row, p.bytes), row_part<1>(row, p.bytes),
                       row_part<2>(row, p.bytes), row_part<3>(row, p.bytes));
        updated = bit_and(updated, load_packet(p.preserve.data()));
    }
    for (unsigned i = 0; i < p.round_count; ++i)
        updated = bit_or(updated, transform<true>(input, p.rounds[i]));
#if defined(__AVX512VBMI__)
    const auto mask = p.bytes == 64 ? ~std::uint64_t(0) : (std::uint64_t(1) << p.bytes) - 1;
    _mm512_mask_storeu_epi8(row, mask, updated);
#else
    store_part<0>(row, p.bytes, updated); store_part<1>(row, p.bytes, updated);
    store_part<2>(row, p.bytes, updated); store_part<3>(row, p.bytes, updated);
#endif
    effects |= p.issued_writes;
    return mutation_status::ok;
}

} // namespace tuple_runtime
