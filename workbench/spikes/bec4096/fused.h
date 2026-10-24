#pragma once
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
#include "codec.h"

namespace bec4096::fused {
// Windows: direct when 64 readable bytes follow, else zero-padded scratch.
// Few nonterminal blocks leave the shared upper levels idle; decode them alone.
inline constexpr unsigned fuse_minimum = 5;
[[gnu::always_inline]] inline bool sparse(const frame &f) {
    unsigned live = 0;
    for (unsigned k = 0; k < 16; ++k)
        live += f.population[k] != 0 && f.population[k] != 256;
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
namespace n = ikea::bec256::detail::neon;

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
            pa[r] = vld1q_u16(f.population.data() + 8 * r);
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

[[gnu::always_inline]] inline void decode_all(const reader &input, bitset4096 &output) {
    const auto &f = input.frame();
    if (sparse(f))
        return detail::decode_all(input, output);
    upper u(input);
    // Bec256's leaf kernel, two blocks at a time with joint register
    // constraints (as bec256's V2 decode2) so their chains interleave.
    auto *out = output.words.data();
    for (unsigned k = 0; k < 16; k += 2) {
        const unsigned a = f.population[k], b = f.population[k + 1];
        const bool ta = a == 0 || a == 256, tb = b == 0 || b == 256;
        if (!ta && !tb) {
            auto pa16 = u.leaves(k), pb16 = u.leaves(k + 1);
            const auto body_a = vld1q_u8_x4(u.w[k]), body_b = vld1q_u8_x4(u.w[k + 1]);
            asm("" : "+w"(pa16.val[0]), "+w"(pa16.val[1]), "+w"(pb16.val[0]), "+w"(pb16.val[1]));
            unsigned ua, ub;
            const auto x = n::decode_leaves(body_a, pa16, vgetq_lane_u16(u.qd[k], 0), ua);
            const auto y = n::decode_leaves(body_b, pb16, vgetq_lane_u16(u.qd[k + 1], 0), ub);
            n::store256(out + 4 * k, x);
            n::store256(out + 4 * k + 4, y);
            continue;
        }
        for (unsigned j = k; j < k + 2; ++j) {
            const unsigned c = f.population[j];
            if (c == 0 || c == 256)
                std::memset(out + 4 * j, c ? 255 : 0, 32);
            else {
                unsigned used;
                const auto p16 = u.leaves(j);
                n::store256(out + 4 * j, n::decode_leaves(vld1q_u8_x4(u.w[j]), p16,
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
        const unsigned c = f.population[k];
        if (c == 0 || c == 256) {
            std::memset(out + 32 * k, c ? 8 : 0, 32);
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
namespace a = ikea::bec256::detail::avx512;
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
    const auto parent = _mm512_inserti64x4(
        zero, _mm256_loadu_si256(reinterpret_cast<const __m256i *>(f.population.data())), 0);
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
    auto *out = static_cast<std::uint64_t *>(destination);
    auto *pops = static_cast<std::uint8_t *>(destination);
    const auto *value = ikea::bec256::detail::codes.value.data();
    for (unsigned i = 0; i < 4; ++i) {
        left = a::level<16, 8, 8>(t8[i], pd[i], lanes([](unsigned j) { return 16 * (j / 8); }), qd[i],
                           right);
        const __m512i pe[2]{children<0>(left, right), children<1>(left, right)};
        __m512i qe[2]{children<0>(qd[i], qd[i]), children<1>(qd[i], qd[i])};
        for (unsigned half = 0; half < 2; ++half) {
            const unsigned p = 2 * i + half, x = 2 * p, y = x + 1;
            const unsigned cx = f.population[x], cy = f.population[y];
            if ((cx == 0 || cx == 256) && (cy == 0 || cy == 256)) {
                if constexpr (Counts) {
                    std::memset(pops + 32 * x, cx ? 8 : 0, 32);
                    std::memset(pops + 32 * y, cy ? 8 : 0, 32);
                } else {
                    std::memset(out + 4 * x, cx ? 255 : 0, 32);
                    std::memset(out + 4 * y, cy ? 255 : 0, 32);
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
            _mm512_storeu_si512(out + 4 * x,
                                _mm512_mask_blend_epi8(_mm512_movepi8_mask(slot), low, high));
        }
    }
}
[[gnu::always_inline]] inline void decode_all(const reader &input, bitset4096 &output) {
    if (sparse(input.frame()))
        return detail::decode_all(input, output);
    walk<false>(input, output.words.data());
}
[[gnu::always_inline]] inline void counts_all(const reader &input, std::uint8_t *out) {
    walk<true>(input, out);
}

#endif
} // namespace bec4096::fused
