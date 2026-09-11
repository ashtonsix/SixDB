#pragma once

#include <ikea_predecessor/seriespack/detail/body_avx2.h>
#include <ikea_predecessor/seriespack/detail/physical.h>

#if defined(__AVX2__)
namespace ikea_predecessor::seriespack::avx2 {

/// Native payload shape: W is stored payload bits; lane width is in bytes.
/// Only the low `lanes` positions belong to the fragment; other register lanes
/// carry no logical-value promise, even when their bits are defined as zero.
template<unsigned W, geometry G, unsigned L = sizeof(scalar_for_width<W>)>
struct fragment_traits {
    static_assert(L == 1 || L == 2 || L == 4 || L == 8);
    static_assert(W <= 8 * L);
    using vector_type = __m256i;
    using lane_type = scalar_for_width<8 * L>;
    static constexpr unsigned register_lanes = 32 / L;
    static constexpr unsigned lanes = G == geometry::local8 && register_lanes > 8
        ? 8 : register_lanes;
    static constexpr unsigned tile_values = payload_layout<W, G>::tile_values;
};

namespace native_detail {

template<unsigned L, int Shift>
[[gnu::always_inline]] inline __m256i shift(__m256i v) noexcept {
    if constexpr (Shift == 0) return v;
    else if constexpr (L <= 2) {
        // Byte field users supply the final byte mask, which also removes
        // cross-byte contamination from the native word shift.
        if constexpr (Shift > 0) return _mm256_slli_epi16(v, Shift);
        else return _mm256_srli_epi16(v, -Shift);
    } else if constexpr (L == 4) {
        if constexpr (Shift > 0) return _mm256_slli_epi32(v, Shift);
        else return _mm256_srli_epi32(v, -Shift);
    } else {
        if constexpr (Shift > 0) return _mm256_slli_epi64(v, Shift);
        else return _mm256_srli_epi64(v, -Shift);
    }
}

template<unsigned L, std::uint64_t Mask>
[[gnu::always_inline]] inline __m256i mask() noexcept {
    if constexpr (L == 1) return _mm256_set1_epi8(static_cast<char>(Mask));
    else if constexpr (L == 2) return _mm256_set1_epi16(static_cast<short>(Mask));
    else if constexpr (L == 4) return _mm256_set1_epi32(static_cast<int>(Mask));
    else return _mm256_set1_epi64x(static_cast<long long>(Mask));
}

[[gnu::always_inline]] inline __m128i transpose(__m128i x) noexcept {
#if defined(__GFNI__)
    const auto reverse = _mm_setr_epi8(7,6,5,4,3,2,1,0,15,14,13,12,11,10,9,8);
    return _mm_gf2p8affine_epi64_epi8(_mm_set1_epi64x(0x8040201008040201ULL),
                                    _mm_shuffle_epi8(x, reverse), 0);
#else
    auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
        const auto moved = _mm_and_si128(_mm_xor_si128(x, _mm_srli_epi64(x, Shift)),
                                         _mm_set1_epi64x(Mask));
        x = _mm_xor_si128(x, _mm_xor_si128(moved, _mm_slli_epi64(moved, Shift)));
    };
    exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14, 0x0000cccc0000ccccULL>();
    exchange.template operator()<28, 0x00000000f0f0f0f0ULL>();
    return x;
#endif
}

[[gnu::always_inline]] inline __m256i transpose(__m256i x) noexcept {
#if defined(__GFNI__)
    const auto reverse = _mm256_setr_epi8(
        7,6,5,4,3,2,1,0,15,14,13,12,11,10,9,8,
        7,6,5,4,3,2,1,0,15,14,13,12,11,10,9,8);
    return _mm256_gf2p8affine_epi64_epi8(_mm256_set1_epi64x(0x8040201008040201ULL),
                                       _mm256_shuffle_epi8(x, reverse), 0);
#else
    auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
        const auto moved = _mm256_and_si256(_mm256_xor_si256(x, _mm256_srli_epi64(x, Shift)),
                                           _mm256_set1_epi64x(Mask));
        x = _mm256_xor_si256(x, _mm256_xor_si256(moved, _mm256_slli_epi64(moved, Shift)));
    };
    exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14, 0x0000cccc0000ccccULL>();
    exchange.template operator()<28, 0x00000000f0f0f0f0ULL>();
    return x;
#endif
}

