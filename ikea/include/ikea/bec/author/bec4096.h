#pragma once
// Bec4096 author surface: native block values for composing callers (the
// native namespace at the end, NEON and AVX-512 profiles), over the detail
// bodies that the ordinary API in <ikea/bec/bec4096.h> is compiled from.
#include <ikea/bec/bec4096.h>
#include <ikea/bec/author/native.h>
#include <ikea/bec/detail/scalar.h>
#include <ikea/bec/detail/write.h>
#if defined(IKEA_BEC256_AVX512) || defined(__aarch64__)
#define IKEA_BEC4096_NATIVE 1
#else
#define IKEA_BEC4096_NATIVE 0
#endif
// Native glue: Bec256's split, leaf and packing kernels unchanged, entered at
// an arbitrary bit within a 64-byte window. Only the root step differs from
// bec256::detail::{neon,avx512}::decode: the first field is read from a 16-bit
// window shifted by the starting bit, and the stream cursor starts there.
// Every later gather already addresses bits relative to that cursor.

namespace ikea::bec4096::detail::native {
#if IKEA_BEC4096_NATIVE
using ikea::bec256::native::block;
using ikea::bec256::native::pair;
#endif

[[gnu::always_inline]] inline unsigned root_left(unsigned first16, unsigned bit, unsigned population,
                                                 unsigned &width) {
    width = std::bit_width(std::min(population, 256 - population));
    return ((first16 >> bit) & ((1u << width) - 1)) + (population > 128 ? population - 128 : 0);
}

#if defined(IKEA_BEC256_AVX512)
namespace a = ikea::bec::detail::avx512;
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
    return a::lookup256(ikea::bec::detail::codes.value.data(),
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
    const auto *value = ikea::bec::detail::codes.value.data();
    auto low = _mm512_permutex2var_epi8(_mm512_load_si512(value), slot,
                                        _mm512_load_si512(value + 64));
    auto high = _mm512_permutex2var_epi8(_mm512_load_si512(value + 128), slot,
                                         _mm512_load_si512(value + 192));
    return _mm512_mask_blend_epi8(_mm512_movepi8_mask(slot), low, high);
}
[[gnu::always_inline]] inline block load_block(const std::uint8_t *bytes) {
    return _mm256_loadu_si256(reinterpret_cast<const __m256i *>(bytes));
}
[[gnu::always_inline]] inline void store_block(std::uint8_t *out, block value) {
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(out), value);
}
[[gnu::always_inline]] inline void store_pair(std::uint8_t *out, pair value) {
    _mm512_storeu_si512(out, value);
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
    [[gnu::always_inline]] explicit counted(const std::uint8_t *bytes)
        : bits(load_block(bytes)), counts(_mm256_popcnt_epi8(bits)) {}
    [[gnu::always_inline]] unsigned population() const {
        const auto sums = _mm256_sad_epu8(counts, _mm256_setzero_si256());
        const auto h = _mm_add_epi64(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
        return unsigned(_mm_cvtsi128_si64(h) + _mm_extract_epi64(h, 1));
    }
    template <class Sink> [[gnu::always_inline]] auto encode(unsigned c, Sink &&sink) const {
        return a::encode_counts(bits, counts, c, sink);
    }
};

#elif defined(__aarch64__)
namespace n = ikea::bec::detail::neon;
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
[[gnu::always_inline]] inline block load_block(const std::uint8_t *bytes) {
    return n::load256(bytes);
}
[[gnu::always_inline]] inline void store_block(std::uint8_t *out, block value) {
    n::store256(out, value);
}
[[gnu::always_inline]] inline void store_pair(std::uint8_t *out, pair value) {
    vst1q_u8_x4(out, value);
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
    [[gnu::always_inline]] explicit counted(const std::uint8_t *bytes)
        : bits(load_block(bytes)), b0(vcntq_u8(bits.val[0])), b1(vcntq_u8(bits.val[1])) {}
    [[gnu::always_inline]] unsigned population() const { return vaddlvq_u8(vaddq_u8(b0, b1)); }
    template <class Sink> [[gnu::always_inline]] auto encode(unsigned c, Sink &&sink) const {
        return n::encode_counts(bits, b0, b1, c, sink);
    }
};
#else
// Portable profile: byte-aligned copies of each stream feed Bec256's scalar
// codec. Correct everywhere; not tuned.
using block = std::array<std::uint8_t, 32>;
using pair = std::array<std::uint8_t, 64>;
inline block load_block(const std::uint8_t *bytes) {
    block result;
    std::memcpy(result.data(), bytes, 32);
    return result;
}
inline void store_block(std::uint8_t *out, const block &value) {
    std::memcpy(out, value.data(), 32);
}
inline void store_pair(std::uint8_t *out, const pair &value) { std::memcpy(out, value.data(), 64); }
inline void store_counts(std::uint8_t *out, const block &value) {
    std::memcpy(out, value.data(), 32);
}
// Stream bits of one block: its split field widths plus its byte rank widths.
inline unsigned stream_bits(const std::uint8_t *input, unsigned population) {
    std::array<unsigned, 64> tree{};
    for (unsigned i = 0; i < 32; ++i)
        tree[32 + i] = std::popcount(input[i]);
    for (unsigned i = 31; i; --i)
        tree[i] = tree[2 * i] + tree[2 * i + 1];
    tree[1] = population;
    unsigned bits = 0;
    for (unsigned level = 1, half = 128; level < 32; level *= 2, half /= 2)
        for (unsigned i = level; i < 2 * level; ++i)
            bits += std::bit_width(std::min(tree[i], 2 * half - tree[i]));
    for (unsigned i = 0; i < 32; ++i)
        bits += ikea::bec::detail::byte_width[tree[32 + i]];
    return bits;
}
struct counted {
    block bits;
    unsigned count = 0;
    explicit counted(const std::uint8_t *bytes) : bits(load_block(bytes)) {
        for (auto b : bits)
            count += std::popcount(b);
    }
    unsigned population() const { return count; }
    template <class Sink> unsigned encode(unsigned c, Sink &&sink) const {
        alignas(8) std::uint8_t stream[64] = {};
        ikea::bec::detail::encode_scalar(bits.data(), c, stream);
        std::memcpy(sink.words, stream, 64);
        return stream_bits(bits.data(), c);
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
        _mm512_storeu_si512(words, ikea::bec::detail::assemble(_mm512_loadu_si512(v), widths));
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
            minimum[m] = std::uint8_t(detail::min_bits[m]);
        }
    }
};
inline constexpr length_tables length_lut;
static_assert(detail::length_width[127] == detail::length_width[128]);
static_assert(*std::max_element(detail::min_bits.begin(), detail::min_bits.end()) < 256);
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
        ~__mmask32{1}, ikea::bec::detail::avx512::lanes([](unsigned j) { return j ? j - 1 : 0; }),
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
                               _mm256_set1_epi16(detail::min_bits[128]));
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
#elif defined(__aarch64__)
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
    mn = vbslq_u8(vceqq_u8(m, vdupq_n_u8(128)), vdupq_n_u8(std::uint8_t(detail::min_bits[128])), mn);
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
#else
    std::array<unsigned, 32> tree;
    for (unsigned k = 0; k < 16; ++k)
        tree[16 + k] = population[k];
    for (unsigned i = 15; i; --i)
        tree[i] = tree[2 * i] + tree[2 * i + 1];
    for (unsigned g = 0; g < 8; ++g) {
        std::uint64_t value = 0;
        unsigned used = 0;
        for (unsigned j = 0; j < 4; ++j) {
            const unsigned f = 4 * g + j;
            unsigned v = 0, w = 0;
            if (f < 15) {
                const unsigned i = f + 1, half = 2048u >> (std::bit_width(i) - 1);
                w = split_width(tree[i], half);
                v = tree[2 * i] - lower(tree[i], half);
            } else if (f > 15 && f < 31) {
                const unsigned k = f - 16, c = population[k];
                w = length_width[c];
                v = length[k] - min_bits[c];
            }
            value |= std::uint64_t{v} << used;
            used += w;
        }
        group[g] = value;
        group_width[g] = used;
    }
