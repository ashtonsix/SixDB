#pragma once
#include <ikea2/seriespack/detail/native/neon/read.h>

namespace ikea2::seriespack::neon {
template <class U> [[gnu::always_inline]] inline values<sizeof(U) * 8> load_values(const U* input) {
    values<sizeof(U) * 8> out;
    detail::each<sizeof(U)>([&](auto p) {
        out.v[p] = vld1q_u8(reinterpret_cast<const std::uint8_t*>(input + p * (16 / sizeof(U))));
    });
    return out;
}
template <unsigned To, unsigned From>
[[gnu::always_inline]] inline values<To> narrow(values<From> in) {
    constexpr unsigned A = sizeof(uint_for<From>), B = sizeof(uint_for<To>);
    static_assert(A >= B);
    if constexpr (A == B)
        return {in.v};
    else {
        values<A * 4> next;
        detail::each<A / 2>([&](auto p) {
            if constexpr (A == 8)
                next.v[p] = vreinterpretq_u8_u32(
                    vcombine_u32(vmovn_u64(vreinterpretq_u64_u8(in.v[p * 2])),
                                 vmovn_u64(vreinterpretq_u64_u8(in.v[p * 2 + 1]))));
            if constexpr (A == 4)
                next.v[p] = vreinterpretq_u8_u16(
                    vcombine_u16(vmovn_u32(vreinterpretq_u32_u8(in.v[p * 2])),
                                 vmovn_u32(vreinterpretq_u32_u8(in.v[p * 2 + 1]))));
            if constexpr (A == 2)
                next.v[p] = vcombine_u8(vmovn_u16(vreinterpretq_u16_u8(in.v[p * 2])),
                                        vmovn_u16(vreinterpretq_u16_u8(in.v[p * 2 + 1])));
        });
        return narrow<To>(next);
    }
}
template <unsigned Shift, unsigned K> [[gnu::always_inline]] inline values<K> right(values<K> x) {
    constexpr unsigned L = sizeof(uint_for<K>);
    if constexpr (Shift)
        detail::each<L>([&](auto p) {
            if constexpr (L == 1)
                x.v[p] = vshrq_n_u8(x.v[p], Shift);
            if constexpr (L == 2)
                x.v[p] = vreinterpretq_u8_u16(vshrq_n_u16(vreinterpretq_u16_u8(x.v[p]), Shift));
            if constexpr (L == 4)
                x.v[p] = vreinterpretq_u8_u32(vshrq_n_u32(vreinterpretq_u32_u8(x.v[p]), Shift));
            if constexpr (L == 8)
                x.v[p] = vreinterpretq_u8_u64(vshrq_n_u64(vreinterpretq_u64_u8(x.v[p]), Shift));
        });
    return x;
}
/// Extract a semantic bit window before narrowing: inactive wider input lanes
/// may contain arbitrary bits, and saturating hardware narrowing is not masking.
template <unsigned To, unsigned Shift, unsigned From>
[[gnu::always_inline]] inline values<To> project(values<From> x) {
    static_assert(To + Shift <= From);
    auto y = right<Shift>(x);
    if constexpr (To < From - Shift)
        detail::each<values<From>::parts>([&](auto p) {
            constexpr auto L = sizeof(uint_for<From>);
            constexpr auto mask = (std::uint64_t{1} << To) - 1;
            if constexpr (L == 1)
                y.v[p] = vandq_u8(y.v[p], vdupq_n_u8(mask));
            if constexpr (L == 2)
                y.v[p] = vandq_u8(y.v[p], vreinterpretq_u8_u16(vdupq_n_u16(mask)));
            if constexpr (L == 4)
                y.v[p] = vandq_u8(y.v[p], vreinterpretq_u8_u32(vdupq_n_u32(mask)));
            if constexpr (L == 8)
                y.v[p] = vandq_u8(y.v[p], vreinterpretq_u8_u64(vdupq_n_u64(mask)));
        });
    return narrow<To>(y);
}
template <unsigned To, unsigned Shift, unsigned From>
[[gnu::always_inline]] inline values<To> place(values<From> x) {
    static_assert(From + Shift <= To);
    auto y = widen<To>(x);
    if constexpr (Shift)
        detail::each<values<To>::parts>([&](auto p) {
            constexpr auto L = sizeof(uint_for<To>);
            if constexpr (L == 1)
                y.v[p] = vshlq_n_u8(y.v[p], Shift);
            if constexpr (L == 2)
                y.v[p] = vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(y.v[p]), Shift));
            if constexpr (L == 4)
                y.v[p] = vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(y.v[p]), Shift));
            if constexpr (L == 8)
                y.v[p] = vreinterpretq_u8_u64(vshlq_n_u64(vreinterpretq_u64_u8(y.v[p]), Shift));
        });
    return y;
}
template <unsigned K>
[[gnu::always_inline]] inline values<K> choose(std::uint16_t active, values<K> yes, values<K> no) {
    const auto mask = mask16<K>(active);
    detail::each<values<K>::parts>(
        [&](auto p) { yes.v[p] = vbslq_u8(mask.v[p], yes.v[p], no.v[p]); });
    return yes;
}
template <unsigned First, unsigned Count, std::size_t Parts>
[[gnu::always_inline]] inline uint8x16_t table(const std::array<uint8x16_t, Parts>& source,
                                               const std::array<std::uint8_t, 16>& index) {
    const auto map = vld1q_u8(index.data());
    if constexpr (Count == 1)
        return vqtbl1q_u8(source[First], map);
    if constexpr (Count == 2)
        return vqtbl2q_u8({{source[First], source[First + 1]}}, map);
    if constexpr (Count == 3)
        return vqtbl3q_u8({{source[First], source[First + 1], source[First + 2]}}, map);
    if constexpr (Count == 4)
        return vqtbl4q_u8(
            {{source[First], source[First + 1], source[First + 2], source[First + 3]}}, map);
}
template <unsigned K> [[gnu::always_inline]] inline uint8x16_t low_bytes(values<K> x) {
    constexpr unsigned L = sizeof(uint_for<K>);
    if constexpr (L == 1)
        return x.v[0];
    else {
        static constexpr auto map = [] {
            std::array<std::uint8_t, 16> a{};
            a.fill(255);
            for (unsigned i = 0; i < 8; ++i)
                a[i] = i * L;
            return a;
        }();
        const auto lo = table<0, L / 2>(x.v, map), hi = table<L / 2, L / 2>(x.v, map);
        return vcombine_u8(vget_low_u8(lo), vget_low_u8(hi));
    }
}
template <unsigned N>
[[gnu::always_inline]] inline void store_prefix(std::uint8_t* p, uint8x16_t x) {
    if constexpr (N == 16)
        vst1q_u8(p, x);
    else if constexpr (N <= 8)
        detail::store<N>(p, vgetq_lane_u64(vreinterpretq_u64_u8(x), 0));
    else {
        detail::store<8>(p, vgetq_lane_u64(vreinterpretq_u64_u8(x), 0));
        detail::store<N - 8>(p + 8, vgetq_lane_u64(vreinterpretq_u64_u8(x), 1));
    }
}
template <class F, unsigned Fields = 15>
[[gnu::always_inline]] inline void write16(const view<F, std::uint8_t>& destination, std::size_t i,
                                           values<F::width> x) {
    __builtin_assume(i % 16 == 0);
    constexpr unsigned Q = F::body, R = F::tail, L = sizeof(uint_for<F::width>);
    const auto& plane = destination.stream(0);
    const auto lane = i % F::tile_rows;
    auto* tile = plane.bytes.data();
    if constexpr (F::payload)
        tile += (i / F::tile_rows) * plane.stride;
    if constexpr (Q && (Fields & 1) && std::has_single_bit(Q)) {
        const auto body = project<Q * 8, R>(x);
        if constexpr (F::storage == geometry::striped)
            store16(reinterpret_cast<uint_for<Q * 8>*>(tile + detail::body_offset<F>(lane)), body);
        else
            detail::each<2>([&](auto p) {
                auto* out = tile + p * plane.stride;
                if constexpr (Q == 1) {
                    if constexpr (decltype(p)::value == 0)
                        vst1_u8(out, vget_low_u8(body.v[0]));
                    else
                        vst1_u8(out, vget_high_u8(body.v[0]));
                } else
                    detail::each<Q / 2>(
                        [&](auto c) { vst1q_u8(out + c * 16, body.v[p * (Q / 2) + c]); });
            });
    } else if constexpr (Q && (Fields & 1)) {
        const auto high = right<R>(x);
        constexpr unsigned packets = F::storage == geometry::local ? 2 : 1;
        constexpr unsigned rows = 16 / packets;
        detail::each<packets>([&](auto p) {
            detail::each<(rows * Q + 15) / 16>([&](auto c) {
                constexpr unsigned begin = c * 16, count = std::min<unsigned>(16, rows * Q - begin);
                constexpr unsigned first = (p * rows + begin / Q) * L / 16;
                constexpr unsigned last = ((p * rows + (begin + count - 1) / Q) * L + Q - 1) / 16;
                static constexpr auto map = [] {
                    std::array<std::uint8_t, 16> a{};
                    a.fill(255);
                    for (unsigned j = 0; j < count; ++j)
                        a[j] = (decltype(p)::value * rows + (begin + j) / Q) * L + (begin + j) % Q -
                               first * 16;
                    return a;
                }();
                auto* out = tile +
                            (F::storage == geometry::local ? p * plane.stride
                                                           : detail::body_offset<F>(lane)) +
                            begin;
                store_prefix<count>(out, table<first, last - first + 1>(high.v, map));
            });
        });
    }
    if constexpr (R && (Fields & 2)) {
        const auto low = low_bytes(x);
        if constexpr (F::storage == geometry::local) {
            const auto packed = [&] {
                if constexpr (R == 1) {
                    // The one-bit operation applies equally to a standalone
                    // packet and a residual inside a wider/headed value.
                    static constexpr std::array<std::uint8_t, 16> weights{
                        1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
                    const auto weighted =
                        vandq_u8(vtstq_u8(low, vdupq_n_u8(1)), vld1q_u8(weights.data()));
                    return vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(weighted)));
                } else
                    return vreinterpretq_u64_u8(transpose128(vreinterpretq_u64_u8(low)));
            }();
            detail::store<R>(tile + 8 * Q, vgetq_lane_u64(packed, 0));
            detail::store<R>(tile + plane.stride + 8 * Q, vgetq_lane_u64(packed, 1));
        } else
            detail::tail_fragments<R>(lane / 32, [&](unsigned s, unsigned shift, unsigned mask) {
                auto* out = tile + detail::stripe_offset<F>(s) + lane % 32;
                const auto changed = vshlq_u8(low, vdupq_n_s8(shift));
                vst1q_u8(out, vbslq_u8(vdupq_n_u8(mask), changed, vld1q_u8(out)));
            });
    }
    detail::each<F::heads / 8>([&](auto p) {
        if constexpr (Fields & (4u << p)) {
            const auto bytes = low_bytes(right<F::width - 8 * (p + 1)>(x));
            const auto& head = destination.stream(p + 1);
            auto* out = head.bytes.data() + (i / F::tile_rows) * head.stride + lane;
            if constexpr (F::storage == geometry::local) {
                vst1_u8(out, vget_low_u8(bytes));
                vst1_u8(out + head.stride, vget_high_u8(bytes));
            } else
                vst1q_u8(out, bytes);
        }
    });
}