template<unsigned L>
[[gnu::always_inline]] inline __m256i expand_bytes(__m128i bytes) noexcept {
    if constexpr (L == 1) return _mm256_zextsi128_si256(bytes);
    else if constexpr (L == 2) return _mm256_cvtepu8_epi16(bytes);
    else if constexpr (L == 4) return _mm256_cvtepu8_epi32(bytes);
    else return _mm256_cvtepu8_epi64(bytes);
}

// A one-bit Local tail needs no general 8x8 transpose. Each active result lane
// selects one bit from this exact byte; short byte/word fragments zero the rest.
template<unsigned L, unsigned Begin>
[[gnu::always_inline]] inline __m256i local_one_tail(const std::uint8_t* p) noexcept {
    constexpr unsigned N = std::min(8u, 32u / L);
    static_assert(Begin + N <= 8);
    if constexpr (L >= 4) {
        constexpr auto shifts = [] {
            std::array<scalar_for_width<8 * L>, 32 / L> result{};
            for (unsigned i = 0; i < result.size(); ++i) result[i] = Begin + i;
            return result;
        }();
        // Only shifts below eight are active. Repeated high source bytes are
        // discarded by the final one-bit mask, avoiding a scalar round trip.
        const auto source = _mm256_set1_epi8(static_cast<char>(*p));
        const auto counts = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(shifts.data()));
        const auto values = [&] [[gnu::always_inline]] {
            if constexpr (L == 4) return _mm256_srlv_epi32(source, counts);
            else return _mm256_srlv_epi64(source, counts);
        }();
        return _mm256_and_si256(values, mask<L, 1>());
    } else {
        const auto bits = _mm_set_epi64x(0, static_cast<long long>(0x8040201008040201ULL));
        const auto source = _mm_set1_epi8(static_cast<char>(*p));
        const auto values = _mm_and_si128(
            _mm_cmpeq_epi8(_mm_and_si128(source, bits), bits),
            _mm_set_epi64x(0, 0x0101010101010101ULL));
        return expand_bytes<L>(values);
    }
}

// Four contiguous headless Local1 packets supply 32 ascending byte lanes from
// exactly four bytes. Wider Local packets do not admit this tail coalescing.
[[gnu::always_inline]] inline __m256i local_one_region32(const std::uint8_t* p) noexcept {
    std::uint32_t word;
    std::memcpy(&word, p, sizeof word);
    const auto source = _mm256_broadcastd_epi32(_mm_cvtsi32_si128(static_cast<int>(word)));
    const auto groups = _mm256_setr_epi8(
        0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1,
        2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3);
    const auto bits = _mm256_set1_epi64x(static_cast<long long>(0x8040201008040201ULL));
    const auto expanded = _mm256_shuffle_epi8(source, groups);
    const auto set = _mm256_cmpeq_epi8(_mm256_and_si256(expanded, bits), bits);
    return _mm256_and_si256(set, _mm256_set1_epi8(1));
}

template<std::unsigned_integral UInt>
[[gnu::always_inline]] inline void store_byte_region32(__m256i values, UInt* out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    if constexpr (L == 1) _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), values);
    else detail::static_for<L>([&](auto part) {
        constexpr unsigned offset = part * (32 / L);
        auto bytes = [&] [[gnu::always_inline]] {
            if constexpr (offset < 16) return _mm256_castsi256_si128(values);
            else return _mm256_extracti128_si256(values, 1);
        }();
        if constexpr (offset % 16 != 0) bytes = _mm_srli_si128(bytes, offset % 16);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + offset), expand_bytes<L>(bytes));
    });
}