#endif
}
// Sums of populations and rank widths of bytes before byte j, and byte j's
// population, from a block's 32 byte counts.
struct before_byte {
    unsigned population, width, count;
};
#if IKEA_BEC4096_NATIVE
[[gnu::always_inline]] inline before_byte prefix_counts(block pops, unsigned j) {
#if defined(IKEA_BEC256_AVX512)
    const auto m = _mm256_cmplt_epu8_mask(
        _mm256_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                         21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
        _mm256_set1_epi8(char(j)));
    const auto widths = _mm256_shuffle_epi8(
        _mm256_broadcastsi128_si256(_mm_loadu_si128(
            reinterpret_cast<const __m128i *>(ikea::bec::detail::byte_width.data()))),
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
    const auto table = vld1q_u8(ikea::bec::detail::byte_width.data());
    const auto w0 = vqtbl1q_u8(table, pops.val[0]), w1 = vqtbl1q_u8(table, pops.val[1]);
    return {unsigned(vaddlvq_u8(vandq_u8(pops.val[0], m0)) + vaddlvq_u8(vandq_u8(pops.val[1], m1))),
            unsigned(vaddlvq_u8(vandq_u8(w0, m0)) + vaddlvq_u8(vandq_u8(w1, m1))),
            vgetq_lane_u8(vqtbl2q_u8(pops, at), 0)};
#endif
}
#endif
} // namespace ikea::bec4096::detail::native
// Inline bodies behind bec4096.h, visible to consumers that compose them.

namespace ikea::bec4096::detail {
using native::block;
using native::pair;
// The head and length table fit in 146 + 15*9 bits; 64 readable bytes cover
// every 64-bit field load. Short exact spans are copied to zeroed scratch.
// Scalar on purpose: NEON and AVX-512 versions of these four dependent levels
// (32-bit field windows, table lookups for the length widths) measured slower
// on both V2 and Zen 5; see README.md.
[[gnu::always_inline]] inline void read_frame_scalar(const std::uint8_t *w, unsigned population,
                                                     frame &out, unsigned offset = 0) {
    unsigned pos = 0;
    auto take = [&](unsigned width) {
        std::uint64_t word;
        std::memcpy(&word, w + pos / 8, 8);
        const unsigned value = unsigned(word >> (pos % 8)) & ((1u << width) - 1);
        pos += width;
        return value;
    };
    std::array<unsigned, 32> tree;
    tree[1] = population;
    for (unsigned i = 1; i < 16; ++i) {
        const unsigned half = 2048u >> (std::bit_width(i) - 1), p = tree[i];
        tree[2 * i] = take(split_width(p, half)) + lower(p, half);
        tree[2 * i + 1] = p - tree[2 * i];
    }
    unsigned table = 0;
    for (unsigned k = 0; k < 15; ++k)
        table += length_width[tree[16 + k]];
    unsigned start = offset + pos + table;
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned c = tree[16 + k];
        out.population[k] = std::uint16_t(c);
        out.start[k] = std::uint16_t(start);
        if (k < 15)
            start += min_bits[c] + take(length_width[c]);
    }
}

