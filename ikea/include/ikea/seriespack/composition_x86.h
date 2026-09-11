#pragma once

#include <ikea/seriespack/composition.h>
#if defined(__AVX2__)
#include <ikea/seriespack/native_avx2.h>
#include <ikea/seriespack/native_avx512.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(__AVX2__)
namespace ikea::seriespack::avx2 {

// Private native value operations, independent of source geometry, work grain,
// coordinates and active-mask admission. K is the unsigned value domain; L is
// the selected lane width in bytes. Instruction bodies remain ISA-specific.
namespace composition_detail {
template<unsigned K, unsigned L>
struct consumer_ops {
    static_assert(K >= 1 && K <= 64 && K <= 8 * L);
    static_assert(L == 1 || L == 2 || L == 4 || L == 8);
    using value_type = __m256i;
    using mask_type = __m256i;

    [[gnu::always_inline]] mask_type unsigned_less(value_type values, std::uint64_t cutoff,
                                                  mask_type active) const {
        if constexpr (K < 64)
            if (cutoff >= (std::uint64_t{1} << K)) return active;
        value_type predicate;
        if constexpr (L == 1) {
            const auto sign = _mm256_set1_epi8(-128);
            predicate = _mm256_cmpgt_epi8(
                _mm256_xor_si256(_mm256_set1_epi8(static_cast<char>(cutoff)), sign),
                _mm256_xor_si256(values, sign));
        } else if constexpr (L == 2) {
            const auto sign = _mm256_set1_epi16(-32768);
            predicate = _mm256_cmpgt_epi16(
                _mm256_xor_si256(_mm256_set1_epi16(static_cast<short>(cutoff)), sign),
                _mm256_xor_si256(values, sign));
        } else if constexpr (L == 4) {
            const auto sign = _mm256_set1_epi32(static_cast<int>(0x80000000u));
            predicate = _mm256_cmpgt_epi32(
                _mm256_xor_si256(_mm256_set1_epi32(static_cast<int>(cutoff)), sign),
                _mm256_xor_si256(values, sign));
        } else {
            const auto sign = _mm256_set1_epi64x(static_cast<long long>(std::uint64_t{1} << 63));
            predicate = _mm256_cmpgt_epi64(
                _mm256_xor_si256(_mm256_set1_epi64x(static_cast<long long>(cutoff)), sign),
                _mm256_xor_si256(values, sign));
        }
        auto result = _mm256_and_si256(predicate, active);
        // Preserve native lanes across the domain branch. Clang 21 otherwise
        // packs and re-expands its known boolean result before the consumer.
        // Constraining the result keeps known-all input-mask simplifications.
        asm("" : "+x"(result));
        return result;
    }

    [[gnu::always_inline]] mask_type unsigned_equal(value_type values, std::uint64_t constant,
                                                   mask_type active) const {
        if constexpr (K < 64)
            if (constant >= (std::uint64_t{1} << K)) return _mm256_setzero_si256();
        value_type predicate;
        if constexpr (L == 1) predicate = _mm256_cmpeq_epi8(values, _mm256_set1_epi8(static_cast<char>(constant)));
        if constexpr (L == 2) predicate = _mm256_cmpeq_epi16(values, _mm256_set1_epi16(static_cast<short>(constant)));
        if constexpr (L == 4) predicate = _mm256_cmpeq_epi32(values, _mm256_set1_epi32(static_cast<int>(constant)));
        if constexpr (L == 8) predicate = _mm256_cmpeq_epi64(values, _mm256_set1_epi64x(static_cast<long long>(constant)));
        auto result = _mm256_and_si256(predicate, active);
        asm("" : "+x"(result));
        return result;
    }

    [[gnu::always_inline]] static inline std::uint64_t sum_u64(value_type values) {
        const auto halves = _mm_add_epi64(_mm256_castsi256_si128(values),
                                          _mm256_extracti128_si256(values, 1));
        const auto total = _mm_add_epi64(halves, _mm_srli_si128(halves, 8));
        return static_cast<std::uint64_t>(_mm_cvtsi128_si64(total));
    }

