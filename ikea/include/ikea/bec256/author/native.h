#pragma once
#include <ikea/bec256/codec.h>
#include <cstring>

#if defined(__AVX512VBMI__) && defined(__AVX512BITALG__) && defined(__AVX512VL__) &&               \
    defined(__AVX512BW__) && defined(__AVX512DQ__)
#define IKEA_BEC256_AVX512 1
#include <ikea/bec256/detail/native/avx512.h>
#elif defined(__aarch64__)
#include <ikea/bec256/detail/native/neon.h>
#endif

namespace ikea::bec256::native {
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
#if defined(IKEA_BEC256_AVX512)
using block = __m256i;
using pair = __m512i;
[[gnu::always_inline]] inline block load(const byte *p) {
    return _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
}
[[gnu::always_inline]] inline pair load_pair(const byte *p) { return _mm512_loadu_si512(p); }
[[gnu::always_inline]] inline void store(byte *p, block value) {
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(p), value);
}
[[gnu::always_inline]] inline void store_pair(byte *p, pair value) {
    _mm512_storeu_si512(p, value);
}
[[gnu::always_inline]] inline pair join(block a, block b) {
    return _mm512_inserti64x4(_mm512_castsi256_si512(a), b, 1);
}
template <unsigned I> [[gnu::always_inline]] inline block part(pair value) {
    static_assert(I < 2);
    return _mm512_extracti64x4_epi64(value, I);
}
[[gnu::always_inline]] inline pair intersection(pair a, pair b) { return _mm512_and_si512(a, b); }
[[gnu::always_inline]] inline pair set_union(pair a, pair b) { return _mm512_or_si512(a, b); }
[[gnu::always_inline]] inline pair difference(pair a, pair b) { return _mm512_andnot_si512(b, a); }
[[gnu::always_inline]] inline unsigned population(block value) {
    auto counts = _mm256_sad_epu8(_mm256_popcnt_epi8(value), _mm256_setzero_si256());
    auto sum = _mm_add_epi64(_mm256_castsi256_si128(counts), _mm256_extracti128_si256(counts, 1));
    return _mm_cvtsi128_si64(sum) + _mm_extract_epi64(sum, 1);
}
[[gnu::always_inline]] inline std::uint64_t population(pair value) {
    // VPOPCNTDQ is independent of the decoder's feature requirements.
#if defined(__AVX512VPOPCNTDQ__)
    return _mm512_reduce_add_epi64(_mm512_popcnt_epi64(value));
#else
    const auto counts = _mm512_popcnt_epi8(value);
    return _mm512_reduce_add_epi64(_mm512_sad_epu8(counts, _mm512_setzero_si512()));
#endif
}
[[gnu::always_inline]] inline block decode_unchecked(const byte *readable64, unsigned population) {
    unsigned bits;
    return detail::avx512::decode(reinterpret_cast<const std::uint8_t *>(readable64), population,
                                  bits);
}
[[gnu::always_inline]] inline pair decode_pair_unchecked(const byte *a, unsigned population_a,
                                                         const byte *b, unsigned population_b) {
    unsigned bits_a, bits_b;
    return detail::avx512::decode2(reinterpret_cast<const std::uint8_t *>(a), population_a,
                                   reinterpret_cast<const std::uint8_t *>(b), population_b, bits_a,
                                   bits_b);
}
[[gnu::always_inline]] inline unsigned encode_unchecked(block value, unsigned population,
                                                        byte *writable64) {
    return detail::avx512::encode(value, population, reinterpret_cast<std::uint8_t *>(writable64));
}
#else
using block = uint8x16x2_t;
using pair = uint8x16x4_t;
[[gnu::always_inline]] inline block load(const byte *p) { return detail::neon::load256(p); }
[[gnu::always_inline]] inline pair load_pair(const byte *p) {
    return vld1q_u8_x4(reinterpret_cast<const std::uint8_t *>(p));
}
[[gnu::always_inline]] inline void store(byte *p, block value) { detail::neon::store256(p, value); }
[[gnu::always_inline]] inline void store_pair(byte *p, pair value) {
    vst1q_u8_x4(reinterpret_cast<std::uint8_t *>(p), value);
}
[[gnu::always_inline]] inline pair join(block a, block b) {
    return {{a.val[0], a.val[1], b.val[0], b.val[1]}};
}
template <unsigned I> [[gnu::always_inline]] inline block part(pair value) {
    static_assert(I < 2);
    return {{value.val[2 * I], value.val[2 * I + 1]}};
}
[[gnu::always_inline]] inline pair intersection(pair a, pair b) {
    return {{vandq_u8(a.val[0], b.val[0]), vandq_u8(a.val[1], b.val[1]),
             vandq_u8(a.val[2], b.val[2]), vandq_u8(a.val[3], b.val[3])}};
}
[[gnu::always_inline]] inline pair set_union(pair a, pair b) {
    return {{vorrq_u8(a.val[0], b.val[0]), vorrq_u8(a.val[1], b.val[1]),
             vorrq_u8(a.val[2], b.val[2]), vorrq_u8(a.val[3], b.val[3])}};
}
[[gnu::always_inline]] inline pair difference(pair a, pair b) {
    return {{vbicq_u8(a.val[0], b.val[0]), vbicq_u8(a.val[1], b.val[1]),
             vbicq_u8(a.val[2], b.val[2]), vbicq_u8(a.val[3], b.val[3])}};
}
[[gnu::always_inline]] inline unsigned population(block value) {
    return vaddlvq_u8(vaddq_u8(vcntq_u8(value.val[0]), vcntq_u8(value.val[1])));
}
[[gnu::always_inline]] inline std::uint64_t population(pair value) {
    return vaddlvq_u8(vaddq_u8(vaddq_u8(vcntq_u8(value.val[0]), vcntq_u8(value.val[1])),
                               vaddq_u8(vcntq_u8(value.val[2]), vcntq_u8(value.val[3]))));
}
[[gnu::always_inline]] inline block decode_unchecked(const byte *readable64, unsigned population) {
    unsigned bits;
    return detail::neon::decode(reinterpret_cast<const std::uint8_t *>(readable64), population,
                                bits);
}
[[gnu::always_inline]] inline pair decode_pair_unchecked(const byte *a, unsigned population_a,
                                                         const byte *b, unsigned population_b) {
    return join(decode_unchecked(a, population_a), decode_unchecked(b, population_b));
}
[[gnu::always_inline]] inline unsigned encode_unchecked(block value, unsigned population,
                                                        byte *writable64) {
    return detail::neon::encode(value, population, reinterpret_cast<std::uint8_t *>(writable64));
}
#endif

