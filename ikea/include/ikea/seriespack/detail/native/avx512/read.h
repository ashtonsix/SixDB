#pragma once
#include <ikea/seriespack/detail/native/avx2/read.h>

namespace ikea::seriespack::zmm {
/// Execution grain is independent of the physical packet and semantic domain.
template <unsigned K, unsigned N> struct values {
    static_assert(N == 16 || N == 32 || N == 64);
    static constexpr unsigned bytes = sizeof(uint_for<K>), parts = (N * bytes + 63) / 64;
    std::array<__m512i, parts> v;
};
template <unsigned K, unsigned N> struct selection {
    std::array<std::uint64_t, values<K, N>::parts> bits;
};

// Assemble already native packet results without an intermediate array. The
// packet reconstruction functions are also used by the shorter AVX2 executor.
template <unsigned K, unsigned N, class Reader>
[[gnu::always_inline]] inline values<K, N> packets(Reader&& read) {
    constexpr unsigned L = sizeof(uint_for<K>);
    values<K, N> out;
    if constexpr (L == 1) {
        auto x = _mm512_zextsi128_si512(read(0).v[0]);
        detail::each<N / 16 - 1>(
            [&](auto p) { x = _mm512_inserti32x4(x, read((p + 1) * 16).v[0], p + 1); });
        out.v[0] = x;
    } else if constexpr (L == 2) {
        detail::each<values<K, N>::parts>([&](auto p) {
            auto x = _mm512_zextsi256_si512(read(p * 32).v[0]);
            if constexpr (N >= 32)
                x = _mm512_inserti64x4(x, read(p * 32 + 16).v[0], 1);
            out.v[p] = x;
        });
    } else {
        detail::each<N / 16>([&](auto p) {
            const auto v = read(p * 16);
            detail::each<L / 4>([&](auto j) {
                out.v[p * (L / 4) + j] =
                    _mm512_inserti64x4(_mm512_castsi256_si512(v.v[j * 2]), v.v[j * 2 + 1], 1);
            });
        });
    }
    return out;
}
template <class F, bool Dense, unsigned N>
[[gnu::always_inline]] inline values<F::tail, N> tail(const std::uint8_t* base, std::size_t stride,
                                                      std::size_t i) {
    constexpr unsigned R = F::tail;
    if constexpr (F::storage == geometry::striped && N == 32) {
        if (i % 32 == 0)
            return {{_mm512_zextsi256_si512(x86::striped_tail32<F, Dense>(base, stride, i))}};
    }
#if defined(__AVX512VBMI__) && defined(__GFNI__)
    if constexpr (N == 64 && Dense && F::storage == geometry::local && F::body == 0) {
        static constexpr auto map = [] {
            std::array<std::uint8_t, 64> a{};
            for (unsigned t = 0; t < 8; ++t)
                for (unsigned b = 0; b < R; ++b)
                    a[t * 8 + 7 - b] = t * R + b;
            return a;
        }();
        constexpr std::uint64_t present = 0x0101010101010101ULL * ((1u << R) - 1) << (8 - R);
        const auto packed =
            _mm512_maskz_loadu_epi8((std::uint64_t{1} << (8 * R)) - 1, base + (i / 8) * R);
        const auto matrices =
            _mm512_maskz_permutexvar_epi8(present, _mm512_loadu_si512(map.data()), packed);
        return {
            {_mm512_gf2p8affine_epi64_epi8(_mm512_set1_epi64(0x8040201008040201ULL), matrices, 0)}};
    } else
#endif
        return packets<R, N>([&](unsigned offset) {
            return x86::values<R>{{x86::tail16<F, Dense>(base, stride, i + offset)}};
        });
}
template <class F, bool Dense, unsigned N>
[[gnu::always_inline]] inline values<F::body * 8, N> body(const std::uint8_t* base,
                                                          std::size_t stride, std::size_t i) {
    if constexpr (F::storage == geometry::striped && N == 32) {
        if (i % 32 == 0) {
            const auto* p = base + (i / F::tile_rows) * (Dense ? F::tile_bytes : stride) +
                            detail::body_offset<F>(i % F::tile_rows);
            if constexpr (F::body == 1)
                return {{_mm512_zextsi256_si512(
                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)))}};
            else
                return {{_mm512_loadu_si512(p)}};
        }
    }
    return packets<F::body * 8, N>(
        [&](unsigned offset) { return x86::body16<F, Dense>(base, stride, i + offset); });
}
template <unsigned To, unsigned From, unsigned N>
[[gnu::always_inline]] inline values<To, N> widen(values<From, N> in) {
    constexpr unsigned A = sizeof(uint_for<From>), B = sizeof(uint_for<To>);
    static_assert(A <= B);
    values<To, N> out;
    if constexpr (A == B)
        out.v = in.v;
    else
        detail::each<values<To, N>::parts>([&](auto p) {
            constexpr unsigned at = p * (64 / B), source = at / (64 / A), offset = at % (64 / A);
            if constexpr (B / A == 2) {
                const auto x = _mm512_extracti64x4_epi64(in.v[source], offset * A / 32);
                if constexpr (A == 1)
                    out.v[p] = _mm512_cvtepu8_epi16(x);
                if constexpr (A == 2)
                    out.v[p] = _mm512_cvtepu16_epi32(x);
                if constexpr (A == 4)
                    out.v[p] = _mm512_cvtepu32_epi64(x);
            } else {
                auto x = _mm512_extracti32x4_epi32(in.v[source], offset * A / 16);
                if constexpr (B / A == 8)
                    x = _mm_srli_si128(x, offset * A % 16);
                if constexpr (A == 1 && B == 4)
                    out.v[p] = _mm512_cvtepu8_epi32(x);
                if constexpr (A == 1 && B == 8)
                    out.v[p] = _mm512_cvtepu8_epi64(x);
                if constexpr (A == 2 && B == 8)
                    out.v[p] = _mm512_cvtepu16_epi64(x);
            }
        });
    return out;
}
template <unsigned Shift, unsigned A, unsigned B, unsigned N>
[[gnu::always_inline]] inline values<A + Shift, N> join(values<A, N> high, values<B, N> low) {
    constexpr unsigned K = A + Shift, L = sizeof(uint_for<K>);
    auto h = widen<K>(high), l = widen<K>(low);
    detail::each<values<K, N>::parts>([&](auto p) {
        if constexpr (L == 1)
            h.v[p] = _mm512_slli_epi64(h.v[p], Shift);
        if constexpr (L == 2)
            h.v[p] = _mm512_slli_epi16(h.v[p], Shift);
        if constexpr (L == 4)
            h.v[p] = _mm512_slli_epi32(h.v[p], Shift);
        if constexpr (L == 8)
            h.v[p] = _mm512_slli_epi64(h.v[p], Shift);
        h.v[p] = _mm512_or_si512(h.v[p], l.v[p]);
    });
    return h;
}
template <class F, bool Dense, unsigned N>
[[gnu::always_inline]] inline values<F::payload, N> read(const std::uint8_t* base,
                                                         std::size_t stride, std::size_t i) {
    if constexpr (F::body == 0)
        return tail<F, Dense, N>(base, stride, i);
    else if constexpr (F::tail == 0)
        return body<F, Dense, N>(base, stride, i);
    else
        return join<F::tail>(body<F, Dense, N>(base, stride, i),
                             tail<F, Dense, N>(base, stride, i));
}
template <class U, unsigned K, unsigned N>
[[gnu::always_inline]] inline void store(U* __restrict out, values<K, N> x) {
    auto y = widen<sizeof(U) * 8>(x);
    if constexpr (N * sizeof(U) == 16)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out), _mm512_castsi512_si128(y.v[0]));
    else if constexpr (N * sizeof(U) == 32)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm512_castsi512_si256(y.v[0]));
    else
        detail::each<values<sizeof(U) * 8, N>::parts>(
            [&](auto p) { _mm512_storeu_si512(out + p * (64 / sizeof(U)), y.v[p]); });
}
template <unsigned K, unsigned N>
[[gnu::always_inline]] inline selection<K, N> less(values<K, N> x, std::uint64_t cutoff,
                                                   std::uint64_t active) {
    constexpr unsigned L = sizeof(uint_for<K>), lanes = 64 / L;
    selection<K, N> out;
    detail::each<values<K, N>::parts>([&](auto p) {
        const auto live = active >> (p * lanes);
        if constexpr (K < 64)
            if (cutoff >= (std::uint64_t{1} << K)) {
                out.bits[p] = live;
                return;
            }
        if constexpr (L == 1)
            out.bits[p] = _mm512_mask_cmplt_epu8_mask(live, x.v[p], _mm512_set1_epi8(cutoff));
        if constexpr (L == 2)
            out.bits[p] = _mm512_mask_cmplt_epu16_mask(live, x.v[p], _mm512_set1_epi16(cutoff));
        if constexpr (L == 4)
            out.bits[p] = _mm512_mask_cmplt_epu32_mask(live, x.v[p], _mm512_set1_epi32(cutoff));
        if constexpr (L == 8)
            out.bits[p] = _mm512_mask_cmplt_epu64_mask(live, x.v[p], _mm512_set1_epi64(cutoff));
    });
    return out;
}
struct sum_state {
    __m512i value = _mm512_setzero_si512();
    [[gnu::always_inline]] void add(sum_state x) {
        value = _mm512_add_epi64(value, x.value);
    }
    [[gnu::always_inline]] std::uint64_t finish() const {
        return _mm512_reduce_add_epi64(value);
    }
};
template <unsigned K, unsigned N>
[[gnu::always_inline]] inline sum_state sum(values<K, N> x, selection<K, N> mask) {
    constexpr unsigned L = sizeof(uint_for<K>);
    sum_state out;
    detail::each<values<K, N>::parts>([&](auto p) {
        __m512i v;
        if constexpr (L == 1)
            v = _mm512_maskz_mov_epi8(mask.bits[p], x.v[p]);
        if constexpr (L == 2)
            v = _mm512_maskz_mov_epi16(mask.bits[p], x.v[p]);
        if constexpr (L == 4)
            v = _mm512_maskz_mov_epi32(mask.bits[p], x.v[p]);
        if constexpr (L == 8)
            v = _mm512_maskz_mov_epi64(mask.bits[p], x.v[p]);
        if constexpr (L == 1)
            v = _mm512_sad_epu8(v, _mm512_setzero_si512());
        if constexpr (L == 2)
            v = _mm512_add_epi32(_mm512_and_si512(v, _mm512_set1_epi32(65535)),
                                 _mm512_srli_epi32(v, 16));
        if constexpr (L == 2 || L == 4)
            v = _mm512_add_epi64(_mm512_and_si512(v, _mm512_set1_epi64(0xffffffffULL)),
                                 _mm512_srli_epi64(v, 32));
        out.value = _mm512_add_epi64(out.value, v);
    });
    return out;
}
template <unsigned K, unsigned N>
[[gnu::always_inline]] inline selection<K, N> active_mask(std::uint64_t active) {
    selection<K, N> out;
    detail::each<values<K, N>::parts>(
        [&](auto p) { out.bits[p] = active >> (p * (64 / sizeof(uint_for<K>))); });
    return out;
}
} // namespace ikea::seriespack::zmm
