#pragma once

#include <ikea_predecessor/seriespack/detail/body_avx512.h>
#include <ikea_predecessor/seriespack/native_avx2.h>

// This complete payload family includes irregular byte bodies and therefore
// names BW+VBMI. The body helper header exposes narrower feature subsets.
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
namespace ikea_predecessor::seriespack::avx512 {

/// Native payload shape: W is stored payload bits; lane width is in bytes.
/// Only the low `lanes` positions belong to the fragment; other register lanes
/// carry no logical-value promise, even when their bits are defined as zero.
template<unsigned W, geometry G, unsigned L = sizeof(scalar_for_width<W>)>
struct fragment_traits {
    static_assert(L == 1 || L == 2 || L == 4 || L == 8);
    static_assert(W <= 8 * L);
    using vector_type = __m512i;
    using lane_type = scalar_for_width<8 * L>;
    static constexpr unsigned register_lanes = 64 / L;
    static constexpr unsigned lanes = G == geometry::local8 ? 8 : register_lanes;
    static constexpr unsigned tile_values = payload_layout<W, G>::tile_values;
};

namespace native_detail {

template<unsigned L, int Shift>
[[gnu::always_inline]] inline __m512i shift(__m512i v) noexcept {
    if constexpr (Shift == 0) return v;
    else if constexpr (L <= 2) {
        if constexpr (Shift > 0) return _mm512_slli_epi16(v, Shift);
        else return _mm512_srli_epi16(v, -Shift);
    } else if constexpr (L == 4) {
        if constexpr (Shift > 0) return _mm512_slli_epi32(v, Shift);
        else return _mm512_srli_epi32(v, -Shift);
    } else {
        if constexpr (Shift > 0) return _mm512_slli_epi64(v, Shift);
        else return _mm512_srli_epi64(v, -Shift);
    }
}

template<unsigned L, std::uint64_t Mask>
[[gnu::always_inline]] inline __m512i mask() noexcept {
    if constexpr (L == 1) return _mm512_set1_epi8(static_cast<char>(Mask));
    else if constexpr (L == 2) return _mm512_set1_epi16(static_cast<short>(Mask));
    else if constexpr (L == 4) return _mm512_set1_epi32(static_cast<int>(Mask));
    else return _mm512_set1_epi64(static_cast<long long>(Mask));
}

inline constexpr auto reverse_matrix_bytes = [] {
    std::array<std::uint8_t, 64> indices{};
    for (unsigned i = 0; i < 64; ++i) indices[i] = (i & 8u) + 7 - i % 8;
    return indices;
}();

[[gnu::always_inline]] inline __m512i transpose(__m512i x) noexcept {
#if defined(__GFNI__)
    const auto reversed = _mm512_shuffle_epi8(x, _mm512_loadu_si512(reverse_matrix_bytes.data()));
    return _mm512_gf2p8affine_epi64_epi8(_mm512_set1_epi64(0x8040201008040201ULL), reversed, 0);
#else
    auto exchange = [&]<unsigned Shift, std::uint64_t Mask>() {
        const auto moved = _mm512_and_si512(_mm512_xor_si512(x, _mm512_srli_epi64(x, Shift)),
                                           _mm512_set1_epi64(Mask));
        x = _mm512_ternarylogic_epi64(x, moved, _mm512_slli_epi64(moved, Shift), 0x96);
    };
    exchange.template operator()<7, 0x00aa00aa00aa00aaULL>();
    exchange.template operator()<14, 0x0000cccc0000ccccULL>();
    exchange.template operator()<28, 0x00000000f0f0f0f0ULL>();
    return x;
#endif
}

template<unsigned L>
[[gnu::always_inline]] inline __m512i expand_bytes(__m128i bytes) noexcept {
    if constexpr (L == 1) return _mm512_zextsi128_si512(bytes);
    else if constexpr (L == 2) return _mm512_cvtepu8_epi16(_mm256_zextsi128_si256(bytes));
    else if constexpr (L == 4) return _mm512_cvtepu8_epi32(bytes);
    else return _mm512_cvtepu8_epi64(bytes);
}

template<unsigned L>
[[gnu::always_inline]] inline __m128i low_bytes(__m512i values) noexcept {
    if constexpr (L == 1) return _mm512_castsi512_si128(values);
    else if constexpr (L == 2) return _mm256_castsi256_si128(_mm512_cvtepi16_epi8(values));
    else if constexpr (L == 4) return _mm512_cvtepi32_epi8(values);
    else return _mm512_cvtepi64_epi8(values);
}

// Exact 32-value byte projection. AVX-512 truncating conversions preserve low
// bits without saturation, then concatenate their native results in index order.
template<unsigned InputL>
[[gnu::always_inline]] inline __m256i load_bytes32(const std::uint8_t* p) noexcept {
    static_assert(std::has_single_bit(InputL) && InputL <= 8);
    if constexpr (InputL == 1) return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
    else if constexpr (InputL == 2) return _mm512_cvtepi16_epi8(_mm512_loadu_si512(p));
    else if constexpr (InputL == 4) {
        const auto a = _mm512_cvtepi32_epi8(_mm512_loadu_si512(p));
        const auto b = _mm512_cvtepi32_epi8(_mm512_loadu_si512(p + 64));
        return _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 1);
    } else {
        const auto a = _mm512_cvtepi64_epi8(_mm512_loadu_si512(p));
        const auto b = _mm512_cvtepi64_epi8(_mm512_loadu_si512(p + 64));
        const auto c = _mm512_cvtepi64_epi8(_mm512_loadu_si512(p + 128));
        const auto d = _mm512_cvtepi64_epi8(_mm512_loadu_si512(p + 192));
        return _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_unpacklo_epi64(a, b)),
                                      _mm_unpacklo_epi64(c, d), 1);
    }
}