    [[gnu::always_inline]] std::uint64_t sum(value_type values, mask_type selected,
                                            modulo_u64_sum) const {
        const auto kept = _mm256_and_si256(values, selected);
        if constexpr (L == 1) return sum_u64(_mm256_sad_epu8(kept, _mm256_setzero_si256()));
        else if constexpr (L == 2) {
            // Zero-extend both halfwords of each dword before adding. The total
            // is at most 16*65535, so this intermediate cannot overflow u32.
            const auto pairs = _mm256_add_epi32(_mm256_and_si256(kept, _mm256_set1_epi32(0xffff)),
                                                _mm256_srli_epi32(kept, 16));
            auto total = _mm_add_epi32(_mm256_castsi256_si128(pairs), _mm256_extracti128_si256(pairs, 1));
            total = _mm_hadd_epi32(total, total);
            total = _mm_hadd_epi32(total, total);
            return static_cast<std::uint32_t>(_mm_cvtsi128_si32(total));
        } else if constexpr (L == 4) {
            const auto pairs = _mm256_add_epi64(_mm256_and_si256(kept, _mm256_set1_epi64x(0xffffffffULL)),
                                                _mm256_srli_epi64(kept, 32));
            return sum_u64(pairs);
        } else return sum_u64(kept);
    }
};
} // namespace composition_detail

/// Physical tile ordinal; Begin supplies the fragment offset within that tile.
struct tile_position { std::size_t tile; };

/// Immediate AVX2 fragment with L-byte unsigned lanes and all-ones/zero masks.
/// Admit each actual child's tile independently; masks do not suppress reads.
/// Only selected logical rows are valid: active_all requires a complete fragment.
template<class Format, unsigned Begin>
struct composition_ops {
    static constexpr auto layout = Format::layout;
    static constexpr unsigned W = payload_width(layout);
    static constexpr unsigned L = sizeof(typename Format::scalar_type);
    static constexpr auto G = layout.storage;
    using traits = fragment_traits<W, G, L>;
    static constexpr unsigned lanes = traits::lanes;
    static constexpr std::size_t tile_values = Format::payload::tile_values;
    static_assert(Begin % lanes == 0 && Begin + lanes <= tile_values);
    using value_type = __m256i;
    using mask_type = __m256i;
    static constexpr std::uint64_t lane_bits = (std::uint64_t{1} << lanes) - 1;

    static constexpr std::size_t original_index(tile_position rows, unsigned lane) {
        return rows.tile * tile_values + Begin + lane;
    }

    [[gnu::always_inline]] static inline mask_type active_all() {
        if constexpr (lanes * L == 32) return _mm256_set1_epi8(-1);
        else if constexpr (lanes * L == 16) return _mm256_zextsi128_si256(_mm_set1_epi8(-1));
        else return _mm256_zextsi128_si256(_mm_cvtsi64_si128(-1));
    }

    /// Convert low positional bits to native lanes; performs no admission checks.
    [[gnu::always_inline]] static inline mask_type active_bits(std::uint64_t bits) {
        bits &= lane_bits;
        if constexpr (L == 1) {
            static constexpr auto indices = [] {
                std::array<std::uint8_t, 32> result{};
                for (unsigned i = 0; i != 32; ++i) result[i] = i / 8;
                return result;
            }();
            const auto weights = _mm256_set1_epi64x(0x8040201008040201ULL);
            const auto bytes = _mm256_shuffle_epi8(_mm256_set1_epi32(static_cast<int>(bits)),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(indices.data())));
            return _mm256_cmpeq_epi8(_mm256_and_si256(bytes, weights), weights);
        } else if constexpr (L == 2) {
            const auto weights = _mm256_setr_epi16(1,2,4,8,16,32,64,128,
                                                 256,512,1024,2048,4096,8192,16384,-32768);
            return _mm256_cmpeq_epi16(_mm256_and_si256(
                _mm256_set1_epi16(static_cast<short>(bits)), weights), weights);
        } else if constexpr (L == 4) {
            const auto weights = _mm256_setr_epi32(1,2,4,8,16,32,64,128);
            return _mm256_cmpeq_epi32(_mm256_and_si256(
                _mm256_set1_epi32(static_cast<int>(bits)), weights), weights);
        } else {
            const auto weights = _mm256_setr_epi64x(1,2,4,8);
            return _mm256_cmpeq_epi64(_mm256_and_si256(
                _mm256_set1_epi64x(static_cast<long long>(bits)), weights), weights);
        }
    }

