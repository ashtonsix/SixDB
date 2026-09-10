#pragma once
#include "metadata_format.h"
#include "../ikea-integers/local.h"
#include "../ikea-integers/scan.h"
#include <cstdint>

namespace ikea::heterogeneous {

// Inline native operands, not a prescribed opaque-call return convention.
// Lane i is (absolute body start << 16) | population for logical record i.
#if defined(__aarch64__)
struct EntryLanes16 { uint32x4_t first, second, third, fourth; };
#elif defined(__AVX512BW__)
struct EntryLanes16 { __m512i lanes; };
#elif defined(__AVX2__)
struct EntryLanes16 { __m256i first, second; };
#else
#error "Metadata reconstruction requires AArch64 NEON or x86 AVX2"
#endif

namespace metadata_detail {
using ikea::integers::Bytes;

#if defined(__aarch64__)
IP_INLINE uint32x4_t direct_entries(uint32x4_t words) {
    return vorrq_u32(vandq_u32(words, vdupq_n_u32(511)),
                     vandq_u32(vshlq_n_u32(words, 1), vdupq_n_u32(0x3fff0000)));
}

IP_INLINE EntryLanes16 direct(const std::uint8_t* p) {
    return {direct_entries(vreinterpretq_u32_u8(vld1q_u8(p))),
            direct_entries(vreinterpretq_u32_u8(vld1q_u8(p + 16))),
            direct_entries(vreinterpretq_u32_u8(vld1q_u8(p + 32))),
            direct_entries(vreinterpretq_u32_u8(vld1q_u8(p + 48)))};
}

IP_INLINE uint16x8_t inclusive_prefix8(uint16x8_t values) {
    const auto zero = vdupq_n_u16(0);
    values = vaddq_u16(values, vextq_u16(zero, values, 7));
    values = vaddq_u16(values, vextq_u16(zero, values, 6));
    return vaddq_u16(values, vextq_u16(zero, values, 4));
}

IP_INLINE EntryLanes16 packed(Bytes<16> population_high, Bytes<16> population_low,
                             Bytes<16> lengths, unsigned checkpoint) {
    const auto length_first = vmovl_u8(vget_low_u8(lengths.v));
    const auto length_second = vmovl_high_u8(lengths.v);
    const auto prefix_first = inclusive_prefix8(length_first);
    const auto prefix_second = inclusive_prefix8(length_second);
    const auto base_first = vdupq_n_u16(checkpoint);
    const auto base_second = vaddq_u16(base_first, vdupq_laneq_u16(prefix_first, 7));
    const auto offset_first = vaddq_u16(vsubq_u16(prefix_first, length_first), base_first);
    const auto offset_second = vaddq_u16(vsubq_u16(prefix_second, length_second), base_second);
    const auto population_first = vorrq_u16(
        vshlq_n_u16(vmovl_u8(vget_low_u8(population_high.v)), 1),
        vmovl_u8(vget_low_u8(population_low.v)));
    const auto population_second = vorrq_u16(
        vshlq_n_u16(vmovl_high_u8(population_high.v), 1),
        vmovl_high_u8(population_low.v));
    return {vreinterpretq_u32_u16(vzip1q_u16(population_first, offset_first)),
            vreinterpretq_u32_u16(vzip2q_u16(population_first, offset_first)),
            vreinterpretq_u32_u16(vzip1q_u16(population_second, offset_second)),
            vreinterpretq_u32_u16(vzip2q_u16(population_second, offset_second))};
}
#elif defined(__AVX2__)
IP_INLINE EntryLanes16 direct(const std::uint8_t* p) {
#if defined(__AVX512BW__)
    const auto words = _mm512_loadu_si512(p);
    return {_mm512_or_si512(_mm512_and_si512(words, _mm512_set1_epi32(511)),
        _mm512_and_si512(_mm512_slli_epi32(words, 1), _mm512_set1_epi32(0x3fff0000)))};
#else
    const auto extract = [](__m256i words) {
        return _mm256_or_si256(_mm256_and_si256(words, _mm256_set1_epi32(511)),
            _mm256_and_si256(_mm256_slli_epi32(words, 1), _mm256_set1_epi32(0x3fff0000)));
    };
    return {extract(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p))),
            extract(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32)))};
#endif
}

IP_INLINE EntryLanes16 packed(Bytes<16> population_high, Bytes<16> population_low,
                             Bytes<16> lengths, unsigned checkpoint) {
    const auto length_words = _mm256_cvtepu8_epi16(lengths.v);
    auto prefix = _mm256_add_epi16(length_words, _mm256_slli_si256(length_words, 2));
    prefix = _mm256_add_epi16(prefix, _mm256_slli_si256(prefix, 4));
    prefix = _mm256_add_epi16(prefix, _mm256_slli_si256(prefix, 8));
    // Each 128-bit half has its own inclusive scan. Carry the first half's
    // final sum into all eight lanes of the second half, keeping the first zero.
    const auto half_carry = _mm256_shuffle_epi8(
        _mm256_permute2x128_si256(prefix, prefix, 0x08), _mm256_set1_epi16(0x0f0e));
    const auto offsets = _mm256_add_epi16(
        _mm256_add_epi16(_mm256_sub_epi16(prefix, length_words), half_carry),
        _mm256_set1_epi16(checkpoint));
    const auto populations = _mm256_or_si256(
        _mm256_slli_epi16(_mm256_cvtepu8_epi16(population_high.v), 1),
        _mm256_cvtepu8_epi16(population_low.v));
#if defined(__AVX512BW__)
    return {_mm512_or_si512(_mm512_cvtepu16_epi32(populations),
                            _mm512_slli_epi32(_mm512_cvtepu16_epi32(offsets), 16))};
#else
    const auto even_halves = _mm256_unpacklo_epi16(populations, offsets);
    const auto odd_halves = _mm256_unpackhi_epi16(populations, offsets);
    return {_mm256_permute2x128_si256(even_halves, odd_halves, 0x20),
            _mm256_permute2x128_si256(even_halves, odd_halves, 0x31)};
#endif
}
#endif
} // namespace metadata_detail