/// Dense pure-tail Local execution can join eight physical packets into one
/// store group while keeping the producer/maintenance contract in native lanes.
template <class F, class Produce>
[[gnu::always_inline]] inline void write_local64(std::uint8_t* out, Produce&& produce) {
    static_assert(F::storage == geometry::local && F::heads == 0 && F::width < 8);
    constexpr unsigned R = F::tail;
    if constexpr (R == 1) {
        // A one-bit packet is a weighted horizontal sum, not a full transpose.
        static constexpr std::array<std::uint8_t, 16> weights{1, 2, 4, 8, 16, 32, 64, 128,
                                                              1, 2, 4, 8, 16, 32, 64, 128};
        std::array<uint8x16_t, 4> weighted;
        detail::each<4>([&](auto p) {
            weighted[p] = vmulq_u8(produce(std::integral_constant<unsigned, p * 16>{}).v[0],
                                   vld1q_u8(weights.data()));
        });
        const auto pairs =
            vpaddq_u8(vpaddq_u8(weighted[0], weighted[1]), vpaddq_u8(weighted[2], weighted[3]));
        vst1_u8(out, vget_low_u8(vpaddq_u8(pairs, pairs)));
        return;
    }
    std::array<uint8x16_t, 4> packed;
    detail::each<4>([&](auto p) {
        packed[p] = transpose128(
            vreinterpretq_u64_u8(produce(std::integral_constant<unsigned, p * 16>{}).v[0]));
    });
    detail::each<(8 * R + 15) / 16>([&](auto c) {
        constexpr unsigned begin = c * 16, count = std::min<unsigned>(16, 8 * R - begin);
        constexpr unsigned first = (begin / R) / 2, last = ((begin + count - 1) / R) / 2;
        static constexpr auto map = [] {
            std::array<std::uint8_t, 16> a{};
            a.fill(255);
            for (unsigned j = 0; j < count; ++j)
                a[j] = (begin + j) / R * 8 + (begin + j) % R - first * 16;
            return a;
        }();
        store_prefix<count>(out + begin, table<first, last - first + 1>(packed, map));
    });
}

