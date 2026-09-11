#pragma once

#if defined(__aarch64__)

#include <ikea_predecessor/seriespack/detail/body_neon.h>
#include <ikea_predecessor/seriespack/detail/physical.h>

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace ikea_predecessor::seriespack::neon {

/// Native payload shape: W is stored payload bits; lane width is in bytes.
/// Only the low `lanes` positions belong to the fragment; other register lanes
/// carry no logical-value promise, even when their bits are defined as zero.
template<unsigned W, geometry G, unsigned LaneBytes = sizeof(scalar_for_width<W>)>
struct fragment_traits {
    static_assert(W <= LaneBytes * 8);
    static_assert(LaneBytes == 1 || LaneBytes == 2 || LaneBytes == 4 || LaneBytes == 8);
    using vector_type = uint8x16_t;
    static constexpr unsigned lane_bytes = LaneBytes;
    static constexpr unsigned lanes = std::min<std::size_t>(
        payload_layout<W, G>::tile_values, 16 / LaneBytes);
};

namespace native_detail {

namespace body = detail::neon;
namespace bytes = body::body_detail;

template<unsigned L, int Shift>
[[gnu::always_inline]] inline uint8x16_t shift(uint8x16_t x) noexcept {
    static_assert(L == 1 || L == 2 || L == 4 || L == 8);
    static_assert(Shift > -int(8 * L) && Shift < int(8 * L));
    if constexpr (Shift == 0) return x;
    else if constexpr (Shift > 0) {
        if constexpr (L == 1) return vshlq_n_u8(x, Shift);
        if constexpr (L == 2) return vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(x), Shift));
        if constexpr (L == 4) return vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(x), Shift));
        if constexpr (L == 8) return vreinterpretq_u8_u64(vshlq_n_u64(vreinterpretq_u64_u8(x), Shift));
    } else {
        if constexpr (L == 1) return vshrq_n_u8(x, -Shift);
        if constexpr (L == 2) return vreinterpretq_u8_u16(vshrq_n_u16(vreinterpretq_u16_u8(x), -Shift));
        if constexpr (L == 4) return vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(x), -Shift));
        if constexpr (L == 8) return vreinterpretq_u8_u64(vshrq_n_u64(vreinterpretq_u64_u8(x), -Shift));
    }
}

// Execution ordering derived from the wire fields. Each destination consists
// of disjoint contiguous fields; shift-inserts replace excess high bits from
// earlier leaves instead of requiring a mask at every leaf. This is also valid
// when the source values carry arbitrary higher bits for an enclosing head.
template<unsigned R, unsigned Fixed, bool Packing>
struct residual_order {
    static constexpr unsigned candidates = (Packing ? 8 : R) / std::gcd(R, 8u);
    struct segment { unsigned from, source, destination, length; };
    struct description {
        std::array<segment, candidates> fields{};
        unsigned count = 0, occupied = 0;
        bool contiguous = true, disjoint = true;
    };
    static constexpr description plan = [] {
        description result;
        const auto add = [&](auto field, unsigned from) {
            if (field.value_mask == 0) return;
            const unsigned source = Packing ? field.value_mask : field.wire_mask;
            const unsigned destination = Packing ? field.wire_mask : field.value_mask;
            const unsigned s = std::countr_zero(source), d = std::countr_zero(destination);
            result.contiguous &= std::has_single_bit((source >> s) + 1) &&
                                 std::has_single_bit((destination >> d) + 1);
            result.disjoint &= (result.occupied & destination) == 0;
            result.occupied |= destination;
            result.fields[result.count++] = {from, s, d, unsigned(std::popcount(source))};
        };
        detail::static_for<candidates>([&](auto source) {
            if constexpr (Packing) add(detail::residual_field<R, source, Fixed>::value, source);
            else add(detail::residual_field<R, Fixed, source>::value, source);
        });
        for (unsigned i = 1; i < result.count; ++i)
            for (unsigned j = i; j != 0 && result.fields[j].destination < result.fields[j - 1].destination; --j)
                std::swap(result.fields[j], result.fields[j - 1]);
        return result;
    }();
    static_assert(plan.count != 0 && plan.contiguous && plan.disjoint);
    static_assert(plan.occupied == (Packing ? 255u : (1u << R) - 1));
};