// Gather the low byte of each lane to the low 32/L bytes. Used inside a native
// region, never as a materialized intermediary or a public fragment format.
template<unsigned L>
[[gnu::always_inline]] inline __m128i low_bytes(__m256i values) noexcept {
    if constexpr (L == 1) return _mm256_castsi256_si128(values);
    else {
        const auto indices = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(
            detail::avx2::body_detail::encode_indices<1, L>.data()));
        const auto packed = _mm256_shuffle_epi8(values, indices);
        const auto a = _mm256_castsi256_si128(packed);
        const auto b = _mm256_extracti128_si256(packed, 1);
        if constexpr (L == 2) return _mm_unpacklo_epi64(a, b);
        else if constexpr (L == 4) return _mm_unpacklo_epi32(a, b);
        else return _mm_unpacklo_epi16(a, b);
    }
}

// Concatenate the low half-width fields of two registers in original order.
// AVX2 pack instructions saturate, so explicitly discard the high source bits
// before using them. Wider enclosing values are valid projected inputs here.
template<unsigned From>
[[gnu::always_inline]] inline __m256i narrow_pair(__m256i a, __m256i b) noexcept {
    static_assert(From == 2 || From == 4 || From == 8);
#if defined(__AVX512VL__) && defined(__AVX512BW__)
    const auto low = [&] [[gnu::always_inline]] (__m256i x) {
        if constexpr (From == 8) return _mm256_cvtepi64_epi32(x);
        else if constexpr (From == 4) return _mm256_cvtepi32_epi16(x);
        else return _mm256_cvtepi16_epi8(x);
    };
    return _mm256_inserti128_si256(_mm256_castsi128_si256(low(a)), low(b), 1);
#else
    if constexpr (From == 8) {
        const auto indices = _mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6);
        return _mm256_permute2x128_si256(_mm256_permutevar8x32_epi32(a, indices),
                                        _mm256_permutevar8x32_epi32(b, indices), 0x20);
    } else {
        constexpr std::uint64_t keep = From == 4 ? 65535u : 255u;
        a = _mm256_and_si256(a, mask<From, keep>());
        b = _mm256_and_si256(b, mask<From, keep>());
        const auto packed = [&] [[gnu::always_inline]] () {
            if constexpr (From == 4) return _mm256_packus_epi32(a, b);
            else return _mm256_packus_epi16(a, b);
        }();
        return _mm256_permute4x64_epi64(packed, 0xd8);
    }
#endif
}

// Read exactly 32/WorkL source values into sufficient working lanes. The
// source carrier width determines the read extent, not downstream work grain.
template<unsigned InputL, unsigned WorkL>
[[gnu::always_inline]] inline __m256i load_working(const std::uint8_t* p) noexcept {
    static_assert(std::has_single_bit(InputL) && InputL <= 8);
    static_assert(std::has_single_bit(WorkL) && WorkL <= 8);
    if constexpr (InputL <= WorkL) return detail::avx2::decode_body<InputL, WorkL>(p);
    else return narrow_pair<2 * WorkL>(load_working<InputL, 2 * WorkL>(p),
        load_working<InputL, 2 * WorkL>(p + 16 / WorkL * InputL));
}

template<unsigned From, unsigned To>
[[gnu::always_inline]] inline std::array<__m256i, To> narrow_group32(
    const std::array<__m256i, From>& values) noexcept {
    static_assert(std::has_single_bit(To) && To <= From);
    if constexpr (From == To) return values;
    else {
        std::array<__m256i, From / 2> half;
        detail::static_for<From / 2>([&](auto part) {
            half[part] = narrow_pair<From>(values[part * 2], values[part * 2 + 1]);
        });
        return narrow_group32<From / 2, To>(half);
    }
}

