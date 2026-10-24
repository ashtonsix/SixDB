#pragma once
// Native glue: Bec256's split, leaf and packing kernels unchanged, entered at
// an arbitrary bit within a 64-byte window. Only the root step differs from
// bec256::detail::{neon,avx512}::decode: the first field is read from a 16-bit
// window shifted by the starting bit, and the stream cursor starts there.
// Every later gather already addresses bits relative to that cursor.
#include "bec4096.h"
#include <ikea/bec256/author/native.h>

#if !defined(IKEA_BEC256_AVX512) && !defined(__aarch64__)
#error "Bec4096 spike needs the native Bec256 profile (NEON or AVX-512)"
#endif

namespace bec4096::native {
using ikea::bec256::native::block;
using ikea::bec256::native::pair;

[[gnu::always_inline]] inline unsigned root_left(unsigned first16, unsigned bit, unsigned population,
                                                 unsigned &width) {
    width = std::bit_width(std::min(population, 256 - population));
    return ((first16 >> bit) & ((1u << width) - 1)) + (population > 128 ? population - 128 : 0);
}

#if defined(IKEA_BEC256_AVX512)
namespace a = ikea::bec256::detail::avx512;
using window = __m512i;
// Root and four split levels, returning byte populations and the rank cursor.
[[gnu::always_inline]] inline __m256i tree(__m512i body, unsigned first16, unsigned bit,
                                           unsigned population, unsigned &pos) {
    unsigned n;
    const unsigned left0 = root_left(first16, bit, population, n);
    pos = bit + n;
    auto parent = _mm256_zextsi128_si256(_mm_cvtsi32_si128(left0 | ((population - left0) << 16)));
    __m256i right;
    auto left = a::split<64>(body, parent, pos, right);
    parent = a::interleave8(left, right);
    left = a::split<32>(body, parent, pos, right);
    parent = a::interleave8(left, right);
    left = a::split<16>(body, parent, pos, right);
    parent = a::interleave8(left, right);
    left = a::split<8>(body, parent, pos, right);
    auto l8 = _mm256_cvtepi16_epi8(left), r8 = _mm256_cvtepi16_epi8(right);
    return _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_unpacklo_epi8(l8, r8)),
                                   _mm_unpackhi_epi8(l8, r8), 1);
}
[[gnu::always_inline]] inline block decode_at(__m512i body, unsigned first16, unsigned bit,
                                              unsigned population) {
    unsigned pos, used;
    const auto pops = tree(body, first16, bit, population, pos);
    return a::lookup256(ikea::bec256::detail::codes.value.data(),
                        a::byte_slots(body, pops, _mm512_set1_epi16(pos), used));
}
// Two independent streams, as bec256's decode2_loaded, with starting bits.
[[gnu::always_inline]] inline pair decode2_at(__m512i body_a, unsigned first_a, unsigned bit_a,
                                              unsigned pop_a, __m512i body_b, unsigned first_b,
                                              unsigned bit_b, unsigned pop_b) {
    auto tree2 = _mm512_inserti64x4(body_a, _mm512_castsi512_si256(body_b), 1);
    unsigned na, nb;
    const unsigned la = root_left(first_a, bit_a, pop_a, na);
    const unsigned lb = root_left(first_b, bit_b, pop_b, nb);
    auto parent = _mm512_maskz_set1_epi32(1, la | ((pop_a - la) << 16));
    parent = _mm512_mask_set1_epi32(parent, 1 << 8, lb | ((pop_b - lb) << 16));
    auto pos = _mm512_mask_set1_epi16(_mm512_set1_epi16(bit_a + na), 0xffff0000, bit_b + nb);
    __m512i right;
    auto left = a::split2<64>(tree2, parent, pos, right);
    parent = a::interleave8x2(left, right);
    left = a::split2<32>(tree2, parent, pos, right);
    parent = a::interleave8x2(left, right);
    left = a::split2<16>(tree2, parent, pos, right);
    parent = a::interleave8x2(left, right);
    left = a::split2<8>(tree2, parent, pos, right);
    auto l8 = _mm512_cvtepi16_epi8(left), r8 = _mm512_cvtepi16_epi8(right);
    auto first = _mm256_unpacklo_epi8(l8, r8), second = _mm256_unpackhi_epi8(l8, r8);
    auto pops_a = _mm256_permute2x128_si256(first, second, 0x20);
    auto pops_b = _mm256_permute2x128_si256(first, second, 0x31);
    auto pos_a = _mm512_permutexvar_epi16(_mm512_setzero_si512(), pos);
    auto pos_b = _mm512_permutexvar_epi16(_mm512_set1_epi16(16), pos);
    unsigned used_a, used_b;
    auto slots_a = a::byte_slots(body_a, pops_a, pos_a, used_a),
         slots_b = a::byte_slots(body_b, pops_b, pos_b, used_b);
    auto slot = _mm512_inserti64x4(_mm512_castsi256_si512(slots_a), slots_b, 1);
    const auto *value = ikea::bec256::detail::codes.value.data();
    auto low = _mm512_permutex2var_epi8(_mm512_load_si512(value), slot,
                                        _mm512_load_si512(value + 64));
    auto high = _mm512_permutex2var_epi8(_mm512_load_si512(value + 128), slot,
                                         _mm512_load_si512(value + 192));
    return _mm512_mask_blend_epi8(_mm512_movepi8_mask(slot), low, high);
}
[[gnu::always_inline]] inline block load_block(const std::uint64_t *words) {
    return _mm256_loadu_si256(reinterpret_cast<const __m256i *>(words));
}
[[gnu::always_inline]] inline void store_block(std::uint64_t *words, block value) {
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(words), value);
}
[[gnu::always_inline]] inline void store_pair(std::uint64_t *words, pair value) {
    _mm512_storeu_si512(words, value);
}
[[gnu::always_inline]] inline void store_counts(std::uint8_t *out, __m256i pops) {
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(out), pops);
}
template <class Sink>
[[gnu::always_inline]] inline auto encode_to(block bits, unsigned population, Sink &&sink) {
    return a::encode_to(bits, population, sink);
}
// A block with its byte counts, computed once for population and encoding.
struct counted {
    block bits;
    __m256i counts;
    [[gnu::always_inline]] explicit counted(const std::uint64_t *words)
        : bits(load_block(words)), counts(_mm256_popcnt_epi8(bits)) {}
    [[gnu::always_inline]] unsigned population() const {
        const auto sums = _mm256_sad_epu8(counts, _mm256_setzero_si256());
        const auto h = _mm_add_epi64(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
        return unsigned(_mm_cvtsi128_si64(h) + _mm_extract_epi64(h, 1));
    }
    template <class Sink> [[gnu::always_inline]] auto encode(unsigned c, Sink &&sink) const {
        return a::encode_counts(bits, counts, c, sink);
    }
};

#else
namespace n = ikea::bec256::detail::neon;
using window = const std::uint8_t *;
[[gnu::always_inline]] inline uint16x8x2_t upper(uint8x16x4_t body, unsigned first16,
                                                 unsigned bit, unsigned population,
                                                 unsigned &pos) {
    unsigned width;
    const unsigned left = root_left(first16, bit, population, width);
    pos = bit + width;
    auto p2 = vsetq_lane_u16(left, vdupq_n_u16(0), 0);
    p2 = vsetq_lane_u16(population - left, p2, 1);
    auto p4 = n::split<64>(body, p2, pos).val[0];
    auto p8 = n::split<32>(body, p4, pos).val[0];
    return n::split<16>(body, p8, pos);
}
[[gnu::always_inline]] inline block decode_at(const std::uint8_t *w, unsigned first16, unsigned bit,
                                              unsigned population) {
    const auto body = vld1q_u8_x4(w);
    unsigned pos, used;
    const auto p16 = upper(body, first16, bit, population, pos);
    return n::decode_leaves(body, p16, pos, used);
}
[[gnu::always_inline]] inline block tree(const std::uint8_t *w, unsigned first16, unsigned bit,
                                         unsigned population, unsigned &pos) {
    const auto body = vld1q_u8_x4(w);
    const auto p16 = upper(body, first16, bit, population, pos);
    auto first = n::split<8>(body, p16.val[0], pos);
    auto second = n::split<8>(body, p16.val[1], pos);
    return {{vcombine_u8(vmovn_u16(first.val[0]), vmovn_u16(first.val[1])),
             vcombine_u8(vmovn_u16(second.val[0]), vmovn_u16(second.val[1]))}};
}
[[gnu::always_inline]] inline pair decode2_at(const std::uint8_t *a, unsigned first_a,
                                              unsigned bit_a, unsigned pop_a,
                                              const std::uint8_t *b, unsigned first_b,
                                              unsigned bit_b, unsigned pop_b) {
#if SIXDB_TUNE_NEOVERSE_V2
    const auto body_a = vld1q_u8_x4(a), body_b = vld1q_u8_x4(b);
    unsigned na, nb;
    const unsigned la = root_left(first_a, bit_a, pop_a, na);
    const unsigned lb = root_left(first_b, bit_b, pop_b, nb);
    unsigned pos_a = bit_a + na, pos_b = bit_b + nb;
    auto a2 = vsetq_lane_u16(pop_a - la, vsetq_lane_u16(la, vdupq_n_u16(0), 0), 1);
    auto b2 = vsetq_lane_u16(pop_b - lb, vsetq_lane_u16(lb, vdupq_n_u16(0), 0), 1);
    auto a4 = n::split<64>(body_a, a2, pos_a).val[0];
    auto b4 = n::split<64>(body_b, b2, pos_b).val[0];
    // Same V2 scheduling constraint as bec256's decode2.
    asm("" : "+w"(a4), "+w"(b4));
    auto a8 = n::split<32>(body_a, a4, pos_a).val[0];
    auto b8 = n::split<32>(body_b, b4, pos_b).val[0];
    asm("" : "+w"(a8), "+w"(b8));
    auto a16 = n::split<16>(body_a, a8, pos_a), b16 = n::split<16>(body_b, b8, pos_b);
    asm("" : "+w"(a16.val[0]), "+w"(a16.val[1]), "+w"(b16.val[0]), "+w"(b16.val[1]));
    unsigned used_a, used_b;
    const auto x = n::decode_leaves(body_a, a16, pos_a, used_a);
    const auto y = n::decode_leaves(body_b, b16, pos_b, used_b);
    return {{x.val[0], x.val[1], y.val[0], y.val[1]}};
#else
    const auto x = decode_at(a, first_a, bit_a, pop_a), y = decode_at(b, first_b, bit_b, pop_b);
    return {{x.val[0], x.val[1], y.val[0], y.val[1]}};
#endif
}
[[gnu::always_inline]] inline block load_block(const std::uint64_t *words) {
    return n::load256(words);
}
[[gnu::always_inline]] inline void store_block(std::uint64_t *words, block value) {
    n::store256(words, value);
}
[[gnu::always_inline]] inline void store_pair(std::uint64_t *words, pair value) {
    vst1q_u8_x4(reinterpret_cast<std::uint8_t *>(words), value);
}
[[gnu::always_inline]] inline void store_counts(std::uint8_t *out, block pops) {
    vst1q_u8(out, pops.val[0]);
    vst1q_u8(out + 16, pops.val[1]);
}
template <class Sink>
[[gnu::always_inline]] inline auto encode_to(block bits, unsigned population, Sink &&sink) {
    return n::encode_to(bits, population, sink);
}
struct counted {
    block bits;
    uint8x16_t b0, b1;
    [[gnu::always_inline]] explicit counted(const std::uint64_t *words)
        : bits(load_block(words)), b0(vcntq_u8(bits.val[0])), b1(vcntq_u8(bits.val[1])) {}
    [[gnu::always_inline]] unsigned population() const { return vaddlvq_u8(vaddq_u8(b0, b1)); }
    template <class Sink> [[gnu::always_inline]] auto encode(unsigned c, Sink &&sink) const {
        return n::encode_counts(bits, b0, b1, c, sink);
    }
};

#endif

// Packs one block's eight (value, width) groups into its own words with
// Bec256's packer; returns stream bits. Blocks pack independently, so their
// dependency chains overlap; only the final word-wise append is serial.
struct pack_sink {
    std::uint64_t *words; // 8 words; NEON writes only the stream's own words
    [[gnu::always_inline]] unsigned operator()(const std::uint64_t *v,
                                               const std::uint64_t *w) const {
#if defined(IKEA_BEC256_AVX512)
        const auto widths = _mm512_loadu_si512(w);
        _mm512_storeu_si512(words, ikea::bec256::detail::assemble(_mm512_loadu_si512(v), widths));
        return unsigned(_mm512_reduce_add_epi64(widths));
#else
        // Aligned word stores in a register chain; Bec256's overlapping-store
        // stitch would make the word-sized reloads in emit store-forwarding stalls.
        std::uint64_t acc = 0;
        unsigned fill = 0, n = 0;
        for (unsigned g = 0; g < 8; ++g) {
            const unsigned width = unsigned(w[g]), t = fill + width;
            acc |= v[g] << fill;
            words[n] = acc;
            const bool full = t >= 64;
            n += full;
            fill = t & 63;
            acc = full ? v[g] >> (width - fill) : acc;
        }
        words[n] = acc;
        return 64 * n + fill;
#endif
    }
    [[gnu::always_inline]] unsigned singleton(std::uint8_t v) const {
#if defined(IKEA_BEC256_AVX512)
        // Emission shifts all eight words, so the unused ones must be zero.
        _mm512_storeu_si512(words, _mm512_maskz_set1_epi64(1, v));
#else
        words[0] = v;
#endif
        return 8;
    }
};
// Head and length-table fields, computed in vectors and combined four at a
// time into eight groups of at most 48 bits. Head field f is bisection node
// f+1 (breadth-first); its region and left half are sums of block counts,
// read from an exclusive prefix at fixed lanes. Lane 15 of each half is empty.
struct head_lanes {
    std::array<std::uint16_t, 16> begin{}, middle{}, end{}, half{};
    consteval head_lanes() {
        for (unsigned f = 0; f < 15; ++f) {
            const unsigned i = f + 1, depth = std::bit_width(i) - 1, size = 16u >> depth;
            begin[f] = std::uint16_t((i - (1u << depth)) * size);
            middle[f] = std::uint16_t(begin[f] + size / 2);
            end[f] = std::uint16_t(begin[f] + size);
            half[f] = std::uint16_t(2048u >> depth);
        }
        half[15] = 1;
    }
};
inline constexpr head_lanes head_lut;
// Length widths and minimum stream bits are symmetric in population; both
// depend on min(c, 256-c) <= 128. Widths agree at 127 and 128; minima do not.
struct length_tables {
    alignas(64) std::array<std::uint8_t, 128> width{}, minimum{};
    consteval length_tables() {
        for (unsigned m = 0; m < 128; ++m) {
            width[m] = detail::length_width[m];
            minimum[m] = std::uint8_t(detail::bounds.min[m]);
        }
    }
};
inline constexpr length_tables length_lut;
static_assert(detail::length_width[127] == detail::length_width[128]);
static_assert(*std::max_element(detail::bounds.min.begin(), detail::bounds.min.end()) < 256);
[[gnu::always_inline]] inline void head_groups(const std::uint16_t *population,
                                               const std::uint16_t *length, std::uint64_t *group,
                                               std::uint64_t *group_width) {
#if defined(IKEA_BEC256_AVX512)
    const auto c = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(population));
    const auto inclusive = _mm512_zextsi256_si512(a::prefix16(c));
    const auto load16 = [](const std::array<std::uint16_t, 16> &t) {
        return _mm512_zextsi256_si512(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(t.data())));
    };
    // Exclusive prefix: lane j holds the sum of blocks before j (lane 16: all).
    const auto before = _mm512_maskz_permutexvar_epi16(
        ~__mmask32{1}, ikea::bec256::detail::avx512::lanes([](unsigned j) { return j ? j - 1 : 0; }),
        inclusive);
    const auto base = _mm512_permutexvar_epi16(load16(head_lut.begin), before);
    const auto parent = _mm512_sub_epi16(_mm512_permutexvar_epi16(load16(head_lut.end), before), base);
    const auto left = _mm512_sub_epi16(_mm512_permutexvar_epi16(load16(head_lut.middle), before), base);
    const auto half = load16(head_lut.half);
    const auto hv = _mm512_sub_epi16(left, _mm512_subs_epu16(parent, half));
    const auto k = _mm512_min_epu16(parent, _mm512_sub_epi16(_mm512_add_epi16(half, half), parent));
    const auto hw = _mm512_cvtepi32_epi16(_mm512_sub_epi32(
        _mm512_set1_epi32(32), _mm512_lzcnt_epi32(_mm512_cvtepu16_epi32(_mm512_castsi512_si256(k)))));
    const auto m = _mm256_min_epu16(c, _mm256_sub_epi16(_mm256_set1_epi16(256), c));
    const auto index = _mm512_zextsi128_si512(_mm256_cvtepi16_epi8(_mm256_min_epu16(m, _mm256_set1_epi16(127))));
    const auto lookup = [&](const std::uint8_t *table) {
        return _mm256_cvtepu8_epi16(_mm512_castsi512_si128(_mm512_permutex2var_epi8(
            _mm512_load_si512(table), index, _mm512_load_si512(table + 64))));
    };
    const auto lw = _mm256_maskz_mov_epi16(0x7fff, lookup(length_lut.width.data()));
    auto mn = lookup(length_lut.minimum.data());
    mn = _mm256_mask_mov_epi16(mn, _mm256_cmpeq_epi16_mask(m, _mm256_set1_epi16(128)),
                               _mm256_set1_epi16(detail::bounds.min[128]));
    const auto lv = _mm256_maskz_sub_epi16(
        0x7fff, _mm256_loadu_si256(reinterpret_cast<const __m256i *>(length)), mn);
    const auto v = _mm512_inserti64x4(hv, lv, 1);
    const auto n = _mm512_inserti64x4(_mm512_zextsi256_si512(_mm256_maskz_mov_epi16(0x7fff, hw)), lw, 1);
    const auto ne = _mm512_and_si512(n, _mm512_set1_epi32(0xffff));
    const auto v2 = _mm512_or_si512(_mm512_and_si512(v, _mm512_set1_epi32(0xffff)),
                                    _mm512_sllv_epi32(_mm512_srli_epi32(v, 16), ne));
    const auto n2 = _mm512_add_epi32(ne, _mm512_srli_epi32(n, 16));
    const auto ne2 = _mm512_and_si512(n2, _mm512_set1_epi64(0xffffffff));
    _mm512_storeu_si512(group, _mm512_or_si512(_mm512_and_si512(v2, _mm512_set1_epi64(0xffffffff)),
                                               _mm512_sllv_epi64(_mm512_srli_epi64(v2, 32), ne2)));
    _mm512_storeu_si512(group_width, _mm512_add_epi64(ne2, _mm512_srli_epi64(n2, 32)));