template<unsigned R, unsigned Stripe, unsigned First, unsigned Count>
[[gnu::always_inline]] inline uint8x16_t pack_fields(
    const std::array<uint8x16_t, 8 / std::gcd(R, 8u)>& values) noexcept {
    constexpr auto p = residual_order<R, Stripe, true>::plan;
    if constexpr (Count == 1)
        return shift<1, -int(p.fields[First].source)>(values[p.fields[First].from]);
    else {
        constexpr unsigned middle = First + Count / 2;
        constexpr unsigned offset = p.fields[middle].destination - p.fields[First].destination;
        const auto low = pack_fields<R, Stripe, First, Count / 2>(values);
        const auto high = pack_fields<R, Stripe, middle, Count - Count / 2>(values);
        return vsliq_n_u8(low, high, offset);
    }
}

template<unsigned W, unsigned N, unsigned Group, unsigned First, unsigned Count>
[[gnu::always_inline]] inline uint8x16_t unpack_fields(const std::uint8_t* tile,
                                                       unsigned lane) noexcept {
    constexpr auto p = residual_order<W % 8, Group, false>::plan;
    if constexpr (Count == 1) {
        constexpr auto f = p.fields[First];
        return shift<1, -int(f.source)>(bytes::load_bytes<N>(
            tile + detail::stripe_offset<W>(f.from) + lane));
    } else {
        constexpr unsigned middle = First + Count / 2;
        constexpr unsigned offset = p.fields[middle].destination - p.fields[First].destination;
        const auto low = unpack_fields<W, N, Group, First, Count / 2>(tile, lane);
        const auto high = unpack_fields<W, N, Group, middle, Count - Count / 2>(tile, lane);
        return vsliq_n_u8(low, high, offset);
    }
}

template<unsigned W, unsigned L, unsigned Group>
[[gnu::always_inline]] inline uint8x16_t striped_tail(
    const std::uint8_t* tile, unsigned lane) noexcept {
    constexpr unsigned R = W % 8, N = 16 / L;
    constexpr auto p = residual_order<R, Group, false>::plan;
    constexpr auto last = p.fields[p.count - 1];
    auto low = unpack_fields<W, N, Group, 0, p.count>(tile, lane);
    if constexpr (last.source + last.length != 8)
        low = vandq_u8(low, vdupq_n_u8((1u << R) - 1));
    return bytes::expand_bytes<1, L>(low);
}

template<unsigned W, unsigned L, unsigned Group>
[[gnu::always_inline]] inline uint8x16_t striped_values(
    const std::uint8_t* tile, unsigned lane) noexcept {
    constexpr unsigned Q = W / 8, R = W % 8;
    auto value = striped_tail<W, L, Group>(tile, lane);
    if constexpr (Q != 0) {
        const auto high = body::decode_body<Q, L>(
            tile + detail::body_offset<W, geometry::striped>(Group * 32 + lane));
        value = vorrq_u8(value, shift<L, R>(high));
    }
    return value;
}

template<unsigned L>
[[gnu::always_inline]] inline uint8x16_t low_packet_bytes(
    const std::array<uint8x16_t, std::max(1u, L / 2)>& values) noexcept {
    if constexpr (L == 1) return values[0];
    else {
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result{};
            for (unsigned i = 0; i != 16; ++i)
                result[i] = i < 8 ? static_cast<std::uint8_t>(i * L) : 255;
            return result;
        }();
        return bytes::table<0, L / 2>(values, vld1q_u8(indices.data()));
    }
}