    template<class Ref>
    [[gnu::always_inline]] static inline const std::uint8_t* payload(
        const Ref& ref, tile_position rows) {
        static_assert(Ref::format_type::layout == layout);
        if constexpr (W == 0) return nullptr;
        const auto& p = ref.source.placement().payload;
        return reinterpret_cast<const std::uint8_t*>(p.bytes.data() + rows.tile * p.stride);
    }

    template<class F, class Source>
    [[gnu::always_inline]] value_type read_body(const composition::body_ref<F, Source>& ref,
                                               tile_position rows, mask_type) const {
        return avx2::read_body<W, G, L, Begin>(payload(ref, rows));
    }

    template<class F, class Source>
    [[gnu::always_inline]] value_type read_tail(const composition::tail_ref<F, Source>& ref,
                                               tile_position rows, mask_type) const {
        return avx2::read_tail<W, G, L, Begin>(payload(ref, rows));
    }

    template<class F, unsigned Plane, class Source>
    [[gnu::always_inline]] value_type read_head(const composition::head_ref<F, Plane, Source>& ref,
                                               tile_position rows, mask_type) const {
        static_assert(F::layout == layout && Plane < layout.head_bits / 8);
        const auto& p = ref.source.placement().heads[Plane];
        return detail::avx2::decode_body_prefix<1, L, lanes>(
            reinterpret_cast<const std::uint8_t*>(p.bytes.data() + rows.tile * p.stride + Begin));
    }

    template<unsigned LowBits>
    [[gnu::always_inline]] value_type join(value_type high, value_type low) const {
        static_assert(LowBits < 8 * L);
        return _mm256_or_si256(native_detail::shift<L, LowBits>(high), low);
    }

private:
    using consumer = composition_detail::consumer_ops<layout.width, L>;
public:
    [[gnu::always_inline]] mask_type unsigned_less(value_type values, std::uint64_t cutoff,
                                                  mask_type active) const {
        return consumer{}.unsigned_less(values, cutoff, active);
    }
    [[gnu::always_inline]] mask_type unsigned_equal(value_type values, std::uint64_t constant,
                                                   mask_type active) const {
        return consumer{}.unsigned_equal(values, constant, active);
    }
    [[gnu::always_inline]] std::uint64_t sum(value_type values, mask_type selected,
                                            modulo_u64_sum law) const {
        return consumer{}.sum(values, selected, law);
    }
    [[gnu::always_inline]] static inline std::uint64_t sum_u64(value_type values) {
        return consumer::sum_u64(values);
    }

};

/// Original row at an eight-value packet boundary.
struct dense_position { std::size_t origin; };

/// Headless K=1..7: 32 values in 4 contiguous local packets, exactly 4*K bytes.
/// Admit every actual child for this complete logical region; masks do not reduce reads.
template<class Format>
struct dense_local_ops {
    static constexpr auto layout = Format::layout;
    static constexpr unsigned W = layout.width, L = 1;
    static constexpr auto G = layout.storage;
    static_assert(layout.head_bits == 0 && G == geometry::local8 && W >= 1 && W <= 7);
    static constexpr unsigned lanes = 32;
    static constexpr std::size_t encoded_bytes = lanes * W / 8;
    static constexpr std::uint64_t lane_bits = 0xffffffffULL;
    using format_type = Format;
    using position_type = dense_position;
    using value_type = __m256i;
    using mask_type = __m256i;

