#pragma once
#include <ikea/bec256/detail/tables.h>
#include <ikea/bec256/detail/pack.h>
#include <arm_neon.h>
#include <algorithm>
#include <bit>

namespace ikea::bec256::detail::neon {
using Bits256 = uint8x16x2_t;
// These bodies know native carriers, not the class containing their bytes.
inline Bits256 load256(const void *p) {
    auto b = static_cast<const std::uint8_t *>(p);
    return {{vld1q_u8(b), vld1q_u8(b + 16)}};
}
inline void store256(void *p, Bits256 x) {
    auto b = static_cast<std::uint8_t *>(p);
    vst1q_u8(b, x.val[0]);
    vst1q_u8(b + 16, x.val[1]);
}
inline Bits256 intersection(Bits256 a, Bits256 b) {
    return {{vandq_u8(a.val[0], b.val[0]), vandq_u8(a.val[1], b.val[1])}};
}
inline Bits256 set_union(Bits256 a, Bits256 b) {
    return {{vorrq_u8(a.val[0], b.val[0]), vorrq_u8(a.val[1], b.val[1])}};
}
inline __attribute__((always_inline)) uint8x16_t lookup256(const std::uint8_t *table,
                                                           uint8x16_t indices) {
    auto a = vqtbl4q_u8(vld1q_u8_x4(table), indices);
    auto b = vqtbl4q_u8(vld1q_u8_x4(table + 64), vsubq_u8(indices, vdupq_n_u8(64)));
    auto c = vqtbl4q_u8(vld1q_u8_x4(table + 128), vsubq_u8(indices, vdupq_n_u8(128)));
    auto d = vqtbl4q_u8(vld1q_u8_x4(table + 192), vsubq_u8(indices, vdupq_n_u8(192)));
    return vorrq_u8(vorrq_u8(a, b), vorrq_u8(c, d));
}
struct Fields8 {
    uint8x8_t value, width;
};
inline __attribute__((always_inline)) Fields8 fields(uint16x8_t parent, uint16x8_t left,
                                                     uint16x8_t half) {
    auto lower = vqsubq_u16(parent, half);
    auto alternatives_minus_one = vminq_u16(parent, vsubq_u16(vaddq_u16(half, half), parent));
    return {vmovn_u16(vsubq_u16(left, lower)),
            vmovn_u16(vsubq_u16(vdupq_n_u16(16), vclzq_u16(alternatives_minus_one)))};
}
// Pack 16 independent (value,width) fields into two <=56-bit groups.
inline __attribute__((always_inline)) void pack16(uint8x16_t v, uint8x16_t n, std::uint64_t *group,
                                                  std::uint64_t *width) {
    auto ve = vandq_u16(vreinterpretq_u16_u8(v), vdupq_n_u16(255));
    auto ne = vandq_u16(vreinterpretq_u16_u8(n), vdupq_n_u16(255));
    auto v2 = vorrq_u16(
        ve, vshlq_u16(vshrq_n_u16(vreinterpretq_u16_u8(v), 8), vreinterpretq_s16_u16(ne)));
    auto n2 = vaddq_u16(ne, vshrq_n_u16(vreinterpretq_u16_u8(n), 8));
    auto v4e = vandq_u32(vreinterpretq_u32_u16(v2), vdupq_n_u32(65535));
    auto n4e = vandq_u32(vreinterpretq_u32_u16(n2), vdupq_n_u32(65535));
    auto v4 = vorrq_u32(
        v4e, vshlq_u32(vshrq_n_u32(vreinterpretq_u32_u16(v2), 16), vreinterpretq_s32_u32(n4e)));
    auto n4 = vaddq_u32(n4e, vshrq_n_u32(vreinterpretq_u32_u16(n2), 16));
    auto v8e = vandq_u64(vreinterpretq_u64_u32(v4), vdupq_n_u64(0xffffffff));
    auto n8e = vandq_u64(vreinterpretq_u64_u32(n4), vdupq_n_u64(0xffffffff));
    auto v8 = vorrq_u64(
        v8e, vshlq_u64(vshrq_n_u64(vreinterpretq_u64_u32(v4), 32), vreinterpretq_s64_u64(n8e)));
    auto n8 = vaddq_u64(n8e, vshrq_n_u64(vreinterpretq_u64_u32(n4), 32));
    vst1q_u64(group, v8);
    vst1q_u64(width, n8);
}
template <class Sink>
inline __attribute__((always_inline)) auto encode_to(Bits256 bits, unsigned population, Sink &&sink)
    -> decltype(sink(nullptr, nullptr)) {
    auto b0 = vcntq_u8(bits.val[0]), b1 = vcntq_u8(bits.val[1]);
    if constexpr (requires { sink.population_error(); }) {
        const unsigned actual = vaddlvq_u8(vaddq_u8(b0, b1));
        if (actual != population)
            return sink.population_error();
        if (population == 0 || population == 256)
            return sink.empty();
    }
    auto table = vld1q_u8(::ikea::bec256::detail::byte_width.data());
    uint8x16_t n0, n1;
    if constexpr (requires { sink.enum_bit_cutoff; }) {
        n0 = vqtbl1q_u8(table, b0);
        n1 = vqtbl1q_u8(table, b1);
        const unsigned cost = unsigned(vaddvq_u8(n0)) + vaddvq_u8(n1);
        if (cost >= sink.enum_bit_cutoff)
            return sink.declined();
    }
    if (population == 1 || population == 255) {
        auto lo = population == 1 ? bits.val[0] : vmvnq_u8(bits.val[0]);
        auto hi = population == 1 ? bits.val[1] : vmvnq_u8(bits.val[1]);
        std::uint8_t value;
        ::ikea::bec256::detail::encode_singleton(vgetq_lane_u64(vreinterpretq_u64_u8(lo), 0),
                                                 vgetq_lane_u64(vreinterpretq_u64_u8(lo), 1),
                                                 vgetq_lane_u64(vreinterpretq_u64_u8(hi), 0),
                                                 vgetq_lane_u64(vreinterpretq_u64_u8(hi), 1),
                                                 population == 255, &value);
        return sink.singleton(value);
    }
    auto p16 = vpaddq_u8(b0, b1);
    auto p32 = vpaddq_u8(p16, p16);
    auto p64 = vpaddq_u8(p32, p32);
    auto p128 = vpaddq_u8(p64, p64);
    uint8x16x3_t upper{{p128, p64, p32}};
    auto parent = vmovl_u8(vqtbl3_u8(upper, uint8x8_t{255, 0, 1, 16, 17, 18, 19, 255}));
    parent = vsetq_lane_u16(population, parent, 0);
    auto left = vmovl_u8(vqtbl3_u8(upper, uint8x8_t{0, 16, 18, 32, 34, 36, 38, 255}));
    auto top = fields(parent, left, uint16x8_t{128, 64, 64, 32, 32, 32, 32, 1});
    auto middle = fields(vmovl_u8(vget_low_u8(p32)), vmovl_u8(vget_low_u8(vuzp1q_u8(p16, p16))),
                         vdupq_n_u16(16));
    auto low0 = fields(vmovl_u8(vget_low_u8(p16)), vmovl_u8(vget_low_u8(vuzp1q_u8(b0, b0))),
                       vdupq_n_u16(8));
    auto low1 = fields(vmovl_u8(vget_high_u8(p16)), vmovl_u8(vget_low_u8(vuzp1q_u8(b1, b1))),
                       vdupq_n_u16(8));
    std::uint64_t groups[8], widths[8];
    pack16(vcombine_u8(top.value, middle.value), vcombine_u8(top.width, middle.width), groups,
           widths);
    pack16(vcombine_u8(low0.value, low1.value), vcombine_u8(low0.width, low1.width), groups + 2,
           widths + 2);
    if constexpr (!requires { sink.enum_bit_cutoff; }) {
        n0 = vqtbl1q_u8(table, b0);
        n1 = vqtbl1q_u8(table, b1);
    }
    pack16(lookup256(::ikea::bec256::detail::codes.rank.data(), bits.val[0]), n0, groups + 4,
           widths + 4);
    pack16(lookup256(::ikea::bec256::detail::codes.rank.data(), bits.val[1]), n1, groups + 6,
           widths + 6);
    return sink(groups, widths);
}
inline __attribute__((always_inline)) unsigned encode(Bits256 bits, unsigned population,
                                                      std::uint8_t *out64) {
    return encode_to(bits, population, ::ikea::bec256::detail::wide_sink{out64});
}
inline __attribute__((always_inline)) uint16x8_t prefix8(uint16x8_t widths) {
    auto z = vdupq_n_u16(0);
    auto p = vaddq_u16(widths, vextq_u16(z, widths, 7));
    p = vaddq_u16(p, vextq_u16(z, p, 6));
    return vaddq_u16(p, vextq_u16(z, p, 4));
}
inline __attribute__((always_inline)) uint16x8_t extract(uint8x16x4_t body, uint16x8_t offsets,
                                                         uint16x8_t widths) {
    auto index = vmovn_u16(vshrq_n_u16(offsets, 3));
    auto next = vadd_u8(index, vdup_n_u8(1));
    auto indices = vzip_u8(index, next);
    // The entire population tree is at most 150 bits. Its gathers fit in the
    // first 32 readable bytes; the byte-enumeration phase uses the full window.
    uint8x16x2_t tree{{body.val[0], body.val[1]}};
    auto word = vreinterpretq_u16_u8(vqtbl2q_u8(tree, vcombine_u8(indices.val[0], indices.val[1])));
    auto shifted =
        vshlq_u16(word, vnegq_s16(vreinterpretq_s16_u16(vandq_u16(offsets, vdupq_n_u16(7)))));
    auto mask = vsubq_u16(vshlq_u16(vdupq_n_u16(1), vreinterpretq_s16_u16(widths)), vdupq_n_u16(1));
    return vandq_u16(shifted, mask);
}
template <unsigned Half>
inline __attribute__((always_inline)) uint16x8x2_t split(uint8x16x4_t body, uint16x8_t parent,
                                                         unsigned &pos) {
    auto k = vminq_u16(parent, vsubq_u16(vdupq_n_u16(2 * Half), parent));
    auto n = vsubq_u16(vdupq_n_u16(16), vclzq_u16(k));
    auto prefix = prefix8(n);
    auto offsets = vaddq_u16(vsubq_u16(prefix, n), vdupq_n_u16(pos));
    pos += vgetq_lane_u16(prefix, 7);
    auto left = vaddq_u16(extract(body, offsets, n), vqsubq_u16(parent, vdupq_n_u16(Half)));
    return vzipq_u16(left, vsubq_u16(parent, left));
}
inline __attribute__((always_inline)) uint8x16_t prefix16bytes(uint8x16_t n) {
    auto z = vdupq_n_u8(0);
    auto s = vaddq_u8(n, vextq_u8(z, n, 15));
    s = vaddq_u8(s, vextq_u8(z, s, 14));
    s = vaddq_u8(s, vextq_u8(z, s, 12));
    return vaddq_u8(s, vextq_u8(z, s, 8));
}
inline __attribute__((always_inline)) uint8x16_t ranks16(uint8x16x4_t body, uint8x16_t offsets,
                                                         uint8x16_t widths, unsigned base_byte) {
    auto index = vaddq_u8(vshrq_n_u8(offsets, 3), vdupq_n_u8(base_byte));
    auto low = vqtbl4q_u8(body, index);
    auto high = vqtbl4q_u8(body, vaddq_u8(index, vdupq_n_u8(1)));
    auto shift = vandq_u8(offsets, vdupq_n_u8(7));
    auto half = [](uint8x8_t lo, uint8x8_t hi, uint8x8_t sh, uint8x8_t n) {
        auto word = vorrq_u16(vmovl_u8(lo), vshlq_n_u16(vmovl_u8(hi), 8));
        auto value = vshlq_u16(word, vnegq_s16(vreinterpretq_s16_u16(vmovl_u8(sh))));
        auto mask = vsubq_u16(vshlq_u16(vdupq_n_u16(1), vreinterpretq_s16_u16(vmovl_u8(n))),
                              vdupq_n_u16(1));
        return vmovn_u16(vandq_u16(value, mask));
    };
    return vcombine_u8(
        half(vget_low_u8(low), vget_low_u8(high), vget_low_u8(shift), vget_low_u8(widths)),
        half(vget_high_u8(low), vget_high_u8(high), vget_high_u8(shift), vget_high_u8(widths)));
}
inline __attribute__((always_inline)) Bits256 decode(const std::uint8_t *readable64,
                                                     unsigned population, unsigned &used_bits) {
    auto body = vld1q_u8_x4(readable64);
    unsigned n = std::bit_width(std::min(population, 256 - population));
    unsigned left = (readable64[0] & ((1u << n) - 1)) + (population > 128 ? population - 128 : 0);
    unsigned pos = n;
    auto p2 = vsetq_lane_u16(left, vdupq_n_u16(0), 0);
    p2 = vsetq_lane_u16(population - left, p2, 1);
    auto p4 = split<64>(body, p2, pos).val[0];
    auto p8 = split<32>(body, p4, pos).val[0];
    auto p16 = split<16>(body, p8, pos);
    auto first = split<8>(body, p16.val[0], pos);
    auto second = split<8>(body, p16.val[1], pos);
    auto b0 = vcombine_u8(vmovn_u16(first.val[0]), vmovn_u16(first.val[1]));
    auto b1 = vcombine_u8(vmovn_u16(second.val[0]), vmovn_u16(second.val[1]));
    auto widths = vld1q_u8(::ikea::bec256::detail::byte_width.data());
    auto n0 = vqtbl1q_u8(widths, b0), n1 = vqtbl1q_u8(widths, b1);
    auto prefix0 = prefix16bytes(n0), prefix1 = prefix16bytes(n1);
    auto relative0 = vaddq_u8(vsubq_u8(prefix0, n0), vdupq_n_u8(pos % 8));
    auto relative1 =
        vaddq_u8(vsubq_u8(prefix1, n1), vdupq_n_u8(pos % 8 + vgetq_lane_u8(prefix0, 15)));
    auto r0 = ranks16(body, relative0, n0, pos / 8), r1 = ranks16(body, relative1, n1, pos / 8);
    auto base = vld1q_u8(::ikea::bec256::detail::byte_base.data());
    auto s0 = vaddq_u8(vqtbl1q_u8(base, b0), r0);
    auto s1 = vaddq_u8(vqtbl1q_u8(base, b1), r1);
    used_bits = pos + vgetq_lane_u8(prefix0, 15) + vgetq_lane_u8(prefix1, 15);
    return {{lookup256(::ikea::bec256::detail::codes.value.data(), s0),
             lookup256(::ikea::bec256::detail::codes.value.data(), s1)}};
}
} // namespace ikea::bec256::detail::neon
