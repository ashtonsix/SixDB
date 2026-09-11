#pragma once
#include <ikea2/seriespack/detail/point.h>
#include <immintrin.h>

namespace ikea2::seriespack::x86 {
template <unsigned N> [[gnu::always_inline]] inline __m128i load128(const std::uint8_t* p) {
    if constexpr (N == 16)
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    else if constexpr (N <= 8)
        return _mm_cvtsi64_si128(detail::load<N>(p));
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    else
        return _mm_maskz_loadu_epi8((1u << N) - 1, p);
#else
    else
        return _mm_set_epi64x(detail::load<N - 8>(p + 8), detail::load<8>(p));
#endif
}

/// Sixteen ordered values. Byte values stay narrow; wider values use YMM lanes.
/// This internal carrier is not a storage tile or a continuation ABI.
template <unsigned K, unsigned N = 16> struct values {
    static_assert(N == 16 || N == 32);
    static constexpr unsigned bytes = sizeof(uint_for<K>);
    static constexpr unsigned parts = (N * bytes + 31) / 32;
    using vector = std::conditional_t<N * bytes == 16, __m128i, __m256i>;
    std::array<vector, parts> v;
};

template <class F, bool Dense>
[[gnu::always_inline]] inline __m128i tail16(const std::uint8_t* base, std::size_t stride,
                                             std::size_t i) {
    __builtin_assume(i % 16 == 0);
    constexpr unsigned R = F::tail;
    static_assert(R != 0);
    if constexpr (F::storage == geometry::local) {
        const auto* p = base + (i / 8) * (Dense ? F::tile_bytes : stride) + 8 * F::body;
        const auto* q = p + (Dense ? F::tile_bytes : stride);
#if defined(__GFNI__)
        __m128i matrix;
        if constexpr (Dense && F::body == 0) {
            static constexpr auto indices = [] {
                std::array<std::uint8_t, 16> a{};
                a.fill(128);
                for (unsigned t = 0; t < 2; ++t)
                    for (unsigned b = 0; b < R; ++b)
                        a[t * 8 + 7 - b] = t * R + b;
                return a;
            }();
            matrix =
                _mm_shuffle_epi8(load128<2 * R>(p),
                                 _mm_loadu_si128(reinterpret_cast<const __m128i*>(indices.data())));
        } else {
            matrix = _mm_set_epi64x(detail::load<R>(q), detail::load<R>(p));
            matrix = _mm_shuffle_epi8(
                matrix, _mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8));
        }
        return _mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL), matrix, 0);
#else
        if constexpr (R == 1) {
            const auto bits = _mm_set_epi64x(std::uint64_t(*q) * 0x0101010101010101ULL,
                                             std::uint64_t(*p) * 0x0101010101010101ULL);
            const auto weights = _mm_set1_epi64x(0x8040201008040201ULL);
            return _mm_and_si128(_mm_cmpeq_epi8(_mm_and_si128(bits, weights), weights),
                                 _mm_set1_epi8(1));
        } else
            return _mm_set_epi64x(detail::transpose(detail::load<R>(q)),
                                  detail::transpose(detail::load<R>(p)));