/// Accumulate one sixteen-lane half of a complete striped tile. The driver
/// supplies every contributing group, so no old packed bytes are needed here.
template <class F> struct striped_tail_batch {
    static_assert(F::storage == geometry::striped);
    static constexpr unsigned stripes = F::tail / std::gcd(F::tail, 8u);
    std::array<uint8x16_t, stripes> packed;
    striped_tail_batch() {
        detail::each<stripes>([&](auto s) { packed[s] = vdupq_n_u8(0); });
    }
    template <unsigned Group> [[gnu::always_inline]] void add(values<F::tail> value) {
        detail::tail_fragments<F::tail>(Group, [&](unsigned s, unsigned shift, unsigned mask) {
            packed[s] = vorrq_u8(
                packed[s], vandq_u8(vshlq_u8(value.v[0], vdupq_n_s8(shift)), vdupq_n_u8(mask)));
        });
    }
    [[gnu::always_inline]] void store(std::uint8_t* tile, unsigned half) const {
        detail::each<stripes>(
            [&](auto s) { vst1q_u8(tile + detail::stripe_offset<F>(s) + half * 16, packed[s]); });
    }
};

/// All regions have been admitted for writing. Produce native values (including
/// any preservation/maintenance), write bodies and heads, and assemble complete
/// stripes without reading the old packed stripe for every contributing group.
template <class F, class Produce>
[[gnu::always_inline]] inline void write_striped_tile(const view<F, std::uint8_t>& destination,
                                                      std::size_t first, Produce&& produce) {
    static_assert(F::storage == geometry::striped);
    __builtin_assume(first % F::tile_rows == 0);
    constexpr unsigned groups = F::tile_rows / 32, stripes = F::tail / std::gcd(F::tail, 8u);
    auto* tile =
        destination.stream(0).bytes.data() + (first / F::tile_rows) * destination.stream(0).stride;
    detail::each<2>([&](auto half) {
        std::array<uint8x16_t, groups> low;
        detail::each<groups>([&](auto g) {
            constexpr unsigned offset = g * 32 + half * 16;
            const auto value = produce(std::integral_constant<unsigned, offset>{});
            write16<F, 13>(destination, first + offset, value);
            low[g] = low_bytes(value);
        });
        if constexpr (F::tail == 1 || F::tail == 2 || F::tail == 4) {
            // Each insert overwrites the higher fields; the last discards all
            // unrelated high bits. A complete stripe needs no per-input mask.
            auto packed = low[0];
            detail::each<groups - 1>(
                [&](auto g) { packed = vsliq_n_u8(packed, low[g + 1], (g + 1) * F::tail); });
            vst1q_u8(tile + detail::stripe_offset<F>(0) + half * 16, packed);
        } else
            detail::each<stripes>([&](auto s) {
                auto packed = vdupq_n_u8(0);
                detail::each<groups>([&](auto g) {
                    detail::tail_fragments<F::tail>(g, [&](unsigned field, unsigned shift,
                                                           unsigned mask) {
                        if (field == s)
                            packed = vorrq_u8(packed, vandq_u8(vshlq_u8(low[g], vdupq_n_s8(shift)),
                                                               vdupq_n_u8(mask)));
                    });
                });
                vst1q_u8(tile + detail::stripe_offset<F>(s) + half * 16, packed);
            });
    });
}
} // namespace ikea2::seriespack::neon