// Bytes before the head: the raw mask and 32 bytes per raw block, or nothing
// for an empty or full value, whose body has zero bytes.
[[gnu::always_inline]] inline unsigned prefix_bytes(unsigned population, unsigned raw) {
    return population == 0 || population == positions ? 0 : 2 + 32 * std::popcount(raw);
}
[[gnu::always_inline]] inline unsigned count32(const std::uint8_t *bytes) {
#if defined(__aarch64__)
    return vaddlvq_u8(vaddq_u8(vcntq_u8(vld1q_u8(bytes)), vcntq_u8(vld1q_u8(bytes + 16))));
#else
    unsigned result = 0;
    for (unsigned i = 0; i < 32; i += 8) {
        std::uint64_t word;
        std::memcpy(&word, bytes + i, 8);
        result += std::popcount(word);
    }
    return result;
#endif
}
// Raw blocks precede the head and are counted here; the head codes the
// remaining population, with zero at each raw block.
[[gnu::always_inline]] inline void read_frame(std::span<const byte> storage, unsigned bytes,
                                              unsigned population, frame &out) {
    const auto *body = reinterpret_cast<const std::uint8_t *>(storage.data());
    const unsigned raw = bytes >= 2 ? body[0] | unsigned(body[1]) << 8 : 0;
    const unsigned base = prefix_bytes(population, raw);
    std::array<std::uint16_t, 16> raw_count;
    unsigned raw_total = 0;
    for (unsigned m = raw, j = 0; m; m &= m - 1, ++j) {
        const unsigned k = std::countr_zero(m);
        raw_count[k] = std::uint16_t(count32(body + 2 + 32 * j));
        raw_total += raw_count[k];
    }
    alignas(64) std::uint8_t scratch[64];
    const std::uint8_t *w = body + base;
    if (storage.size() < base + 64) {
        std::memset(scratch, 0, 64);
        // Empty and full values have zero-byte bodies, often a default span.
        if (bytes > base)
            std::memcpy(scratch, w, std::min<unsigned>(bytes - base, 64));
        w = scratch;
    }
    read_frame_scalar(w, population - raw_total, out, 8 * base);
    for (unsigned m = raw, j = 0; m; m &= m - 1, ++j) {
        const unsigned k = std::countr_zero(m);
        out.population[k] = raw_count[k];
        out.start[k] = std::uint16_t(8 * (2 + 32 * j));
    }
    out.raw = std::uint16_t(raw);
}
[[gnu::always_inline]] inline bool is_raw(const frame &f, unsigned k) { return f.raw >> k & 1; }
[[gnu::always_inline]] inline const std::uint8_t *raw_block(const reader &input, unsigned k) {
    return reinterpret_cast<const std::uint8_t *>(input.storage().data()) + input.frame().start[k] / 8;
}

// A block stream starts at byte `at`, and needs a 64-byte window from there.
// A readable suffix makes every window direct; otherwise adapt the short tail.
#if defined(IKEA_BEC256_AVX512)
struct source {
    __m512i body;
    unsigned first;
};
[[gnu::always_inline]] inline source window(const reader &input, unsigned at) {
    const auto *p = input.storage().data() + at;
    const auto first = [&] {
        if (at + 2 <= input.storage().size()) {
            std::uint16_t v;
            std::memcpy(&v, p, 2);
            return unsigned(v);
        }
        return std::to_integer<unsigned>(*p);
    }();
    if (input.storage().size() >= at + 64)
        return {_mm512_loadu_si512(p), first};
    return {_mm512_maskz_loadu_epi8((std::uint64_t{1} << (input.bytes() - at)) - 1, p), first};
}
[[gnu::always_inline]] inline block decode_one(const reader &input, unsigned k) {
    const unsigned s = input.frame().start[k];
    const auto w = window(input, s / 8);
    return native::decode_at(w.body, w.first, s % 8, input.frame().population[k]);
}
// Blocks k of x and j of y: one reader or two, nonterminal.
[[gnu::always_inline]] inline pair decode_two(const reader &x, unsigned k, const reader &y,
                                              unsigned j) {
    const unsigned s = x.frame().start[k], t = y.frame().start[j];
    const auto a = window(x, s / 8), b = window(y, t / 8);
    return native::decode2_at(a.body, a.first, s % 8, x.frame().population[k], b.body, b.first,
                              t % 8, y.frame().population[j]);
}
[[gnu::always_inline]] inline __m256i tree_one(const reader &input, unsigned k, unsigned &pos) {
    const unsigned s = input.frame().start[k];
    const auto w = window(input, s / 8);
    return native::tree(w.body, w.first, s % 8, input.frame().population[k], pos);
}
#elif defined(__aarch64__)
[[gnu::always_inline]] inline const std::uint8_t *window(const reader &input, unsigned at,
                                                         std::uint8_t *scratch64) {
    const auto *p = reinterpret_cast<const std::uint8_t *>(input.storage().data()) + at;
    if (input.storage().size() >= at + 64)
        return p;
    std::memset(scratch64, 0, 64);
    std::memcpy(scratch64, p, input.bytes() - at);
    return scratch64;
}
[[gnu::always_inline]] inline unsigned first16(const std::uint8_t *w) {
    std::uint16_t v;
    std::memcpy(&v, w, 2);
    return v;
}
[[gnu::always_inline]] inline block decode_one(const reader &input, unsigned k) {
    alignas(64) std::uint8_t scratch[64];
    const unsigned s = input.frame().start[k];
    const auto *w = window(input, s / 8, scratch);
    return native::decode_at(w, first16(w), s % 8, input.frame().population[k]);
}
[[gnu::always_inline]] inline pair decode_two(const reader &x, unsigned k, const reader &y,
                                              unsigned j) {
    alignas(64) std::uint8_t sa[64], sb[64];
    const unsigned s = x.frame().start[k], t = y.frame().start[j];
    const auto *a = window(x, s / 8, sa), *b = window(y, t / 8, sb);
    return native::decode2_at(a, first16(a), s % 8, x.frame().population[k], b, first16(b),
                              t % 8, y.frame().population[j]);
}
[[gnu::always_inline]] inline block tree_one(const reader &input, unsigned k, unsigned &pos) {
    alignas(64) std::uint8_t scratch[64];
    const unsigned s = input.frame().start[k];
    const auto *w = window(input, s / 8, scratch);
    return native::tree(w, first16(w), s % 8, input.frame().population[k], pos);
}
#else
// The stream's bits from its start, realigned to byte 0 of a zeroed buffer.
inline void aligned_stream(const reader &input, unsigned k, std::uint8_t *out48) {
    const auto *body = reinterpret_cast<const std::uint8_t *>(input.storage().data());
    const unsigned start = input.frame().start[k], n = input.bytes();
    for (unsigned i = 0; i < 48; ++i) {
        const unsigned at = start / 8 + i, shift = start % 8;
        unsigned value = at < n ? body[at] >> shift : 0;
        if (shift && at + 1 < n)
            value |= unsigned(body[at + 1]) << (8 - shift);
        out48[i] = std::uint8_t(value);
    }
}
inline block decode_one(const reader &input, unsigned k) {
    std::uint8_t stream[48];
    aligned_stream(input, k, stream);
    block result;
    ikea::bec::detail::decode_scalar(stream, input.frame().population[k], result.data());
    return result;
}
inline pair decode_two(const reader &x, unsigned k, const reader &y, unsigned j) {
    pair result;
    native::store_block(result.data(), decode_one(x, k));
    native::store_block(result.data() + 32, decode_one(y, j));
    return result;
}
inline block tree_one(const reader &input, unsigned k, unsigned &pos) {
    auto counts = decode_one(input, k);
    for (auto &b : counts)
        b = std::uint8_t(std::popcount(b));
    pos = 0;
    return counts;
}
#endif