#endif
    } else {
        const unsigned lane = i % F::tile_rows;
        const unsigned group = lane / 32;
        const auto* tile = base + (i / F::tile_rows) * (Dense ? F::tile_bytes : stride) + lane % 32;
        auto stripe = [&](unsigned s) {
            return _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(tile + detail::stripe_offset<F>(s)));
        };
        if constexpr (R == 1 || R == 2 || R == 4) {
            return _mm_and_si128(_mm_srl_epi64(stripe(0), _mm_cvtsi32_si128(group * R)),
                                 _mm_set1_epi8((1u << R) - 1));
        } else if constexpr (R == 3) {
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
            const unsigned d = static_cast<unsigned>(descriptors >> (group * 8));
            const auto a = stripe((d >> 5) & 3);
            if (d & 8)
                return _mm_or_si128(
                    _mm_and_si128(_mm_srli_epi64(a, 6), _mm_set1_epi8(3)),
                    _mm_and_si128(_mm_srl_epi64(stripe(1), _mm_cvtsi32_si128(4 + (group >> 2))),
                                  _mm_set1_epi8(4)));
            return _mm_and_si128(_mm_srl_epi64(a, _mm_cvtsi32_si128(d & 7)), _mm_set1_epi8(7));
        } else if constexpr (R == 6) {
            const unsigned edge = group & 2;
            const auto a = stripe(edge);
            if (((group + 1) & 2) == 0)
                return _mm_and_si128(a, _mm_set1_epi8(63));
            return _mm_or_si128(_mm_and_si128(_mm_srli_epi64(a, 2), _mm_set1_epi8(48)),
                                _mm_and_si128(_mm_srl_epi64(stripe(1), _mm_cvtsi32_si128(edge * 2)),
                                              _mm_set1_epi8(15)));
        } else {
            static_assert(R == 5 || R == 7);
            const unsigned start = group * R, s = start / 8, shift = start % 8;
            const auto a = stripe(s);
            if (shift <= 8 - R)
                return _mm_and_si128(_mm_srl_epi64(a, _mm_cvtsi32_si128(shift)),
                                     _mm_set1_epi8((1u << R) - 1));
            const auto low = _mm_set1_epi8((1u << (shift + R - 8)) - 1);
            const auto high = _mm_and_si128(_mm_srli_epi64(a, 8 - R), _mm_set1_epi8((1u << R) - 1));
            return _mm_or_si128(_mm_andnot_si128(low, high), _mm_and_si128(low, stripe(s + 1)));
        }
    }
}

