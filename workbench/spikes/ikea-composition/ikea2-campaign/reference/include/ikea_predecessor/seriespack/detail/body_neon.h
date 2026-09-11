#pragma once

#if defined(__aarch64__)

#include <arm_neon.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace ikea_predecessor::seriespack::detail::neon {

// These helpers use the little-endian byte representation of unsigned NEON lanes.
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

namespace body_detail {

template<unsigned Q, unsigned LaneBytes>
inline constexpr bool supported = Q >= 1 && Q <= 8 && Q <= LaneBytes &&
    (LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);

// Constant-size lane accesses do not require alignment or a readable suffix.
template<unsigned Bytes>
[[gnu::always_inline]] inline uint8x16_t load_bytes(const std::uint8_t* p) noexcept {
    static_assert(Bytes <= 16);
    if constexpr (Bytes == 16) {
        return vld1q_u8(p);
    } else {
        auto x = vdupq_n_u8(0);
        if constexpr (Bytes >= 8) x = vcombine_u8(vld1_u8(p), vdup_n_u8(0));
        if constexpr ((Bytes & 4) != 0) {
            constexpr unsigned offset = Bytes & ~7u;
            x = vreinterpretq_u8_u32(vld1q_lane_u32(
                reinterpret_cast<const std::uint32_t*>(p + offset),
                vreinterpretq_u32_u8(x), offset / 4));
        }
        if constexpr ((Bytes & 2) != 0) {
            constexpr unsigned offset = Bytes & ~3u;
            x = vreinterpretq_u8_u16(vld1q_lane_u16(
                reinterpret_cast<const std::uint16_t*>(p + offset),
                vreinterpretq_u16_u8(x), offset / 2));
        }
        if constexpr ((Bytes & 1) != 0) {
            constexpr unsigned offset = Bytes & ~1u;
            x = vld1q_lane_u8(p + offset, x, offset);
        }
        return x;
    }
}

template<unsigned Bytes>
[[gnu::always_inline]] inline void store_bytes(std::uint8_t* p, uint8x16_t x) noexcept {
    static_assert(Bytes <= 16);
    if constexpr (Bytes == 16) {
        vst1q_u8(p, x);
    } else {
        if constexpr (Bytes >= 8) vst1_u8(p, vget_low_u8(x));
        if constexpr ((Bytes & 4) != 0) {
            constexpr unsigned offset = Bytes & ~7u;
            vst1q_lane_u32(reinterpret_cast<std::uint32_t*>(p + offset),
                          vreinterpretq_u32_u8(x), offset / 4);
        }
        if constexpr ((Bytes & 2) != 0) {
            constexpr unsigned offset = Bytes & ~3u;
            vst1q_lane_u16(reinterpret_cast<std::uint16_t*>(p + offset),
                          vreinterpretq_u16_u8(x), offset / 2);
        }
        if constexpr ((Bytes & 1) != 0) {
            constexpr unsigned offset = Bytes & ~1u;
            vst1q_lane_u8(p + offset, x, offset);
        }
    }
}

template<unsigned Q, unsigned LaneBytes, unsigned Offset = 0>
[[gnu::always_inline]] inline uint8x16_t expand_bytes(uint8x16_t x) noexcept {
    if constexpr (Q == LaneBytes && Offset == 0) {
        return x;
    } else if constexpr (Q * 2 == LaneBytes && Offset == 0) {
        if constexpr (Q == 1) return vreinterpretq_u8_u16(vmovl_u8(vget_low_u8(x)));
        if constexpr (Q == 2) return vreinterpretq_u8_u32(vmovl_u16(vget_low_u16(vreinterpretq_u16_u8(x))));
        if constexpr (Q == 4) return vreinterpretq_u8_u64(vmovl_u32(vget_low_u32(vreinterpretq_u32_u8(x))));
    } else {
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result{};
            for (unsigned i = 0; i != 16; ++i)
                result[i] = i % LaneBytes < Q
                    ? static_cast<std::uint8_t>(Offset + (i / LaneBytes) * Q + i % LaneBytes)
                    : 255;
            return result;
        }();
        return vqtbl1q_u8(x, vld1q_u8(indices.data()));
    }
}

template<unsigned Q, unsigned LaneBytes>
[[gnu::always_inline]] inline uint8x16_t compact_bytes(uint8x16_t x) noexcept {
    if constexpr (Q == LaneBytes) {
        return x;
    } else if constexpr (Q * 2 == LaneBytes) {
        if constexpr (Q == 1) return vcombine_u8(vmovn_u16(vreinterpretq_u16_u8(x)), vdup_n_u8(0));
        if constexpr (Q == 2) return vreinterpretq_u8_u16(vcombine_u16(vmovn_u32(vreinterpretq_u32_u8(x)), vdup_n_u16(0)));
        if constexpr (Q == 4) return vreinterpretq_u8_u32(vcombine_u32(vmovn_u64(vreinterpretq_u64_u8(x)), vdup_n_u32(0)));
    } else {
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result{};
            for (unsigned i = 0; i != 16; ++i)
                result[i] = i < (16 / LaneBytes) * Q
                    ? static_cast<std::uint8_t>((i / Q) * LaneBytes + i % Q)
                    : 255;
            return result;
        }();
        return vqtbl1q_u8(x, vld1q_u8(indices.data()));
    }
}