    /// Check range, packet start, complete groups and density on this attached source.
    /// Empty ranges bypass start/grain/stride checks; validate each child separately.
    [[nodiscard]] static constexpr std::expected<void, error> validate_region(
        const static_const_view<Format>& source, index_range rows) {
        if (auto valid = validate_range(rows, source.size()); !valid) return valid;
        if (rows.empty()) return {};
        if (rows.begin % 8 != 0 || rows.size() % lanes != 0)
            return std::unexpected(error::invalid_range);
        if (source.placement().payload.stride != Format::payload::tile_bytes)
            return std::unexpected(error::invalid_stride);
        return {};
    }
    static constexpr std::size_t original_index(dense_position rows, unsigned lane) {
        return rows.origin + lane;
    }
    [[gnu::always_inline]] static inline mask_type active_all() { return _mm256_set1_epi8(-1); }
    [[gnu::always_inline]] static inline mask_type active_bits(std::uint64_t bits) {
        bits &= lane_bits;
        static constexpr auto indices = [] {
            std::array<std::uint8_t, 32> result{};
            for (unsigned i = 0; i != 32; ++i) result[i] = i / 8;
            return result;
        }();
        const auto weights = _mm256_set1_epi64x(0x8040201008040201ULL);
        const auto bytes = _mm256_shuffle_epi8(_mm256_set1_epi32(static_cast<int>(bits)),
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(indices.data())));
        return _mm256_cmpeq_epi8(_mm256_and_si256(bytes, weights), weights);
    }
    template<class F, class Source>
    [[gnu::always_inline]] value_type read_tail(const composition::tail_ref<F, Source>& ref,
                                               dense_position rows, mask_type) const {
        static_assert(F::layout == layout);
        const auto* p = reinterpret_cast<const std::uint8_t*>(ref.source.placement().payload.bytes.data());
        return avx2::read_local_region32<W>(p + rows.origin / 8 * W);
    }
private:
    using consumer = composition_detail::consumer_ops<layout.width, L>;
public:
    [[gnu::always_inline]] mask_type unsigned_less(value_type values, std::uint64_t cutoff,
                                                  mask_type active) const {
        return consumer{}.unsigned_less(values, cutoff, active);
    }
    [[gnu::always_inline]] mask_type unsigned_equal(value_type values, std::uint64_t constant,
                                                   mask_type active) const {
        return consumer{}.unsigned_equal(values, constant, active);
    }
    [[gnu::always_inline]] std::uint64_t sum(value_type values, mask_type selected,
                                            modulo_u64_sum law) const {
        return consumer{}.sum(values, selected, law);
    }

};

} // namespace ikea::seriespack::avx2
#endif

#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
namespace ikea::seriespack::avx512 {

// Private native value operations, independent of source geometry, work grain,
// coordinates and active-mask admission. K is the unsigned value domain; L is
// the selected lane width in bytes. Instruction bodies remain ISA-specific.
namespace composition_detail {
template<unsigned K, unsigned L>
struct consumer_ops {
    static_assert(K >= 1 && K <= 64 && K <= 8 * L);
    static_assert(L == 1 || L == 2 || L == 4 || L == 8);
    using value_type = __m512i;
    using mask_type = std::conditional_t<L == 1, __mmask64, std::conditional_t<L == 2, __mmask32,
                      std::conditional_t<L == 4, __mmask16, __mmask8>>>;

    [[gnu::always_inline]] mask_type unsigned_less(value_type values, std::uint64_t cutoff,
                                                  mask_type active) const {
        // Preserve the native k handoff. With known narrow active bits Clang
        // 21 otherwise extracts a comparison result to a GPR for its AND.
        // Statically unconditional reads retain their mask simplifications.
        if constexpr (K < 64)
            if (__builtin_constant_p(cutoff) && cutoff >= (std::uint64_t{1} << K))
                return active;
        if (!(__builtin_constant_p(active) && active == static_cast<mask_type>(~std::uint64_t{0})))
            asm("" : "+k"(active));
        if constexpr (K < 64)
            if (cutoff >= (std::uint64_t{1} << K)) return active;
        mask_type result;
        if constexpr (L == 1) result = _mm512_mask_cmplt_epu8_mask(active, values, _mm512_set1_epi8(static_cast<char>(cutoff)));
        if constexpr (L == 2) result = _mm512_mask_cmplt_epu16_mask(active, values, _mm512_set1_epi16(static_cast<short>(cutoff)));
        if constexpr (L == 4) result = _mm512_mask_cmplt_epu32_mask(active, values, _mm512_set1_epi32(static_cast<int>(cutoff)));
        if constexpr (L == 8) result = _mm512_mask_cmplt_epu64_mask(active, values, _mm512_set1_epi64(static_cast<long long>(cutoff)));
        asm("" : "+k"(result));
        return result;
    }