[[gnu::always_inline]] inline uint8x16_t transpose_pair(uint8x16_t bytes) noexcept {
    auto x = vreinterpretq_u64_u8(bytes);
    const auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
        auto changed = vandq_u64(veorq_u64(x, vshrq_n_u64(x, Shift)), vdupq_n_u64(Mask));
        // Clang 21 otherwise scalarizes known-zero exchange terms into lane
        // extracts/inserts or narrowing multiplies. This emits no instruction
        // and keeps the exchange in NEON; it is not a memory barrier.
        asm("" : "+w"(changed));
        x = veorq_u64(x, veorq_u64(changed, vshlq_n_u64(changed, Shift)));
    };
    exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14, 0x0000cccc0000ccccULL>();
    exchange.template operator()<28, 0x00000000f0f0f0f0ULL>();
    return vreinterpretq_u8_u64(x);
}

template<unsigned L, unsigned To, bool KeepNative = false>
[[gnu::always_inline]] inline std::array<uint8x16_t, To> narrow_lanes(
    const std::array<uint8x16_t, L>& values) noexcept {
    static_assert(To == 1 || To == 2 || To == 4 || To == 8);
    static_assert(To <= L);
    if constexpr (L == To) return values;
    else {
        std::array<uint8x16_t, L / 2> half{};
        detail::static_for<L / 2>([&](auto i) {
            if constexpr (L == 8) half[i] = vreinterpretq_u8_u32(vcombine_u32(
                vmovn_u64(vreinterpretq_u64_u8(values[2 * i])),
                vmovn_u64(vreinterpretq_u64_u8(values[2 * i + 1]))));
            if constexpr (L == 4) half[i] = vreinterpretq_u8_u16(vcombine_u16(
                vmovn_u32(vreinterpretq_u32_u8(values[2 * i])),
                vmovn_u32(vreinterpretq_u32_u8(values[2 * i + 1]))));
            if constexpr (L == 2) half[i] = vcombine_u8(
                vmovn_u16(vreinterpretq_u16_u8(values[2 * i])),
                vmovn_u16(vreinterpretq_u16_u8(values[2 * i + 1])));
            // In the wider striped region, preserve successive narrowing
            // stages. Clang 21 otherwise forms large consecutive-register
            // table operands and spills live input banks around them.
            if constexpr (KeepNative) asm("" : "+w"(half[i]));
        });
        return narrow_lanes<L / 2, To, KeepNative>(half);
    }
}

template<unsigned L>
[[gnu::always_inline]] inline uint8x16_t narrow_values(
    const std::array<uint8x16_t, L>& values) noexcept {
    return narrow_lanes<L, 1>(values)[0];
}

// One requested bit plane does not need the full eight-plane transpose.
// Masking is necessary for low-bit projection from values carrying heads.
[[gnu::always_inline]] inline uint8x16_t weight_low_bits(uint8x16_t values) noexcept {
    const int8x16_t shifts = {0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7};
    return vshlq_u8(vandq_u8(values, vdupq_n_u8(1)), shifts);
}

// A sixty-four-value region spans eight adjacent Local1 tiles. The trusted
// preset consumes the enclosing K=1 admission; the projection preset also
// accepts arbitrary higher source bits. Both write exactly eight bytes.
template<bool WidthValid, std::unsigned_integral UInt>
[[gnu::always_inline]] inline void encode_local1_octet(
    const UInt* __restrict in, std::uint8_t* __restrict out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    std::array<uint8x16_t, 4> weighted{};
    detail::static_for<4>([&](auto block) {
        std::array<uint8x16_t, L> values{};
        detail::static_for<L>([&](auto part) {
            values[part] = vld1q_u8(reinterpret_cast<const std::uint8_t*>(
                in + block * 16 + part * (16 / L)));
        });
        const auto low = narrow_lanes<L, 1, true>(values)[0];
        if constexpr (WidthValid) {
            const uint8x16_t weights = {1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
            weighted[block] = vmulq_u8(low, weights);
        } else weighted[block] = weight_low_bits(low);
    });
    const auto a = vpaddq_u8(weighted[0], weighted[1]);
    const auto b = vpaddq_u8(weighted[2], weighted[3]);
    const auto c = vpaddq_u8(a, b);
    bytes::store_bytes<8>(out, vpaddq_u8(c, c));
}

template<unsigned W, unsigned Pair>
[[gnu::always_inline]] inline uint8x16_t expand_low_planes(uint8x16_t encoded) noexcept {
    static_assert(W == 1 || W == 2);
    const uint8x16_t weights = {1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
    auto result = vdupq_n_u8(0);
    detail::static_for<W>([&](auto plane) {
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result{};
            for (unsigned lane = 0; lane != 16; ++lane)
                result[lane] = (Pair * 2 + lane / 8) * W + decltype(plane)::value;
            return result;
        }();
        auto repeated = vqtbl1q_u8(encoded, vld1q_u8(indices.data()));
        auto tested = vtstq_u8(repeated, weights);
        // Keep the predicate in a native register: Clang 21 otherwise expands
        // test-and-select into AND, inverted equality and BIC instead of CMTST.
        asm("" : "+w"(tested));
        auto low = vandq_u8(tested, vdupq_n_u8(1u << plane));
        result = vorrq_u8(result, low);
    });
    return result;
}

} // namespace native_detail

