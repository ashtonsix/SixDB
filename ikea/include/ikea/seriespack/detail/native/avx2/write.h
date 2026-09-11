#pragma once
#include <ikea/seriespack/detail/native/avx2/read.h>

namespace ikea::seriespack::x86 {
template <class U> [[gnu::always_inline]] inline values<sizeof(U) * 8> load_values(const U* input) {
    values<sizeof(U) * 8> out;
    if constexpr (sizeof(U) == 1)
        out.v[0] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input));
    else
        detail::each<sizeof(U) / 2>([&](auto p) {
            out.v[p] =
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(input + p * (32 / sizeof(U))));
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
        if constexpr (A == 8) {
            auto half = [](__m256i x) {
                const auto a =
                    _mm_shuffle_epi32(_mm256_castsi256_si128(x), _MM_SHUFFLE(2, 0, 2, 0));
                const auto b =
                    _mm_shuffle_epi32(_mm256_extracti128_si256(x, 1), _MM_SHUFFLE(2, 0, 2, 0));
                return _mm_unpacklo_epi64(a, b);
            };
            detail::each<2>([&](auto p) {
                next.v[p] = _mm256_set_m128i(half(in.v[p * 2 + 1]), half(in.v[p * 2]));
            });
        }
        // Selected inputs have already been admitted to the smaller domain.
        if constexpr (A == 4)
            next.v[0] = _mm256_permute4x64_epi64(_mm256_packus_epi32(in.v[0], in.v[1]),
                                                 _MM_SHUFFLE(3, 1, 2, 0));
        if constexpr (A == 2)
            next.v[0] = _mm_packus_epi16(_mm256_castsi256_si128(in.v[0]),
                                         _mm256_extracti128_si256(in.v[0], 1));
        return narrow<To>(next);
    }
}
template <unsigned Shift, unsigned K> [[gnu::always_inline]] inline values<K> right(values<K> x) {
    constexpr unsigned L = sizeof(uint_for<K>);
    if constexpr (Shift)
        detail::each<values<K>::parts>([&](auto p) {
            if constexpr (L == 1)
                x.v[p] = _mm_and_si128(_mm_srli_epi16(x.v[p], Shift), _mm_set1_epi8(255 >> Shift));
            if constexpr (L == 2)
                x.v[p] = _mm256_srli_epi16(x.v[p], Shift);
            if constexpr (L == 4)
                x.v[p] = _mm256_srli_epi32(x.v[p], Shift);
            if constexpr (L == 8)
                x.v[p] = _mm256_srli_epi64(x.v[p], Shift);
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
                y.v[p] = _mm_and_si128(y.v[p], _mm_set1_epi8(mask));
            if constexpr (L == 2)
                y.v[p] = _mm256_and_si256(y.v[p], _mm256_set1_epi16(mask));
            if constexpr (L == 4)
                y.v[p] = _mm256_and_si256(y.v[p], _mm256_set1_epi32(mask));
            if constexpr (L == 8)
                y.v[p] = _mm256_and_si256(y.v[p], _mm256_set1_epi64x(mask));
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
                y.v[p] = _mm_slli_epi64(y.v[p], Shift);
            if constexpr (L == 2)
                y.v[p] = _mm256_slli_epi16(y.v[p], Shift);
            if constexpr (L == 4)
                y.v[p] = _mm256_slli_epi32(y.v[p], Shift);
            if constexpr (L == 8)
                y.v[p] = _mm256_slli_epi64(y.v[p], Shift);
        });
    return y;
}
template <unsigned K>
[[gnu::always_inline]] inline values<K> choose(std::uint16_t active, values<K> yes, values<K> no) {
    const auto mask = mask16<K>(active);
    detail::each<values<K>::parts>([&](auto p) {
        if constexpr (sizeof(uint_for<K>) == 1)
            yes.v[p] = _mm_blendv_epi8(no.v[p], yes.v[p], mask.v[p]);
        else
            yes.v[p] = _mm256_blendv_epi8(no.v[p], yes.v[p], mask.v[p]);
    });
    return yes;
}
template <unsigned K> [[gnu::always_inline]] inline auto chunks128(values<K> x) {
    constexpr unsigned L = sizeof(uint_for<K>);
    std::array<__m128i, L> out;
    if constexpr (L == 1)
        out[0] = x.v[0];
    else
        detail::each<L>([&](auto p) { out[p] = _mm256_extracti128_si256(x.v[p / 2], p % 2); });
    return out;
}
template <unsigned First, unsigned Count, std::size_t Parts>
[[gnu::always_inline]] inline __m128i table(const std::array<__m128i, Parts>& source,
                                            const std::array<std::uint8_t, 16>& index) {
    const auto map = _mm_loadu_si128(reinterpret_cast<const __m128i*>(index.data()));
    auto out = _mm_setzero_si128();
    detail::each<Count>([&](auto p) {
        const auto local = _mm_sub_epi8(map, _mm_set1_epi8(p * 16));
        const auto valid =
            _mm_cmpeq_epi8(_mm_and_si128(local, _mm_set1_epi8(-16)), _mm_setzero_si128());
        const auto selected = _mm_or_si128(local, _mm_andnot_si128(valid, _mm_set1_epi8(-128)));
        out = _mm_or_si128(out, _mm_shuffle_epi8(source[First + p], selected));
    });
    return out;
}
template <unsigned K> [[gnu::always_inline]] inline __m128i low_bytes(values<K> x) {
    constexpr unsigned L = sizeof(uint_for<K>);
    if constexpr (L == 1)
        return x.v[0];
#if defined(__AVX512VL__) && defined(__AVX512BW__)
    else if constexpr (L == 2)
        return _mm256_cvtepi16_epi8(x.v[0]);
    else if constexpr (L == 4)
        return _mm_unpacklo_epi64(_mm256_cvtepi32_epi8(x.v[0]), _mm256_cvtepi32_epi8(x.v[1]));
    else {
        const auto a = _mm512_inserti64x4(_mm512_castsi256_si512(x.v[0]), x.v[1], 1);
        const auto b = _mm512_inserti64x4(_mm512_castsi256_si512(x.v[2]), x.v[3], 1);
        return _mm_unpacklo_epi64(_mm512_cvtepi64_epi8(a), _mm512_cvtepi64_epi8(b));
    }
#else
    else {
        static constexpr auto map = [] {
            std::array<std::uint8_t, 16> a{};
            a.fill(255);
            for (unsigned i = 0; i < 8; ++i)
                a[i] = i * L;
            return a;
        }();
        const auto chunks = chunks128(x);
        return _mm_unpacklo_epi64(table<0, L / 2>(chunks, map), table<L / 2, L / 2>(chunks, map));
    }
#endif
}
template <unsigned R = 8> [[gnu::always_inline]] inline __m128i transpose128(__m128i x) {
#if defined(__GFNI__)
    return _mm_gf2p8affine_epi64_epi8(
        _mm_set1_epi64x(0x8040201008040201ULL),
        _mm_shuffle_epi8(x, _mm_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8)),
        0);