/// One complete striped row group. This kernel has a 32-row admission contract;
/// random 16-row reads retain their own endpoint and physical access extent.
template <class F, bool Dense>
[[gnu::always_inline]] inline __m256i striped_tail32(const std::uint8_t* base, std::size_t stride,
                                                     std::size_t i) {
    static_assert(F::storage == geometry::striped);
    __builtin_assume(i % 32 == 0);
    const auto* tile = base + (i / F::tile_rows) * (Dense ? F::tile_bytes : stride);
    auto result = _mm256_setzero_si256();
    detail::tail_fragments<F::tail>((i % F::tile_rows) / 32, [&](unsigned stripe, unsigned shift,
                                                                 unsigned mask) {
        const auto raw = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(tile + detail::stripe_offset<F>(stripe)));
        result =
            _mm256_or_si256(result, _mm256_srl_epi64(_mm256_and_si256(raw, _mm256_set1_epi8(mask)),
                                                     _mm_cvtsi32_si128(shift)));
    });
    return result;
}
template <class F, bool Dense>
[[gnu::always_inline]] inline values<F::body * 8, 32>
striped_body32(const std::uint8_t* base, std::size_t stride, std::size_t i) {
    static_assert(F::storage == geometry::striped && (F::body == 1 || F::body == 2));
    __builtin_assume(i % 32 == 0);
    const auto* body = base + (i / F::tile_rows) * (Dense ? F::tile_bytes : stride) +
                       detail::body_offset<F>(i % F::tile_rows);
    values<F::body * 8, 32> out;
    detail::each<F::body>([&](auto p) {
        out.v[p] = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(body + p * 32));
    });
    return out;
}
template <unsigned To, unsigned From>
[[gnu::always_inline]] inline values<To, 32> widen32(values<From, 32> in) {
    constexpr unsigned A = sizeof(uint_for<From>), B = sizeof(uint_for<To>);
    static_assert(A <= B);
    values<To, 32> out;
    if constexpr (A == B)
        out.v = in.v;
    else
        detail::each<values<To, 32>::parts>([&](auto p) {
            constexpr unsigned at = p * (32 / B), source = at / (32 / A), within = at % (32 / A);
            const auto half = _mm256_extracti128_si256(in.v[source], within * A / 16);
            const auto x = _mm_srli_si128(half, within * A % 16);
            if constexpr (A == 1 && B == 2)
                out.v[p] = _mm256_cvtepu8_epi16(x);
            if constexpr (A == 1 && B == 4)
                out.v[p] = _mm256_cvtepu8_epi32(x);
            if constexpr (A == 1 && B == 8)
                out.v[p] = _mm256_cvtepu8_epi64(x);
            if constexpr (A == 2 && B == 4)
                out.v[p] = _mm256_cvtepu16_epi32(x);
            if constexpr (A == 2 && B == 8)
                out.v[p] = _mm256_cvtepu16_epi64(x);
            if constexpr (A == 4 && B == 8)
                out.v[p] = _mm256_cvtepu32_epi64(x);
        });
    return out;
}
template <class F, bool Dense>
[[gnu::always_inline]] inline values<F::payload, 32>
read_striped32(const std::uint8_t* base, std::size_t stride, std::size_t i) {
    const values<F::tail, 32> low{{striped_tail32<F, Dense>(base, stride, i)}};
    if constexpr (F::body == 0)
        return low;
    else {
        auto body = widen32<F::payload>(striped_body32<F, Dense>(base, stride, i));
        const auto tail = widen32<F::payload>(low);
        detail::each<values<F::payload, 32>::parts>([&](auto p) {
            if constexpr (F::payload <= 16)
                body.v[p] = _mm256_slli_epi16(body.v[p], F::tail);
            else
                body.v[p] = _mm256_slli_epi32(body.v[p], F::tail);
            body.v[p] = _mm256_or_si256(body.v[p], tail.v[p]);
        });
        return body;
    }
}
template <class U, unsigned K>
[[gnu::always_inline]] inline void store32(U* __restrict out, values<K, 32> value) {
    const auto widened = widen32<sizeof(U) * 8>(value);
    detail::each<sizeof(U)>([&](auto p) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + p * (32 / sizeof(U))), widened.v[p]);
    });
}

template <unsigned To, unsigned From>
[[gnu::always_inline]] inline values<To> widen(values<From> in) {
    constexpr unsigned A = sizeof(uint_for<From>), B = sizeof(uint_for<To>);
    static_assert(A <= B);
    values<To> out;
    if constexpr (A == B)
        out.v = in.v;
    else if constexpr (A == 1) {
        detail::each<values<To>::parts>([&](auto part) {
            const auto x = _mm_srli_si128(in.v[0], part * (32 / B));
            if constexpr (B == 2)
                out.v[part] = _mm256_cvtepu8_epi16(x);
            if constexpr (B == 4)
                out.v[part] = _mm256_cvtepu8_epi32(x);
            if constexpr (B == 8)
                out.v[part] = _mm256_cvtepu8_epi64(x);
        });
    } else {
        detail::each<values<To>::parts>([&](auto part) {
            constexpr unsigned at = part * (32 / B), from = at / (32 / A),
                               half = (at % (32 / A)) / (16 / A);
            const auto x = _mm256_extracti128_si256(in.v[from], half);
            if constexpr (A == 2 && B == 4)
                out.v[part] = _mm256_cvtepu16_epi32(x);
            if constexpr (A == 2 && B == 8) {
                constexpr unsigned shift = (at % (16 / A)) * A;
                out.v[part] = _mm256_cvtepu16_epi64(_mm_srli_si128(x, shift));
            }
            if constexpr (A == 4 && B == 8)
                out.v[part] = _mm256_cvtepu32_epi64(x);
        });
    }
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
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
    if constexpr (F::storage == geometry::local && !std::has_single_bit(Q)) {
        // Expand a whole eight-value body with its native byte permutation.
        // Residuals and stride gaps remain outside the masked load. Short
        // composition and wider execution reuse this same reconstruction.
        static constexpr auto map = [] {
            std::array<std::uint8_t, 8 * L> a{};
            for (unsigned v = 0; v < 8; ++v)
                for (unsigned b = 0; b < Q; ++b)
                    a[v * L + b] = v * Q + b;
            return a;
        }();
        static constexpr auto live = [] {
            std::uint64_t m = 0;
            for (unsigned v = 0; v < 8; ++v)
                for (unsigned b = 0; b < Q; ++b)
                    m |= std::uint64_t{1} << (v * L + b);
            return m;
        }();
        detail::each<2>([&](auto packet) {
            const auto* p = base + (i / 8 + packet) * (Dense ? F::tile_bytes : stride);
            if constexpr (L == 4) {
                const auto bytes = _mm256_maskz_loadu_epi8((1u << (8 * Q)) - 1, p);
                out.v[packet] = _mm256_maskz_permutexvar_epi8(
                    live, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(map.data())), bytes);
            } else {
                const auto bytes = _mm512_maskz_loadu_epi8((std::uint64_t{1} << (8 * Q)) - 1, p);
                const auto expanded =
                    _mm512_maskz_permutexvar_epi8(live, _mm512_loadu_si512(map.data()), bytes);
                out.v[packet * 2] = _mm512_castsi512_si256(expanded);
                out.v[packet * 2 + 1] = _mm512_extracti64x4_epi64(expanded, 1);
            }
        });
        return out;
    }