/// Read the whole-byte body (payload value shifted right by W % 8).
/// L is unsigned lane width in bytes; lane i names tile position Begin+i.
/// The caller supplies a complete readable payload tile and a fragment-aligned
/// Begin within it. Reads no heads, bytes beyond the physical tile, or stride gaps.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline uint8x16_t read_body(const std::uint8_t* tile) noexcept {
    using F = fragment_traits<W, G, L>;
    static_assert(Begin % F::lanes == 0 && Begin + F::lanes <= payload_layout<W, G>::tile_values);
    constexpr unsigned Q = W / 8;
    if constexpr (Q == 0) return vdupq_n_u8(0);
    else if constexpr (G == geometry::local8 && L >= 2)
        return detail::neon::decode_packet_body<Q, L, Begin / F::lanes>(tile);
    else if constexpr (G == geometry::local8)
        return detail::neon::decode_body_prefix<Q, L, F::lanes>(tile);
    else
        return detail::neon::decode_body<Q, L>(tile + detail::body_offset<W, G>(Begin));
}

/// Read the low W % 8 payload bits, zero-extended into L-byte unsigned lanes.
/// Lane i names tile position Begin+i; Begin must align to this fragment's grain.
/// Requires the complete readable payload tile; reads no heads or stride gaps.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline uint8x16_t read_tail(const std::uint8_t* tile) noexcept {
    using F = fragment_traits<W, G, L>;
    static_assert(Begin % F::lanes == 0 && Begin + F::lanes <= payload_layout<W, G>::tile_values);
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (R == 0) return vdupq_n_u8(0);
    else if constexpr (G == geometry::local8) {
        const auto tails = detail::transpose_bytes(detail::load_le<R>(tile + 8 * Q));
        const auto low = vcombine_u8(vcreate_u8(tails), vdup_n_u8(0));
        return native_detail::bytes::expand_bytes<1, L, Begin>(low);
    } else {
        return native_detail::striped_tail<W, L, Begin / 32>(tile, Begin % 32);
    }
}

template<unsigned LaneBytes, unsigned TailBits>
[[gnu::always_inline]] inline uint8x16_t join(uint8x16_t body, uint8x16_t tail) noexcept {
    return vorrq_u8(native_detail::shift<LaneBytes, TailBits>(body), tail);
}

/// W-bit payload values without heads; lane i names tile position Begin+i.
/// L is lane width in bytes. Requires a complete readable payload tile and Begin
/// aligned to fragment_traits<W, G, L>::lanes; performs no admission.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline uint8x16_t read_fragment(const std::uint8_t* tile) noexcept {
    return join<L, W % 8>(read_body<W, G, L, Begin>(tile), read_tail<W, G, L, Begin>(tile));
}