    [[gnu::always_inline]] mask_type unsigned_equal(value_type values, std::uint64_t constant,
                                                   mask_type active) const {
        if (!(__builtin_constant_p(active) && active == static_cast<mask_type>(~std::uint64_t{0})))
            asm("" : "+k"(active));
        if constexpr (K < 64)
            if (constant >= (std::uint64_t{1} << K)) return 0;
        mask_type result;
        if constexpr (L == 1) result = _mm512_mask_cmpeq_epu8_mask(active, values, _mm512_set1_epi8(static_cast<char>(constant)));
        if constexpr (L == 2) result = _mm512_mask_cmpeq_epu16_mask(active, values, _mm512_set1_epi16(static_cast<short>(constant)));
        if constexpr (L == 4) result = _mm512_mask_cmpeq_epu32_mask(active, values, _mm512_set1_epi32(static_cast<int>(constant)));
        if constexpr (L == 8) result = _mm512_mask_cmpeq_epu64_mask(active, values, _mm512_set1_epi64(static_cast<long long>(constant)));
        asm("" : "+k"(result));
        return result;
    }

    [[gnu::always_inline]] std::uint64_t sum(value_type values, mask_type selected,
                                            modulo_u64_sum) const {
        value_type kept;
        if constexpr (L == 1) kept = _mm512_maskz_mov_epi8(selected, values);
        if constexpr (L == 2) kept = _mm512_maskz_mov_epi16(selected, values);
        if constexpr (L == 4) kept = _mm512_maskz_mov_epi32(selected, values);
        if constexpr (L == 8) kept = _mm512_maskz_mov_epi64(selected, values);
        if constexpr (L == 1) return static_cast<std::uint64_t>(
            _mm512_reduce_add_epi64(_mm512_sad_epu8(kept, _mm512_setzero_si512())));
        else if constexpr (L == 2) {
            const auto pairs = _mm512_add_epi32(_mm512_and_si512(kept, _mm512_set1_epi32(0xffff)),
                                                _mm512_srli_epi32(kept, 16));
            return static_cast<std::uint32_t>(_mm512_reduce_add_epi32(pairs));
        } else if constexpr (L == 4) {
            const auto pairs = _mm512_add_epi64(_mm512_and_si512(kept, _mm512_set1_epi64(0xffffffffULL)),
                                                _mm512_srli_epi64(kept, 32));
            return static_cast<std::uint64_t>(_mm512_reduce_add_epi64(pairs));
        } else return static_cast<std::uint64_t>(_mm512_reduce_add_epi64(kept));
    }
};
} // namespace composition_detail

/// Physical tile ordinal; Begin supplies the fragment offset within that tile.
struct tile_position { std::size_t tile; };

/// Immediate AVX-512 fragment with L-byte unsigned lanes and native mask bits.
/// Admit each actual child's tile independently; masks do not suppress reads.
/// Only selected logical rows are valid: active_all requires a complete fragment.
template<class Format, unsigned Begin>
struct composition_ops {
    static constexpr auto layout = Format::layout;
    static constexpr unsigned W = payload_width(layout);
    static constexpr unsigned L = sizeof(typename Format::scalar_type);
    static constexpr auto G = layout.storage;
    using traits = fragment_traits<W, G, L>;
    static constexpr unsigned lanes = traits::lanes;
    static constexpr std::size_t tile_values = Format::payload::tile_values;
    static_assert(Begin % lanes == 0 && Begin + lanes <= tile_values);
    using value_type = __m512i;
    using mask_type = std::conditional_t<L == 1, __mmask64, std::conditional_t<L == 2, __mmask32,
                      std::conditional_t<L == 4, __mmask16, __mmask8>>>;
    static constexpr std::uint64_t lane_bits = [] {
        if constexpr (lanes == 64) return ~std::uint64_t{0};
        else return (std::uint64_t{1} << lanes) - 1;
    }();

    static constexpr std::size_t original_index(tile_position rows, unsigned lane) {
        return rows.tile * tile_values + Begin + lane;
    }
    [[gnu::always_inline]] static inline mask_type active_all() { return static_cast<mask_type>(lane_bits); }
    [[gnu::always_inline]] static inline mask_type active_bits(std::uint64_t bits) {
        return static_cast<mask_type>(bits & lane_bits);
    }

    template<class Ref>
    [[gnu::always_inline]] static inline const std::uint8_t* payload(
        const Ref& ref, tile_position rows) {
        static_assert(Ref::format_type::layout == layout);
        if constexpr (W == 0) return nullptr;
        const auto& p = ref.source.placement().payload;
        return reinterpret_cast<const std::uint8_t*>(p.bytes.data() + rows.tile * p.stride);
    }