// Eight low bits, in original-index order. Native scalar-lane masks avoid a
// full byte transpose for wider one-bit input carriers.
template<unsigned InputL>
[[gnu::always_inline]] inline std::uint8_t local_one(const std::uint8_t* p) noexcept {
    if constexpr (InputL == 8) {
        const auto a = _mm256_slli_epi64(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)), 63);
        const auto b = _mm256_slli_epi64(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + 32)), 63);
        return std::uint8_t(_mm256_movemask_pd(_mm256_castsi256_pd(a)) |
                           (_mm256_movemask_pd(_mm256_castsi256_pd(b)) << 4));
    } else if constexpr (InputL == 4) {
        const auto values = _mm256_slli_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)), 31);
        return std::uint8_t(_mm256_movemask_ps(_mm256_castsi256_ps(values)));
    } else if constexpr (InputL == 2) {
        const auto values = _mm_and_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)), _mm_set1_epi16(1));
        const auto bytes = _mm_packus_epi16(values, _mm_setzero_si128());
        return std::uint8_t(_mm_movemask_epi8(_mm_slli_epi64(bytes, 7)));
    } else {
        const auto bytes = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
        return std::uint8_t(_mm_movemask_epi8(_mm_slli_epi64(bytes, 7)));
    }
}

template<unsigned R, unsigned Stripe>
inline constexpr auto stripe_plan = [] {
    struct Plan {
        std::array<unsigned, 8 / std::gcd(R, 8u)> groups{}, masks{};
        unsigned count = 0;
    } plan;
    detail::static_for<8 / std::gcd(R, 8u)>([&](auto group) {
        constexpr auto f = detail::residual_field<R, group, Stripe>::value;
        if constexpr (f.wire_mask != 0) {
            plan.groups[plan.count] = group;
            plan.masks[plan.count++] = f.wire_mask;
        }
    });
    for (unsigned i = 0; i < plan.count; ++i) {
        for (unsigned j = i + 1; j < plan.count; ++j) {
            if (plan.masks[j] < plan.masks[i]) {
                std::swap(plan.masks[i], plan.masks[j]);
                std::swap(plan.groups[i], plan.groups[j]);
            }
        }
    }
    return plan;
}();

template<unsigned R, unsigned Group>
inline constexpr auto group_plan = [] {
    std::array<unsigned, R / std::gcd(R, 8u)> stripes{}, masks{};
    detail::static_for<stripes.size()>([&](auto stripe) {
        stripes[stripe] = stripe;
        masks[stripe] = detail::residual_field<R, Group, stripe>::value.value_mask;
    });
    for (unsigned i = 0; i < stripes.size(); ++i)
        for (unsigned j = i + 1; j < stripes.size(); ++j)
            if (masks[stripes[j]] > masks[stripes[i]]) std::swap(stripes[i], stripes[j]);
    return stripes;
}();

#if defined(__AVX512VL__)
template<unsigned R, unsigned L, unsigned Begin, unsigned Count, std::size_t Groups>
[[gnu::always_inline]] inline __m256i pack_power(const std::array<__m256i, Groups>& values) noexcept {
    if constexpr (Count == 1) return values[Begin];
    else {
        constexpr unsigned Shift = R * Count / 2;
        const auto low = pack_power<R, L, Begin, Count / 2>(values);
        const auto high = shift<L, Shift>(pack_power<R, L, Begin + Count / 2, Count / 2>(values));
        return _mm256_ternarylogic_epi32((mask<L, (255u << Shift) & 255u>()), high, low, 0xca);
    }
}
#endif