// One full working register, consuming exactly 64/WorkL source values. Only
// sufficient working widths 2/4 occur in the admitted wider striped layouts.
template<unsigned InputL, unsigned WorkL>
[[gnu::always_inline]] inline __m512i load_working(const std::uint8_t* p) noexcept {
    static_assert(WorkL == 2 || WorkL == 4);
    if constexpr (InputL <= WorkL) return detail::avx512::decode_body<InputL, WorkL>(p);
    else if constexpr (InputL == 8 && WorkL == 2) {
        auto result = _mm512_castsi128_si512(_mm512_cvtepi64_epi16(_mm512_loadu_si512(p)));
        result = _mm512_inserti32x4(result, _mm512_cvtepi64_epi16(_mm512_loadu_si512(p + 64)), 1);
        result = _mm512_inserti32x4(result, _mm512_cvtepi64_epi16(_mm512_loadu_si512(p + 128)), 2);
        return _mm512_inserti32x4(result, _mm512_cvtepi64_epi16(_mm512_loadu_si512(p + 192)), 3);
    } else {
        const auto narrow = [] [[gnu::always_inline]] (__m512i x) {
            if constexpr (InputL == 8) return _mm512_cvtepi64_epi32(x);
            else return _mm512_cvtepi32_epi16(x);
        };
        return _mm512_inserti64x4(_mm512_castsi256_si512(narrow(_mm512_loadu_si512(p))),
                                  narrow(_mm512_loadu_si512(p + 64)), 1);
    }
}

template<unsigned R, unsigned L, unsigned Begin, unsigned Count, std::size_t Groups>
[[gnu::always_inline]] inline __m512i pack_power(const std::array<__m512i, Groups>& values) noexcept {
    if constexpr (Count == 1) return values[Begin];
    else {
        constexpr unsigned Shift = R * Count / 2;
        const auto low = pack_power<R, L, Begin, Count / 2>(values);
        const auto high = shift<L, Shift>(pack_power<R, L, Begin + Count / 2, Count / 2>(values));
        return _mm512_ternarylogic_epi32((mask<L, (255u << Shift) & 255u>()), high, low, 0xca);
    }
}