[[gnu::always_inline]] inline void fill(std::uint8_t *out, unsigned population) {
    std::memset(out, population ? 255 : 0, 32);
}
[[gnu::always_inline]] inline void decode_block(const reader &input, unsigned k,
                                                std::uint8_t *out) {
    const unsigned c = input.frame().population[k];
    if (is_raw(input.frame(), k))
        return void(std::memcpy(out, raw_block(input, k), 32));
    if (terminal(c))
        return fill(out, c);
    native::store_block(out, decode_one(input, k));
}
[[gnu::always_inline]] inline void decode_blocks(const reader &input, std::uint8_t *out) {
    for (unsigned k = 0; k < 16; k += 2) {
        const unsigned a = input.frame().population[k], b = input.frame().population[k + 1];
        if (!terminal(a) && !terminal(b) && !is_raw(input.frame(), k) &&
            !is_raw(input.frame(), k + 1))
            native::store_pair(out + 32 * k, decode_two(input, k, input, k + 1));
        else {
            decode_block(input, k, out + 32 * k);
            decode_block(input, k + 1, out + 32 * k + 32);
        }
    }
}
[[gnu::always_inline]] inline void block_counts(const reader &input, unsigned k,
                                                std::uint8_t *out) {
    const unsigned c = input.frame().population[k];
    if (is_raw(input.frame(), k)) {
#if defined(IKEA_BEC256_AVX512)
        native::store_counts(out, _mm256_popcnt_epi8(native::load_block(raw_block(input, k))));
#elif defined(__aarch64__)
        const auto bits = native::load_block(raw_block(input, k));
        native::store_counts(out, {{vcntq_u8(bits.val[0]), vcntq_u8(bits.val[1])}});
#else
        const auto *bytes = raw_block(input, k);
        for (unsigned i = 0; i < 32; ++i)
            out[i] = std::uint8_t(std::popcount(bytes[i]));
#endif
        return;
    }
    if (terminal(c))
        return void(std::memset(out, c ? 8 : 0, 32));
    unsigned pos;
    native::store_counts(out, tree_one(input, k, pos));
}
// Whole blocks from the frame; within a block, byte counts from its tree and
// at most one byte rank. No other rank fields are read.
[[gnu::always_inline]] inline unsigned rank(const reader &input, unsigned position) {
    const unsigned k = position / 256, within = position % 256;
    unsigned result = 0;
    for (unsigned i = 0; i < k; ++i)
        result += input.frame().population[i];
    if (!within)
        return result;
    const unsigned c = input.frame().population[k];
    if (is_raw(input.frame(), k))
        return result + bitset256::load(std::as_bytes(std::span<const std::uint8_t, 32>(
                                            raw_block(input, k), 32)))
                            .rank(within);
    if (terminal(c))
        return result + (c ? within : 0);
#if !IKEA_BEC4096_NATIVE
    bitset256 bits;
    const auto plain = decode_one(input, k);
    bits = bitset256::load(std::as_bytes(std::span<const std::uint8_t, 32>(plain)));
    return result + bits.rank(within);
#else
    unsigned pos;
    const auto counts = native::prefix_counts(tree_one(input, k, pos), within / 8);
    result += counts.population;
    if (const unsigned bit = within % 8) {
        // The tree cursor is relative to the window's first byte.
        const unsigned at = input.frame().start[k] / 8 * 8 + pos + counts.width;
        const auto *body = reinterpret_cast<const std::uint8_t *>(input.storage().data());
        const unsigned width = ikea::bec::detail::byte_width[counts.count];
        // Empty and full bytes have no rank field, possibly at the body's end.
        unsigned value = 0;
        if (width) {
            value = body[at / 8] >> (at % 8);
            if (at % 8 + width > 8)
                value |= unsigned(body[at / 8 + 1]) << (8 - at % 8);
            value &= (1u << width) - 1;
        }
        const unsigned pattern =
            ikea::bec::detail::codes.value[ikea::bec::detail::byte_base[counts.count] + value];
        result += std::popcount(pattern & ((1u << bit) - 1));
    }
    return result;
#endif
}