template<unsigned R, unsigned Stripe, unsigned L, std::size_t Groups>
[[gnu::always_inline]] inline __m256i pack_stripe(const std::array<__m256i, Groups>& values) noexcept {
#if defined(__AVX512VL__)
    if constexpr (R == 1 || R == 2 || R == 4) return pack_power<R, L, 0, Groups>(values);
    else {
        constexpr auto plan = stripe_plan<R, Stripe>;
        constexpr auto first = detail::residual_field<R, plan.groups[0], Stripe>::value;
        auto packed = shift<L, first.shift>(values[plan.groups[0]]);
        detail::static_for<plan.count - 1>([&](auto after) {
            constexpr unsigned group = plan.groups[after + 1];
            constexpr auto field = detail::residual_field<R, group, Stripe>::value;
            packed = _mm256_ternarylogic_epi32((mask<L, field.wire_mask>()),
                (shift<L, field.shift>(values[group])), packed, 0xca);
        });
        return packed;
    }
#else
    auto packed = _mm256_setzero_si256();
    detail::static_for<Groups>([&](auto group) {
        constexpr auto field = detail::residual_field<R, group, Stripe>::value;
        if constexpr (field.value_mask != 0) {
            const auto bits = _mm256_and_si256(shift<L, field.shift>(values[group]), mask<L, field.wire_mask>());
            packed = _mm256_or_si256(packed, bits);
        }
    });
    return packed;
#endif
}

template<unsigned W, unsigned L, unsigned Group>
[[gnu::always_inline]] inline __m256i read_striped_tail_group(const std::uint8_t* tile, unsigned lane) noexcept {
    constexpr unsigned R = W % 8;
    auto tail = _mm256_setzero_si256();
    detail::static_for<R / std::gcd(R, 8u)>([&](auto position) {
        constexpr unsigned stripe = group_plan<R, Group>[position];
        constexpr auto field = detail::residual_field<R, Group, stripe>::value;
        if constexpr (field.value_mask != 0) {
            const auto raw = detail::avx2::decode_body<1, L>(
                tile + detail::stripe_offset<W>(stripe) + lane);
#if defined(__AVX512VL__)
            if constexpr (position == 0) tail = _mm256_and_si256(shift<L, -field.shift>(raw), mask<L, (1u << R) - 1>());
            else tail = _mm256_ternarylogic_epi32((mask<L, (1u << std::bit_width(field.value_mask)) - 1>()),
                                                  (shift<L, -field.shift>(raw)), tail, 0xca);
#else
            tail = _mm256_or_si256(tail, _mm256_and_si256(shift<L, -field.shift>(raw), mask<L, field.value_mask>()));
#endif
        }
    });
    return tail;
}

template<unsigned W, unsigned L, unsigned Group>
[[gnu::always_inline]] inline __m256i read_striped_group(const std::uint8_t* tile, unsigned lane) noexcept {
    const auto tail = read_striped_tail_group<W, L, Group>(tile, lane);
    if constexpr (W / 8 == 0) return tail;
    else {
        const auto body = detail::avx2::decode_body<W / 8, L>(
            tile + detail::body_offset<W, geometry::striped>(Group * 32 + lane));
        return _mm256_or_si256(shift<L, W % 8>(body), tail);
    }
}

} // namespace native_detail

/// Read one stripe group's payload, starting at tile position Group*32+lane.
/// Delivers 32/L ascending L-byte unsigned lanes. The caller supplies a
/// complete readable tile and lane + 32/L <= 32; no heads are joined.
template<unsigned W, unsigned L, unsigned Group>
[[gnu::always_inline]] inline __m256i read_group(const std::uint8_t* tile, unsigned lane) noexcept {
    static_assert(W <= L * 8 && (L == 1 || L == 2 || L == 4 || L == 8));
    static_assert(Group < payload_layout<W, geometry::striped>::tile_values / 32);
    return native_detail::read_striped_group<W, L, Group>(tile, lane);
}

/// Read the whole-byte body (payload value shifted right by W % 8).
/// L is unsigned lane width in bytes; lane i names tile position Begin+i.
/// The caller supplies a complete readable payload tile and a fragment-aligned
/// Begin within it. Reads no heads, bytes beyond the physical tile, or stride gaps.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline __m256i read_body(const std::uint8_t* tile) noexcept {
    using traits = fragment_traits<W, G, L>;
    static_assert(Begin % traits::lanes == 0 && Begin + traits::lanes <= traits::tile_values);
    constexpr unsigned Q = W / 8;
    if constexpr (Q == 0) return _mm256_setzero_si256();
    else if constexpr (G == geometry::local8)
        return detail::avx2::decode_body_prefix<Q, L, traits::lanes>(tile + Begin * Q);
    else return detail::avx2::decode_body<Q, L>(tile + detail::body_offset<W, G>(Begin));
}

