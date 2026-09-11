#pragma once

#include <ikea/seriespack/native_avx2.h>
#include <ikea/seriespack/native_avx512.h>

namespace head_encode_experiment {
namespace sp = ikea::seriespack;

struct avx2 {
    static constexpr unsigned bytes = 32;
    using vector = __m256i;
    template<unsigned L, unsigned N>
    [[gnu::always_inline]] static inline auto load(const void* p) {
        return sp::detail::avx2::decode_body_prefix<L, L, N>(static_cast<const std::uint8_t*>(p));
    }
    template<unsigned L, unsigned N>
    [[gnu::always_inline]] static inline void store(std::uint8_t* p, vector v) {
        sp::detail::avx2::encode_body_prefix<1, L, N>(p, v);
    }
    template<unsigned L, unsigned Shift>
    [[gnu::always_inline]] static inline auto right(vector v) {
        if constexpr (Shift >= 8 * L) return _mm256_setzero_si256();
        else {
            auto result = sp::avx2::native_detail::shift<L, -int(Shift)>(v);
            if constexpr (L == 1 && Shift != 0)
                result = _mm256_and_si256(result, _mm256_set1_epi8(static_cast<char>(255U >> Shift)));
            return result;
        }
    }
};

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
struct avx512 {
    static constexpr unsigned bytes = 64;
    using vector = __m512i;
    template<unsigned L, unsigned N>
    [[gnu::always_inline]] static inline auto load(const void* p) {
        return sp::detail::avx512::decode_body_prefix<L, L, N>(static_cast<const std::uint8_t*>(p));
    }
    template<unsigned L, unsigned N>
    [[gnu::always_inline]] static inline void store(std::uint8_t* p, vector v) {
        sp::detail::avx512::encode_body_prefix<1, L, N>(p, v);
    }
    template<unsigned L, unsigned Shift>
    [[gnu::always_inline]] static inline auto right(vector v) {
        if constexpr (Shift >= 8 * L) return _mm512_setzero_si512();
        else {
            auto result = sp::avx512::native_detail::shift<L, -int(Shift)>(v);
            if constexpr (L == 1 && Shift != 0)
                result = _mm512_and_si512(result, _mm512_set1_epi8(static_cast<char>(255U >> Shift)));
            return result;
        }
    }
};
#endif

template<unsigned Shift, class U>
[[gnu::always_inline]] inline std::uint8_t byte(U value) {
    if constexpr (Shift >= sizeof(U) * 8) return 0;
    else return static_cast<std::uint8_t>(value >> Shift);
}

// Baseline preserves native.cpp's independent plane traversal and fixed-tile
// inner loop. Only placement/view bookkeeping is removed from this diagnostic.
template<class Ops, unsigned Shift, unsigned T, class U>
[[gnu::always_inline]] inline void separate_plane(std::uint8_t* plane,
    std::size_t stride, const U* input, std::size_t n) {
    constexpr unsigned L = sizeof(U), N = std::min(T, Ops::bytes / L);
    const auto full = n / T;
    for (std::size_t tile = 0; tile < full; ++tile) {
        auto* out = plane + tile * stride;
        if constexpr (Shift >= 8 * L) std::memset(out, 0, T);
        else for (unsigned i = 0; i < T; i += N) {
            const auto values = Ops::template load<L, N>(input + tile * T + i);
            Ops::template store<L, N>(out + i, Ops::template right<L, Shift>(values));
        }
    }
    if (const auto left = n % T; left != 0) {
        auto* out = plane + full * stride;
        for (std::size_t i = 0; i < left; ++i) {
            U value;
            std::memcpy(&value, input + full * T + i, sizeof(U));
            out[i] = byte<Shift>(value);
        }
        std::memset(out + left, 0, T - left);
    }
}

// A shared register supplies both head projections; its native carrier and
// store grain are otherwise the same as the baseline.
template<class Ops, unsigned W, unsigned H, unsigned N, class U>
[[gnu::always_inline]] inline void joint_native(const U* input,
    std::uint8_t* head0, std::uint8_t* head1) {
    constexpr unsigned L = sizeof(U);
    const auto source = Ops::template load<L, N>(input);
    const auto low = Ops::template right<L, W>(source);
    if constexpr (H == 16) {
        Ops::template store<L, N>(head0, Ops::template right<L, 8>(low));
        Ops::template store<L, N>(head1, low);
    } else Ops::template store<L, N>(head0, low);
}

// Exactly sixteen projected words. Recursion narrows registers, with no
// temporary materialized array. Shifting happens before any source truncation.
template<unsigned W, unsigned InputL, unsigned WorkL = 2>
[[gnu::always_inline]] inline __m256i words256(const std::uint8_t* input) {
    if constexpr (W >= InputL * 8) return _mm256_setzero_si256();
    else if constexpr (InputL <= WorkL)
        return avx2::right<WorkL, W>(sp::detail::avx2::decode_body<InputL, WorkL>(input));
    else return sp::avx2::native_detail::narrow_pair<WorkL * 2>(
        words256<W, InputL, WorkL * 2>(input),
        words256<W, InputL, WorkL * 2>(input + 16 / WorkL * InputL));
}

template<unsigned W, unsigned H, class U>
[[gnu::always_inline]] inline void compact256(const U* input,
    std::uint8_t* head0, std::uint8_t* head1) {
    const auto words = words256<W, sizeof(U)>(reinterpret_cast<const std::uint8_t*>(input));
    if constexpr (H == 16) {
        sp::detail::avx2::encode_body<1, 2>(head0, _mm256_srli_epi16(words, 8));
        sp::detail::avx2::encode_body<1, 2>(head1, words);
    } else sp::detail::avx2::encode_body<1, 2>(head0, words);
}

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
// Exactly thirty-two projected words. Direct truncation avoids a qword->dword
// ->word cascade; the final byte narrowing permits one 32-byte store per plane.
template<unsigned W, class U>
[[gnu::always_inline]] inline __m512i words512(const U* input) {
    constexpr unsigned L = sizeof(U);
    if constexpr (W >= L * 8) return _mm512_setzero_si512();
    else if constexpr (L <= 2)
        return avx512::right<2, W>(sp::detail::avx512::decode_body<L, 2>(
            reinterpret_cast<const std::uint8_t*>(input)));
    else if constexpr (L == 4) {
        const auto a = _mm512_cvtepi32_epi16(avx512::right<4, W>(_mm512_loadu_si512(input)));
        const auto b = _mm512_cvtepi32_epi16(avx512::right<4, W>(_mm512_loadu_si512(input + 16)));
        return _mm512_inserti64x4(_mm512_castsi256_si512(a), b, 1);
    } else {
        const auto part = [&] [[gnu::always_inline]] (auto i) {
            return _mm512_cvtepi64_epi16(avx512::right<8, W>(_mm512_loadu_si512(input + i * 8)));
        };
        auto result = _mm512_castsi128_si512(part(std::integral_constant<unsigned, 0>{}));
        sp::detail::static_for<3>([&](auto after) { result = _mm512_inserti32x4(result, part(after + 1), after + 1); });
        return result;
    }
}

template<unsigned W, unsigned H, class U>
[[gnu::always_inline]] inline void compact512(const U* input,
    std::uint8_t* head0, std::uint8_t* head1) {
    const auto words = words512<W>(input);
    if constexpr (H == 16) {
        sp::detail::avx512::encode_body<1, 2>(head0, _mm512_srli_epi16(words, 8));
        sp::detail::avx512::encode_body<1, 2>(head1, words);
    } else sp::detail::avx512::encode_body<1, 2>(head0, words);
}
#endif

// Mode0: separate passes; Mode1: shared pass at the existing tile/native grain;
// Mode2: shared compact region; Mode3: explicit full-tile-unroll diagnostic
// control. The latter measures compiler grain, not a production unroll policy.
// region. All sources contain exactly n readable values; the final head packet
// is fully writable and receives canonical zero slack. Plane gaps are excluded.
template<class Ops, unsigned W, unsigned H, unsigned T, class U, unsigned Mode>
[[gnu::always_inline]] inline void encode(const U* __restrict input, std::size_t n,
    std::uint8_t* __restrict head0, std::uint8_t* __restrict head1,
    std::size_t stride0 = T, std::size_t stride1 = T) {
    static_assert(H == 8 || H == 16);
    static_assert(T == 8 || T == 256);
    static_assert(Mode <= 3);
    if (n == 0) return;
    if constexpr (Mode == 0) {
        separate_plane<Ops, W + H - 8, T>(head0, stride0, input, n);
        if constexpr (H == 16) separate_plane<Ops, W, T>(head1, stride1, input, n);
    } else if constexpr (Mode == 1 || Mode == 3) {
        // Match the baseline's fixed-T inner loop and its compiler unrolling.
        // This arm changes the number of source passes, not logical loop grain.
        constexpr unsigned Native = std::min<unsigned>(T, Ops::bytes / sizeof(U));
        const auto full = n / T;
        for (std::size_t tile = 0; tile < full; ++tile) {
            auto* high = head0 + tile * stride0;
            auto* low = H == 16 ? head1 + tile * stride1 : head1;
            const auto fragment = [&] [[gnu::always_inline]] (unsigned i) {
                joint_native<Ops, W, H, Native>(input + tile * T + i,
                    high + i, H == 16 ? low + i : low);
            };
            if constexpr (Mode == 3) {
#pragma clang loop unroll(full)
                for (unsigned i = 0; i < T; i += Native) fragment(i);
            } else for (unsigned i = 0; i < T; i += Native) fragment(i);
        }
        if (const auto left = n % T; left != 0) {
            auto* high = head0 + full * stride0;
            auto* low = H == 16 ? head1 + full * stride1 : head1;
            for (std::size_t i = 0; i < left; ++i) {
                U value;
                std::memcpy(&value, input + full * T + i, sizeof(U));
                high[i] = byte<W + H - 8>(value);
                if constexpr (H == 16) low[i] = byte<W>(value);
            }
            std::memset(high + left, 0, T - left);
            if constexpr (H == 16) std::memset(low + left, 0, T - left);
        }
    } else {
        constexpr unsigned Native = std::min<unsigned>(T, Ops::bytes / sizeof(U));
        constexpr unsigned Compact = Ops::bytes / 2;
        const auto region = [&] [[gnu::always_inline]] (const U* source,
            std::uint8_t* high, std::uint8_t* low, std::size_t count) {
            std::size_t i = 0;
            if constexpr (Mode == 2) {
#pragma clang loop unroll(disable)
                for (; count - i >= Compact; i += Compact) {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
                    if constexpr (Ops::bytes == 64) compact512<W, H>(source + i, high + i, H == 16 ? low + i : low);
                    else
#endif
                        compact256<W, H>(source + i, high + i, H == 16 ? low + i : low);
                }
            }
#pragma clang loop unroll(disable)
            for (; count - i >= Native; i += Native)
                joint_native<Ops, W, H, Native>(source + i, high + i, H == 16 ? low + i : low);
            for (; i < count; ++i) {
                U value;
                std::memcpy(&value, source + i, sizeof(U));
                high[i] = byte<W + H - 8>(value);
                if constexpr (H == 16) low[i] = byte<W>(value);
            }
        };
        if (stride0 == T && (H == 8 || stride1 == T)) {
            region(input, head0, head1, n);
            if (const auto left = n % T; left != 0) {
                std::memset(head0 + n, 0, T - left);
                if constexpr (H == 16) std::memset(head1 + n, 0, T - left);
            }
        } else {
            const auto tiles = (n + T - 1) / T;
            for (std::size_t tile = 0; tile < tiles; ++tile) {
                const auto count = std::min<std::size_t>(T, n - tile * T);
                auto* high = head0 + tile * stride0;
                auto* low = H == 16 ? head1 + tile * stride1 : head1;
                region(input + tile * T, high, low, count);
                if (count != T) {
                    std::memset(high + count, 0, T - count);
                    if constexpr (H == 16) std::memset(low + count, 0, T - count);
                }
            }
        }
    }
}
} // namespace head_encode_experiment