// Encoding: blocks are packed first, so the length table and exact size are
// known before any destination store.
// population is each block's count in the head (zero for raw blocks); bits
// counts the head, length table and streams after the raw prefix. words holds
// each coded block's packed stream, or a raw block's plain bytes, so emission
// never rereads the input.
struct plan {
    std::array<std::uint16_t, 16> population;
    std::array<std::uint16_t, 16> length;
    alignas(64) std::uint64_t words[16][8];
    unsigned bits, raw, prefix;
    unsigned bytes() const { return prefix + (bits + 7) / 8; }
};
[[gnu::always_inline]] inline unsigned prepare(const std::uint8_t *input, plan &out) {
    unsigned total = 0;
    out.raw = 0;
    for (unsigned k = 0; k < 16; ++k) {
        const native::counted block(input + 32 * k);
        const unsigned c = block.population();
        total += c;
        const unsigned length =
            terminal(c) ? 0 : block.encode(c, native::pack_sink{out.words[k]});
        // A stream longer than the block itself is stored raw instead.
        const bool raw = length > raw_bits;
        out.raw |= unsigned(raw) << k;
        if (raw)
            std::memcpy(out.words[k], input + 32 * k, 32);
        out.population[k] = std::uint16_t(raw ? 0 : c);
        out.length[k] = std::uint16_t(raw ? 0 : length);
    }
    out.prefix = prefix_bytes(total, out.raw);
    std::array<unsigned, 32> tree;
    for (unsigned k = 0; k < 16; ++k)
        tree[16 + k] = out.population[k];
    for (unsigned i = 15; i; --i)
        tree[i] = tree[2 * i] + tree[2 * i + 1];
    unsigned bits = 0;
    for (unsigned i = 1; i < 16; ++i)
        bits += split_width(tree[i], 2048u >> (std::bit_width(i) - 1));
    for (unsigned k = 0; k < 16; ++k)
        bits += (k < 15 ? length_width[out.population[k]] : 0) + out.length[k];
    out.bits = bits;
    return total;
}
// Exact emission: full words while all 64 bits belong to the body, then the
// final partial word's bytes. Field values are below 2^width.
struct bit_writer {
    std::uint8_t *out;
    unsigned at = 0, fill = 0;
    std::uint64_t acc = 0;
    [[gnu::always_inline]] void put(std::uint64_t value, unsigned width) {
        acc |= value << fill;
        fill += width;
        if (fill >= 64) {
            std::memcpy(out + at, &acc, 8);
            at += 8;
            fill -= 64;
            acc = value >> (width - fill);
        }
    }
    // A complete word: always stores. (v >> 1) >> (63 - fill) is v >> (64 - fill),
    // and zero when fill is zero.
    [[gnu::always_inline]] void put64(std::uint64_t value) {
        acc |= value << fill;
        std::memcpy(out + at, &acc, 8);
        at += 8;
        acc = (value >> 1) >> (63 - fill);
    }
    [[gnu::always_inline]] void append(const std::uint64_t *words, unsigned bits) {
        unsigned i = 0;
        for (; i < bits / 64; ++i)
            put64(words[i]);
        if (bits % 64)
            put(words[i] & ((std::uint64_t{1} << (bits % 64)) - 1), bits % 64);
    }
    [[gnu::always_inline]] void finish() {
        const unsigned n = (fill + 7) / 8;
        for (unsigned i = 0; i < n; ++i)
            out[at + i] = std::uint8_t(acc >> (8 * i));
    }
};
// The head and length table: thirty fields, independent given the counts,
// in eight groups of four.
[[gnu::always_inline]] inline void head_groups(const plan &in, std::uint64_t *group,
                                               std::uint64_t *group_width) {
    native::head_groups(in.population.data(), in.length.data(), group, group_width);
}
#if defined(IKEA_BEC256_AVX512)
// Exact emission straight into the destination. Each chunk (the head, then
// each block's assembled words) is shifted into place in one register; its
// first word merges the carry from the previous chunk's register, never a
// reload, and a byte-masked store keeps every write inside the body.
// BZHI reads only the low eight bits of its index, so clamp first.
[[gnu::always_inline]] inline std::uint64_t first_bytes(unsigned n) {
    return n >= 64 ? ~std::uint64_t{0} : _bzhi_u64(~std::uint64_t{0}, n);
}
[[gnu::always_inline]] inline void emit_coded(const plan &in, std::uint8_t *out) {
    const unsigned n = (in.bits + 7) / 8;
    alignas(64) std::uint64_t group[8], group_width[8];
    head_groups(in, group, group_width);
    const auto widths = _mm512_load_si512(group_width);
    auto last = ikea::bec::detail::assemble(_mm512_load_si512(group), widths);
    unsigned at = unsigned(_mm512_reduce_add_epi64(widths)), base = 0;
    _mm512_mask_storeu_epi8(out, first_bytes(n), last);
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned length = in.length[k];
        if (!length)
            continue;
        const unsigned j = at / 64;
        const auto v = _mm512_load_si512(in.words[k]);
        auto shifted = _mm512_shldv_epi64(v, _mm512_alignr_epi64(v, _mm512_setzero_si512(), 7),
                                          _mm512_set1_epi64(at % 64));
        const auto carry = _mm512_permutexvar_epi64(_mm512_set1_epi64(j - base), last);
        shifted = _mm512_mask_or_epi64(shifted, 1, shifted, carry);
        _mm512_mask_storeu_epi8(out + 8 * j, first_bytes(n - 8 * j), shifted);
        last = shifted;
        base = j;
        at += length;
    }
}
#else
// The eight head groups go through one bit chain into staging words. Each
// block's packed words then shift into place independently: only its first
// word merges with what precedes it. The exact body is copied out last.
[[gnu::always_inline]] inline void emit_coded(const plan &in, std::uint8_t *out) {
    alignas(64) std::uint64_t staging[max_bytes / 8 + 8];
    auto *bytes = reinterpret_cast<std::uint8_t *>(staging);
    bit_writer bits{bytes};
    alignas(64) std::uint64_t group[8], group_width[8];
    head_groups(in, group, group_width);
    for (unsigned g = 0; g < 8; ++g)
        bits.put(group[g], unsigned(group_width[g]));
    std::memcpy(bytes + bits.at, &bits.acc, 8);
    unsigned at = bits.at * 8 + bits.fill;
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned length = in.length[k];
        if (!length)
            continue;
        // A fixed seven words (a block is at most 374 bits): no data-dependent
        // trip count to mispredict. Words past the block are rewritten by the
        // next block or fall in staging slack; selects stand in for the
        // packed words the block never wrote.
        const unsigned j = at / 64, s = at % 64, m = (length + 63) / 64;
        const auto *w = in.words[k];
        std::uint64_t word[7];
        for (unsigned i = 0; i < 7; ++i)
            word[i] = i < m ? w[i] : 0;
        staging[j] |= word[0] << s;
        for (unsigned i = 1; i < 7; ++i)
            staging[j + i] = (word[i] << s) | ((word[i - 1] >> 1) >> (63 - s));
        at += length;
    }
    std::memcpy(out, bytes, (in.bits + 7) / 8);
}
#endif
// The raw prefix (mask, then each raw block's plain bytes), then the head,
// length table and streams from the next byte.
[[gnu::always_inline]] inline void emit(const plan &in, std::uint8_t *out) {
    if (in.prefix) {
        out[0] = std::uint8_t(in.raw);
        out[1] = std::uint8_t(in.raw >> 8);
        for (unsigned k = 0, j = 0; k < 16; ++k)
            if (in.raw >> k & 1)
                std::memcpy(out + 2 + 32 * j++, in.words[k], 32);
    }
    emit_coded(in, out + in.prefix);
}
} // namespace ikea::bec4096::detail
// Fused decode: the top four block levels of all sixteen streams advance
// together, densely packed in vector lanes (16, 32, 64, 128 nodes), instead of
// sixteen separate root-to-leaf chains that leave most lanes idle. Leaves and
// ranks then use Bec256's own per-block kernels.
//
// Lanes are stream-major: at a level with Active nodes per stream, stream s
// owns Active consecutive lanes. Each vector reads its streams' bytes from a
// 64-byte slot table holding the first 4*Active bytes of each stream's window
// (the bisection fields of the first four levels end by bit 7+8+14+24+40 = 93).
// Terminal blocks need no branch: every field width is zero.

