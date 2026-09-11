#pragma once
#include <ikea/seriespack/detail/point.h>
#include <arm_neon.h>

namespace ikea::seriespack::neon {
template <unsigned N> [[gnu::always_inline]] inline uint8x16_t load128(const std::uint8_t* p) {
    if constexpr (N == 16)
        return vld1q_u8(p);
    else if constexpr (N <= 8)
        return vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(detail::load<N>(p)), vdup_n_u64(0)));
    else
        return vreinterpretq_u8_u64(
            vcombine_u64(vcreate_u64(detail::load<8>(p)), vcreate_u64(detail::load<N - 8>(p + 8))));
}
[[gnu::always_inline]] inline uint8x16_t transpose128(uint64x2_t x) {
    auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
        auto t = vandq_u64(veorq_u64(x, vshrq_n_u64(x, Shift)), vdupq_n_u64(Mask));
        // Clang 21 otherwise extracts lanes and reinserts scalar ORs
        // for known-zero bits. Keep the native exchange; no instruction
        // or memory barrier is emitted by this constraint.
        asm("" : "+w"(t));
        x = veorq_u64(x, veorq_u64(t, vshlq_n_u64(t, Shift)));
    };
    exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14, 0x0000cccc0000ccccULL>();
    exchange.template operator()<28, 0x00000000f0f0f0f0ULL>();
    return vreinterpretq_u8_u64(x);
}
template <unsigned K> struct values {
    using vector = uint8x16_t;
    static constexpr unsigned bytes = sizeof(uint_for<K>), parts = bytes;
    std::array<uint8x16_t, parts> v;
};
template <class F, bool Dense>
[[gnu::always_inline]] inline uint8x16_t tail16(const std::uint8_t* base, std::size_t stride,
                                                std::size_t i) {
    __builtin_assume(i % 16 == 0);
    constexpr unsigned R = F::tail;
    if constexpr (F::storage == geometry::local) {
        const auto* p = base + (i / 8) * (Dense ? F::tile_bytes : stride) + 8 * F::body;
        const auto* q = p + (Dense ? F::tile_bytes : stride);
        if constexpr (R <= 2) {
            static constexpr std::array<std::uint8_t, 16> bits{1, 2, 4, 8, 16, 32, 64, 128,
                                                               1, 2, 4, 8, 16, 32, 64, 128};
            auto x = vdupq_n_u8(0);
            detail::each<R>([&](auto b) {
                const auto plane = vcombine_u8(vdup_n_u8(p[b]), vdup_n_u8(q[b]));
                x = vorrq_u8(x,
                             vandq_u8(vtstq_u8(plane, vld1q_u8(bits.data())), vdupq_n_u8(1u << b)));
            });
            return x;
        } else if constexpr (F::body != 0) {
            // Mixed Local reconstruction already has substantial vector work
            // in body expansion and joining. Transpose its small residuals in
            // integer registers, leaving the SIMD pipelines for those pieces.
            auto low = detail::transpose(detail::load<R>(p));
            asm("" : "+r"(low));
            auto high = detail::transpose(detail::load<R>(q));
            asm("" : "+r"(high));
            return vreinterpretq_u8_u64(vcombine_u64(vcreate_u64(low), vcreate_u64(high)));
        } else {
            return transpose128(
                vcombine_u64(vcreate_u64(detail::load<R>(p)), vcreate_u64(detail::load<R>(q))));
        }
    } else {
        const unsigned lane = i % F::tile_rows, g = lane / 32;
        const auto* tile = base + (i / F::tile_rows) * (Dense ? F::tile_bytes : stride) + lane % 32;
        auto stripe = [&](unsigned s) { return vld1q_u8(tile + detail::stripe_offset<F>(s)); };
        if constexpr (R == 1 || R == 2 || R == 4)
            return vandq_u8(vshlq_u8(stripe(0), vdupq_n_s8(-int(g * R))),
                            vdupq_n_u8((1u << R) - 1));
        else if constexpr (R == 3) {
            static constexpr std::uint64_t descriptors = [] {
                std::uint64_t x = 0;
                for (unsigned g = 0; g < 8; ++g) {
                    const unsigned low = detail::tail_bit<3>(g, 0),
                                   high = detail::tail_bit<3>(g, 2);
                    x |= std::uint64_t((low / 8) * 32 | low % 8 | (low / 8 != high / 8 ? 8 : 0))
                         << (g * 8);
                }
                return x;
            }();
            const unsigned d = static_cast<unsigned>(descriptors >> (g * 8));
            const auto a = stripe((d >> 5) & 3);
            if (d & 8)
                return vorrq_u8(
                    vshrq_n_u8(a, 6),
                    vandq_u8(vshlq_u8(stripe(1), vdupq_n_s8(-int(4 + (g >> 2)))), vdupq_n_u8(4)));
            return vandq_u8(vshlq_u8(a, vdupq_n_s8(-int(d & 7))), vdupq_n_u8(7));
        } else if constexpr (R == 6) {
            const unsigned edge = g & 2;
            const auto a = stripe(edge);
            if (((g + 1) & 2) == 0)
                return vandq_u8(a, vdupq_n_u8(63));
            return vorrq_u8(
                vandq_u8(vshrq_n_u8(a, 2), vdupq_n_u8(48)),
                vandq_u8(vshlq_u8(stripe(1), vdupq_n_s8(-int(edge * 2))), vdupq_n_u8(15)));
        } else {
            const unsigned start = g * R, s = start / 8, shift = start % 8;
            const auto a = stripe(s);
            if (shift <= 8 - R)
                return vandq_u8(vshlq_u8(a, vdupq_n_s8(-int(shift))), vdupq_n_u8((1u << R) - 1));
            const auto low = vdupq_n_u8((1u << (shift + R - 8)) - 1);
            return vbslq_u8(low, stripe(s + 1), vshrq_n_u8(a, 8 - R));
        }
    }
}
template <unsigned To, unsigned From>
[[gnu::always_inline]] inline values<To> widen(values<From> in) {
    constexpr unsigned A = sizeof(uint_for<From>), B = sizeof(uint_for<To>);
    static_assert(A <= B);
    values<To> out;
    if constexpr (A == B)
        out.v = in.v;
    else if constexpr (B == 2 * A)
        detail::each<values<To>::parts>([&](auto k) {
            constexpr unsigned source = k / 2;
            // One widening instruction per result, including the upper half.
            // Wider ratios retain a single table operation: chained widenings
            // increased vector work and regressed the broad mixed-width spectrum.
            if constexpr (A == 1) {
                if constexpr (k % 2)
                    out.v[k] = vreinterpretq_u8_u16(vmovl_high_u8(in.v[source]));
                else
                    out.v[k] = vreinterpretq_u8_u16(vmovl_u8(vget_low_u8(in.v[source])));
            }
            if constexpr (A == 2) {
                if constexpr (k % 2)
                    out.v[k] =
                        vreinterpretq_u8_u32(vmovl_high_u16(vreinterpretq_u16_u8(in.v[source])));
                else
                    out.v[k] = vreinterpretq_u8_u32(
                        vmovl_u16(vget_low_u16(vreinterpretq_u16_u8(in.v[source]))));
            }
            if constexpr (A == 4) {
                if constexpr (k % 2)
                    out.v[k] =
                        vreinterpretq_u8_u64(vmovl_high_u32(vreinterpretq_u32_u8(in.v[source])));
                else
                    out.v[k] = vreinterpretq_u8_u64(
                        vmovl_u32(vget_low_u32(vreinterpretq_u32_u8(in.v[source]))));
            }
        });
    else
        detail::each<values<To>::parts>([&](auto k) {
            constexpr unsigned first = k * (16 / B), source = first / (16 / A),
                               offset = first % (16 / A);
            static constexpr auto map = [] {
                std::array<std::uint8_t, 16> a{};
                a.fill(255);
                for (unsigned v = 0; v < 16 / B; ++v)
                    for (unsigned b = 0; b < A; ++b)
                        a[v * B + b] = (offset + v) * A + b;
                return a;
            }();
            out.v[k] = vqtbl1q_u8(in.v[source], vld1q_u8(map.data()));
        });
    return out;
}
template <class F, bool Dense>
[[gnu::always_inline]] inline values<F::body * 8> body16(const std::uint8_t* base,
                                                         std::size_t stride, std::size_t i)
    requires(F::body != 0)
{
    __builtin_assume(i % 16 == 0);
    constexpr unsigned Q = F::body, L = sizeof(uint_for<Q * 8>), N = 16 / L;
    values<Q * 8> out;
    detail::each<values<Q * 8>::parts>([&](auto k) {
        const auto pos = i + k * N;
        const auto* p = base + (pos / F::tile_rows) * (Dense ? F::tile_bytes : stride) +
                        detail::body_offset<F>(pos % F::tile_rows);
        if constexpr (L == 1 && F::storage == geometry::local)
            out.v[k] = vcombine_u8(vld1_u8(p), vld1_u8(p + (Dense ? F::tile_bytes : stride)));
        else if constexpr (Q == L)
            out.v[k] = vld1q_u8(p);
        else {
            // The entire body of the packet is admitted. Reuse neighboring
            // body bytes for a full load; never borrow residual or gap bytes.
            static_assert(F::storage == geometry::local && 8 * Q >= 16);
            constexpr unsigned begin = (k * N % 8) * Q;
            constexpr unsigned back = begin + 16 > 8 * Q ? begin + 16 - 8 * Q : 0;
            static constexpr auto map = [] {
                std::array<std::uint8_t, 16> a{};
                a.fill(255);
                for (unsigned v = 0; v < N; ++v)
                    for (unsigned b = 0; b < Q; ++b)
                        a[v * L + b] = back + v * Q + b;
                return a;
            }();
            out.v[k] = vqtbl1q_u8(vld1q_u8(p - back), vld1q_u8(map.data()));
        }
    });
    return out;
}
template <unsigned Shift, unsigned A, unsigned B>
[[gnu::always_inline]] inline values<A + Shift> join(values<A> high, values<B> low) {
    constexpr unsigned K = A + Shift, L = sizeof(uint_for<K>);
    auto h = widen<K>(high), l = widen<K>(low);
    detail::each<values<K>::parts>([&](auto k) {
        if constexpr (L == 1)
            h.v[k] = vorrq_u8(vshlq_n_u8(h.v[k], Shift), l.v[k]);
        if constexpr (L == 2)
            h.v[k] = vorrq_u8(
                vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(h.v[k]), Shift)), l.v[k]);
        if constexpr (L == 4)
            h.v[k] = vorrq_u8(
                vreinterpretq_u8_u32(vshlq_n_u32(vreinterpretq_u32_u8(h.v[k]), Shift)), l.v[k]);
        if constexpr (L == 8)
            h.v[k] = vorrq_u8(
                vreinterpretq_u8_u64(vshlq_n_u64(vreinterpretq_u64_u8(h.v[k]), Shift)), l.v[k]);
    });
    return h;
}
template <class F, bool Dense>
[[gnu::always_inline]] inline values<F::payload> read16(const std::uint8_t* base,
                                                        std::size_t stride, std::size_t i)
    requires(F::payload != 0)
{
    if constexpr (F::body == 0)
        return {{tail16<F, Dense>(base, stride, i)}};
    else if constexpr (F::tail == 0)
        return body16<F, Dense>(base, stride, i);
    else
        return join<F::tail>(body16<F, Dense>(base, stride, i),
                             values<F::tail>{{tail16<F, Dense>(base, stride, i)}});
}
template <class U, unsigned K> [[gnu::always_inline]] inline void store16(U* out, values<K> x) {
    static_assert(sizeof(U) * 8 >= K);
    auto y = widen<sizeof(U) * 8>(x);
    detail::each<values<sizeof(U) * 8>::parts>([&](auto p) {
        vst1q_u8(reinterpret_cast<std::uint8_t*>(out + p * (16 / sizeof(U))), y.v[p]);
    });
}
template <unsigned K> [[gnu::always_inline]] inline values<K> mask16(std::uint16_t active) {
    static constexpr std::array<std::uint8_t, 16> bits{1, 2, 4, 8, 16, 32, 64, 128,
                                                       1, 2, 4, 8, 16, 32, 64, 128};
    const auto broadcast = vcombine_u8(vdup_n_u8(active), vdup_n_u8(active >> 8));
    const auto bytes = vceqq_u8(vandq_u8(broadcast, vld1q_u8(bits.data())), vld1q_u8(bits.data()));
    values<K> out;
    constexpr auto L = values<K>::bytes;
    detail::each<values<K>::parts>([&](auto p) {
        static constexpr auto map = [] {
            std::array<std::uint8_t, 16> a{};
            for (unsigned i = 0; i < 16; ++i)
                a[i] = decltype(p)::value * (16 / L) + i / L;
            return a;
        }();
        out.v[p] = vqtbl1q_u8(bytes, vld1q_u8(map.data()));
    });
    return out;
}
template <unsigned K>
[[gnu::always_inline]] inline values<K> less(values<K> x, std::uint64_t cutoff,
                                             std::uint16_t active) {
    auto mask = mask16<K>(active);
    if constexpr (K < 64)
        if (cutoff >= (std::uint64_t{1} << K))
            return mask;
    constexpr auto L = values<K>::bytes;
    detail::each<values<K>::parts>([&](auto p) {
        uint8x16_t keep;
        if constexpr (L == 1)
            keep = vcltq_u8(x.v[p], vdupq_n_u8(cutoff));
        if constexpr (L == 2)
            keep =
                vreinterpretq_u8_u16(vcltq_u16(vreinterpretq_u16_u8(x.v[p]), vdupq_n_u16(cutoff)));
        if constexpr (L == 4)
            keep =
                vreinterpretq_u8_u32(vcltq_u32(vreinterpretq_u32_u8(x.v[p]), vdupq_n_u32(cutoff)));
        if constexpr (L == 8)
            keep =
                vreinterpretq_u8_u64(vcltq_u64(vreinterpretq_u64_u8(x.v[p]), vdupq_n_u64(cutoff)));
        mask.v[p] = vandq_u8(mask.v[p], keep);
    });
    return mask;
}
struct sum_state {
    uint64x2_t value = vdupq_n_u64(0);
    [[gnu::always_inline]] void add(sum_state x) {
        value = vaddq_u64(value, x.value);
    }
    [[gnu::always_inline]] std::uint64_t finish() const {
        return vaddvq_u64(value);
    }
};
template <unsigned K> [[gnu::always_inline]] inline sum_state sum(values<K> x, values<K> mask) {
    sum_state out;
    constexpr auto L = values<K>::bytes;
    if constexpr (L == 4 && K < 32) {
        // Two admitted K-bit values fit in u32 for K<=31. Pair there first,
        // then widen; this halves the dependent u64 accumulation chain without
        // allowing an unbounded range sum to overflow its intermediate lanes.
        const auto a = vpaddq_u32(vreinterpretq_u32_u8(vandq_u8(x.v[0], mask.v[0])),
                                  vreinterpretq_u32_u8(vandq_u8(x.v[1], mask.v[1])));
        const auto b = vpaddq_u32(vreinterpretq_u32_u8(vandq_u8(x.v[2], mask.v[2])),
                                  vreinterpretq_u32_u8(vandq_u8(x.v[3], mask.v[3])));
        out.value = vaddq_u64(vpaddlq_u32(a), vpaddlq_u32(b));
        return out;
    }
    detail::each<values<K>::parts>([&](auto p) {
        const auto v = vandq_u8(x.v[p], mask.v[p]);
        uint64x2_t y;
        if constexpr (L == 1)
            y = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(v)));
        if constexpr (L == 2)
            y = vpaddlq_u32(vpaddlq_u16(vreinterpretq_u16_u8(v)));
        if constexpr (L == 4)
            y = vpaddlq_u32(vreinterpretq_u32_u8(v));
        if constexpr (L == 8)
            y = vreinterpretq_u64_u8(v);
        out.value = vaddq_u64(out.value, y);
    });
    return out;
}
} // namespace ikea::seriespack::neon