// Full-tile trusted decoder. Input and output storage are disjoint. The native
// lane width follows UInt; no intermediate materialized tile or per-value scalar
// codec is imposed. Complete-tile extents are caller facts.
template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void decode_tile(const std::uint8_t* __restrict tile, UInt* __restrict out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    using F = fragment_traits<W, G, L>;
    if constexpr (G == geometry::local8) {
        detail::static_for<8 / F::lanes>([&](auto part) {
            const auto x = read_fragment<W, G, L, part * F::lanes>(tile);
            native_detail::bytes::store_bytes<F::lanes * L>(
                reinterpret_cast<std::uint8_t*>(out + part * F::lanes), x);
        });
    } else {
        constexpr unsigned Q = W / 8, R = W % 8;
        constexpr unsigned WorkL = sizeof(scalar_for_width<W>), N = 16 / WorkL;
        // Decode each sixteen-byte tail region once before widening for the
        // requested output scalar. A u64 destination does not force repeated
        // two-byte field assembly for values that fit in byte lanes.
        detail::static_for<payload_layout<W, G>::tile_values / 32>([&](auto group) {
            for (unsigned lane = 0; lane < 32; lane += 16) {
                const auto low = native_detail::striped_tail<W, 1, group>(tile, lane);
                detail::static_for<WorkL>([&](auto part) {
                    auto value = native_detail::bytes::expand_bytes<1, WorkL, part * N>(low);
                    if constexpr (Q != 0) {
                        const auto high = native_detail::body::decode_body<Q, WorkL>(
                            tile + detail::body_offset<W, G>(group * 32 + lane + part * N));
                        value = join<WorkL, R>(high, value);
                    }
                    detail::static_for<L / WorkL>([&](auto sub) {
                        const auto expanded = native_detail::bytes::expand_bytes<WorkL, L,
                            sub * (16 / L) * WorkL>(value);
                        vst1q_u8(reinterpret_cast<std::uint8_t*>(
                            out + group * 32 + lane + part * N + sub * (16 / L)), expanded);
                    });
                });
            }
        });
    }
}