template<unsigned R, unsigned Stripe, unsigned L, std::size_t Groups>
[[gnu::always_inline]] inline __m512i pack_stripe(const std::array<__m512i, Groups>& values) noexcept {
    if constexpr (R == 1 || R == 2 || R == 4) return pack_power<R, L, 0, Groups>(values);
    else {
        constexpr auto plan = avx2::native_detail::stripe_plan<R, Stripe>;
        constexpr auto first = detail::residual_field<R, plan.groups[0], Stripe>::value;
        auto packed = shift<L, first.shift>(values[plan.groups[0]]);
        detail::static_for<plan.count - 1>([&](auto after) {
            constexpr unsigned group = plan.groups[after + 1];
            constexpr auto field = detail::residual_field<R, group, Stripe>::value;
            packed = _mm512_ternarylogic_epi32((mask<L, field.wire_mask>()),
                (shift<L, field.shift>(values[group])), packed, 0xca);
        });
        return packed;
    }
}

template<unsigned W, unsigned L, unsigned Group>
[[gnu::always_inline]] inline __m512i read_striped_tail_group(const std::uint8_t* tile, unsigned lane) noexcept {
    constexpr unsigned R = W % 8;
    if constexpr (L == 1) {
        return _mm512_zextsi256_si512(avx2::native_detail::read_striped_tail_group<W, 1, Group>(tile, lane));
    } else {
        auto tail = _mm512_setzero_si512();
        detail::static_for<R / std::gcd(R, 8u)>([&](auto position) {
            constexpr unsigned stripe = avx2::native_detail::group_plan<R, Group>[position];
            constexpr auto field = detail::residual_field<R, Group, stripe>::value;
            if constexpr (field.value_mask != 0) {
                const auto raw = detail::avx512::decode_body<1, L>(
                    tile + detail::stripe_offset<W>(stripe) + lane);
                if constexpr (position == 0) tail = _mm512_and_si512(shift<L, -field.shift>(raw), mask<L, (1u << R) - 1>());
                else tail = _mm512_ternarylogic_epi32((mask<L, (1u << std::bit_width(field.value_mask)) - 1>()),
                                                      (shift<L, -field.shift>(raw)), tail, 0xca);
            }
        });
        return tail;
    }
}

template<unsigned W, unsigned L, unsigned Group>
[[gnu::always_inline]] inline __m512i read_striped_group(const std::uint8_t* tile, unsigned lane) noexcept {
    const auto tail = read_striped_tail_group<W, L, Group>(tile, lane);
    if constexpr (W / 8 == 0) return tail;
    else {
        const auto body = detail::avx512::decode_body<W / 8, L>(
            tile + detail::body_offset<W, geometry::striped>(Group * 32 + lane));
        return _mm512_or_si512(shift<L, W % 8>(body), tail);
    }
}

} // namespace native_detail

/// Read one stripe group's payload, starting at tile position Group*32+lane.
/// Delivers min(32,64/L) ascending L-byte unsigned lanes. The caller supplies a
/// complete readable tile and lane + min(32,64/L) <= 32; no heads are joined.
template<unsigned W, unsigned L, unsigned Group>
[[gnu::always_inline]] inline __m512i read_group(const std::uint8_t* tile, unsigned lane) noexcept {
    static_assert(W <= L * 8 && (L == 1 || L == 2 || L == 4 || L == 8));
    static_assert(Group < payload_layout<W, geometry::striped>::tile_values / 32);
    return native_detail::read_striped_group<W, L, Group>(tile, lane);
}

/// Read the whole-byte body (payload value shifted right by W % 8).
/// L is unsigned lane width in bytes; lane i names tile position Begin+i.
/// The caller supplies a complete readable payload tile and a fragment-aligned
/// Begin within it. Reads no heads, bytes beyond the physical tile, or stride gaps.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline __m512i read_body(const std::uint8_t* tile) noexcept {
    using traits = fragment_traits<W, G, L>;
    static_assert(Begin % traits::lanes == 0 && Begin + traits::lanes <= traits::tile_values);
    constexpr unsigned Q = W / 8;
    if constexpr (Q == 0) return _mm512_setzero_si512();
    else if constexpr (G == geometry::local8) return detail::avx512::decode_body_prefix<Q, L, 8>(tile);
    else return detail::avx512::decode_body<Q, L>(tile + detail::body_offset<W, G>(Begin));
}