/// Read the low W % 8 payload bits, zero-extended into L-byte unsigned lanes.
/// Lane i names tile position Begin+i; Begin must align to this fragment's grain.
/// Requires the complete readable payload tile; reads no heads or stride gaps.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline __m256i read_tail(const std::uint8_t* tile) noexcept {
    using traits = fragment_traits<W, G, L>;
    static_assert(Begin % traits::lanes == 0 && Begin + traits::lanes <= traits::tile_values);
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (R == 0) return _mm256_setzero_si256();
    else if constexpr (G == geometry::striped)
        return native_detail::read_striped_tail_group<W, L, Begin / 32>(tile, Begin % 32);
#if !defined(__GFNI__)
    else if constexpr (R == 1) return native_detail::local_one_tail<L, Begin>(tile + 8 * Q);
#endif
    else {
        auto bytes = native_detail::transpose(_mm_cvtsi64_si128(
            static_cast<long long>(detail::load_le<R>(tile + 8 * Q))));
        if constexpr (Begin != 0) bytes = _mm_srli_si128(bytes, Begin);
        return native_detail::expand_bytes<L>(bytes);
    }
}

template<unsigned LaneBytes, unsigned TailBits>
[[gnu::always_inline]] inline __m256i join(__m256i body, __m256i tail) noexcept {
    if constexpr (TailBits >= 8 * LaneBytes) return tail;
    else {
        auto shifted = native_detail::shift<LaneBytes, TailBits>(body);
        if constexpr (LaneBytes == 1 && TailBits != 0)
            shifted = _mm256_and_si256(shifted, native_detail::mask<1, (255u << TailBits) & 255u>());
        return _mm256_or_si256(shifted, tail);
    }
}

