#pragma once

#include <ikea/seriespack/native_avx2.h>

// Experimental Local1 lowering only. No production selection depends on it.
namespace local1_experiment {
namespace sp = ikea::seriespack;

// Four Local1 bytes -> 32 ascending byte lanes. Each packed byte supplies
// exactly eight lanes, with bit zero first. The only readable extent is p[0:4].
[[gnu::always_inline]] inline __m256i read_region32(const std::uint8_t* p) noexcept {
    std::uint32_t word;
    std::memcpy(&word, p, sizeof word);
    const auto replicated = _mm256_broadcastd_epi32(_mm_cvtsi32_si128(static_cast<int>(word)));
    const auto groups = _mm256_setr_epi8(
        0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1,
        2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3);
    const auto bits = _mm256_set1_epi64x(static_cast<long long>(0x8040201008040201ULL));
    const auto expanded = _mm256_shuffle_epi8(replicated, groups);
    const auto set = _mm256_cmpeq_epi8(_mm256_and_si256(expanded, bits), bits);
    return _mm256_and_si256(set, _mm256_set1_epi8(1));
}

// A subset of one Local1 byte -> unsigned native lanes, high lanes zero.
// Count=0 does not read p. All other instances read precisely p[0], including
// arbitrary Begin/Count boundary fragments that are not a native full grain.
template<unsigned L, unsigned Begin, unsigned Count>
[[gnu::always_inline]] inline __m256i read_fragment(const std::uint8_t* p) noexcept {
    static_assert(L == 1 || L == 2 || L == 4 || L == 8);
    static_assert(Begin <= 8 && Count <= 32 / L && Begin + Count <= 8);
    if constexpr (Count == 0) return _mm256_setzero_si256();
    else if constexpr (L >= 4) {
        constexpr auto shifts = [] {
            std::array<sp::scalar_for_width<8 * L>, 32 / L> v{};
            for (unsigned i = 0; i < v.size(); ++i) v[i] = i + Begin;
            return v;
        }();
        constexpr auto active = [] {
            std::array<sp::scalar_for_width<8 * L>, 32 / L> v{};
            for (unsigned i = 0; i < Count; ++i) v[i] = 1;
            return v;
        }();
        // Every active shift selects a bit below eight. Byte broadcast avoids
        // a scalar-to-vector round trip; the final one-bit mask discards the
        // repeated copies in the higher bytes of each integer lane.
        const auto source = _mm256_set1_epi8(static_cast<char>(*p));
        const auto count = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(shifts.data()));
        const auto values = [&] [[gnu::always_inline]] {
            if constexpr (L == 4) return _mm256_srlv_epi32(source, count);
            else return _mm256_srlv_epi64(source, count);
        }();
        return _mm256_and_si256(values,
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(active.data())));
    } else {
        constexpr auto bits = [] {
            std::array<std::uint8_t, 16> v{};
            for (unsigned i = 0; i < Count; ++i) v[i] = std::uint8_t(1u << (Begin + i));
            return v;
        }();
        constexpr auto active = [] {
            std::array<std::uint8_t, 16> v{};
            for (unsigned i = 0; i < Count; ++i) v[i] = 1;
            return v;
        }();
        const auto mask = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bits.data()));
        const auto source = _mm_set1_epi8(static_cast<char>(*p));
        const auto values = _mm_and_si128(
            _mm_cmpeq_epi8(_mm_and_si128(source, mask), mask),
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(active.data())));
        return sp::avx2::native_detail::expand_bytes<L>(values);
    }
}

// Decode once in byte lanes, then reuse native unsigned widening to write an
// exact 32-value region. The carrier changes output extent, not bit work grain.
template<std::unsigned_integral UInt>
[[gnu::always_inline]] inline void store_region32(__m256i values, UInt* out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    if constexpr (L == 1) _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), values);
    else sp::detail::static_for<L>([&](auto part) {
        constexpr unsigned offset = part * (32 / L);
        auto bytes = [&] [[gnu::always_inline]] {
            if constexpr (offset < 16) return _mm256_castsi256_si128(values);
            else return _mm256_extracti128_si256(values, 1);
        }();
        if constexpr (offset % 16 != 0) bytes = _mm_srli_si128(bytes, offset % 16);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + offset),
            sp::avx2::native_detail::expand_bytes<L>(bytes));
    });
}

template<std::unsigned_integral UInt>
[[gnu::always_inline]] inline void decode_region32(const std::uint8_t* p, UInt* out) noexcept {
    store_region32(read_region32(p), out);
}

template<std::unsigned_integral UInt>
[[gnu::always_inline]] inline void decode_tile(const std::uint8_t* p, UInt* out) noexcept {
    constexpr unsigned L = sizeof(UInt), N = std::min(8u, 32u / L);
    sp::detail::static_for<8 / N>([&](auto part) {
        sp::detail::avx2::encode_body_prefix<L, L, N>(
            reinterpret_cast<std::uint8_t*>(out + part * N), read_fragment<L, part * N, N>(p));
    });
}

template<std::unsigned_integral UInt, unsigned Regions = 1>
inline void decode_tiles(const std::uint8_t* __restrict p, UInt* __restrict out,
                         std::size_t tiles) noexcept {
#pragma clang loop unroll(disable)
    for (; tiles >= 4 * Regions; tiles -= 4 * Regions, p += 4 * Regions, out += 32 * Regions)
        sp::detail::static_for<Regions>([&](auto part) { decode_region32(p + part * 4, out + part * 32); });
    for (; tiles; --tiles, ++p, out += 8) decode_tile(p, out);
}
} // namespace local1_experiment