/// Read the low W % 8 payload bits, zero-extended into L-byte unsigned lanes.
/// Lane i names tile position Begin+i; Begin must align to this fragment's grain.
/// Requires the complete readable payload tile; reads no heads or stride gaps.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline __m512i read_tail(const std::uint8_t* tile) noexcept {
    using traits = fragment_traits<W, G, L>;
    static_assert(Begin % traits::lanes == 0 && Begin + traits::lanes <= traits::tile_values);
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (R == 0) return _mm512_setzero_si512();
    else if constexpr (G == geometry::striped) {
        if constexpr (L == 1) {
            const auto first = avx2::native_detail::read_striped_tail_group<W, 1, Begin / 32>(tile, 0);
            const auto second = avx2::native_detail::read_striped_tail_group<W, 1, Begin / 32 + 1>(tile, 0);
            return _mm512_inserti64x4(_mm512_castsi256_si512(first), second, 1);
        } else return native_detail::read_striped_tail_group<W, L, Begin / 32>(tile, Begin % 32);
    } else {
        const auto bytes = avx2::native_detail::transpose(_mm_cvtsi64_si128(
            static_cast<long long>(detail::load_le<R>(tile + 8 * Q))));
        return native_detail::expand_bytes<L>(bytes);
    }
}

template<unsigned LaneBytes, unsigned TailBits>
[[gnu::always_inline]] inline __m512i join(__m512i body, __m512i tail) noexcept {
    if constexpr (TailBits >= 8 * LaneBytes) return tail;
    else {
        auto shifted = native_detail::shift<LaneBytes, TailBits>(body);
        if constexpr (LaneBytes == 1 && TailBits != 0)
            shifted = _mm512_and_si512(shifted, native_detail::mask<1, (255u << TailBits) & 255u>());
        return _mm512_or_si512(shifted, tail);
    }
}