namespace ikea::bec4096::detail::fused {
// Windows: direct when 64 readable bytes follow, else zero-padded scratch.
// Empty, full and raw blocks need no stream decoding; the fused levels see
// them as zero-width.
[[gnu::always_inline]] inline bool quiet(const frame &f, unsigned k) {
    return detail::terminal(f.population[k]) || detail::is_raw(f, k);
}
// Few nonterminal blocks leave the shared upper levels idle; decode them alone.
inline constexpr unsigned fuse_minimum = 5;
[[gnu::always_inline]] inline bool sparse(const frame &f) {
    unsigned live = 0;
    for (unsigned k = 0; k < 16; ++k)
        live += !quiet(f, k);
    return live < fuse_minimum;
}
[[gnu::always_inline]] inline void windows(const reader &input, std::uint8_t (&scratch)[16][64],
                                           const std::uint8_t *(&w)[16]) {
    const auto *base = reinterpret_cast<const std::uint8_t *>(input.storage().data());
    for (unsigned k = 0; k < 16; ++k) {
        const unsigned at = input.frame().start[k] / 8;
        if (input.storage().size() >= at + 64)
            w[k] = base + at;
        else {
            std::memset(scratch[k], 0, 64);
            if (input.bytes() > at)
                std::memcpy(scratch[k], base + at, input.bytes() - at);
            w[k] = scratch[k];
        }
    }
}
#if defined(__aarch64__) && !defined(IKEA_BEC256_AVX512)
namespace n = ikea::bec::detail::neon;

// Levels A to C for all sixteen streams; level D per stream on demand.
struct upper {
    alignas(64) std::uint8_t scratch[16][64];
    const std::uint8_t *w[16];
    uint8x16_t s[16];
    uint16x8_t pd[16], qd[16];
    [[gnu::always_inline]] explicit upper(const reader &input) {
        const auto &f = input.frame();
        windows(input, scratch, w);
        for (unsigned k = 0; k < 16; ++k)
            s[k] = vld1q_u8(w[k]);
        // Slot tables: 8-byte slots for eight streams, 4-byte slots for sixteen.
        uint8x16x4_t t4[2], t2;
        for (unsigned h = 0; h < 2; ++h)
            for (unsigned i = 0; i < 4; ++i)
                t4[h].val[i] = vreinterpretq_u8_u64(vzip1q_u64(vreinterpretq_u64_u8(s[8 * h + 2 * i]),
                                                               vreinterpretq_u64_u8(s[8 * h + 2 * i + 1])));
        for (unsigned i = 0; i < 4; ++i)
            t2.val[i] = vreinterpretq_u8_u32(vuzp1q_u32(vreinterpretq_u32_u8(t4[i / 2].val[2 * (i % 2)]),
                                                        vreinterpretq_u32_u8(t4[i / 2].val[2 * (i % 2) + 1])));
        const uint16x8_t lane{0, 1, 2, 3, 4, 5, 6, 7};
        // Level A: 128-position halves, one node per stream.
        uint16x8_t pa[2], qa[2];
        for (unsigned r = 0; r < 2; ++r) {
            // Raw blocks have no stream: zero-width in every level.
            const auto raw = vtstq_u16(vdupq_n_u16(std::uint16_t(f.raw >> 8 * r)),
                                       uint16x8_t{1, 2, 4, 8, 16, 32, 64, 128});
            pa[r] = vbicq_u16(vld1q_u16(f.population.data() + 8 * r), raw);
            qa[r] = vandq_u16(vld1q_u16(f.start.data() + 8 * r), vdupq_n_u16(7));
        }
        uint16x8_t pb[4], qb[4];
        for (unsigned r = 0; r < 2; ++r) {
            const auto slot = vshlq_n_u16(vaddq_u16(lane, vdupq_n_u16(8 * r)), 2);
            const auto next = n::level<128, 1>(t2, pa[r], slot, qa[r]);
            const auto dup = vzipq_u16(qa[r], qa[r]);
            pb[2 * r] = next.val[0], pb[2 * r + 1] = next.val[1];
            qb[2 * r] = dup.val[0], qb[2 * r + 1] = dup.val[1];
        }
        // Level B: two nodes per stream, four streams per vector.
        uint16x8_t pc[8], qc[8];
        for (unsigned r = 0; r < 4; ++r) {
            const auto slot = vshlq_n_u16(vaddq_u16(vshrq_n_u16(lane, 1), vdupq_n_u16(4 * r)), 2);
            const auto next = n::level<64, 2>(t2, pb[r], slot, qb[r]);
            const auto dup = vzipq_u16(qb[r], qb[r]);
            pc[2 * r] = next.val[0], pc[2 * r + 1] = next.val[1];
            qc[2 * r] = dup.val[0], qc[2 * r + 1] = dup.val[1];
        }
        // Level C: four nodes per stream, two streams per vector, 8-byte slots.
        for (unsigned r = 0; r < 8; ++r) {
            const auto slot =
                vshlq_n_u16(vaddq_u16(vshrq_n_u16(lane, 2), vdupq_n_u16(2 * (r % 4))), 3);
            const auto next = n::level<32, 4>(t4[r / 4], pc[r], slot, qc[r]);
            const auto dup = vzipq_u16(qc[r], qc[r]);
            pd[2 * r] = next.val[0], pd[2 * r + 1] = next.val[1];
            qd[2 * r] = dup.val[0], qd[2 * r + 1] = dup.val[1];
        }
    }
    // Level D: eight nodes per stream, one stream per vector, 16-byte slots
    // that are simply four streams' first bytes. Leaves the stream's cursor
    // in qd[k] lane 0 and returns its sixteen 16-position counts.
    [[gnu::always_inline]] uint16x8x2_t leaves(unsigned k) {
        const uint8x16x4_t t8{{s[k & ~3u], s[(k & ~3u) + 1], s[(k & ~3u) + 2], s[(k & ~3u) + 3]}};
        return n::level<16, 8>(t8, pd[k], vdupq_n_u16(16 * (k % 4)), qd[k]);
    }
};

[[gnu::always_inline]] inline void decode_all(const reader &input, std::uint8_t *out) {
    const auto &f = input.frame();
    if (sparse(f))
        return detail::decode_blocks(input, out);
    upper u(input);
    // Bec256's leaf kernel, two blocks at a time with joint register
    // constraints (as bec256's V2 decode2) so their chains interleave.
    for (unsigned k = 0; k < 16; k += 2) {
        if (!quiet(f, k) && !quiet(f, k + 1)) {
            auto pa16 = u.leaves(k), pb16 = u.leaves(k + 1);
            const auto body_a = vld1q_u8_x4(u.w[k]), body_b = vld1q_u8_x4(u.w[k + 1]);
            asm("" : "+w"(pa16.val[0]), "+w"(pa16.val[1]), "+w"(pb16.val[0]), "+w"(pb16.val[1]));
            unsigned ua, ub;
            const auto x = n::decode_leaves(body_a, pa16, vgetq_lane_u16(u.qd[k], 0), ua);
            const auto y = n::decode_leaves(body_b, pb16, vgetq_lane_u16(u.qd[k + 1], 0), ub);
            n::store256(out + 32 * k, x);
            n::store256(out + 32 * k + 32, y);
            continue;
        }
        for (unsigned j = k; j < k + 2; ++j) {
            if (quiet(f, j))
                detail::decode_block(input, j, out + 32 * j);
            else {
                unsigned used;
                const auto p16 = u.leaves(j);
                n::store256(out + 32 * j, n::decode_leaves(vld1q_u8_x4(u.w[j]), p16,
                                                          vgetq_lane_u16(u.qd[j], 0), used));
            }
        }
    }
}
// Byte populations of all 512 bytes: the whole count tree, no rank fields.
[[gnu::always_inline]] inline void counts_all(const reader &input, std::uint8_t *out) {
    const auto &f = input.frame();
    upper u(input);
    for (unsigned k = 0; k < 16; ++k) {
        if (quiet(f, k)) {
            detail::block_counts(input, k, out + 32 * k);
            continue;
        }
        const auto p16 = u.leaves(k);
        const auto body = vld1q_u8_x4(u.w[k]);
        unsigned pos = vgetq_lane_u16(u.qd[k], 0);
        const auto first = n::split<8>(body, p16.val[0], pos);
        const auto second = n::split<8>(body, p16.val[1], pos);
        vst1q_u8(out + 32 * k, vcombine_u8(vmovn_u16(first.val[0]), vmovn_u16(first.val[1])));
        vst1q_u8(out + 32 * k + 16, vcombine_u8(vmovn_u16(second.val[0]), vmovn_u16(second.val[1])));
    }
}
#elif defined(IKEA_BEC256_AVX512)
namespace a = ikea::bec::detail::avx512;
using a::lanes;
// Children of lanes 0..15 (low) or 16..31 (high), left before right.
template <unsigned High>
[[gnu::always_inline]] inline __m512i children(__m512i left, __m512i right) {
    return _mm512_permutex2var_epi16(
        left, lanes([](unsigned j) { return (j & 1 ? 32 : 0) + j / 2 + 16 * High; }), right);
}

// Counts: byte populations of all 512 bytes instead of bits, from the count
// tree alone. Otherwise the whole decode.
template <bool Counts>
[[gnu::always_inline]] inline void walk(const reader &input, void *destination) {
    const auto &f = input.frame();
    alignas(64) std::uint8_t scratch[16][64];
    const std::uint8_t *w[16];
    windows(input, scratch, w);
    // Slot tables: 32, 16, 8 and 4 bytes per stream, by permutes.
    __m512i t16[8], t8[4], t4[2];
    for (unsigned j = 0; j < 8; ++j)
        t16[j] = _mm512_inserti64x4(
            _mm512_castsi256_si512(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(w[2 * j]))),
            _mm256_loadu_si256(reinterpret_cast<const __m256i *>(w[2 * j + 1])), 1);
    for (unsigned i = 0; i < 4; ++i)
        t8[i] = _mm512_permutex2var_epi64(t16[2 * i], _mm512_setr_epi64(0, 1, 4, 5, 8, 9, 12, 13),
                                          t16[2 * i + 1]);
    for (unsigned h = 0; h < 2; ++h)
        t4[h] = _mm512_permutex2var_epi64(t8[2 * h], _mm512_setr_epi64(0, 2, 4, 6, 8, 10, 12, 14),
                                          t8[2 * h + 1]);
    const auto t2 = _mm512_permutex2var_epi32(
        t4[0], _mm512_setr_epi32(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30), t4[1]);
    const auto zero = _mm512_setzero_si512();
    // Level A: one node per stream, sixteen lanes.
    // Raw blocks have no stream: zero-width in every level.
    const auto parent = _mm512_inserti64x4(
        zero,
        _mm256_maskz_mov_epi16(__mmask16(~f.raw),
                               _mm256_loadu_si256(reinterpret_cast<const __m256i *>(f.population.data()))),
        0);
    auto pos = _mm512_inserti64x4(
        zero,
        _mm256_and_si256(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(f.start.data())),
                         _mm256_set1_epi16(7)),
        0);
    __m512i right;
    auto left = a::level<128, 1, 1>(t2, parent, lanes([](unsigned j) { return j < 16 ? 4 * j : 0; }),
                             pos, right);
    const auto pb = children<0>(left, right);
    auto qb = children<0>(pos, pos);
    // Level B: two nodes per stream, sixteen streams.
    left = a::level<64, 2, 2>(t2, pb, lanes([](unsigned j) { return 4 * (j / 2); }), qb, right);
    const __m512i pc[2]{children<0>(left, right), children<1>(left, right)};
    __m512i qc[2]{children<0>(qb, qb), children<1>(qb, qb)};
    // Level C: four nodes per stream, eight streams per vector.
    __m512i pd[4], qd[4];
    for (unsigned h = 0; h < 2; ++h) {
        left = a::level<32, 4, 4>(t4[h], pc[h], lanes([](unsigned j) { return 8 * (j / 4); }), qc[h],
                           right);
        pd[2 * h] = children<0>(left, right), pd[2 * h + 1] = children<1>(left, right);
        qd[2 * h] = children<0>(qc[h], qc[h]), qd[2 * h + 1] = children<1>(qc[h], qc[h]);
    }
    // Level D: eight nodes per stream, four streams per vector; then each
    // pair's leaves and ranks exactly as bec256's decode2.
    auto *out = static_cast<std::uint8_t *>(destination);
    auto *pops = static_cast<std::uint8_t *>(destination);
    const auto *value = ikea::bec::detail::codes.value.data();
    for (unsigned i = 0; i < 4; ++i) {
        left = a::level<16, 8, 8>(t8[i], pd[i], lanes([](unsigned j) { return 16 * (j / 8); }), qd[i],
                           right);
        const __m512i pe[2]{children<0>(left, right), children<1>(left, right)};
        __m512i qe[2]{children<0>(qd[i], qd[i]), children<1>(qd[i], qd[i])};
        for (unsigned half = 0; half < 2; ++half) {
            const unsigned p = 2 * i + half, x = 2 * p, y = x + 1;
            // Raw blocks decode as zero-width here and are copied after the walk.
            if (quiet(f, x) && quiet(f, y)) {
                for (unsigned z : {x, y})
                    if (!detail::is_raw(f, z)) {
                        if constexpr (Counts)
                            std::memset(pops + 32 * z, f.population[z] ? 8 : 0, 32);
                        else
                            std::memset(out + 32 * z, f.population[z] ? 255 : 0, 32);
                    }
                continue;
            }
            __m512i r;
            const auto l = a::split2<8>(t16[p], pe[half], qe[half], r);
            const auto l8 = _mm512_cvtepi16_epi8(l), r8 = _mm512_cvtepi16_epi8(r);
            const auto first = _mm256_unpacklo_epi8(l8, r8), second = _mm256_unpackhi_epi8(l8, r8);
            const auto pops_a = _mm256_permute2x128_si256(first, second, 0x20);
            const auto pops_b = _mm256_permute2x128_si256(first, second, 0x31);
            if constexpr (Counts) {
                _mm256_storeu_si256(reinterpret_cast<__m256i *>(pops + 32 * x), pops_a);
                _mm256_storeu_si256(reinterpret_cast<__m256i *>(pops + 32 * y), pops_b);
                continue;
            }
            const auto pos_a = _mm512_permutexvar_epi16(_mm512_setzero_si512(), qe[half]);
            const auto pos_b = _mm512_permutexvar_epi16(_mm512_set1_epi16(16), qe[half]);
            unsigned ua, ub;
            const auto slots_a = a::byte_slots(_mm512_loadu_si512(w[x]), pops_a, pos_a, ua);
            const auto slots_b = a::byte_slots(_mm512_loadu_si512(w[y]), pops_b, pos_b, ub);
            const auto slot = _mm512_inserti64x4(_mm512_castsi256_si512(slots_a), slots_b, 1);
            const auto low = _mm512_permutex2var_epi8(_mm512_load_si512(value), slot,
                                                      _mm512_load_si512(value + 64));
            const auto high = _mm512_permutex2var_epi8(_mm512_load_si512(value + 128), slot,
                                                       _mm512_load_si512(value + 192));
            _mm512_storeu_si512(out + 32 * x,
                                _mm512_mask_blend_epi8(_mm512_movepi8_mask(slot), low, high));
        }
    }
    for (unsigned raw = f.raw; raw; raw &= raw - 1) {
        const unsigned z = std::countr_zero(raw);
        if constexpr (Counts)
            detail::block_counts(input, z, pops + 32 * z);
        else
            detail::decode_block(input, z, out + 32 * z);
    }
}
[[gnu::always_inline]] inline void decode_all(const reader &input, std::uint8_t *out) {
    if (sparse(input.frame()))
        return detail::decode_blocks(input, out);
    walk<false>(input, out);
}
[[gnu::always_inline]] inline void counts_all(const reader &input, std::uint8_t *out) {
    walk<true>(input, out);
}
#else
inline void decode_all(const reader &input, std::uint8_t *out) {
    detail::decode_blocks(input, out);
}
inline void counts_all(const reader &input, std::uint8_t *out) {
    for (unsigned k = 0; k < 16; ++k)
        detail::block_counts(input, k, out + 32 * k);
}
#endif
} // namespace ikea::bec4096::detail::fused