/// W-bit payload values without heads; lane i names tile position Begin+i.
/// L is lane width in bytes. Requires a complete readable payload tile and Begin
/// aligned to fragment_traits<W, G, L>::lanes; performs no admission.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline __m256i read_fragment(const std::uint8_t* tile) noexcept {
    return join<L, W % 8>(read_body<W, G, L, Begin>(tile), read_tail<W, G, L, Begin>(tile));
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
[[gnu::always_inline]] inline void decode_tile(const std::uint8_t* __restrict tile, UInt* __restrict out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    using traits = fragment_traits<W, G, L>;
    if constexpr (G == geometry::local8) {
        detail::static_for<traits::tile_values / traits::lanes>([&](auto part) {
            const auto values = read_fragment<W, G, L, part * traits::lanes>(tile);
            detail::avx2::encode_body_prefix<L, L, traits::lanes>(
                reinterpret_cast<std::uint8_t*>(out + part * traits::lanes), values);
        });
    } else {
#pragma clang loop unroll(disable)
        for (unsigned lane = 0; lane < 32; lane += traits::lanes) {
            detail::static_for<traits::tile_values / 32>([&](auto group) {
                detail::avx2::encode_body<L, L>(
                    reinterpret_cast<std::uint8_t*>(out + group * 32 + lane), read_group<W, L, group>(tile, lane));
            });
        }
    }
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
[[gnu::always_inline]] inline void encode_low_tile(const UInt* __restrict in, std::uint8_t* __restrict tile) noexcept {
    // Project the low W bits of the unsigned input; a narrower source is
    // zero-extended natively. Input and destination extents are disjoint.
    // The enclosing checked API owns full-value fit.
    constexpr unsigned L = sizeof(UInt) > sizeof(scalar_for_width<W>)
        ? sizeof(UInt) : sizeof(scalar_for_width<W>);
    constexpr unsigned Q = W / 8, R = W % 8;
    using traits = fragment_traits<W, G, L>;
    if constexpr (W == 0) return;
    else if constexpr (G == geometry::local8 && W == 1) {
        tile[0] = native_detail::local_one<sizeof(UInt)>(reinterpret_cast<const std::uint8_t*>(in));
    } else if constexpr (G == geometry::local8) {
        auto tails = _mm_setzero_si128();
        detail::static_for<8 / traits::lanes>([&](auto part) {
            const auto values = detail::avx2::decode_body_prefix<sizeof(UInt), L, traits::lanes>(
                reinterpret_cast<const std::uint8_t*>(in + part * traits::lanes));
            if constexpr (Q != 0) {
                detail::avx2::encode_body_prefix<Q, L, traits::lanes>(
                    tile + part * traits::lanes * Q, native_detail::shift<L, -int(R)>(values));
            }
            if constexpr (R != 0) {
                auto bytes = native_detail::low_bytes<L>(values);
                if constexpr (part != 0) bytes = _mm_slli_si128(bytes, part * traits::lanes);
                tails = _mm_or_si128(tails, bytes);
            }
        });
        if constexpr (R != 0) detail::store_le<R>(tile + 8 * Q,
            static_cast<std::uint64_t>(_mm_cvtsi128_si64(native_detail::transpose(tails))));
    } else {
        constexpr unsigned WorkL = sizeof(scalar_for_width<W>);
        constexpr unsigned Groups = payload_layout<W, G>::tile_values / 32;
        std::array<__m256i, Groups> tails;
        detail::static_for<Groups>([&](auto group) {
            std::array<__m256i, WorkL> values;
            detail::static_for<WorkL>([&](auto part) {
                values[part] = native_detail::load_working<sizeof(UInt), WorkL>(
                    reinterpret_cast<const std::uint8_t*>(in + group * 32 + part * (32 / WorkL)));
            });
            tails[group] = native_detail::narrow_group32<WorkL, 1>(values)[0];
            if constexpr (Q != 0) {
                detail::static_for<WorkL>([&](auto part) {
                    values[part] = native_detail::shift<WorkL, -int(R)>(values[part]);
                });
                const auto body = native_detail::narrow_group32<WorkL, Q>(values);
                detail::static_for<Q>([&](auto part) {
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(
                        tile + detail::body_offset<W, G>(group * 32) + part * 32), body[part]);
                });
            }
        });
        detail::static_for<R / std::gcd(R, 8u)>([&](auto stripe) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(tile + detail::stripe_offset<W>(stripe)),
                native_detail::pack_stripe<R, stripe, 1>(tails));
        });
    }
}

// Four adjacent LocalPack packets are one 32-value byte-lane region. This
// changes compute granularity only; the four exact R-byte packets remain the
// wire. It is eligible only for a contiguous admitted region without gaps.
template<unsigned R>
[[gnu::always_inline]] inline __m256i read_local_region32(const std::uint8_t* p) noexcept {
    static_assert(R >= 1 && R <= 7);
#if !defined(__GFNI__)
    if constexpr (R == 1) return native_detail::local_one_region32(p);
    else
#endif
    return native_detail::transpose(detail::avx2::decode_body<R, 8>(p));
}

#if !defined(__GFNI__) && !defined(__AVX512F__)
// Only each qword's low32 bits are meaningful. The low32 bits of moved<<28
// in the final exchange are zero, so compute just the surviving projection.
[[gnu::always_inline]] inline __m256i local4_projected_transpose(__m256i x) noexcept {
    const auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
        const auto moved = _mm256_and_si256(_mm256_xor_si256(x, _mm256_srli_epi64(x, Shift)),
                                          _mm256_set1_epi64x(Mask));
        x = _mm256_xor_si256(x, _mm256_xor_si256(moved, _mm256_slli_epi64(moved, Shift)));
    };
    exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14, 0x0000cccc0000ccccULL>();
    const auto mask = _mm256_set1_epi64x(0x00000000f0f0f0f0ULL);
    return _mm256_or_si256(_mm256_andnot_si256(mask, x),
                           _mm256_and_si256(mask, _mm256_srli_epi64(x, 28)));
}
#endif