/// W-bit payload values without heads; lane i names tile position Begin+i.
/// L is lane width in bytes. Requires a complete readable payload tile and Begin
/// aligned to fragment_traits<W, G, L>::lanes; performs no admission.
template<unsigned W, geometry G, unsigned L, unsigned Begin>
[[gnu::always_inline]] inline __m512i read_fragment(const std::uint8_t* tile) noexcept {
    return join<L, W % 8>(read_body<W, G, L, Begin>(tile), read_tail<W, G, L, Begin>(tile));
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
[[gnu::always_inline]] inline void decode_tile(const std::uint8_t* __restrict tile, UInt* __restrict out) noexcept {
    constexpr unsigned L = sizeof(UInt);
    using traits = fragment_traits<W, G, L>;
    if constexpr (G == geometry::striped && L == 1) {
        // A materializing endpoint need not first combine the native 32-byte
        // stripe groups merely to split them into ordinary output stores.
        avx2::decode_tile<W, G>(tile, out);
    } else if constexpr (G == geometry::local8 && L <= 4 && W / 8 <= 2) {
        // Eight materialized values with one-/two-byte bodies fit a YMM here.
        // Keep the public ZMM fragment contract separate from this endpoint.
        avx2::decode_tile<W, G>(tile, out);
    } else if constexpr (G == geometry::local8) {
        const auto values = read_fragment<W, G, L, 0>(tile);
        detail::avx512::encode_body_prefix<L, L, 8>(reinterpret_cast<std::uint8_t*>(out), values);
    } else {
#pragma clang loop unroll(disable)
        for (unsigned lane = 0; lane < 32; lane += traits::lanes) {
            detail::static_for<traits::tile_values / 32>([&](auto group) {
                detail::avx512::encode_body<L, L>(
                    reinterpret_cast<std::uint8_t*>(out + group * 32 + lane), read_group<W, L, group>(tile, lane));
            });
        }
    }
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
[[gnu::always_inline]] inline void encode_low_tile(const UInt* __restrict in, std::uint8_t* __restrict tile) noexcept {
    // Low-W-bit projection, with native zero-extension of narrower inputs.
    // Input and destination extents are disjoint.
    constexpr unsigned L = sizeof(UInt) > sizeof(scalar_for_width<W>)
        ? sizeof(UInt) : sizeof(scalar_for_width<W>);
    constexpr unsigned Q = W / 8, R = W % 8;
    if constexpr (W == 0) return;
    else if constexpr (G == geometry::local8 && W == 1) {
        if constexpr (sizeof(UInt) == 8)
            tile[0] = std::uint8_t(_mm512_test_epi64_mask(_mm512_loadu_si512(in), _mm512_set1_epi64(1)));
        else tile[0] = avx2::native_detail::local_one<sizeof(UInt)>(reinterpret_cast<const std::uint8_t*>(in));
    } else if constexpr (G == geometry::local8 && L <= 4 && W / 8 <= 2) {
        // One-/two-byte bodies fit the complete local packet on AVX2;
        // avoid expanding them into a mostly inactive ZMM before low stores.
        avx2::encode_low_tile<W, G>(in, tile);
    } else if constexpr (G == geometry::local8) {
        const auto values = detail::avx512::decode_body_prefix<sizeof(UInt), L, 8>(
            reinterpret_cast<const std::uint8_t*>(in));
        if constexpr (Q != 0) detail::avx512::encode_body_prefix<Q, L, 8>(
            tile, native_detail::shift<L, -int(R)>(values));
        if constexpr (R != 0) {
            const auto transposed = avx2::native_detail::transpose(native_detail::low_bytes<L>(values));
            detail::store_le<R>(tile + 8 * Q, static_cast<std::uint64_t>(_mm_cvtsi128_si64(transposed)));
        }
    } else {
        constexpr unsigned WorkL = sizeof(scalar_for_width<W>);
        constexpr unsigned Groups = payload_layout<W, G>::tile_values / 32;
        std::array<__m256i, Groups> tails;
        detail::static_for<Groups>([&](auto group) {
            const auto* source = reinterpret_cast<const std::uint8_t*>(in + group * 32);
            if constexpr (WorkL == 1) tails[group] = native_detail::load_bytes32<sizeof(UInt)>(source);
            else {
                static_assert(WorkL == 2 || WorkL == 4);
                std::array<__m512i, WorkL / 2> values;
                detail::static_for<WorkL / 2>([&](auto part) {
                    values[part] = native_detail::load_working<sizeof(UInt), WorkL>(
                        source + part * (64 / WorkL) * sizeof(UInt));
                });
                if constexpr (WorkL == 2) {
                    tails[group] = _mm512_cvtepi16_epi8(values[0]);
                    const auto high = _mm512_cvtepi16_epi8(native_detail::shift<2, -int(R)>(values[0]));
                    _mm256_storeu_si256(reinterpret_cast<__m256i*>(
                        tile + detail::body_offset<W, G>(group * 32)), high);
                } else {
                    const auto a = _mm512_cvtepi32_epi8(values[0]);
                    const auto b = _mm512_cvtepi32_epi8(values[1]);
                    tails[group] = _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 1);
                    const auto high_a = _mm512_cvtepi32_epi16(native_detail::shift<4, -int(R)>(values[0]));
                    const auto high_b = _mm512_cvtepi32_epi16(native_detail::shift<4, -int(R)>(values[1]));
                    _mm512_storeu_si512(tile + detail::body_offset<W, G>(group * 32),
                        _mm512_inserti64x4(_mm512_castsi256_si512(high_a), high_b, 1));
                }
            }
        });
        // The wire owns 32-byte stripes. Wider source preparation does not
        // oblige these byte fields to make a redundant 64-byte handoff.
        detail::static_for<R / std::gcd(R, 8u)>([&](auto stripe) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(tile + detail::stripe_offset<W>(stripe)),
                avx2::native_detail::pack_stripe<R, stripe, 1>(tails));
        });
    }
}