// Project the low W bits. Narrower input types widen inside native registers;
// wider inputs may carry separately stored heads. Both extents are exact and
// disjoint. No input-width validation or materialized widening array is imposed.
template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void encode_low_tile(const UInt* __restrict in, std::uint8_t* __restrict tile) noexcept {
    constexpr unsigned InputL = sizeof(UInt);
    constexpr unsigned L = std::max(InputL, unsigned(sizeof(scalar_for_width<W>)));
    constexpr unsigned Q = W / 8, R = W % 8;
    using F = fragment_traits<W, G, L>;
    if constexpr (W == 0) return;
    else if constexpr (G == geometry::local8) {
        std::array<uint8x16_t, 8 / F::lanes> values{};
        detail::static_for<8 / F::lanes>([&](auto part) {
            values[part] = detail::neon::decode_body_prefix<InputL, L, F::lanes>(
                reinterpret_cast<const std::uint8_t*>(in + part * F::lanes));
        });
        if constexpr (R != 0) {
            const auto low = native_detail::low_packet_bytes<L>(values);
            if constexpr (R == 1)
                tile[8 * Q] = vaddv_u8(vget_low_u8(native_detail::weight_low_bits(low)));
            else {
                const auto transposed = detail::transpose_bytes(vgetq_lane_u64(vreinterpretq_u64_u8(low), 0));
                detail::store_le<R>(tile + 8 * Q, transposed);
            }
        }
        if constexpr (Q != 0) {
            if constexpr (L == 1) native_detail::bytes::store_bytes<8>(tile, values[0]);
            else {
                detail::static_for<8 / F::lanes>([&](auto part) {
                    values[part] = native_detail::shift<L, -int(R)>(values[part]);
                });
                detail::neon::encode_packet_body<Q, L>(tile, values);
            }
        }
    } else {
        constexpr unsigned Groups = payload_layout<W, G>::tile_values / 32;
        constexpr unsigned Stripes = R / std::gcd(R, 8u);
        constexpr unsigned WorkL = sizeof(scalar_for_width<W>);
        // Tail packing has sixteen byte lanes even when the input uses wider
        // scalars. Retain those values in registers through body projection and
        // narrowing, then write complete tail vectors and coalesced bodies.
        for (unsigned lane = 0; lane < 32; lane += 16) {
            std::array<uint8x16_t, Groups> lows{};
            detail::static_for<Groups>([&](auto group) {
                std::array<uint8x16_t, WorkL> values{};
                if constexpr (InputL > WorkL) {
                    std::array<uint8x16_t, InputL> input{};
                    detail::static_for<InputL>([&](auto part) {
                        input[part] = vld1q_u8(reinterpret_cast<const std::uint8_t*>(
                            in + group * 32 + lane + part * (16 / InputL)));
                    });
                    // Only low W bits are projected. Narrow wide input once
                    // before body/tail divergence instead of shifting full
                    // source-width lanes and narrowing each branch separately.
                    values = native_detail::narrow_lanes<InputL, WorkL, true>(input);
                } else {
                    detail::static_for<WorkL>([&](auto part) {
                        values[part] = detail::neon::decode_body_prefix<InputL, WorkL, 16 / WorkL>(
                            reinterpret_cast<const std::uint8_t*>(in + group * 32 + lane + part * (16 / WorkL)));
                    });
                }
                lows[group] = native_detail::narrow_lanes<WorkL, 1, true>(values)[0];
                if constexpr (Q != 0) {
                    detail::static_for<WorkL>([&](auto part) {
                        values[part] = native_detail::shift<WorkL, -int(R)>(values[part]);
                    });
                    const auto high = native_detail::narrow_lanes<WorkL, Q, true>(values);
                    detail::static_for<Q>([&](auto part) {
                        vst1q_u8((tile + detail::body_offset<W, G>(group * 32 + lane) + part * 16),
                                 high[part]);
                    });
                }
            });
            detail::static_for<Stripes>([&](auto stripe) {
                constexpr unsigned count = native_detail::residual_order<R, stripe, true>::plan.count;
                const auto packed = native_detail::pack_fields<R, stripe, 0, count>(lows);
                vst1q_u8(tile + detail::stripe_offset<W>(stripe) + lane, packed);
            });
        }
    }
}

// The enclosing trusted encode contract has already established values fit W.
template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
[[gnu::always_inline]] inline void encode_tile(
    const UInt* __restrict in, std::uint8_t* __restrict tile) noexcept {
    encode_low_tile<W, G>(in, tile);
}

// A distinct dense region: precisely two adjacent Local8 tiles, with no stride
// gap between them. The sixteen byte lanes name original indices 0..15. Both
// transposes run together in native 64-bit lanes; no eight-value handoff is
// introduced at the internal seam. This entry cannot be used for a lone tile.
template<unsigned W>
[[gnu::always_inline]] inline uint8x16_t read_local_pair(const std::uint8_t* tiles) noexcept {
    static_assert(W >= 1 && W <= 7);
    if constexpr (W <= 2)
        return native_detail::expand_low_planes<W, 0>(native_detail::bytes::load_bytes<2 * W>(tiles));
    else return native_detail::transpose_pair(detail::neon::decode_body<W, 8>(tiles));
}

template<unsigned W, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void decode_local_pair(const std::uint8_t* __restrict tiles, UInt* __restrict out) noexcept {
    const auto values = read_local_pair<W>(tiles);
    constexpr unsigned L = sizeof(UInt);
    static_assert(L == 1 || L == 2 || L == 4 || L == 8);
    detail::static_for<L>([&](auto part) {
        const auto x = native_detail::bytes::expand_bytes<1, L, part * (16 / L)>(values);
        vst1q_u8(reinterpret_cast<std::uint8_t*>(out + part * (16 / L)), x);
    });
}