template<unsigned R>
[[gnu::always_inline]] inline void write_local_region32(std::uint8_t* p, __m256i values) noexcept {
    static_assert(R >= 1 && R <= 7);
    if constexpr (R == 1) {
        detail::store_le<4>(p, static_cast<std::uint32_t>(
            _mm256_movemask_epi8(_mm256_slli_epi64(values, 7))));
    }
#if !defined(__GFNI__) && !defined(__AVX512F__)
    else if constexpr (R == 4)
        detail::avx2::encode_body<4, 8>(p, local4_projected_transpose(values));
#endif
    else detail::avx2::encode_body<R, 8>(p, native_detail::transpose(values));
}

// Dense run helpers are useful enclosing regions, not obligations imposed on
// a single-tile read. Strided placement uses the tile/native-fragment entries.
template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void decode_tiles(const std::uint8_t* __restrict p, UInt* __restrict out, std::size_t tiles) noexcept {
    constexpr unsigned T = payload_layout<W, G>::tile_values, B = payload_layout<W, G>::tile_bytes;
#if defined(__GFNI__)
    constexpr bool direct_one = W == 1 && sizeof(UInt) == 2;
#else
    constexpr bool direct_one = W == 1;
#endif
    if constexpr (G == geometry::local8 && direct_one) {
        // Working bytes stay independent of the output carrier. With GFNI,
        // this materializing region is selected only for u16; other carriers
        // keep their existing lowering, as the paired target results differ.
#pragma clang loop unroll(disable)
        for (; tiles >= 4; tiles -= 4, p += 4, out += 32)
            native_detail::store_byte_region32(native_detail::local_one_region32(p), out);
        for (; tiles; --tiles, ++p, out += 8) decode_tile<W, G>(p, out);
        return;
    }
    std::size_t tile = 0;
    if constexpr (G == geometry::local8 && W >= 1 && W <= 7 && sizeof(UInt) == 1) {
        for (; tiles - tile >= 4; tile += 4)
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + tile * 8), read_local_region32<W>(p + tile * W));
    }
    for (; tile < tiles; ++tile) {
        if constexpr (B == 0) decode_tile<W, G>(p, out + tile * T);
        else decode_tile<W, G>(p + tile * B, out + tile * T);
    }
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void encode_low_tiles(const UInt* __restrict in, std::uint8_t* __restrict p, std::size_t tiles) noexcept {
    constexpr unsigned T = payload_layout<W, G>::tile_values, B = payload_layout<W, G>::tile_bytes;
    std::size_t tile = 0;
    if constexpr (G == geometry::local8 && W >= 1 && W <= 7 &&
                  (W != 1 || sizeof(UInt) < 4)) {
        for (; tiles - tile >= 4; tile += 4)
            write_local_region32<W>(p + tile * W,
                native_detail::load_working<sizeof(UInt), 1>(reinterpret_cast<const std::uint8_t*>(in + tile * 8)));
    }
    for (; tile < tiles; ++tile) {
        if constexpr (B != 0) encode_low_tile<W, G>(in + tile * T, p + tile * B);
    }
}

// Trusted width-valid spellings. Input-fit validation belongs to admission or
// the checked outer operation; these add no hot-path checking or carrier tag.
template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
[[gnu::always_inline]] inline void encode_tile(const UInt* in, std::uint8_t* tile) noexcept {
    encode_low_tile<W, G>(in, tile);
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void encode_tiles(const UInt* in, std::uint8_t* p, std::size_t tiles) noexcept {
    encode_low_tiles<W, G>(in, p, tiles);
}

} // namespace ikea_predecessor::seriespack::avx2
#endif