#else
    if constexpr (R == 1 || R == 2) {
        std::uint64_t low = 0, high = 0;
        detail::each<R>([&](auto bit) {
            const unsigned mask = _mm_movemask_epi8(_mm_slli_epi64(x, 7 - bit));
            low |= std::uint64_t(mask & 255) << (bit * 8);
            high |= std::uint64_t(mask >> 8) << (bit * 8);
        });
        return _mm_set_epi64x(high, low);
    }
    auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
        const auto t =
            _mm_and_si128(_mm_xor_si128(x, _mm_srli_epi64(x, Shift)), _mm_set1_epi64x(Mask));
        x = _mm_xor_si128(x, _mm_xor_si128(t, _mm_slli_epi64(t, Shift)));
    };
    exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14, 0x0000cccc0000ccccULL>();
    exchange.template operator()<28, 0x00000000f0f0f0f0ULL>();
    return x;
#endif
}
template <unsigned N> [[gnu::always_inline]] inline void store_prefix(std::uint8_t* p, __m128i x) {
    if constexpr (N == 16)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(p), x);
    else if constexpr (N <= 8)
        detail::store<N>(p, _mm_cvtsi128_si64(x));
    else {
        detail::store<8>(p, _mm_cvtsi128_si64(x));
        detail::store<N - 8>(p + 8, _mm_extract_epi64(x, 1));
    }
}
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
template <unsigned N> [[gnu::always_inline]] inline void store_prefix(std::uint8_t* p, __m512i x) {
    static_assert(N && N <= 64 && N % 8 == 0);
    // Use stores whose instruction widths match the occupied bytes. A masked
    // 64-byte store for a 24-byte packet can impede loads from the following
    // packet in an in-place operation, despite the logical byte sets being disjoint.
    if constexpr (N == 64)
        _mm512_storeu_si512(p, x);
    else {
        if constexpr (N >= 32)
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), _mm512_castsi512_si256(x));
        if constexpr (N % 32 >= 16)
            _mm_storeu_si128(reinterpret_cast<__m128i*>(p + N / 32 * 32),
                             _mm512_extracti32x4_epi32(x, N / 32 * 2));
        if constexpr (N % 16)
            detail::store<8>(p + N / 16 * 16,
                             _mm_cvtsi128_si64(_mm512_extracti32x4_epi32(x, N / 16)));
    }
}
#endif
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
        else {
            const auto chunks = chunks128(body);
            detail::each<2>([&](auto p) {
                auto* out = tile + p * plane.stride;
                if constexpr (Q == 1)
                    detail::store<8>(out, _mm_extract_epi64(body.v[0], p));
                else
                    detail::each<Q / 2>([&](auto c) {
                        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + c * 16),
                                         chunks[p * (Q / 2) + c]);
                    });
            });
        }
    } else if constexpr (Q && (Fields & 1)) {
        const auto high = right<R>(x);
        const auto chunks = chunks128(high);
        detail::each<2>([&](auto p) {
            auto* output =
                tile + (F::storage == geometry::local ? p * plane.stride
                                                      : detail::body_offset<F>(lane) + p * 8 * Q);
#if defined(__AVX512VBMI__)
            __m512i packet;
            if constexpr (L == 1)
                packet = _mm512_zextsi128_si512(_mm_srli_si128(high.v[0], p * 8));
            if constexpr (L == 2)
                packet = _mm512_zextsi128_si512(chunks[p]);
            if constexpr (L == 4)
                packet = _mm512_zextsi256_si512(high.v[p]);
            if constexpr (L == 8)
                packet =
                    _mm512_inserti64x4(_mm512_castsi256_si512(high.v[p * 2]), high.v[p * 2 + 1], 1);
            static constexpr auto map = [] {
                std::array<std::uint8_t, 64> a{};
                for (unsigned j = 0; j < 8 * Q; ++j)
                    a[j] = (j / Q) * L + j % Q;
                return a;
            }();
            const auto packed = _mm512_permutexvar_epi8(_mm512_loadu_si512(map.data()), packet);
            store_prefix<8 * Q>(output, packed);
#else
            detail::each<(8 * Q + 15) / 16>([&](auto c) {
                constexpr unsigned begin = c * 16, count = std::min<unsigned>(16, 8 * Q - begin);
                constexpr unsigned first = (p * 8 + begin / Q) * L / 16,
                                   last = ((p * 8 + (begin + count - 1) / Q) * L + Q - 1) / 16;
                static constexpr auto map = [] {
                    std::array<std::uint8_t, 16> a{};
                    a.fill(255);
                    for (unsigned j = 0; j < count; ++j)
                        a[j] = (decltype(p)::value * 8 + (begin + j) / Q) * L + (begin + j) % Q -
                               first * 16;
                    return a;
                }();
                store_prefix<count>(output + begin, table<first, last - first + 1>(chunks, map));
            });
#endif
        });
    }
    if constexpr (R && (Fields & 2)) {
        const auto low = low_bytes(x);
        if constexpr (F::storage == geometry::local) {
            const auto packed = transpose128<R>(low);
            detail::store<R>(tile + 8 * Q, _mm_cvtsi128_si64(packed));
            detail::store<R>(tile + plane.stride + 8 * Q, _mm_extract_epi64(packed, 1));
        } else
            detail::tail_fragments<R>(lane / 32, [&](unsigned s, unsigned shift, unsigned mask) {
                auto* out = tile + detail::stripe_offset<F>(s) + lane % 32;
                const auto changed = _mm_sll_epi64(low, _mm_cvtsi32_si128(shift));
                const auto m = _mm_set1_epi8(mask);
                const auto before = _mm_loadu_si128(reinterpret_cast<const __m128i*>(out));
                _mm_storeu_si128(
                    reinterpret_cast<__m128i*>(out),
                    _mm_or_si128(_mm_andnot_si128(m, before), _mm_and_si128(m, changed)));
            });
    }
    detail::each<F::heads / 8>([&](auto p) {
        if constexpr (Fields & (4u << p)) {
            const auto bytes = low_bytes(right<F::width - 8 * (p + 1)>(x));
            const auto& head = destination.stream(p + 1);
            auto* out = head.bytes.data() + (i / F::tile_rows) * head.stride + lane;
            if constexpr (F::storage == geometry::local) {
                detail::store<8>(out, _mm_cvtsi128_si64(bytes));
                detail::store<8>(out + head.stride, _mm_extract_epi64(bytes, 1));
            } else
                _mm_storeu_si128(reinterpret_cast<__m128i*>(out), bytes);
        }
    });
}

