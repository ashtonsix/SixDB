#pragma once
#include <ikea/seriespack/native_avx2.h>
#include <ikea/seriespack/native_avx512.h>

// Isolated dense-region experiment. Heads and residual payload have independent
// pointers and strides. Each coalesced region reads exactly its complete dense
// packets; strided placements fall back to exact individual packets.
namespace headed_region_experiment {
namespace sp = ikea::seriespack;

template<unsigned Working, class UInt>
[[gnu::always_inline]] inline void store256(__m256i values, UInt* out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    static_assert((Working == 2 || Working == 4) && L >= Working && L <= 8);
    if constexpr (L == Working) _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), values);
    else sp::detail::static_for<L / Working>([&](auto part) {
        constexpr unsigned byte = part * 32 * Working / L;
        auto source = [&] [[gnu::always_inline]] {
            if constexpr (byte < 16) return _mm256_castsi256_si128(values);
            else return _mm256_extracti128_si256(values, 1);
        }();
        if constexpr (byte % 16) source = _mm_srli_si128(source, byte % 16);
        const auto widened = [&] [[gnu::always_inline]] {
            if constexpr (Working == 4) return _mm256_cvtepu32_epi64(source);
            else if constexpr (L == 4) return _mm256_cvtepu16_epi32(source);
            else return _mm256_cvtepu16_epi64(source);
        }();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + part * (32 / L)), widened);
    });
}

template<unsigned W, unsigned H, class UInt>
[[gnu::always_inline]] inline void region32_256(const std::uint8_t* p,
    const std::uint8_t* head0, const std::uint8_t* head1, UInt* out) noexcept {
    static_assert(W >= 1 && W <= 7 && (H == 8 || H == 16));
    constexpr unsigned Working = H == 8 ? 2 : 4;
    const auto residual = sp::avx2::read_local_region32<W>(p);
    sp::detail::static_for<Working>([&](auto part) {
        constexpr unsigned begin = part * (32 / Working);
        auto bytes = [&] [[gnu::always_inline]] {
            if constexpr (begin < 16) return _mm256_castsi256_si128(residual);
            else return _mm256_extracti128_si256(residual, 1);
        }();
        if constexpr (begin % 16) bytes = _mm_srli_si128(bytes, begin % 16);
        const auto tail = sp::avx2::native_detail::expand_bytes<Working>(bytes);
        auto head = sp::detail::avx2::decode_body<1, Working>(head0 + begin);
        if constexpr (H == 16) head = sp::avx2::join<Working, 8>(head,
            sp::detail::avx2::decode_body<1, Working>(head1 + begin));
        store256<Working>(sp::avx2::join<Working, W>(head, tail), out + begin);
    });
}

template<unsigned W, unsigned H, class UInt>
[[gnu::always_inline]] inline void tile256(const std::uint8_t* p,
    const std::uint8_t* head0, const std::uint8_t* head1, UInt* out) noexcept {
    constexpr unsigned L = sizeof(UInt), N = std::min(8u, 32u / L);
    sp::detail::static_for<8 / N>([&](auto part) {
        const auto tail = sp::avx2::read_fragment<W, sp::geometry::local8, L, part * N>(p);
        auto head = sp::detail::avx2::decode_body_prefix<1, L, N>(head0 + part * N);
        if constexpr (H == 16) head = sp::avx2::join<L, 8>(head,
            sp::detail::avx2::decode_body_prefix<1, L, N>(head1 + part * N));
        const auto value = sp::avx2::join<L, W>(head, tail);
        sp::detail::avx2::encode_body_prefix<L, L, N>(reinterpret_cast<std::uint8_t*>(out + part * N), value);
    });
}

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
template<unsigned Working, class UInt>
[[gnu::always_inline]] inline void store512(__m512i values, UInt* out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    static_assert((Working == 2 || Working == 4) && L >= Working && L <= 8);
    if constexpr (L == Working) _mm512_storeu_si512(out, values);
    else sp::detail::static_for<L / Working>([&](auto part) {
        const auto widened = [&] [[gnu::always_inline]] {
            if constexpr (L / Working == 2) {
                const auto source = _mm512_extracti64x4_epi64(values, part);
                if constexpr (Working == 4) return _mm512_cvtepu32_epi64(source);
                else return _mm512_cvtepu16_epi32(source);
            } else return _mm512_cvtepu16_epi64(_mm512_extracti32x4_epi32(values, part));
        }();
        _mm512_storeu_si512(out + part * (64 / L), widened);
    });
}