template<unsigned R>
[[gnu::always_inline]] inline __m512i read_local_region64(const std::uint8_t* p) noexcept {
    static_assert(R >= 1 && R <= 7);
#if defined(__GFNI__)
    constexpr auto indices = [] {
        std::array<std::uint8_t, 64> result{};
        for (unsigned i = 0; i < 64; ++i)
            result[i] = i % 8 >= 8 - R ? i / 8 * R + 7 - i % 8 : 8 * R;
        return result;
    }();
    const auto packed = _mm512_maskz_loadu_epi8((__mmask64{1} << (8 * R)) - 1, p);
    const auto matrix = _mm512_permutexvar_epi8(_mm512_loadu_si512(indices.data()), packed);
    return _mm512_gf2p8affine_epi64_epi8(_mm512_set1_epi64(0x8040201008040201ULL), matrix, 0);
#else
    return native_detail::transpose(detail::avx512::decode_body<R, 8>(p));
#endif
}

template<unsigned R>
[[gnu::always_inline]] inline void write_local_region64(std::uint8_t* p, __m512i values) noexcept {
    static_assert(R >= 1 && R <= 7);
    if constexpr (R == 1) {
        detail::store_le<8>(p, _mm512_movepi8_mask(_mm512_slli_epi64(values, 7)));
    } else {
        const auto matrix = native_detail::transpose(values);
#if defined(__GFNI__) && defined(__AVX512VBMI2__)
        constexpr __mmask64 rows = 0x0101010101010101ULL * ((1u << R) - 1);
        const auto packed = _mm512_maskz_compress_epi8(rows, matrix);
        _mm512_mask_storeu_epi8(p, (__mmask64{1} << (8 * R)) - 1, packed);
#else
        detail::avx512::encode_body<R, 8>(p, matrix);
#endif
    }
}

template<unsigned W, geometry G, std::unsigned_integral UInt>
    requires (!std::same_as<UInt, bool>)
inline void decode_tiles(const std::uint8_t* __restrict p, UInt* __restrict out, std::size_t tiles) noexcept {
    constexpr unsigned T = payload_layout<W, G>::tile_values, B = payload_layout<W, G>::tile_bytes;
    if constexpr (G == geometry::local8 && W == 1 && sizeof(UInt) == 2) {
        // This endpoint already used AVX2 for each eight-value tile. Preserve
        // its measured 32-value working region across the dense materializer.
        avx2::decode_tiles<W, G>(p, out, tiles);
        return;
    }
    std::size_t tile = 0;
    if constexpr (G == geometry::local8 && W >= 1 && W <= 7 && sizeof(UInt) == 1) {
        for (; tiles - tile >= 8; tile += 8)
            _mm512_storeu_si512(out + tile * 8, read_local_region64<W>(p + tile * W));
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
        for (; tiles - tile >= 8; tile += 8) {
            if constexpr (sizeof(UInt) == 1)
                write_local_region64<W>(p + tile * W, _mm512_loadu_si512(in + tile * 8));
            else {
                const auto* source = reinterpret_cast<const std::uint8_t*>(in + tile * 8);
                const auto a = native_detail::load_bytes32<sizeof(UInt)>(source);
                const auto b = native_detail::load_bytes32<sizeof(UInt)>(source + 32 * sizeof(UInt));
                write_local_region64<W>(p + tile * W,
                    _mm512_inserti64x4(_mm512_castsi256_si512(a), b, 1));
            }
        }
    } else if constexpr (G == geometry::local8 && W == 56 && sizeof(UInt) == 8) {
        for (; tiles - tile >= 4; tile += 4) detail::avx512::encode_body_region32_7(
            p + tile * 56, _mm512_loadu_si512(in + tile * 8),
            _mm512_loadu_si512(in + tile * 8 + 8), _mm512_loadu_si512(in + tile * 8 + 16),
            _mm512_loadu_si512(in + tile * 8 + 24));
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

} // namespace ikea_predecessor::seriespack::avx512
#endif