#else
    const auto c0 = vld1q_u16(population), c1 = vld1q_u16(population + 8);
    const auto s0 = n::prefix8(c0);
    const auto s1 = vaddq_u16(n::prefix8(c1), vdupq_laneq_u16(s0, 7));
    const auto z = vdupq_n_u16(0);
    // Exclusive prefix as a byte table: u16 lane j holds the blocks before j.
    const uint8x16x3_t before{{vreinterpretq_u8_u16(vextq_u16(z, s0, 7)),
                               vreinterpretq_u8_u16(vextq_u16(s0, s1, 7)),
                               vreinterpretq_u8_u16(vextq_u16(s1, s1, 7))}};
    const auto gather = [&](const std::array<std::uint16_t, 16> &t, unsigned h) {
        const auto i = vmovn_u16(vld1q_u16(t.data() + 8 * h));
        const auto twice = vshl_n_u8(i, 1);
        const auto pairs = vzip_u8(twice, vadd_u8(twice, vdup_n_u8(1)));
        return vreinterpretq_u16_u8(vqtbl3q_u8(before, vcombine_u8(pairs.val[0], pairs.val[1])));
    };
    uint16x8_t value[4], width[4];
    for (unsigned h = 0; h < 2; ++h) {
        const auto base = gather(head_lut.begin, h);
        const auto parent = vsubq_u16(gather(head_lut.end, h), base);
        const auto left = vsubq_u16(gather(head_lut.middle, h), base);
        const auto half = vld1q_u16(head_lut.half.data() + 8 * h);
        value[h] = vsubq_u16(left, vqsubq_u16(parent, half));
        const auto k = vminq_u16(parent, vsubq_u16(vaddq_u16(half, half), parent));
        width[h] = vsubq_u16(vdupq_n_u16(16), vclzq_u16(k));
    }
    const auto m0 = vminq_u16(c0, vsubq_u16(vdupq_n_u16(256), c0));
    const auto m1 = vminq_u16(c1, vsubq_u16(vdupq_n_u16(256), c1));
    const auto m = vcombine_u8(vmovn_u16(m0), vmovn_u16(m1));
    const auto clamped = vminq_u8(m, vdupq_n_u8(127));
    const auto lookup = [&](const std::uint8_t *table) {
        return vorrq_u8(vqtbl4q_u8(vld1q_u8_x4(table), clamped),
                        vqtbl4q_u8(vld1q_u8_x4(table + 64), vsubq_u8(clamped, vdupq_n_u8(64))));
    };
    const auto lw = vsetq_lane_u8(0, lookup(length_lut.width.data()), 15);
    auto mn = lookup(length_lut.minimum.data());
    mn = vbslq_u8(vceqq_u8(m, vdupq_n_u8(128)), vdupq_n_u8(std::uint8_t(detail::bounds.min[128])), mn);
    const auto l0 = vld1q_u16(length), l1 = vsetq_lane_u16(0, vld1q_u16(length + 8), 7);
    const auto mn1 = vsetq_lane_u16(0, vmovl_u8(vget_high_u8(mn)), 7);
    value[2] = vsubq_u16(l0, vmovl_u8(vget_low_u8(mn)));
    value[3] = vsubq_u16(l1, mn1);
    width[2] = vmovl_u8(vget_low_u8(lw));
    width[3] = vmovl_u8(vget_high_u8(lw));
    for (unsigned r = 0; r < 4; ++r) {
        const auto v = vreinterpretq_u32_u16(value[r]), w = vreinterpretq_u32_u16(width[r]);
        const auto ne = vandq_u32(w, vdupq_n_u32(0xffff));
        const auto v2 = vorrq_u32(vandq_u32(v, vdupq_n_u32(0xffff)),
                                  vshlq_u32(vshrq_n_u32(v, 16), vreinterpretq_s32_u32(ne)));
        const auto n2 = vaddq_u32(ne, vshrq_n_u32(w, 16));
        const auto v64 = vreinterpretq_u64_u32(v2), n64 = vreinterpretq_u64_u32(n2);
        const auto ne2 = vandq_u64(n64, vdupq_n_u64(0xffffffff));
        vst1q_u64(group + 2 * r,
                  vorrq_u64(vandq_u64(v64, vdupq_n_u64(0xffffffff)),
                            vshlq_u64(vshrq_n_u64(v64, 32), vreinterpretq_s64_u64(ne2))));
        vst1q_u64(group_width + 2 * r, vaddq_u64(ne2, vshrq_n_u64(n64, 32)));
    }
