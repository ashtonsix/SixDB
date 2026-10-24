#pragma once
#include <ikea/bec256/detail/tables.h>
#include <ikea/bec256/detail/pack.h>
#include <ikea/bec256/detail/native/assemble.h>
#include <immintrin.h>
#include <algorithm>
#include <array>
#include <bit>

namespace ikea::bec256::detail::avx512 {
// Execution width does not prescribe a storage type or a number of children.
inline __m512i intersection512(__m512i a, __m512i b) { return _mm512_and_si512(a, b); }
inline __m512i union512(__m512i a, __m512i b) { return _mm512_or_si512(a, b); }
inline __m512i load512(const void *p) { return _mm512_loadu_si512(p); }
inline void store512(void *p, __m512i bits) { _mm512_storeu_si512(p, bits); }
inline __attribute__((always_inline)) __m256i lookup256(const std::uint8_t *lut, __m256i indices) {
    auto index = _mm512_zextsi256_si512(indices);
    auto a = _mm512_permutex2var_epi8(_mm512_loadu_si512(lut), index, _mm512_loadu_si512(lut + 64));
    auto b = _mm512_permutex2var_epi8(_mm512_loadu_si512(lut + 128), index,
                                      _mm512_loadu_si512(lut + 192));
    return _mm512_castsi512_si256(_mm512_mask_blend_epi8(_mm512_movepi8_mask(index), a, b));
}
inline __attribute__((always_inline)) __m256i log128(__m256i indices) {
    return _mm512_castsi512_si256(
        _mm512_permutex2var_epi8(_mm512_load_si512(::ikea::bec256::detail::log_width.data()),
                                 _mm512_zextsi256_si512(indices),
                                 _mm512_load_si512(::ikea::bec256::detail::log_width.data() + 64)));
}
// bytepop is _mm256_popcnt_epi8(bits); a caller that already holds it (a
// larger bitset counting many blocks at once) passes it in.
template <class Sink>
inline __attribute__((always_inline)) auto encode_counts(__m256i bits, __m256i bytepop,
                                                         unsigned population, Sink &&sink)
    -> decltype(sink(nullptr, nullptr)) {
    if constexpr (requires { sink.population_error(); }) {
        // Admission reuses the counts needed by encoding. A separate wrapper
        // popcount would scan the same input and reconstruct this information.
        auto sums = _mm256_sad_epu8(bytepop, _mm256_setzero_si256());
        auto halves =
            _mm_add_epi64(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
        const unsigned actual = _mm_cvtsi128_si64(halves) + _mm_extract_epi64(halves, 1);
        if (actual != population)
            return sink.population_error();
        if (population == 0 || population == 256)
            return sink.empty();
    }
    __m256i byte_n;
    auto enum_widths = [&] {
        return _mm256_shuffle_epi8(
            _mm256_broadcastsi128_si256(_mm_loadu_si128(
                reinterpret_cast<const __m128i *>(::ikea::bec256::detail::byte_width.data()))),
            bytepop);
    };
    if constexpr (requires { sink.enum_bit_cutoff; }) {
        byte_n = enum_widths();
        const auto sums = _mm256_sad_epu8(byte_n, _mm256_setzero_si256());
        const auto halves =
            _mm_add_epi64(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
        const unsigned cost = _mm_cvtsi128_si64(halves) + _mm_extract_epi64(halves, 1);
        // Heuristic decline precedes the tree, rank lookup, packing and all
        // destination/effect access. The accepted arm reuses these widths.
        if (cost >= sink.enum_bit_cutoff)
            return sink.declined();
    }
    if (population == 1 || population == 255) {
        auto one = population == 1 ? bits : _mm256_xor_si256(bits, _mm256_set1_epi64x(-1));
        std::uint8_t value;
        ::ikea::bec256::detail::encode_singleton(
            _mm256_extract_epi64(one, 0), _mm256_extract_epi64(one, 1),
            _mm256_extract_epi64(one, 2), _mm256_extract_epi64(one, 3), population == 255, &value);
        return sink.singleton(value);
    }
    auto p16 = _mm256_maddubs_epi16(bytepop, _mm256_set1_epi8(1));
    auto p32 = _mm_hadd_epi16(_mm256_castsi256_si128(p16), _mm256_extracti128_si256(p16, 1));
    auto p64 = _mm_hadd_epi16(p32, p32);
    auto p128 = _mm_hadd_epi16(p64, p64);
    auto head_parent =
        _mm_or_si128(_mm_slli_si128(p64, 6),
                     _mm_slli_si128(_mm_and_si128(p128, _mm_set_epi64x(0, 0xffffffff)), 2));
    head_parent = _mm_insert_epi16(head_parent, population, 0);
    head_parent = _mm_insert_epi16(head_parent, 0, 7);
    auto head_left =
        _mm_or_si128(_mm_shuffle_epi8(p32, _mm_setr_epi8(-1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9,
                                                         12, 13, -1, -1)),
                     _mm_shuffle_epi8(p64, _mm_setr_epi8(-1, -1, 0, 1, 4, 5, -1, -1, -1, -1, -1, -1,
                                                         -1, -1, -1, -1)));
    head_left = _mm_or_si128(head_left, _mm_and_si128(p128, _mm_set_epi64x(0, 65535)));
    auto even_words = _mm256_setr_epi16(0, 2, 4, 6, 8, 10, 12, 14, 0, 0, 0, 0, 0, 0, 0, 0);
    auto middle_left = _mm256_castsi256_si128(_mm256_permutexvar_epi16(even_words, p16));
    auto low_left = _mm256_cvtepu8_epi16(_mm256_castsi256_si128(_mm256_permutexvar_epi8(
        _mm256_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 0, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        bytepop)));
    auto parent_hi = _mm256_inserti128_si256(_mm256_castsi128_si256(head_parent), p32, 1);
    auto child_hi = _mm256_inserti128_si256(_mm256_castsi128_si256(head_left), middle_left, 1);
    auto parent = _mm512_inserti64x4(_mm512_castsi256_si512(parent_hi), p16, 1);
    auto child = _mm512_inserti64x4(_mm512_castsi256_si512(child_hi), low_left, 1);
    alignas(64) static constexpr std::uint16_t half_width[32]{
        128, 64, 64, 32, 32, 32, 32, 1, 16, 16, 16, 16, 16, 16, 16, 16,
        8,   8,  8,  8,  8,  8,  8,  8, 8,  8,  8,  8,  8,  8,  8,  8};
    auto half = _mm512_load_si512(half_width);
    auto minimum = _mm512_subs_epu16(parent, half);
    auto k = _mm512_min_epu16(parent, _mm512_sub_epi16(_mm512_add_epi16(half, half), parent));
    auto narrow_k = _mm512_cvtepi16_epi8(k);
    auto widths = log128(narrow_k);
    widths = _mm256_mask_mov_epi8(widths, _mm256_cmpeq_epi8_mask(narrow_k, _mm256_set1_epi8(-128)),
                                  _mm256_set1_epi8(8));
    auto values = _mm512_cvtepi16_epi8(_mm512_sub_epi16(child, minimum));
    if constexpr (!requires { sink.enum_bit_cutoff; })
        byte_n = enum_widths();
    auto v = _mm512_inserti64x4(_mm512_castsi256_si512(values),
                                lookup256(::ikea::bec256::detail::codes.rank.data(), bits), 1);
    auto n = _mm512_inserti64x4(_mm512_castsi256_si512(widths), byte_n, 1);
    auto even_n = _mm512_and_si512(n, _mm512_set1_epi16(255));
    auto v2 = _mm512_or_si512(_mm512_and_si512(v, _mm512_set1_epi16(255)),
                              _mm512_sllv_epi16(_mm512_srli_epi16(v, 8), even_n));
    auto n2 = _mm512_add_epi16(even_n, _mm512_srli_epi16(n, 8));
    auto even_n2 = _mm512_and_si512(n2, _mm512_set1_epi32(65535));
    auto v4 = _mm512_or_si512(_mm512_and_si512(v2, _mm512_set1_epi32(65535)),
                              _mm512_sllv_epi32(_mm512_srli_epi32(v2, 16), even_n2));
    auto n4 = _mm512_add_epi32(even_n2, _mm512_srli_epi32(n2, 16));
    auto even_n4 = _mm512_and_si512(n4, _mm512_set1_epi64(0xffffffff));
    auto v8 = _mm512_or_si512(_mm512_and_si512(v4, _mm512_set1_epi64(0xffffffff)),
                              _mm512_sllv_epi64(_mm512_srli_epi64(v4, 32), even_n4));
    auto n8 = _mm512_add_epi64(even_n4, _mm512_srli_epi64(n4, 32));
    alignas(64) std::uint64_t groups[8], group_width[8];
    _mm512_store_si512(groups, v8);
    _mm512_store_si512(group_width, n8);
    return sink(groups, group_width);
}
template <class Sink>
inline __attribute__((always_inline)) auto encode_to(__m256i bits, unsigned population, Sink &&sink)
    -> decltype(sink(nullptr, nullptr)) {
    return encode_counts(bits, _mm256_popcnt_epi8(bits), population, sink);
}
inline __attribute__((always_inline)) unsigned encode(__m256i bits, unsigned population,
                                                      std::uint8_t *out64) {
    struct sink {
        std::uint8_t *output;
        [[gnu::always_inline]] unsigned operator()(const std::uint64_t *values,
                                                   const std::uint64_t *widths) const {
            auto packed = ::ikea::bec256::detail::assemble(_mm512_loadu_si512(values),
                                                           _mm512_loadu_si512(widths));
            _mm512_storeu_si512(output, packed);
            return ::ikea::bec256::detail::encoded_bytes(widths);
        }
        unsigned singleton(std::uint8_t value) const {
            *output = value;
            return 1;
        }
    };
    return encode_to(bits, population, sink{out64});
}
inline __attribute__((always_inline)) __m256i prefix16(__m256i n) {
    auto s = _mm256_add_epi16(n, _mm256_bslli_epi128(n, 2));
    s = _mm256_add_epi16(s, _mm256_bslli_epi128(s, 4));
    s = _mm256_add_epi16(s, _mm256_bslli_epi128(s, 8));
    return _mm256_add_epi16(s, _mm256_maskz_permutexvar_epi16(0xff00, _mm256_set1_epi16(7), s));
}
inline __attribute__((always_inline)) unsigned total(__m256i p) {
    return _mm_extract_epi16(_mm256_extracti128_si256(p, 1), 7);
}
inline __attribute__((always_inline)) __m256i extract(__m512i body, __m256i offsets, __m256i n) {
    auto index = _mm256_srli_epi16(offsets, 3);
    index = _mm256_add_epi16(_mm256_or_si256(index, _mm256_slli_epi16(index, 8)),
                             _mm256_set1_epi16(256));
    auto word = _mm256_permutex2var_epi8(_mm512_castsi512_si256(body), index,
                                         _mm512_extracti64x4_epi64(body, 1));
    auto shifted = _mm256_srlv_epi16(word, _mm256_and_si256(offsets, _mm256_set1_epi16(7)));
    auto mask = _mm256_sub_epi16(_mm256_sllv_epi16(_mm256_set1_epi16(1), n), _mm256_set1_epi16(1));
    return _mm256_and_si256(shifted, mask);
}
template <unsigned Half>
inline __attribute__((always_inline)) __m256i split(__m512i body, __m256i parent, unsigned &pos,
                                                    __m256i &right) {
    auto k = _mm256_min_epu16(parent, _mm256_sub_epi16(_mm256_set1_epi16(2 * Half), parent));
    __m256i n;
    if constexpr (Half == 8) {
        n = _mm256_shuffle_epi8(_mm256_broadcastsi128_si256(
                                    _mm_setr_epi8(0, 1, 2, 2, 3, 3, 3, 3, 4, 0, 0, 0, 0, 0, 0, 0)),
                                k);
    } else {
        n = _mm256_cvtepu8_epi16(
            _mm256_castsi256_si128(log128(_mm256_zextsi128_si256(_mm256_cvtepi16_epi8(k)))));
    }
    auto p = prefix16(n);
    auto offset = _mm256_add_epi16(_mm256_sub_epi16(p, n), _mm256_set1_epi16(pos));
    pos += total(p);
    auto left = _mm256_add_epi16(extract(body, offset, n),
                                 _mm256_subs_epu16(parent, _mm256_set1_epi16(Half)));
    right = _mm256_sub_epi16(parent, left);
    return left;
}
inline __attribute__((always_inline)) __m256i interleave8(__m256i a, __m256i b) {
    return _mm256_permutex2var_epi16(
        a, _mm256_setr_epi16(0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23), b);
}
inline __attribute__((always_inline)) __m256i byte_slots(__m512i body, __m256i pops, __m512i pos,
                                                         unsigned &used_bits);
inline __attribute__((always_inline)) __m256i decode_loaded(__m512i body, unsigned first_byte,
                                                            unsigned population,
                                                            unsigned &used_bits) {
    unsigned n = std::bit_width(std::min(population, 256 - population));
    unsigned left0 = (first_byte & ((1u << n) - 1)) + (population > 128 ? population - 128 : 0);
    unsigned pos = n;
    auto parent = _mm256_zextsi128_si256(_mm_cvtsi32_si128(left0 | ((population - left0) << 16)));
    __m256i right;
    auto left = split<64>(body, parent, pos, right);
    parent = interleave8(left, right);
    left = split<32>(body, parent, pos, right);
    parent = interleave8(left, right);
    left = split<16>(body, parent, pos, right);
    parent = interleave8(left, right);
    left = split<8>(body, parent, pos, right);
    auto l8 = _mm256_cvtepi16_epi8(left), r8 = _mm256_cvtepi16_epi8(right);
    auto pops = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_unpacklo_epi8(l8, r8)),
                                        _mm_unpackhi_epi8(l8, r8), 1);
    return lookup256(::ikea::bec256::detail::codes.value.data(),
                     byte_slots(body, pops, _mm512_set1_epi16(pos), used_bits));
}

inline __attribute__((always_inline)) __m256i decode(const std::uint8_t *readable64,
                                                     unsigned population, unsigned &used_bits) {
    return decode_loaded(_mm512_loadu_si512(readable64), readable64[0], population, used_bits);
}

// Two independent BEC streams, one 512-position native result. The streams
// need not be adjacent in memory or belong to the same physical container.
inline __attribute__((always_inline)) __m512i prefix16x2(__m512i n) {
    auto s = _mm512_add_epi16(n, _mm512_bslli_epi128(n, 2));
    s = _mm512_add_epi16(s, _mm512_bslli_epi128(s, 4));
    s = _mm512_add_epi16(s, _mm512_bslli_epi128(s, 8));
    auto index = _mm512_mask_set1_epi16(_mm512_set1_epi16(7), 0xffff0000, 23);
    return _mm512_add_epi16(s, _mm512_maskz_permutexvar_epi16(0xff00ff00, index, s));
}
// Constant u16 lane vector from a lane-index function.
template <class F> inline __attribute__((always_inline)) __m512i lanes(F) {
    alignas(64) static constexpr auto table = [] {
        std::array<std::uint16_t, 32> r{};
        for (unsigned j = 0; j < 32; ++j)
            r[j] = std::uint16_t(F{}(j));
        return r;
    }();
    return _mm512_load_si512(table.data());
}
template <unsigned Domain, unsigned D> consteval __mmask32 from() {
    std::uint32_t m = 0;
    for (unsigned j = 0; j < 32; ++j)
        if (j % Domain >= D)
            m |= 1u << j;
    return m;
}
// Inclusive prefix of the first Active lanes of each Domain-lane stream.
// Domains of eight or more lanes need no masks: byte shifts stay within
// 128-bit lanes and dead lanes are never read by the next level.
template <unsigned Active, unsigned Domain>
inline __attribute__((always_inline)) __m512i segmented(__m512i s) {
    constexpr bool masked = Domain < 8;
    const auto add = [&](__m512i shifted, __mmask32 m) {
        return masked ? _mm512_mask_add_epi16(s, m, s, shifted) : _mm512_add_epi16(s, shifted);
    };
    if constexpr (Active >= 2)
        s = add(_mm512_bslli_epi128(s, 2), from<Domain, 1>());
    if constexpr (Active >= 4)
        s = add(_mm512_bslli_epi128(s, 4), from<Domain, 2>());
    if constexpr (Active >= 8)
        s = add(_mm512_bslli_epi128(s, 8), from<Domain, 4>());
    if constexpr (Active == 16)
        s = _mm512_add_epi16(
            s, _mm512_maskz_permutexvar_epi16(from<Domain, 8>(),
                                              lanes([](unsigned j) { return j / 16 * 16 + 7; }), s));
    return s;
}
template <unsigned Half> inline __attribute__((always_inline)) __m512i widths(__m512i k) {
    const auto *log16 = ::ikea::bec256::detail::log_width16.data();
    if constexpr (Half == 8)
        return _mm512_shuffle_epi8(
            _mm512_broadcast_i32x4(_mm_setr_epi8(0, 1, 2, 2, 3, 3, 3, 3, 4, 0, 0, 0, 0, 0, 0, 0)),
            k);
    else if constexpr (Half == 16)
        return _mm512_permutexvar_epi16(k, _mm512_load_si512(log16));
    else {
        auto w = _mm512_permutex2var_epi16(_mm512_load_si512(log16), k,
                                           _mm512_load_si512(log16 + 32));
        if constexpr (Half == 64)
            w = _mm512_mask_set1_epi16(w, _mm512_cmpeq_epi16_mask(k, _mm512_set1_epi16(64)), 7);
        if constexpr (Half == 128) {
            w = _mm512_mask_set1_epi16(w, _mm512_cmpge_epu16_mask(k, _mm512_set1_epi16(64)), 7);
            w = _mm512_mask_set1_epi16(w, _mm512_cmpeq_epi16_mask(k, _mm512_set1_epi16(128)), 8);
        }
        return w;
    }
}
// The bisection level step shared by every stream geometry. Lanes are
// stream-major: each stream owns Domain consecutive lanes, the first Active
// holding this level's nodes. slot is each lane's byte base in table (64
// bytes) and positions its bit cursor from there. A pair of 256-position
// blocks uses two 16-lane domains over two 32-byte trees; Bec4096 packs
// sixteen streams densely (Domain == Active) to fill its upper levels.
template <unsigned Half, unsigned Active, unsigned Domain>
inline __attribute__((always_inline)) __m512i level(__m512i table, __m512i parent, __m512i slot,
                                                    __m512i &positions, __m512i &right) {
    auto k = _mm512_min_epu16(parent, _mm512_sub_epi16(_mm512_set1_epi16(2 * Half), parent));
    const auto w = widths<Half>(k);
    auto p = segmented<Active, Domain>(w);
    auto offset = _mm512_add_epi16(_mm512_sub_epi16(p, w), positions);
    positions = _mm512_add_epi16(
        positions, _mm512_permutexvar_epi16(
                       lanes([](unsigned j) { return j / Domain * Domain + Active - 1; }), p));
    auto index = _mm512_add_epi16(_mm512_srli_epi16(offset, 3), slot);
    index = _mm512_or_si512(index, _mm512_slli_epi16(index, 8));
    index = _mm512_add_epi16(index, _mm512_set1_epi16(256));
    auto word = _mm512_permutexvar_epi8(index, table);
    auto shifted = _mm512_srlv_epi16(word, _mm512_and_si512(offset, _mm512_set1_epi16(7)));
    auto mask = _mm512_sub_epi16(_mm512_sllv_epi16(_mm512_set1_epi16(1), w), _mm512_set1_epi16(1));
    auto left = _mm512_add_epi16(_mm512_and_si512(shifted, mask),
                                 _mm512_subs_epu16(parent, _mm512_set1_epi16(Half)));
    right = _mm512_sub_epi16(parent, left);
    return left;
}
// Two blocks: each population tree occupies <=150 bits, so two 32-byte
// windows fit in one ZMM and every u16 window is ONE byte permutation.
template <unsigned Half>
inline __attribute__((always_inline)) __m512i split2(__m512i tree, __m512i parent,
                                                     __m512i &positions, __m512i &right) {
    return level<Half, 128 / Half, 16>(tree, parent, lanes([](unsigned j) { return j < 16 ? 0 : 32; }),
                                       positions, right);
}
inline __attribute__((always_inline)) __m512i interleave8x2(__m512i left, __m512i right) {
    alignas(64) static constexpr std::uint16_t indices[32]{
        0,  32, 1,  33, 2,  34, 3,  35, 4,  36, 5,  37, 6,  38, 7,  39,
        16, 48, 17, 49, 18, 50, 19, 51, 20, 52, 21, 53, 22, 54, 23, 55};
    return _mm512_permutex2var_epi16(left, _mm512_load_si512(indices), right);
}
inline __attribute__((always_inline)) __m256i byte_slots(__m512i body, __m256i pops, __m512i pos,
                                                         unsigned &used_bits) {
    auto n8 = _mm256_shuffle_epi8(
        _mm256_broadcastsi128_si256(_mm_loadu_si128(
            reinterpret_cast<const __m128i *>(::ikea::bec256::detail::byte_width.data()))),
        pops);
    auto n = _mm512_cvtepu8_epi16(n8);
    auto p = prefix16x2(n);
    p = _mm512_add_epi16(p, _mm512_maskz_permutexvar_epi16(0xffff0000, _mm512_set1_epi16(15), p));
    auto offset = _mm512_add_epi16(_mm512_sub_epi16(p, n), pos);
    auto index = _mm512_zextsi256_si512(_mm512_cvtepi16_epi8(_mm512_srli_epi16(offset, 3)));
    auto lo = _mm512_castsi512_si256(_mm512_permutexvar_epi8(index, body));
    auto hi = _mm512_castsi512_si256(
        _mm512_permutexvar_epi8(_mm512_add_epi8(index, _mm512_set1_epi8(1)), body));
    auto word =
        _mm512_or_si512(_mm512_cvtepu8_epi16(lo), _mm512_slli_epi16(_mm512_cvtepu8_epi16(hi), 8));
    auto value = _mm512_srlv_epi16(word, _mm512_and_si512(offset, _mm512_set1_epi16(7)));
    auto mask = _mm512_sub_epi16(_mm512_sllv_epi16(_mm512_set1_epi16(1), n), _mm512_set1_epi16(1));
    auto rank = _mm512_cvtepi16_epi8(_mm512_and_si512(value, mask));
    auto end = _mm512_add_epi16(p, pos);
    used_bits = _mm_extract_epi16(_mm512_extracti32x4_epi32(end, 3), 7);
    auto base = _mm256_shuffle_epi8(
        _mm256_broadcastsi128_si256(_mm_loadu_si128(
            reinterpret_cast<const __m128i *>(::ikea::bec256::detail::byte_base.data()))),
        pops);
    return _mm256_add_epi8(base, rank);
}
inline __attribute__((always_inline)) __m512i decode2_loaded(__m512i body_a, unsigned first_a,
                                                             unsigned pop_a, __m512i body_b,
                                                             unsigned first_b, unsigned pop_b,
                                                             unsigned &bits_a, unsigned &bits_b) {
    auto tree = _mm512_inserti64x4(body_a, _mm512_castsi512_si256(body_b), 1);
    unsigned na = std::bit_width(std::min(pop_a, 256 - pop_a));
    unsigned nb = std::bit_width(std::min(pop_b, 256 - pop_b));
    unsigned la = (first_a & ((1u << na) - 1)) + (pop_a > 128 ? pop_a - 128 : 0);
    unsigned lb = (first_b & ((1u << nb) - 1)) + (pop_b > 128 ? pop_b - 128 : 0);
    auto parent = _mm512_maskz_set1_epi32(1, la | ((pop_a - la) << 16));
    parent = _mm512_mask_set1_epi32(parent, 1 << 8, lb | ((pop_b - lb) << 16));
    auto pos = _mm512_mask_set1_epi16(_mm512_set1_epi16(na), 0xffff0000, nb);
    __m512i right;
    auto left = split2<64>(tree, parent, pos, right);
    parent = interleave8x2(left, right);
    left = split2<32>(tree, parent, pos, right);
    parent = interleave8x2(left, right);
    left = split2<16>(tree, parent, pos, right);
    parent = interleave8x2(left, right);
    left = split2<8>(tree, parent, pos, right);
    auto l8 = _mm512_cvtepi16_epi8(left), r8 = _mm512_cvtepi16_epi8(right);
    auto first = _mm256_unpacklo_epi8(l8, r8), second = _mm256_unpackhi_epi8(l8, r8);
    auto pops_a = _mm256_permute2x128_si256(first, second, 0x20);
    auto pops_b = _mm256_permute2x128_si256(first, second, 0x31);
    auto pos_a = _mm512_permutexvar_epi16(_mm512_setzero_si512(), pos);
    auto pos_b = _mm512_permutexvar_epi16(_mm512_set1_epi16(16), pos);
    auto slots_a = byte_slots(body_a, pops_a, pos_a, bits_a),
         slots_b = byte_slots(body_b, pops_b, pos_b, bits_b);
    auto slot = _mm512_inserti64x4(_mm512_castsi256_si512(slots_a), slots_b, 1);
    auto low = _mm512_permutex2var_epi8(
        _mm512_load_si512(::ikea::bec256::detail::codes.value.data()), slot,
        _mm512_load_si512(::ikea::bec256::detail::codes.value.data() + 64));
    auto high = _mm512_permutex2var_epi8(
        _mm512_load_si512(::ikea::bec256::detail::codes.value.data() + 128), slot,
        _mm512_load_si512(::ikea::bec256::detail::codes.value.data() + 192));
    return _mm512_mask_blend_epi8(_mm512_movepi8_mask(slot), low, high);
}
inline __attribute__((always_inline)) __m512i decode2(const std::uint8_t *a, unsigned pop_a,
                                                      const std::uint8_t *b, unsigned pop_b,
                                                      unsigned &bits_a, unsigned &bits_b) {
    return decode2_loaded(_mm512_loadu_si512(a), a[0], pop_a, _mm512_loadu_si512(b), b[0], pop_b,
                          bits_a, bits_b);
}

} // namespace ikea::bec256::detail::avx512