#endif
    auto part = [&]<unsigned Part>(std::integral_constant<unsigned, Part>) {
        const auto pos = i + Part * N;
        const auto* p = base + (pos / F::tile_rows) * (Dense ? F::tile_bytes : stride) +
                        detail::body_offset<F>(pos % F::tile_rows);
        if constexpr (Q == L)
            return load128<16>(p);
        else {
            // Load inside the admitted packet body, including neighboring
            // values when useful. Residuals and placement gaps stay unread.
            static_assert(F::storage == geometry::local && 8 * Q >= 16);
            constexpr unsigned begin = (Part * N % 8) * Q;
            constexpr unsigned back = begin + 16 > 8 * Q ? begin + 16 - 8 * Q : 0;
            static constexpr auto map = [] {
                std::array<std::uint8_t, 16> a{};
                a.fill(128);
                for (unsigned v = 0; v < N; ++v)
                    for (unsigned b = 0; b < Q; ++b)
                        a[v * L + b] = back + v * Q + b;
                return a;
            }();
            return _mm_shuffle_epi8(load128<16>(p - back),
                                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(map.data())));
        }
    };
    if constexpr (L == 1) {
        // Each local packet has only eight bodies before its residuals.
        if constexpr (F::storage == geometry::local) {
            const auto* p = base + (i / 8) * (Dense ? F::tile_bytes : stride);
            out.v[0] = _mm_set_epi64x(detail::load<8>(p + (Dense ? F::tile_bytes : stride)),
                                      detail::load<8>(p));
        } else
            out.v[0] = part(std::integral_constant<unsigned, 0>{});
    } else
        detail::each<values<Q * 8>::parts>([&](auto k) {
            out.v[k] = _mm256_set_m128i(part(std::integral_constant<unsigned, 2 * k + 1>{}),
                                        part(std::integral_constant<unsigned, 2 * k>{}));
        });
    return out;
}