/// All unchecked decoders require a valid body/population association and 64
/// initialized readable bytes per input, even at population 0 or 256. Two inputs
/// are independent addresses; the low/high 256 bits preserve argument order.
/// Encoders require correct population and 64 exclusive writable bytes. They
/// return logical byte length but may store beyond it; no input/output overlap.
struct lengths {
    unsigned first, second;
};
[[gnu::always_inline]] inline lengths encode_pair_unchecked(pair value, unsigned population_a,
                                                            byte *a64, unsigned population_b,
                                                            byte *b64) {
    return {encode_unchecked(part<0>(value), population_a, a64),
            encode_unchecked(part<1>(value), population_b, b64)};
}

namespace access_detail {
#if defined(IKEA_BEC256_AVX512)
// The scalar root count can start independently of the full-width load. Do not
// route its first byte through the vector value. Empty bodies permit no load.
[[gnu::always_inline]] inline unsigned first_byte(const source &input) {
    return input.bytes() ? std::to_integer<unsigned>(input.storage()[0]) : 0;
}
[[gnu::always_inline]] inline __m512i window(const source &input) {
    if (input.storage().size() >= scratch_bytes)
        return _mm512_loadu_si512(input.storage().data());
    // A masked load grants no access to neighbouring bodies and avoids the
    // scratch store/reload boundary on exact input spans, including zero bytes.
    return _mm512_maskz_loadu_epi8((std::uint64_t{1} << input.bytes()) - 1, input.storage().data());
}
#else
[[gnu::always_inline]] inline const byte *window(const source &input, byte *scratch64) {
    if (input.storage().size() >= scratch_bytes)
        return input.storage().data();
    std::memset(scratch64, 0, scratch_bytes);
    if (input.bytes())
        std::memcpy(scratch64, input.storage().data(), input.bytes());
    return scratch64;
}
#endif
} // namespace access_detail

/// Native decoded values. Exact spans use masked register loads on AVX-512;
/// NEON adapts short spans through bounded local scratch. Readable suffixes
/// remove that adaptation without changing the logical body or its framing.
[[gnu::always_inline]] inline block read(const source &input) {
#if defined(IKEA_BEC256_AVX512)
    unsigned bits;
    return detail::avx512::decode_loaded(
        access_detail::window(input), access_detail::first_byte(input), input.population(), bits);
#else
    alignas(64) byte scratch[64];
    return decode_unchecked(access_detail::window(input, scratch), input.population());
#endif
}
[[gnu::always_inline]] inline pair read_pair(const source &a, const source &b) {
#if defined(IKEA_BEC256_AVX512)
    unsigned bits_a, bits_b;
    return detail::avx512::decode2_loaded(
        access_detail::window(a), access_detail::first_byte(a), a.population(),
        access_detail::window(b), access_detail::first_byte(b), b.population(), bits_a, bits_b);
#else
    alignas(64) byte scratch_a[64], scratch_b[64];
    return decode_pair_unchecked(access_detail::window(a, scratch_a), a.population(),
                                 access_detail::window(b, scratch_b), b.population());
#endif
}
#endif
} // namespace ikea::bec256::native