/// Dense pure-tail Local execution can join eight physical packets into one
/// store group while keeping the producer/maintenance contract in native lanes.
template <class F, class Produce>
[[gnu::always_inline]] inline void write_local64(std::uint8_t* out, Produce&& produce) {
    static_assert(F::storage == geometry::local && F::heads == 0 && F::width < 8);
    constexpr unsigned R = F::tail;
#if defined(__AVX512VBMI__) && defined(__GFNI__)
    auto values = _mm512_zextsi128_si512(produce(std::integral_constant<unsigned, 0>{}).v[0]);
    detail::each<3>([&](auto p) {
        values = _mm512_inserti32x4(
            values, produce(std::integral_constant<unsigned, (p + 1) * 16>{}).v[0], p + 1);
    });
    if constexpr (R == 1) {
        detail::store<8>(out, _mm512_movepi8_mask(_mm512_slli_epi64(values, 7)));
        return;
    }
    static constexpr auto reverse = [] {
        std::array<std::uint8_t, 64> a{};
        for (unsigned j = 0; j < 64; ++j)
            a[j] = j / 8 * 8 + 7 - j % 8;
        return a;
    }();
    const auto packed = _mm512_gf2p8affine_epi64_epi8(
        _mm512_set1_epi64(0x8040201008040201ULL),
        _mm512_permutexvar_epi8(_mm512_loadu_si512(reverse.data()), values), 0);
    static constexpr auto map = [] {
        std::array<std::uint8_t, 64> a{};
        for (unsigned j = 0; j < 8 * R; ++j)
            a[j] = j / R * 8 + j % R;
        return a;
    }();
    store_prefix<8 * R>(out, _mm512_permutexvar_epi8(_mm512_loadu_si512(map.data()), packed));
#else
    if constexpr (R == 1) {
        std::uint64_t packed = 0;
        detail::each<4>([&](auto p) {
            packed |= std::uint64_t(_mm_movemask_epi8(_mm_slli_epi64(
                          produce(std::integral_constant<unsigned, p * 16>{}).v[0], 7)))
                      << (p * 16);
        });
        detail::store<8>(out, packed);
        return;
    }
    std::array<__m128i, 4> packed;
    if constexpr (R >= 3) {
        detail::each<2>([&](auto p) {
            auto x = _mm256_set_m128i(produce(std::integral_constant<unsigned, p * 32 + 16>{}).v[0],
                                      produce(std::integral_constant<unsigned, p * 32>{}).v[0]);
            auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
                const auto t = _mm256_and_si256(_mm256_xor_si256(x, _mm256_srli_epi64(x, Shift)),
                                                _mm256_set1_epi64x(Mask));
                x = _mm256_xor_si256(x, _mm256_xor_si256(t, _mm256_slli_epi64(t, Shift)));
            };
            exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
            exchange.template operator()<14, 0x0000cccc0000ccccULL>();
            exchange.template operator()<28, 0x00000000f0f0f0f0ULL>();
            packed[p * 2] = _mm256_castsi256_si128(x);
            packed[p * 2 + 1] = _mm256_extracti128_si256(x, 1);
        });
    } else
        detail::each<4>([&](auto p) {
            packed[p] = transpose128<R>(produce(std::integral_constant<unsigned, p * 16>{}).v[0]);
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
#endif
}

template <class F> struct striped_tail_batch {
    static_assert(F::storage == geometry::striped);
    static constexpr unsigned stripes = F::tail / std::gcd(F::tail, 8u);
    std::array<__m128i, stripes> packed;
    striped_tail_batch() {
        detail::each<stripes>([&](auto s) { packed[s] = _mm_setzero_si128(); });
    }
    template <unsigned Group> [[gnu::always_inline]] void add(values<F::tail> value) {
        detail::tail_fragments<F::tail>(Group, [&](unsigned s, unsigned shift, unsigned mask) {
            packed[s] = _mm_or_si128(
                packed[s], _mm_and_si128(_mm_sll_epi64(value.v[0], _mm_cvtsi32_si128(shift)),
                                         _mm_set1_epi8(mask)));
        });
    }
    [[gnu::always_inline]] void store(std::uint8_t* tile, unsigned half) const {
        detail::each<stripes>([&](auto s) {
            _mm_storeu_si128(
                reinterpret_cast<__m128i*>(tile + detail::stripe_offset<F>(s) + half * 16),
                packed[s]);
        });
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
    std::array<__m256i, groups> low;
    detail::each<groups>([&](auto g) {
        const auto a = produce(std::integral_constant<unsigned, g * 32>{});
        const auto b = produce(std::integral_constant<unsigned, g * 32 + 16>{});
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
        if constexpr (F::body) {
            // Admit all 32 values together. Narrowing and body stores use that
            // whole native group instead of two separate 16-row encoders.
            const auto pa = project<F::payload, 0>(a), pb = project<F::payload, 0>(b);
            auto* out = tile + detail::body_offset<F>(g * 32);
            if constexpr (F::body == 1) {
                const auto values = _mm512_inserti64x4(_mm512_castsi256_si512(pa.v[0]), pb.v[0], 1);
                low[g] = _mm512_cvtepi16_epi8(values);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(out),
                                    _mm512_cvtepi16_epi8(_mm512_srli_epi16(values, F::tail)));
            } else {
                const auto x = _mm512_inserti64x4(_mm512_castsi256_si512(pa.v[0]), pa.v[1], 1);
                const auto y = _mm512_inserti64x4(_mm512_castsi256_si512(pb.v[0]), pb.v[1], 1);
                low[g] = _mm256_set_m128i(_mm512_cvtepi32_epi8(y), _mm512_cvtepi32_epi8(x));
                const auto lo = _mm512_cvtepi32_epi16(_mm512_srli_epi32(x, F::tail));
                const auto hi = _mm512_cvtepi32_epi16(_mm512_srli_epi32(y, F::tail));
                _mm512_storeu_si512(out, _mm512_inserti64x4(_mm512_castsi256_si512(lo), hi, 1));
            }
            write16<F, 12>(destination, first + g * 32, a);
            write16<F, 12>(destination, first + g * 32 + 16, b);
        } else
#endif
        {
            write16<F, 13>(destination, first + g * 32, a);
            const auto lo = low_bytes(a);
            write16<F, 13>(destination, first + g * 32 + 16, b);
            low[g] = _mm256_set_m128i(low_bytes(b), lo);
        }
    });
    detail::each<stripes>([&](auto s) {
        auto packed = _mm256_setzero_si256();
        detail::each<groups>([&](auto g) {
            detail::tail_fragments<F::tail>(g, [&](unsigned field, unsigned shift, unsigned mask) {
                if (field == s)
                    packed = _mm256_or_si256(
                        packed, _mm256_and_si256(_mm256_sll_epi64(low[g], _mm_cvtsi32_si128(shift)),
                                                 _mm256_set1_epi8(mask)));
            });
        });
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(tile + detail::stripe_offset<F>(s)), packed);
    });
}
} // namespace ikea::seriespack::x86