// Trusted admitted metadata only: capacity and group obey the chosen format,
// group is a multiple of 16 below capacity, and the allocation is exact-sized.
// Checkpoints plus decoded prefixes fit u16; no runtime admission happens here.
template<MetadataKind Kind>
IP_INLINE EntryLanes16 read_metadata16(const std::uint8_t* base, unsigned capacity,
                                      unsigned group) {
    using namespace ikea::integers;
    __builtin_assume(group % 16 == 0 && group < capacity && capacity <= 256);
    if constexpr(Kind == MetadataKind::scan128) __builtin_assume(capacity % 128 == 0);
    else __builtin_assume(capacity % 16 == 0);
    if constexpr(Kind == MetadataKind::direct32) {
        return metadata_detail::direct(base + group * 4);
    } else if constexpr(Kind == MetadataKind::local16) {
        const auto* packet = base + group * 2;
        return metadata_detail::packed(Bytes<16>::load(packet + 2),
            local_read16<1>(packet + 18, 0), local_read16<6>(packet + 20, 0),
            unsigned(read_bytes<2>(packet)));
    } else {
        static_assert(Kind == MetadataKind::scan128);
        const auto* population = base + capacity / 8 + (group / 16) * 18;
        const auto* lengths = base + capacity * 10 / 8;
        return metadata_detail::packed(Bytes<16>::load(population),
            local_read16<1>(population + 16, 0), scan_read16<6>(lengths, group),
            unsigned(read_bytes<2>(base + group / 8)));
    }
}

template<unsigned I>
IP_INLINE std::uint32_t metadata_entry(EntryLanes16 entries) {
    static_assert(I < 16);
#if defined(__aarch64__)
    if constexpr(I < 4) return vgetq_lane_u32(entries.first, I);
    else if constexpr(I < 8) return vgetq_lane_u32(entries.second, I - 4);
    else if constexpr(I < 12) return vgetq_lane_u32(entries.third, I - 8);
    else return vgetq_lane_u32(entries.fourth, I - 12);
#elif defined(__AVX512BW__)
    return std::uint32_t(_mm_extract_epi32(_mm512_extracti32x4_epi32(entries.lanes, I / 4), I % 4));
#elif defined(__AVX2__)
    const auto half = [&] {
        if constexpr(I < 8) return entries.first;
        else return entries.second;
    }();
    if constexpr(I % 8 < 4)
        return std::uint32_t(_mm_extract_epi32(_mm256_castsi256_si128(half), I % 4));
    else return std::uint32_t(_mm_extract_epi32(_mm256_extracti128_si256(half, 1), I % 4));
#endif
}

// The caller supplies lane < 16. Dynamic selection still consumes the native
// frame directly; it does not construct or index an array of decoded entries.
IP_INLINE std::uint32_t metadata_entry_at(EntryLanes16 entries, unsigned lane) {
    __builtin_assume(lane < 16);
#if defined(__aarch64__)
    const uint8x16x4_t table{{vreinterpretq_u8_u32(entries.first),
                             vreinterpretq_u8_u32(entries.second),
                             vreinterpretq_u8_u32(entries.third),
                             vreinterpretq_u8_u32(entries.fourth)}};
    const auto indices = vreinterpret_u8_u32(vdup_n_u32(lane * 0x04040404u + 0x03020100u));
    return vget_lane_u32(vreinterpret_u32_u8(vqtbl4_u8(table, indices)), 0);
#elif defined(__AVX512BW__)
    const auto selected = _mm512_permutexvar_epi32(_mm512_set1_epi32(lane), entries.lanes);
    return std::uint32_t(_mm_cvtsi128_si32(_mm512_castsi512_si128(selected)));
#elif defined(__AVX2__)
    const auto half = lane < 8 ? entries.first : entries.second;
    const auto selected = _mm256_permutevar8x32_epi32(half, _mm256_set1_epi32(lane));
    return std::uint32_t(_mm_cvtsi128_si32(_mm256_castsi256_si128(selected)));
#endif
}

// Explicit materialization for checks; runtime consumers use metadata_entry.
IP_INLINE void store_metadata16(std::uint32_t* out, EntryLanes16 entries) {
#if defined(__aarch64__)
    vst1q_u32(out, entries.first);
    vst1q_u32(out + 4, entries.second);
    vst1q_u32(out + 8, entries.third);
    vst1q_u32(out + 12, entries.fourth);
#elif defined(__AVX512BW__)
    _mm512_storeu_si512(out, entries.lanes);
#elif defined(__AVX2__)
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), entries.first);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + 8), entries.second);
#endif
}
} // namespace ikea::heterogeneous