#if IKEA_BEC4096_NATIVE
namespace ikea::bec4096::native {
// Blocks of an admitted Bec4096 reader as Bec256's native values, so the same
// Boolean, population and encoding bodies consume both variants. Stream
// addressing stays behind the reader; block < 16 is trusted. Reads use the
// reader's granted span (exact bodies adapt the tail as decode does) and may
// load neighbouring stream bytes without decoding them.
using ikea::bec256::native::block;
using ikea::bec256::native::pair;
[[gnu::always_inline]] inline block filled(std::uint8_t value) {
#if defined(IKEA_BEC256_AVX512)
    return _mm256_set1_epi8(char(value));
#else
    return {{vdupq_n_u8(value), vdupq_n_u8(value)}};
#endif
}
/// One block, including empty and full blocks.
[[gnu::always_inline]] inline block read(const reader &input, unsigned index) {
    const unsigned c = input.frame().population[index];
    if (detail::is_raw(input.frame(), index))
        return ikea::bec256::native::load(
            reinterpret_cast<const byte *>(detail::raw_block(input, index)));
    if (detail::terminal(c))
        return filled(c ? 255 : 0);
    return detail::decode_one(input, index);
}
/// Block i of x (low half) and block j of y (high half); one reader or two.
[[gnu::always_inline]] inline pair read_pair(const reader &x, unsigned i, const reader &y,
                                             unsigned j) {
    if (detail::terminal(x.frame().population[i]) || detail::terminal(y.frame().population[j]) ||
        detail::is_raw(x.frame(), i) || detail::is_raw(y.frame(), j))
        return ikea::bec256::native::join(read(x, i), read(y, j));
    return detail::decode_two(x, i, y, j);
}
/// One block's 32 byte populations, from its count tree alone.
[[gnu::always_inline]] inline block counts(const reader &input, unsigned index) {
    const unsigned c = input.frame().population[index];
    if (detail::is_raw(input.frame(), index)) {
        const auto bits = read(input, index);
#if defined(IKEA_BEC256_AVX512)
        return _mm256_popcnt_epi8(bits);
#else
        return {{vcntq_u8(bits.val[0]), vcntq_u8(bits.val[1])}};
#endif
    }
    if (detail::terminal(c))
        return filled(c ? 8 : 0);
    unsigned pos;
    return detail::tree_one(input, index, pos);
}
} // namespace ikea::bec4096::native
#endif