    template<class F, class Source>
    [[gnu::always_inline]] value_type read_body(const composition::body_ref<F, Source>& ref,
                                               tile_position rows, mask_type) const {
        return avx512::read_body<W, G, L, Begin>(payload(ref, rows));
    }

    template<class F, class Source>
    [[gnu::always_inline]] value_type read_tail(const composition::tail_ref<F, Source>& ref,
                                               tile_position rows, mask_type) const {
        return avx512::read_tail<W, G, L, Begin>(payload(ref, rows));
    }

    template<class F, unsigned Plane, class Source>
    [[gnu::always_inline]] value_type read_head(const composition::head_ref<F, Plane, Source>& ref,
                                               tile_position rows, mask_type) const {
        static_assert(F::layout == layout && Plane < layout.head_bits / 8);
        const auto& p = ref.source.placement().heads[Plane];
        return detail::avx512::decode_body_prefix<1, L, lanes>(
            reinterpret_cast<const std::uint8_t*>(p.bytes.data() + rows.tile * p.stride + Begin));
    }

    template<unsigned LowBits>
    [[gnu::always_inline]] value_type join(value_type high, value_type low) const {
        static_assert(LowBits < 8 * L);
        return _mm512_or_si512(native_detail::shift<L, LowBits>(high), low);
    }

private:
    using consumer = composition_detail::consumer_ops<layout.width, L>;
public:
    [[gnu::always_inline]] mask_type unsigned_less(value_type values, std::uint64_t cutoff,
                                                  mask_type active) const {
        return consumer{}.unsigned_less(values, cutoff, active);
    }
    [[gnu::always_inline]] mask_type unsigned_equal(value_type values, std::uint64_t constant,
                                                   mask_type active) const {
        return consumer{}.unsigned_equal(values, constant, active);
    }
    [[gnu::always_inline]] std::uint64_t sum(value_type values, mask_type selected,
                                            modulo_u64_sum law) const {
        return consumer{}.sum(values, selected, law);
    }

};

/// Original row at an eight-value packet boundary.
struct dense_position { std::size_t origin; };

/// Headless K=1..7: 64 values in 8 contiguous local packets, exactly 8*K bytes.
/// Admit every actual child for this complete logical region; masks do not reduce reads.
template<class Format>
struct dense_local_ops {
    static constexpr auto layout = Format::layout;
    static constexpr unsigned W = layout.width, L = 1;
    static constexpr auto G = layout.storage;
    static_assert(layout.head_bits == 0 && G == geometry::local8 && W >= 1 && W <= 7);
    static constexpr unsigned lanes = 64;
    static constexpr std::size_t encoded_bytes = lanes * W / 8;
    static constexpr std::uint64_t lane_bits = ~std::uint64_t{0};
    using format_type = Format;
    using position_type = dense_position;
    using value_type = __m512i;
    using mask_type = __mmask64;