template<unsigned First, unsigned Count, std::size_t Size>
[[gnu::always_inline]] inline uint8x16_t table(
    const std::array<uint8x16_t, Size>& values, uint8x16_t indices) noexcept {
    static_assert(Count >= 1 && Count <= 4 && First + Count <= Size);
    if constexpr (Count == 1) return vqtbl1q_u8(values[First], indices);
    if constexpr (Count == 2) return vqtbl2q_u8({{values[First], values[First + 1]}}, indices);
    if constexpr (Count == 3) return vqtbl3q_u8({{values[First], values[First + 1], values[First + 2]}}, indices);
    if constexpr (Count == 4) return vqtbl4q_u8({{values[First], values[First + 1], values[First + 2], values[First + 3]}}, indices);
}

template<unsigned Q, unsigned LaneBytes, unsigned Chunk>
[[gnu::always_inline]] inline void encode_packet_chunk(
    std::uint8_t* packet, const std::array<uint8x16_t, LaneBytes / 2>& values) noexcept {
    constexpr unsigned begin = 16 * Chunk;
    constexpr unsigned bytes = 8 * Q - begin < 16 ? 8 * Q - begin : 16;
    if constexpr (Q == LaneBytes) {
        store_bytes<bytes>(packet + begin, values[Chunk]);
    } else {
        constexpr unsigned first = ((begin / Q) * LaneBytes) / 16;
        constexpr unsigned last = (((begin + bytes - 1) / Q) * LaneBytes) / 16;
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result{};
            for (unsigned i = 0; i != 16; ++i) {
                const unsigned source = ((begin + i) / Q) * LaneBytes + (begin + i) % Q;
                result[i] = i < bytes ? static_cast<std::uint8_t>(source - 16 * first) : 255;
            }
            return result;
        }();
        store_bytes<bytes>(packet + begin,
            table<first, last - first + 1>(values, vld1q_u8(indices.data())));
    }
}

} // namespace body_detail

// N = 16 / LaneBytes values, in ascending logical order. Only N*Q source bytes
// are read. Each result lane is unsigned and zero-extended to LaneBytes bytes.
// The carrier is a value and remains valid across subsequent producer calls.
template<unsigned Q, unsigned LaneBytes>
[[gnu::always_inline]] inline uint8x16_t decode_body(const std::uint8_t* p) noexcept {
    static_assert(body_detail::supported<Q, LaneBytes>);
    return body_detail::expand_bytes<Q, LaneBytes>(
        body_detail::load_bytes<(16 / LaneBytes) * Q>(p));
}

// A short logical fragment occupies the low Count lanes; every other lane is
// zero. This includes an eight-byte Local8 body in a sixteen-byte native carrier.
template<unsigned Q, unsigned LaneBytes, unsigned Count>
[[gnu::always_inline]] inline uint8x16_t decode_body_prefix(const std::uint8_t* p) noexcept {
    static_assert(body_detail::supported<Q, LaneBytes> && Count <= 16 / LaneBytes);
    return body_detail::expand_bytes<Q, LaneBytes>(body_detail::load_bytes<Count * Q>(p));
}

// Writes exactly N*Q bytes. Only the low Q bytes of each unsigned lane matter;
// establishing that these are the intended whole-body bits belongs to the caller.
template<unsigned Q, unsigned LaneBytes>
[[gnu::always_inline]] inline void encode_body(std::uint8_t* p, uint8x16_t values) noexcept {
    static_assert(body_detail::supported<Q, LaneBytes>);
    body_detail::store_bytes<(16 / LaneBytes) * Q>(
        p, body_detail::compact_bytes<Q, LaneBytes>(values));
}

// The caller admits the complete eight-value AoS body [packet, packet + 8*Q).
// Part numbers native fragments, so the first result value is Part*(16/LaneBytes).
// A 16-byte window may include neighboring values; its final window is shifted
// backward to remain inside that body. No tail, head, stride gap or suffix is read.
template<unsigned Q, unsigned LaneBytes, unsigned Part>
[[gnu::always_inline]] inline uint8x16_t decode_packet_body(const std::uint8_t* packet) noexcept {
    static_assert(body_detail::supported<Q, LaneBytes> && LaneBytes >= 2);
    static_assert(Part < LaneBytes / 2);
    constexpr unsigned begin = Part * (16 / LaneBytes) * Q;
    if constexpr (8 * Q < 16) {
        return body_detail::expand_bytes<Q, LaneBytes, begin>(
            body_detail::load_bytes<8 * Q>(packet));
    } else {
        constexpr unsigned window = begin < 8 * Q - 16 ? begin : 8 * Q - 16;
        return body_detail::expand_bytes<Q, LaneBytes, begin - window>(
            vld1q_u8(packet + window));
    }
}

// Pack all eight values together so stores may coalesce across native fragments.
// The only written bytes are the complete body's 8*Q bytes. Values[Part] follows
// the same ascending lane map as decode_packet_body<Q,LaneBytes,Part>.
template<unsigned Q, unsigned LaneBytes>
[[gnu::always_inline]] inline void encode_packet_body(
    std::uint8_t* packet, const std::array<uint8x16_t, LaneBytes / 2>& values) noexcept {
    static_assert(body_detail::supported<Q, LaneBytes> && LaneBytes >= 2);
    [&]<std::size_t... Chunk>(std::index_sequence<Chunk...>) {
        (body_detail::encode_packet_chunk<Q, LaneBytes, Chunk>(packet, values), ...);
    }(std::make_index_sequence<(8 * Q + 15) / 16>{});
}

} // namespace ikea_predecessor::seriespack::detail::neon

#endif // defined(__aarch64__)