template <unsigned Shift, unsigned A, unsigned B>
[[gnu::always_inline]] inline values<A + Shift> join(values<A> high, values<B> low) {
    constexpr unsigned K = A + Shift, L = sizeof(uint_for<K>);
    auto h = widen<K>(high), l = widen<K>(low);
    detail::each<values<K>::parts>([&](auto k) {
        if constexpr (L == 1)
            h.v[k] = _mm_or_si128(_mm_slli_epi64(h.v[k], Shift), l.v[k]);
        if constexpr (L == 2)
            h.v[k] = _mm256_or_si256(_mm256_slli_epi16(h.v[k], Shift), l.v[k]);
        if constexpr (L == 4)
            h.v[k] = _mm256_or_si256(_mm256_slli_epi32(h.v[k], Shift), l.v[k]);
        if constexpr (L == 8)
            h.v[k] = _mm256_or_si256(_mm256_slli_epi64(h.v[k], Shift), l.v[k]);
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
    if constexpr (sizeof(U) == 1)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out), y.v[0]);
    else
        detail::each<values<sizeof(U) * 8>::parts>([&](auto p) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + p * (32 / sizeof(U))), y.v[p]);
        });
}

template <unsigned K> [[gnu::always_inline]] inline values<K> mask16(std::uint16_t active) {
    const auto bits = _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, -128, 1, 2, 4, 8, 16, 32, 64, -128);
    const auto broadcast = _mm_shuffle_epi8(
        _mm_cvtsi32_si128(active), _mm_setr_epi8(0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1));
    const auto bytes = _mm_cmpeq_epi8(_mm_and_si128(broadcast, bits), bits);
    values<K> out;
    constexpr auto L = values<K>::bytes;
    if constexpr (L == 1)
        out.v[0] = bytes;
    else
        detail::each<values<K>::parts>([&](auto p) {
            const auto part = _mm_srli_si128(bytes, p * (32 / L));
            if constexpr (L == 2)
                out.v[p] = _mm256_cvtepi8_epi16(part);
            if constexpr (L == 4)
                out.v[p] = _mm256_cvtepi8_epi32(part);
            if constexpr (L == 8)
                out.v[p] = _mm256_cvtepi8_epi64(part);
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
        if constexpr (L == 1) {
            const auto c = _mm_set1_epi8(static_cast<char>(cutoff - 1));
            const auto keep =
                cutoff == 0 ? _mm_setzero_si128() : _mm_cmpeq_epi8(_mm_min_epu8(x.v[p], c), x.v[p]);
            mask.v[p] = _mm_and_si128(mask.v[p], keep);
        } else {
            __m256i keep;
            if constexpr (L == 2)
                keep = _mm256_cmpeq_epi16(
                    _mm256_min_epu16(x.v[p], _mm256_set1_epi16(static_cast<short>(cutoff - 1))),
                    x.v[p]);
            if constexpr (L == 4)
                keep = _mm256_cmpeq_epi32(
                    _mm256_min_epu32(x.v[p], _mm256_set1_epi32(static_cast<int>(cutoff - 1))),
                    x.v[p]);
            if constexpr (L == 8) {
                const auto sign = _mm256_set1_epi64x(std::int64_t{1} << 63);
                keep = _mm256_cmpgt_epi64(_mm256_xor_si256(_mm256_set1_epi64x(cutoff), sign),
                                          _mm256_xor_si256(x.v[p], sign));
            }
            if (cutoff == 0)
                keep = _mm256_setzero_si256();
            mask.v[p] = _mm256_and_si256(mask.v[p], keep);
        }
    });
    return mask;
}
/// Compact original-coordinate evidence, directly from native values. A
/// continuation carrying a row mask need not expand it into vector lanes and
/// compress it again when this ISA can compare directly into mask registers.
template <unsigned K>
[[gnu::always_inline]] inline std::uint16_t less_bits(values<K> x, std::uint64_t cutoff,
                                                      std::uint16_t active) {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
    if constexpr (K < 64)
        if (cutoff >= (std::uint64_t{1} << K))
            return active;
    constexpr auto L = values<K>::bytes;
    if constexpr (L == 1)
        return _mm_mask_cmplt_epu8_mask(active, x.v[0], _mm_set1_epi8(cutoff));
    else {
        std::uint16_t out = 0;
        detail::each<values<K>::parts>([&](auto p) {
            const auto mask = active >> (p * (32 / L));
            unsigned kept;
            if constexpr (L == 2)
                kept = _mm256_mask_cmplt_epu16_mask(mask, x.v[p], _mm256_set1_epi16(cutoff));
            if constexpr (L == 4)
                kept = _mm256_mask_cmplt_epu32_mask(mask, x.v[p], _mm256_set1_epi32(cutoff));
            if constexpr (L == 8)
                kept = _mm256_mask_cmplt_epu64_mask(mask, x.v[p], _mm256_set1_epi64x(cutoff));
            out |= kept << (p * (32 / L));
        });
        return out;
    }
#else
    const auto mask = less(x, cutoff, active);
    constexpr auto L = values<K>::bytes;
    if constexpr (L == 1)
        return _mm_movemask_epi8(mask.v[0]);
    else if constexpr (L == 2)
        return _mm_movemask_epi8(_mm_packs_epi16(_mm256_castsi256_si128(mask.v[0]),
                                                 _mm256_extracti128_si256(mask.v[0], 1)));
    else {
        std::uint16_t out = 0;
        detail::each<values<K>::parts>([&](auto p) {
            if constexpr (L == 4)
                out |= _mm256_movemask_ps(_mm256_castsi256_ps(mask.v[p])) << (p * 8);
            else
                out |= _mm256_movemask_pd(_mm256_castsi256_pd(mask.v[p])) << (p * 4);
        });
        return out;
    }
#endif
}
struct sum_state {
    __m256i value = _mm256_setzero_si256();
    [[gnu::always_inline]] void add(sum_state x) {
        value = _mm256_add_epi64(value, x.value);
    }
    [[gnu::always_inline]] std::uint64_t finish() const {
        auto x = _mm_add_epi64(_mm256_castsi256_si128(value), _mm256_extracti128_si256(value, 1));
        x = _mm_add_epi64(x, _mm_srli_si128(x, 8));
        return _mm_cvtsi128_si64(x);
    }
};
template <unsigned K> [[gnu::always_inline]] inline sum_state sum(values<K> x, values<K> mask) {
    constexpr auto L = values<K>::bytes;
    detail::each<values<K>::parts>([&](auto p) {
        if constexpr (L == 1)
            x.v[p] = _mm_and_si128(x.v[p], mask.v[p]);
        else
            x.v[p] = _mm256_and_si256(x.v[p], mask.v[p]);
    });
    sum_state out;
    if constexpr (L == 1)
        out.value =
            _mm256_inserti128_si256(out.value, _mm_sad_epu8(x.v[0], _mm_setzero_si128()), 0);
    else if constexpr (L == 2) {
        auto pairs = _mm256_madd_epi16(x.v[0], _mm256_set1_epi16(1));
        if constexpr (K == 16) {
            const auto high =
                _mm256_madd_epi16(_mm256_srli_epi16(x.v[0], 15), _mm256_set1_epi16(1));
            pairs = _mm256_add_epi32(pairs, _mm256_slli_epi32(high, 16));
        }
        const auto sums =
            _mm_add_epi32(_mm256_castsi256_si128(pairs), _mm256_extracti128_si256(pairs, 1));
        out.value = _mm256_cvtepu32_epi64(sums);
    } else if constexpr (L == 4)
        detail::each<values<K>::parts>([&](auto p) {
            out.value =
                _mm256_add_epi64(out.value, _mm256_cvtepu32_epi64(_mm256_castsi256_si128(x.v[p])));
            out.value = _mm256_add_epi64(
                out.value, _mm256_cvtepu32_epi64(_mm256_extracti128_si256(x.v[p], 1)));
        });
    else
        detail::each<values<K>::parts>(
            [&](auto p) { out.value = _mm256_add_epi64(out.value, x.v[p]); });
    return out;
}
} // namespace ikea2::seriespack::x86
