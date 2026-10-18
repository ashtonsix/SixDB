#pragma once
#include "../geometry.h"
#include "hash.h"
#include "packed30.h"
#include "bucket-filter.h"
namespace ikea::hashtable::detail {
template <unsigned W, unsigned b, unsigned V, unsigned E, search_kernel Kernel> struct bucket_ops {
    static constexpr auto layout = geometry::at(W, b);
    static constexpr unsigned key_width = W, bucket_bits = b, K = layout.k;
    static constexpr bool bitmap = K <= 9;
    static constexpr unsigned B = layout.bytes, T = layout.tiles, N = layout.slots, encoding = E;
    static_assert(layout.admits(E), "encoding outside fixed bucket geometry");
    static_assert(Kernel == search_kernel::generic ||
                      ((Kernel == search_kernel::exact30 || Kernel == search_kernel::zero30 ||
                        Kernel == search_kernel::body30 || Kernel == search_kernel::neon30) &&
                       B == 64 && T == 2 && E == 30) ||
                      ((Kernel == search_kernel::fused64 || Kernel == search_kernel::neon64) &&
                       E == 64 && N == 8) ||
                      (Kernel == search_kernel::exact_small512 && B == 64 && E >= 10 && E <= 20) ||
                      ((Kernel == search_kernel::body_small256 ||
                        Kernel == search_kernel::body_small_neon ||
                        Kernel == search_kernel::exact_small256) &&
                       B == 64 && E > 8 && E < 16),
                  "search kernel does not support this fixed bucket geometry");
    static constexpr unsigned value_rows = std::max(8u, N);
    static constexpr std::size_t stride = B + std::size_t(value_rows) * V;
    static constexpr std::size_t bucket_alignment =
        std::size_t{1} << std::min(6u, unsigned(std::countr_zero(stride)));
    static constexpr std::size_t value_offset = B;
    static constexpr std::size_t flags_offset = T * E;
    static constexpr std::size_t overflow_offset = B - 1;
    static_assert(value_offset + std::size_t(value_rows) * V <= stride && overflow_offset < stride);
    static constexpr __uint128_t logical_bucket_count = __uint128_t{1} << b;
    static constexpr std::size_t bucket_count = std::size_t(logical_bucket_count);
    static constexpr unsigned key_bytes = (W + 7) / 8, tuple_bytes = key_bytes + V;
    using value_type = bytes<V>;
    using mix_type = permutation<W, K>;
    struct location {
        word bucket;
        unsigned slot;
        bool primary;
    };
    static unsigned present(const byte* p, unsigned tile) {
        return unsigned(byte(~p[flags_offset + tile]));
    }
    static word occupied(const byte* p) {
        static_assert(!bitmap && T <= 8);
        return ~load<T>(p + flags_offset) & mask<N>;
    }
    static word live_matches(const byte* p, word q, word matches) {
        // Empty slots contain all-ones when E>K, otherwise zero. Zero remains
        // a legal quotient: only that query needs the independent flags mask.
        if constexpr (E == K)
            if (q == 0) [[unlikely]]
                return matches & occupied(p);
        return matches;
    }
    [[gnu::always_inline]] static bool residual_matches(const byte* p, unsigned row, word q) {
        constexpr unsigned R = E % 8;
        constexpr word plane_bits = [] {
            word bits = 0;
            for (unsigned i = 0; i < R; ++i)
                bits |= word{1} << (8 * i);
            return bits;
        }();
        const auto planes = load<8>(p + (row / 8) * E + 8 * (E / 8)) >> (row % 8);
        const auto residual = (((planes & plane_bits) * 0x0102040810204080ULL) >> 56) & mask<R>;
        return residual == (q & mask<R>);
    }
    static bool has(const byte* p, unsigned q) {
        return p[q / 8] & (1u << (q % 8));
    }
    static void mark(byte* p, unsigned slot, bool occupied) {
        if constexpr (bitmap) {
            if (occupied)
                p[slot / 8] |= byte(1u << (slot % 8));
            else
                p[slot / 8] &= byte(~(1u << (slot % 8)));
        } else {
            const byte bit = byte(1u << (slot % 8));
            auto& flags = p[flags_offset + slot / 8];
            if (occupied)
                flags &= byte(~bit);
            else
                flags |= bit;
        }
    }
    static word primary_matches(const byte* p, word q) {
        static_assert(!bitmap);
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr (Kernel == search_kernel::exact_small512 && B == 64 && E >= 10 && E <= 20)
            return live_matches(p, q, exact_small_bucket<E, T>(p, q));
#endif
#if defined(__AVX2__) && defined(__BMI2__)
#if defined(__GFNI__)
        if constexpr (Kernel == search_kernel::exact_small256 && B == 64 && E > 8 && E < 16)
            return live_matches(p, q, exact_small_bucket_avx2<E, T>(p, q));
#endif
        if constexpr (Kernel == search_kernel::body_small256 && B == 64 && E > 8 && E < 16) {
            static_assert((T - 1) * E + 16 <= B);
            constexpr unsigned R = E % 8;
            constexpr word plane_bits = [] {
                word bits = 0;
                for (unsigned i = 0; i < R; ++i)
                    bits |= word{1} << (8 * i);
                return bits;
            }();
            auto matches = live_matches(p, q, small_body_matches<E, T>(p, q));
            while (matches) {
                const auto row = unsigned(std::countr_zero(matches));
                const auto planes = load<8>(p + (row / 8) * E + 8) >> (row % 8);
                if (_pext_u64(planes, plane_bits) == (q & mask<R>))
                    return word{1} << row;
                matches &= matches - 1;
            }
            return 0;
        }
#endif
        if constexpr (Kernel == search_kernel::body30 && B == 64 && T == 2 && E == 30) {
            const sp::composition::local_equal8<6> match(q & 63);
            unsigned residual = 0;
            each<2>([&](auto tile) {
                residual |= unsigned(match(load<8>(p + tile * 30 + 24))) << (tile * 8);
            });
            const auto body = [&] {
#if defined(__AVX2__) && !defined(__AVX512VBMI__)
                if constexpr (Kernel == search_kernel::body30)
                    return body30_dword(p, q);
#endif
                return body30(p, q);
            }();
            const auto matches = live_matches(p, q, body & residual);
            return matches;
        } else if constexpr ((Kernel == search_kernel::exact30 ||
                              Kernel == search_kernel::zero30) &&
                             B == 64 && T == 2 && E == 30)
            return live_matches(p, q, exact30<true, Kernel == search_kernel::zero30>(p, q));
        else {
            using F = sp::format<E, sp::geometry::local, 0>;
            const sp::dense_source<F> source{p, N};
            const auto expression = sp::composition::describe(source);
            word matches = 0;
            each<T>([&](auto tile) {
                matches |= word(sp::composition::filter8_unchecked<ikea::comparison::equal>(
                               expression, tile * 8, q))
                           << (tile * 8);
            });
            return live_matches(p, q, matches);
        }
    }
#if defined(__aarch64__)
    // E64 is an eight-qword SeriesPack tile. Preserve the native comparison
    // lanes until a single byte per row remains, then find the first row.
    // This avoids four independent SIMD-to-GPR bitmap reductions.
    [[gnu::always_inline]] static word equal64_neon(const byte* p, word q) {
        const auto query = vdupq_n_u64(q);
        const auto eq0 = vceqq_u64(vld1q_u64(reinterpret_cast<const word*>(p)), query);
        const auto eq1 = vceqq_u64(vld1q_u64(reinterpret_cast<const word*>(p + 16)), query);
        const auto eq2 = vceqq_u64(vld1q_u64(reinterpret_cast<const word*>(p + 32)), query);
        const auto eq3 = vceqq_u64(vld1q_u64(reinterpret_cast<const word*>(p + 48)), query);
        const auto ab = vuzp1q_u32(vreinterpretq_u32_u64(eq0), vreinterpretq_u32_u64(eq1));
        const auto cd = vuzp1q_u32(vreinterpretq_u32_u64(eq2), vreinterpretq_u32_u64(eq3));
        const auto halves = vuzp1q_u16(vreinterpretq_u16_u32(ab), vreinterpretq_u16_u32(cd));
        const auto lanes = vmovn_u16(halves);
        return vget_lane_u64(vreinterpret_u64_u8(lanes), 0);
    }
#endif
    static unsigned primary_find(const byte* p, word q) {
        if constexpr (bitmap)
            return has(p, unsigned(q)) ? unsigned(q) : N;
#if defined(__aarch64__)
        else if constexpr (Kernel == search_kernel::body_small_neon && B == 64 && E > 8 && E < 16) {
            static_assert((T - 1) * E + 16 <= B);
            auto exact = [&](unsigned row)
                             __attribute__((always_inline)) { return residual_matches(p, row, q); };
            word candidates;
            if constexpr (T == 4) {
                const auto bodies = small_body_comparisons_neon<E>(p, q);
                if (q) {
                    const auto first = first_small_body_neon(bodies);
                    if (first == 255)
                        return N;
                    // ORN leaves each true lane's index (0..31), or 255 for
                    // a false lane. A non-255 horizontal minimum is a row.
                    __builtin_assume(first < N);
                    if (exact(first))
                        return first;
                    // The first minimum was the lowest body match. Retain the
                    // equality vectors and build an iterable mask only on retry.
                    candidates = small_body_mask_neon(bodies);
                    candidates &= candidates - 1;
                } else
                    candidates = live_matches(p, q, small_body_mask_neon(bodies));
            } else
                candidates = live_matches(p, q, small_body_matches_neon<E, T>(p, q));
            while (candidates) {
                const auto row = unsigned(std::countr_zero(candidates));
                if (exact(row))
                    return row;
                candidates &= candidates - 1;
            }
            return N;
        } else if constexpr (Kernel == search_kernel::neon30 && B == 64 && T == 2 && E == 30) {
            // Select a body row before verifying its six residual bits.
            // At K=E, empty rows encode zero and that query needs flags. For
            // smaller K the all-ones sentinel makes this zero check redundant.
            auto candidates = body30_neon_nibbles(p, q);
            while (candidates) {
                const auto row = unsigned(std::countr_zero(candidates) / 4);
                if (residual_matches(p, row, q) && (q || (present(p, row / 8) & (1u << (row % 8)))))
                    return row;
                candidates &= candidates - 1;
            }
            return N;
        } else if constexpr (Kernel == search_kernel::neon64 && E == 64 && N == 8) {
            if constexpr (E == K)
                if (!q) {
                    const auto matches = primary_matches(p, q);
                    return matches ? unsigned(std::countr_zero(matches)) : N;
                }
            return std::countr_zero(equal64_neon(p, q)) / 8; // Zero mask gives absent row N.
        }
#endif
        else {
            const auto matches = primary_matches(p, q);
            return matches ? unsigned(std::countr_zero(matches)) : N;
        }
    }
    template <class Consume>
    [[gnu::always_inline]] static void consume_primary(const byte* p, unsigned slot,
                                                       Consume&& consume) {
        if constexpr (V > 0) {
            // Wide byte values are consecutive 64-bit SeriesPack limbs. Their
            // limbs remain together: consuming one 64-byte value must not
            // require eight separately strided cache lines.
            value_type value;
            each<(V + 7) / 8>([&](auto c) {
                constexpr unsigned width = std::min(8u, V - c * 8);
                const auto v = [&] {
                    if constexpr (V % 8 == 0)
                        return direct_point<64>(p + value_offset, slot * (V / 8) + c,
                                                value_rows * (V / 8));
                    else
                        return direct_point<width * 8>(
                            p + value_offset + std::size_t(c) * value_rows * 8, slot, value_rows);
                }();
                store<width>(value.data() + c * 8, v);
            });
            consume(value);
        }
    }
    static void write_value(byte* p, unsigned slot, const value_type& value) {
        if constexpr (V > 0)
            each<(V + 7) / 8>([&](auto c) {
                constexpr unsigned width = std::min(8u, V - c * 8);
                if constexpr (V % 8 == 0)
                    assign_point<64>(p + value_offset, slot * (V / 8) + c, value_rows * (V / 8),
                                     load<width>(value.data() + c * 8));
                else
                    assign_point<width * 8>(p + value_offset + std::size_t(c) * value_rows * 8,
                                            slot, value_rows, load<width>(value.data() + c * 8));
            });
    }
    static word tuple_key(const byte* p) {
        return tuple_field<key_bytes>::read(p);
    }
    template <class Consume> static void consume_tuple(const byte* p, Consume&& consume) {
        if constexpr (V) {
            value_type value;
            each<(V + 7) / 8>([&](auto c) {
                constexpr unsigned width = std::min(8u, V - c * 8);
                store<width>(value.data() + c * 8, tuple_field<width>::read(p + key_bytes + c * 8));
            });
            consume(value);
        }
    }
    static word vacancies(const byte* p) {
        return load<T>(p + flags_offset);
    }
#if defined(__AVX512VBMI__) && defined(__GFNI__)
    static constexpr bool vector_write_supported = E == 14 || E == 16 || E == 30 || E == 32;
    // Share physical SIMD comparisons, retaining each caller's logical mask
    // width and liveness/first-row handling. First-match historically uses word.
    [[gnu::always_inline]] static word vector_write_matches(const byte* p, word q) {
        if constexpr (E == 14)
            return exact_small_bucket<E, T>(p, q);
        else if constexpr (E == 16)
            return _mm512_cmpeq_epi16_mask(_mm512_loadu_si512(p), _mm512_set1_epi16(short(q))) &
                   mask<N>;
        else if constexpr (E == 30)
            return exact30<true, false>(p, q);
        else {
            static_assert(E == 32);
            return _mm512_cmpeq_epi32_mask(_mm512_loadu_si512(p), _mm512_set1_epi32(int(q)));
        }
    }
#endif
    static unsigned write_find(const byte* p, word q) {
        if constexpr (bitmap)
            return primary_find(p, q);
        else {
            word matches = 0;
#if defined(__AVX512VBMI__) && defined(__GFNI__)
            if constexpr (vector_write_supported)
                matches = vector_write_matches(p, q);
            else
#endif
#if defined(__aarch64__)
                if constexpr (E == 14) {
                auto exact = [&](unsigned row) __attribute__((always_inline)) {
                    return residual_matches(p, row, q);
                };
                const auto bodies = small_body_comparisons_neon<E>(p, q);
                if (q) {
                    const auto first = first_small_body_neon(bodies);
                    if (first == 255)
                        return N;
                    if (exact(first))
                        return first;
                    matches = small_body_mask_neon(bodies);
                    matches &= matches - 1;
                } else
                    matches = small_body_mask_neon(bodies) & ~vacancies(p);
                while (matches) {
                    const auto row = unsigned(std::countr_zero(matches));
                    if (exact(row))
                        return row;
                    matches &= matches - 1;
                }
                return N;
            } else if constexpr (E == 30) {
                // Maintained NEON reader: pick a body row, then check its six
                // residual bits. Shared upserts retain their complete-mask path.
                auto candidates = body30_neon_nibbles(p, q);
                while (candidates) {
                    const auto row = unsigned(std::countr_zero(candidates) / 4);
                    if (residual_matches(p, row, q) && (q || !(vacancies(p) & (word{1} << row))))
                        return row;
                    candidates &= candidates - 1;
                }
                return N;
            } else if constexpr (E == 16) {
                const auto query = vdupq_n_u16(std::uint16_t(q));
                auto a = vceqq_u16(vld1q_u16(reinterpret_cast<const std::uint16_t*>(p)), query);
                auto c =
                    vceqq_u16(vld1q_u16(reinterpret_cast<const std::uint16_t*>(p + 16)), query);
                auto d =
                    vceqq_u16(vld1q_u16(reinterpret_cast<const std::uint16_t*>(p + 32)), query);
                auto e =
                    vceqq_u16(vld1q_u16(reinterpret_cast<const std::uint16_t*>(p + 48)), query);
                matches = ikea::detail::filter::byte_bits(vcombine_u8(vmovn_u16(a), vmovn_u16(c)));
                matches |=
                    word(ikea::detail::filter::byte_bits(vcombine_u8(vmovn_u16(d), vmovn_u16(e))))
                    << 16;
                matches &= mask<N>;
            } else
#endif
            {
                // Keep this stack-backed SeriesPack expression in its caller's
                // scope. A generic helper increased AVX2 K14 register spills; the
                // comparison primitive and its geometry remain owned by SeriesPack.
                using F = sp::format<E, sp::geometry::local, 0>;
                const sp::dense_source<F> source{p, N};
                const auto expression = sp::composition::describe(source);
                each<T>([&](auto tile) {
                    matches |= word(sp::composition::filter8_unchecked<ikea::comparison::equal>(
                                   expression, tile * 8, q))
                               << (tile * 8);
                });
            }
            if constexpr (E == K)
                if (!q)
                    matches &= ~vacancies(p);
            return matches ? unsigned(std::countr_zero(matches)) : N;
        }
    }
    using row_mask = std::conditional_t<(N <= 32), unsigned, word>;
    static row_mask matching_rows(const byte* p, word q) {
        word matches = 0;
#if defined(__AVX512VBMI__) && defined(__GFNI__)
        if constexpr (vector_write_supported)
            matches = vector_write_matches(p, q);
        else
#endif
        {
            // Complete masks avoid retaining the first-body matcher's inner
            // presence/retry branches in the mixed-upsert control. The split
            // path still uses write_find() and its first-row optimization.
            // Keep this stack-backed SeriesPack expression in its caller's
            // scope. A generic helper increased AVX2 K14 register spills; the
            // comparison primitive and its geometry remain owned by SeriesPack.
            using F = sp::format<E, sp::geometry::local, 0>;
            const sp::dense_source<F> source{p, N};
            const auto expression = sp::composition::describe(source);
            each<T>([&](auto tile) {
                matches |= word(sp::composition::filter8_unchecked<ikea::comparison::equal>(
                               expression, tile * 8, q))
                           << (tile * 8);
            });
        }
        if constexpr (E == K)
            if (!q)
                matches &= ~vacancies(p);
        return row_mask(matches);
    }
    static void occupy(byte* p, unsigned row, word q, const bytes<V>& v) {
        assign_point<E>(p, row, N, q);
        write_value(p, row, v);
        p[flags_offset + row / 8] &= byte(~(1u << (row % 8)));
    }
};
} // namespace ikea::hashtable::detail