#endif
}
// Sums of populations and rank widths of bytes before byte j, and byte j's
// population, from a block's 32 byte counts.
struct before_byte {
    unsigned population, width, count;
};
[[gnu::always_inline]] inline before_byte prefix_counts(block pops, unsigned j) {
#if defined(IKEA_BEC256_AVX512)
    const auto m = _mm256_cmplt_epu8_mask(
        _mm256_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                         21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
        _mm256_set1_epi8(char(j)));
    const auto widths = _mm256_shuffle_epi8(
        _mm256_broadcastsi128_si256(_mm_loadu_si128(
            reinterpret_cast<const __m128i *>(ikea::bec256::detail::byte_width.data()))),
        pops);
    const auto sum = [](__m256i x) {
        const auto s = _mm256_sad_epu8(x, _mm256_setzero_si256());
        const auto h = _mm_add_epi64(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
        return unsigned(_mm_cvtsi128_si64(h) + _mm_extract_epi64(h, 1));
    };
    return {sum(_mm256_maskz_mov_epi8(m, pops)), sum(_mm256_maskz_mov_epi8(m, widths)),
            unsigned(_mm_extract_epi8(
                _mm256_castsi256_si128(_mm256_permutexvar_epi8(_mm256_set1_epi8(char(j)), pops)), 0))};
#else
    const auto at = vdupq_n_u8(std::uint8_t(j));
    const uint8x16_t i0{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const auto m0 = vcltq_u8(i0, at), m1 = vcltq_u8(vaddq_u8(i0, vdupq_n_u8(16)), at);
    const auto table = vld1q_u8(ikea::bec256::detail::byte_width.data());
    const auto w0 = vqtbl1q_u8(table, pops.val[0]), w1 = vqtbl1q_u8(table, pops.val[1]);
    return {unsigned(vaddlvq_u8(vandq_u8(pops.val[0], m0)) + vaddlvq_u8(vandq_u8(pops.val[1], m1))),
            unsigned(vaddlvq_u8(vandq_u8(w0, m0)) + vaddlvq_u8(vandq_u8(w1, m1))),
            vgetq_lane_u8(vqtbl2q_u8(pops, at), 0)};
#endif
}
} // namespace bec4096::native