template<unsigned W, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void encode_low_local_pair(const UInt* __restrict in, std::uint8_t* __restrict tiles) noexcept {
    static_assert(W >= 1 && W <= 7);
    constexpr unsigned L = sizeof(UInt);
    static_assert(L == 1 || L == 2 || L == 4 || L == 8);
    std::array<uint8x16_t, L> values{};
    detail::static_for<L>([&](auto part) {
        values[part] = vld1q_u8(reinterpret_cast<const std::uint8_t*>(in + part * (16 / L)));
    });
    const auto low = native_detail::narrow_values<L>(values);
    if constexpr (W == 1) {
        auto packed = native_detail::weight_low_bits(low);
        packed = vpaddq_u8(packed, packed);
        packed = vpaddq_u8(packed, packed);
        packed = vpaddq_u8(packed, packed);
        native_detail::bytes::store_bytes<2>(tiles, packed);
    } else {
        const auto transposed = native_detail::transpose_pair(low);
        detail::neon::encode_body<W, 8>(tiles, transposed);
    }
}

template<unsigned W, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
[[gnu::always_inline]] inline void encode_local_pair(
    const UInt* __restrict in, std::uint8_t* __restrict tiles) noexcept {
    encode_low_local_pair<W>(in, tiles);
}

namespace native_detail {

// Eight adjacent Local8 tiles admit one 64-value region. Their W-byte plane
// packets can share complete vector loads/stores without reading a suffix or
// writing across a stride gap. The selectors discard high source planes, so
// encoding retains the low-bit projection contract.
template<unsigned W>
[[gnu::always_inline]] inline void encode_low_local_octet(
    const std::uint8_t* __restrict in, std::uint8_t* __restrict out) noexcept {
    static_assert(W >= 2 && W <= 7);
    std::array<uint8x16_t, 4> matrix;
    detail::static_for<4>([&](auto part) {
        matrix[part] = transpose_pair(vld1q_u8(in + 16 * part));
    });
    detail::static_for<(8 * W + 15) / 16>([&](auto chunk) {
        constexpr unsigned begin = 16 * chunk;
        constexpr unsigned count = std::min<unsigned>(16, 8 * W - begin);
        constexpr unsigned first = begin / W / 2;
        constexpr unsigned last = (begin + count - 1) / W / 2;
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result;
            result.fill(255);
            for (unsigned byte = 0; byte != count; ++byte)
                result[byte] = (begin + byte) / W * 8 + (begin + byte) % W - first * 16;
            return result;
        }();
        bytes::store_bytes<count>(out + begin,
            bytes::table<first, last - first + 1>(matrix, vld1q_u8(indices.data())));
    });
}

template<unsigned W>
[[gnu::always_inline]] inline void decode_local_octet(
    const std::uint8_t* __restrict in, std::uint8_t* __restrict out) noexcept {
    static_assert(W >= 3 && W <= 7);
    std::array<uint8x16_t, (8 * W + 15) / 16> encoded;
    detail::static_for<(8 * W + 15) / 16>([&](auto chunk) {
        constexpr unsigned count = std::min<unsigned>(16, 8 * W - 16 * chunk);
        encoded[chunk] = bytes::load_bytes<count>(in + 16 * chunk);
    });
    detail::static_for<4>([&](auto part) {
        constexpr unsigned first = part * 2 * W / 16;
        constexpr unsigned last = ((part + 1) * 2 * W - 1) / 16;
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 16> result;
            result.fill(255);
            for (unsigned byte = 0; byte != 16; ++byte)
                if (byte % 8 < W)
                    result[byte] = (decltype(part)::value * 2 + byte / 8) * W + byte % 8 - first * 16;
            return result;
        }();
        const auto matrix = bytes::table<first, last - first + 1>(encoded, vld1q_u8(indices.data()));
        vst1q_u8(out + part * 16, transpose_pair(matrix));
    });
}

} // namespace native_detail