    /// Check range, packet start, complete groups and density on this attached source.
    /// Empty ranges bypass start/grain/stride checks; validate each child separately.
    [[nodiscard]] static constexpr std::expected<void, error> validate_region(
        const static_const_view<Format>& source, index_range rows) {
        if (auto valid = validate_range(rows, source.size()); !valid) return valid;
        if (rows.empty()) return {};
        if (rows.begin % 8 != 0 || rows.size() % lanes != 0)
            return std::unexpected(error::invalid_range);
        if (source.placement().payload.stride != Format::payload::tile_bytes)
            return std::unexpected(error::invalid_stride);
        return {};
    }
    static constexpr std::size_t original_index(dense_position rows, unsigned lane) {
        return rows.origin + lane;
    }
    [[gnu::always_inline]] static inline mask_type active_all() { return ~std::uint64_t{0}; }
    [[gnu::always_inline]] static inline mask_type active_bits(std::uint64_t bits) {
        return static_cast<mask_type>(bits);
    }
    template<class F, class Source>
    [[gnu::always_inline]] value_type read_tail(const composition::tail_ref<F, Source>& ref,
                                               dense_position rows, mask_type) const {
        static_assert(F::layout == layout);
        const auto* p = reinterpret_cast<const std::uint8_t*>(ref.source.placement().payload.bytes.data());
        return avx512::read_local_region64<W>(p + rows.origin / 8 * W);
    }
private:
    using consumer = composition_detail::consumer_ops<layout.width, L>;
public:
    [[gnu::always_inline]] mask_type unsigned_less(value_type values, std::uint64_t cutoff,
                                                  mask_type active) const {
        return consumer{}.unsigned_less(values, cutoff, active);
    }
    [[gnu::always_inline]] mask_type unsigned_equal(value_type values, std::uint64_t constant,
                                                   mask_type active) const {
        return consumer{}.unsigned_equal(values, constant, active);
    }
    [[gnu::always_inline]] std::uint64_t sum(value_type values, mask_type selected,
                                            modulo_u64_sum law) const {
        return consumer{}.sum(values, selected, law);
    }

};

/// Original row at an eight-value packet boundary.
struct grouped_position { std::size_t origin; };

/// Headless local K=9..16: 32 u16 lanes; K=17..32: 16 u32 lanes.
/// Reads each actual child's packets with its own stride, excluding gaps.
/// Admit complete logical groups; masks do not reduce the physical read extent.
template<class Format>
struct grouped_local_ops {
    static constexpr auto layout = Format::layout;
    static constexpr unsigned K = layout.width;
    static_assert(layout.storage == geometry::local8 && layout.head_bits == 0);
    static_assert(K >= 9 && K <= 32);
    static constexpr unsigned L = sizeof(typename Format::scalar_type);
    static constexpr unsigned packets = 8 / L;
    static constexpr unsigned lanes = 8 * packets;
    static constexpr std::uint64_t lane_bits = (std::uint64_t{1} << lanes) - 1;
    static constexpr std::size_t encoded_bytes = packets * K;
    using format_type = Format;
    using position_type = grouped_position;
    using value_type = __m512i;
    using mask_type = std::conditional_t<L == 2, __mmask32, __mmask16>;

    /// Check range, packet start and complete groups on this attached source.
    /// Empty ranges bypass start/grain checks; validate each child separately.
    [[nodiscard]] static constexpr std::expected<void, error> validate_region(
        const static_const_view<Format>& source, index_range rows) {
        if (auto check = validate_range(rows, source.size()); !check) return check;
        if (rows.empty()) return {};
        if (rows.begin % 8 != 0 || rows.size() % lanes != 0)
            return std::unexpected(error::invalid_range);
        return {};
    }
    static constexpr std::size_t original_index(grouped_position rows, unsigned lane) {
        return rows.origin + lane;
    }
    [[gnu::always_inline]] static inline mask_type active_all() { return lane_bits; }
    [[gnu::always_inline]] static inline mask_type active_bits(std::uint64_t bits) {
        return static_cast<mask_type>(bits & lane_bits);
    }

private:
    template<bool Body, class Ref>
    [[gnu::always_inline]] static inline value_type read_part(const Ref& ref, grouped_position rows) {
        static_assert(Ref::format_type::layout == layout);
        const auto& p = ref.source.placement().payload;
        const auto first = rows.origin / 8;
        const auto packet = [&]<unsigned Part>() {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(
                p.bytes.data() + (first + Part) * p.stride);
            if constexpr (Body) return avx2::read_body<K, geometry::local8, L, 0>(bytes);
            else return avx2::read_tail<K, geometry::local8, L, 0>(bytes);
        };
        // Eight u16 values occupy the low 128 bits of each packet result;
        // eight u32 values occupy its full YMM. The physical readers are shared
        // with tile executors; concatenation changes only the execution grain.
        if constexpr (L == 2) {
            const auto p0 = _mm256_castsi256_si128(packet.template operator()<0>());
            const auto p1 = _mm256_castsi256_si128(packet.template operator()<1>());
            const auto p2 = _mm256_castsi256_si128(packet.template operator()<2>());
            const auto p3 = _mm256_castsi256_si128(packet.template operator()<3>());
            const auto low = _mm256_inserti128_si256(_mm256_castsi128_si256(p0), p1, 1);
            const auto high = _mm256_inserti128_si256(_mm256_castsi128_si256(p2), p3, 1);
            return _mm512_inserti64x4(_mm512_castsi256_si512(low), high, 1);
        } else {
            const auto low = packet.template operator()<0>();
            const auto high = packet.template operator()<1>();
            return _mm512_inserti64x4(_mm512_castsi256_si512(low), high, 1);
        }
    }
    using consumer = avx512::composition_detail::consumer_ops<K, L>;

public:
    template<class F, class Source>
    [[gnu::always_inline]] value_type read_body(const composition::body_ref<F, Source>& ref,
                                               grouped_position rows, mask_type) const {
        return read_part<true>(ref, rows);
    }
    template<class F, class Source>
    [[gnu::always_inline]] value_type read_tail(const composition::tail_ref<F, Source>& ref,
                                               grouped_position rows, mask_type) const {
        return read_part<false>(ref, rows);
    }
    template<unsigned LowBits>
    [[gnu::always_inline]] value_type join(value_type high, value_type low) const {
        return avx512::join<L, LowBits>(high, low);
    }
    [[gnu::always_inline]] mask_type unsigned_less(value_type values, std::uint64_t cutoff, mask_type active) const {
        return consumer{}.unsigned_less(values, cutoff, active);
    }
    [[gnu::always_inline]] mask_type unsigned_equal(value_type values, std::uint64_t constant, mask_type active) const {
        return consumer{}.unsigned_equal(values, constant, active);
    }
    [[gnu::always_inline]] std::uint64_t sum(value_type values, mask_type selected, modulo_u64_sum law) const {
        return consumer{}.sum(values, selected, law);
    }
};


/// One modulo-2^64 sum held as eight owned u64 partials, not row-mapped values.
/// finish() reduces to a scalar without consuming or resetting the partials.
struct deferred_u64_sum {
    __m512i partials;