template<unsigned W, unsigned H, class UInt, unsigned Count>
[[gnu::always_inline]] inline void region512(const std::uint8_t* p,
    const std::uint8_t* head0, const std::uint8_t* head1, UInt* out) noexcept {
    static_assert(W >= 1 && W <= 7 && (H == 8 || H == 16));
    static_assert(Count == 32 || Count == 64);
    constexpr unsigned Working = H == 8 ? 2 : 4;
    const auto residual = [&] [[gnu::always_inline]] {
        if constexpr (Count == 32) return sp::avx2::read_local_region32<W>(p);
        else return sp::avx512::read_local_region64<W>(p);
    }();
    sp::detail::static_for<Count / (64 / Working)>([&](auto part) {
        constexpr unsigned begin = part * (64 / Working);
        const auto tail = [&] [[gnu::always_inline]] {
            if constexpr (Working == 2) {
                const auto bytes = [&] [[gnu::always_inline]] {
                    if constexpr (Count == 32) return residual;
                    else return _mm512_extracti64x4_epi64(residual, part);
                }();
                return _mm512_cvtepu8_epi16(bytes);
            } else {
                const auto bytes = [&] [[gnu::always_inline]] {
                    if constexpr (Count == 32) return _mm256_extracti128_si256(residual, part);
                    else return _mm512_extracti32x4_epi32(residual, part);
                }();
                return _mm512_cvtepu8_epi32(bytes);
            }
        }();
        auto head = sp::detail::avx512::decode_body<1, Working>(head0 + begin);
        if constexpr (H == 16) head = sp::avx512::join<Working, 8>(head,
            sp::detail::avx512::decode_body<1, Working>(head1 + begin));
        store512<Working>(sp::avx512::join<Working, W>(head, tail), out + begin);
    });
}

template<unsigned W, unsigned H, class UInt>
[[gnu::always_inline]] inline void tile512(const std::uint8_t* p,
    const std::uint8_t* head0, const std::uint8_t* head1, UInt* out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    const auto tail = sp::avx512::read_fragment<W, sp::geometry::local8, L, 0>(p);
    auto head = sp::detail::avx512::decode_body_prefix<1, L, 8>(head0);
    if constexpr (H == 16) head = sp::avx512::join<L, 8>(head,
        sp::detail::avx512::decode_body_prefix<1, L, 8>(head1));
    const auto value = sp::avx512::join<L, W>(head, tail);
    sp::detail::avx512::encode_body_prefix<L, L, 8>(reinterpret_cast<std::uint8_t*>(out), value);
}
#endif

// Modes: current256 eight-value tile; coalesced32/YMM; current512 tile;
// coalesced32/ZMM; coalesced64/ZMM. Logical wire packets remain eight values.
template<unsigned W, unsigned H, class UInt, unsigned Mode>
[[gnu::always_inline]] inline void decode(const std::uint8_t* __restrict p,
    const std::uint8_t* __restrict head0, const std::uint8_t* __restrict head1,
    UInt* __restrict out, std::size_t count, std::size_t payload_stride = W,
    std::size_t head0_stride = 8, std::size_t head1_stride = 8) noexcept {
    // Complete packets are admitted before this private entry. Each physical
    // source retains its own stride; dense coalescing requires all used planes.
    __builtin_assume(count % 8 == 0);
    const auto tile = [&] [[gnu::always_inline]] (const std::uint8_t* payload,
        const std::uint8_t* high, const std::uint8_t* low, UInt* destination) {
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        if constexpr (Mode >= 2) tile512<W, H>(payload, high, low, destination);
        else
#endif
            tile256<W, H>(payload, high, low, destination);
    };
    if (payload_stride != W || head0_stride != 8 || (H == 16 && head1_stride != 8)) {
#pragma clang loop unroll(disable)
        for (std::size_t index = 0; index < count / 8; ++index) {
            const auto* low = H == 16 ? head1 + index * head1_stride : head1;
            tile(p + index * payload_stride, head0 + index * head0_stride, low, out + index * 8);
        }
        return;
    }
    constexpr unsigned Grain = Mode == 0 || Mode == 2 ? 8 : Mode == 4 ? 64 : 32;
#pragma clang loop unroll(disable)
    for (; count >= Grain; count -= Grain, p += Grain / 8 * W, head0 += Grain, out += Grain) {
        if constexpr (Mode == 0) tile256<W, H>(p, head0, head1, out);
        else if constexpr (Mode == 1) region32_256<W, H>(p, head0, head1, out);
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
        else if constexpr (Mode == 2) tile512<W, H>(p, head0, head1, out);
        else region512<W, H, UInt, Grain>(p, head0, head1, out);
#else
        else static_assert(Mode < 2);
#endif
        if constexpr (H == 16) head1 += Grain;
    }
#pragma clang loop unroll(disable)
    for (; count; count -= 8, p += W, head0 += 8, out += 8) {
        tile(p, head0, head1, out);
        if constexpr (H == 16) head1 += 8;
    }
}
} // namespace headed_region_experiment