// Dense exact tile region. One-bit packing reduces four native byte carriers
// together, so no intermediate two-byte handoff constrains its physical grain.
// The final pair/single cases consume only their own admitted source extents.
template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void encode_low_tiles(const UInt* __restrict in, std::uint8_t* __restrict out,
                             std::size_t tiles) noexcept {
    if constexpr (W == 0) return;
    constexpr auto T = payload_layout<W, G>::tile_values;
    constexpr auto B = payload_layout<W, G>::tile_bytes;
    std::size_t tile = 0;
    if constexpr (G == geometry::local8 && W == 1)
        for (; tiles - tile >= 8; tile += 8)
            native_detail::encode_local1_octet<false>(in + tile * T, out + tile);
    if constexpr (G == geometry::local8 && W >= 2 && W <= 7 && sizeof(UInt) == 1)
        for (; tiles - tile >= 8; tile += 8)
            native_detail::encode_low_local_octet<W>(
                reinterpret_cast<const std::uint8_t*>(in + tile * T), out + tile * B);
    if constexpr (G == geometry::local8 && W == 8 && sizeof(UInt) == 8) {
        // Retain the existing low-byte gather, exposing eight packets together
        // to reduce loop/store overhead. Larger regions increase live vectors.
#pragma clang loop unroll(disable)
        for (; tiles - tile >= 8; tile += 8)
            detail::static_for<8>([&](auto packet) {
                encode_low_tile<W, G>(in + (tile + packet) * T, out + (tile + packet) * B);
            });
    }
    if constexpr (G == geometry::local8 && W >= 1 && W <= 7)
        for (; tiles - tile >= 2; tile += 2)
            encode_low_local_pair<W>(in + tile * T, out + tile * B);
    for (; tile != tiles; ++tile)
        encode_low_tile<W, G>(in + tile * T, out + tile * B);
}

// Trusted Local1 dense encode: every source value is already known to be 0
// or 1. This narrow preset avoids projecting a head that admission proved absent.
// Remainders keep the same exact pair/single-tile handling as low-bit encoding.
template<std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void encode_local1_tiles(const UInt* __restrict in, std::uint8_t* __restrict out,
                                std::size_t tiles) noexcept {
    std::size_t tile = 0;
    for (; tiles - tile >= 8; tile += 8)
        native_detail::encode_local1_octet<true>(in + tile * 8, out + tile);
    if (tile != tiles)
        encode_low_tiles<1, geometry::local8>(in + tile * 8, out + tile, tiles - tile);
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void decode_tiles(const std::uint8_t* __restrict in, UInt* __restrict out,
                         std::size_t tiles) noexcept {
    constexpr auto T = payload_layout<W, G>::tile_values;
    constexpr auto B = payload_layout<W, G>::tile_bytes;
    constexpr unsigned L = sizeof(UInt);
    static_assert(W <= 8 * L);
    std::size_t tile = 0;
    if constexpr (G == geometry::local8 && (W == 1 || W == 2)) {
        for (; tiles - tile >= 8; tile += 8) {
            const auto encoded = native_detail::bytes::load_bytes<8 * W>(in + tile * B);
            detail::static_for<4>([&](auto pair) {
                const auto values = native_detail::expand_low_planes<W, pair>(encoded);
                detail::static_for<L>([&](auto part) {
                    const auto expanded = native_detail::bytes::expand_bytes<1, L, part * (16 / L)>(values);
                    vst1q_u8(reinterpret_cast<std::uint8_t*>(out + tile * T + pair * 16 + part * (16 / L)), expanded);
                });
            });
        }
    }
    if constexpr (G == geometry::local8 && W >= 3 && W <= 7 && L == 1)
        for (; tiles - tile >= 8; tile += 8)
            native_detail::decode_local_octet<W>(
                in + tile * B, reinterpret_cast<std::uint8_t*>(out + tile * T));
    if constexpr (G == geometry::local8 && W >= 1 && W <= 7)
        for (; tiles - tile >= 2; tile += 2)
            decode_local_pair<W>(in + tile * B, out + tile * T);
    for (; tile != tiles; ++tile)
        decode_tile<W, G>(W == 0 ? nullptr : in + tile * B, out + tile * T);
}

} // namespace ikea_predecessor::seriespack::neon

#endif // defined(__aarch64__)