    [[gnu::always_inline]] static inline deferred_u64_sum zero() {
        return {_mm512_setzero_si512()};
    }
    [[gnu::always_inline]] inline deferred_u64_sum plus(deferred_u64_sum rhs) const {
        return {_mm512_add_epi64(partials, rhs.partials)};
    }
    [[gnu::always_inline]] inline std::uint64_t finish() const {
        return static_cast<std::uint64_t>(_mm512_reduce_add_epi64(partials));
    }
};

/// Changes only Base's sum result to deferred_u64_sum; retains its other contracts.
/// Initialize a stateful Base with {base}; the caller chooses when to finish().
template<class Base>
struct deferred_sum_ops : Base {
    using typename Base::value_type;
    using typename Base::mask_type;
    static_assert(Base::L == 1 || Base::L == 2 || Base::L == 4 || Base::L == 8);
    static_assert(std::is_same_v<value_type, __m512i>);
    static_assert(std::is_same_v<mask_type,
        std::conditional_t<Base::L == 1, __mmask64,
        std::conditional_t<Base::L == 2, __mmask32,
        std::conditional_t<Base::L == 4, __mmask16, __mmask8>>>>);

    [[gnu::always_inline]] deferred_u64_sum sum(value_type values, mask_type selected,
                                                modulo_u64_sum) const {
        value_type kept;
        if constexpr (Base::L == 1) kept = _mm512_maskz_mov_epi8(selected, values);
        if constexpr (Base::L == 2) kept = _mm512_maskz_mov_epi16(selected, values);
        if constexpr (Base::L == 4) kept = _mm512_maskz_mov_epi32(selected, values);
        if constexpr (Base::L == 8) kept = _mm512_maskz_mov_epi64(selected, values);
        if constexpr (Base::L == 1)
            return {_mm512_sad_epu8(kept, _mm512_setzero_si512())};
        else if constexpr (Base::L == 2) {
            const auto pairs = _mm512_add_epi32(
                _mm512_and_si512(kept, _mm512_set1_epi32(0xffff)),
                _mm512_srli_epi32(kept, 16));
            // One fragment fits u32, but repeated composition may not. Move
            // its partials to u64 lanes before any inter-fragment addition.
            return {_mm512_add_epi64(
                _mm512_and_si512(pairs, _mm512_set1_epi64(0xffffffffULL)),
                _mm512_srli_epi64(pairs, 32))};
        } else if constexpr (Base::L == 4)
            return {_mm512_add_epi64(
                _mm512_and_si512(kept, _mm512_set1_epi64(0xffffffffULL)),
                _mm512_srli_epi64(kept, 32))};
        else return {kept};
    }
};

} // namespace ikea::seriespack::avx512
#endif
