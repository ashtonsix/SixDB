#pragma once
#include <algorithm>
#include <bit>
#include <ikea/tuplepack/author/native.h>
#include <ikea/tuplepack/detail/native/selection.h>
#include <ikea/tuplepack/detail/packet_plan.h>
#include <utility>
#if defined(__aarch64__) || defined(__AVX2__)
namespace ikea::tuplepack::native::packet_detail {
using namespace native_detail;
template <unsigned Rows> inline constexpr std::uint64_t all = ~std::uint64_t(0) >> (64 - Rows);

template <unsigned Bytes, unsigned I>
[[gnu::always_inline]] inline vector16 insert(vector16 v, std::uint64_t x) {
#if defined(__aarch64__)
    if constexpr (Bytes == 8)
        return vreinterpretq_u8_u64(vsetq_lane_u64(x, vreinterpretq_u64_u8(v), I));
    if constexpr (Bytes == 4)
        return vreinterpretq_u8_u32(vsetq_lane_u32(x, vreinterpretq_u32_u8(v), I));
    if constexpr (Bytes == 2)
        return vreinterpretq_u8_u16(vsetq_lane_u16(x, vreinterpretq_u16_u8(v), I));
    if constexpr (Bytes == 1)
        return vsetq_lane_u8(x, v, I);
#else
    if constexpr (Bytes == 8)
        return _mm_insert_epi64(v, x, I);
    if constexpr (Bytes == 4)
        return _mm_insert_epi32(v, x, I);
    if constexpr (Bytes == 2)
        return _mm_insert_epi16(v, x, I);
    if constexpr (Bytes == 1)
        return _mm_insert_epi8(v, x, I);
#endif
}
template <unsigned Bytes, unsigned I>
[[gnu::always_inline]] inline std::uint64_t extract(vector16 v) {
#if defined(__aarch64__)
    if constexpr (Bytes == 8)
        return vgetq_lane_u64(vreinterpretq_u64_u8(v), I);
    if constexpr (Bytes == 4)
        return vgetq_lane_u32(vreinterpretq_u32_u8(v), I);
    if constexpr (Bytes == 2)
        return vgetq_lane_u16(vreinterpretq_u16_u8(v), I);
    if constexpr (Bytes == 1)
        return vgetq_lane_u8(v, I);
#else
    if constexpr (Bytes == 8)
        return _mm_extract_epi64(v, I);
    if constexpr (Bytes == 4)
        return unsigned(_mm_extract_epi32(v, I));
    if constexpr (Bytes == 2)
        return _mm_extract_epi16(v, I);
    if constexpr (Bytes == 1)
        return _mm_extract_epi8(v, I);
#endif
}
template <unsigned Bytes, unsigned Mode>
[[gnu::always_inline]] inline std::uint64_t load_word(const detail::packet_placement &p,
                                                      const byte *row) {
    if constexpr (Mode != 2) {
        if constexpr (Mode == 0) {
            using word = std::conditional_t<
                Bytes == 8, std::uint64_t,
                std::conditional_t<Bytes == 4, std::uint32_t,
                                   std::conditional_t<Bytes == 2, std::uint16_t, byte>>>;
            word x;
            __builtin_memcpy(&x, row + p.first, Bytes);
            return x;
        }
        if constexpr (Bytes == 4) {
            std::uint16_t low;
            __builtin_memcpy(&low, row + p.first, 2);
            return low | (std::uint64_t(row[p.first + 2]) << 16);
        }
        return tail8(row + p.first, p.count);
    }
    std::uint64_t word = 0;
    [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
        ((word |= I < p.count ? std::uint64_t(row[p.offsets[I]]) << (8 * I) : 0), ...);
    }(std::make_index_sequence<Bytes>{});
    return word;
}
template <unsigned Bytes, unsigned Mode>
[[gnu::always_inline]] inline void store_word(const detail::packet_placement &p, byte *row,
                                              std::uint64_t value) {
    if constexpr (Mode != 2) {
        if constexpr (Mode == 0) {
            __builtin_memcpy(row + p.first, &value, Bytes);
        } else if constexpr (Bytes == 4) {
            __builtin_memcpy(row + p.first, &value, 2);
            row[p.first + 2] = byte(value >> 16);
        } else
            store_tail8(row + p.first, p.count, value);
        return;
    }
    [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
        ((I < p.count ? void(row[p.offsets[I]] = byte(value >> (8 * I))) : void()), ...);
    }(std::make_index_sequence<Bytes>{});
}
template <unsigned Part, unsigned Mode>
[[gnu::always_inline]] inline vector16 gather16(const detail::packet_placement &p,
                                                const byte *row) {
    if constexpr (Mode == 0)
        return load16(row + p.first + Part * 16, 16);
    if (p.count <= Part * 16)
        return zero16();
    if (p.contiguous)
        return load16(row + p.first + Part * 16, std::min(16u, p.count - Part * 16));
    auto result = zero16();
    [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
        ((result =
              insert<1, I>(result, Part * 16 + I < p.count ? row[p.offsets[Part * 16 + I]] : 0)),
         ...);
    }(std::make_index_sequence<16>{});
    return result;
}
template <unsigned Part, unsigned Mode>
[[gnu::always_inline]] inline void scatter16(const detail::packet_placement &p, byte *row,
                                             vector16 value) {
    if constexpr (Mode == 0) {
        store16(row + p.first + Part * 16, 16, value);
        return;
    }
    if (p.count <= Part * 16)
        return;
    if (p.contiguous) {
        store16(row + p.first + Part * 16, std::min(16u, p.count - Part * 16), value);
        return;
    }
    [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
        ((Part * 16 + I < p.count ? void(row[p.offsets[Part * 16 + I]] = extract<1, I>(value))
                                  : void()),
         ...);
    }(std::make_index_sequence<16>{});
}
template <unsigned Rows, unsigned Grain, unsigned Part, bool Full, unsigned Mode, class Address>
[[gnu::always_inline]] inline vector16 gather_part(const detail::packet_placement &p,
                                                   Address address, std::uint64_t active) {
    constexpr unsigned B = Grain;
    if constexpr (Part * 16 >= Rows * Grain)
        return zero16();
    else if constexpr (B >= 16) {
        constexpr unsigned row = Part / (B / 16), piece = Part % (B / 16);
        if (!Full && !(active & (std::uint64_t(1) << row)))
            return zero16();
        return gather16<piece, Mode>(p, address(row));
    } else {
        auto result = zero16();
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            ((result = insert<B, I>(
                  result, (Part * (16 / B) + I < Rows) &&
                                  (Full || (active & (std::uint64_t(1) << (Part * (16 / B) + I))))
                              ? load_word<B, Mode>(p, address(Part * (16 / B) + I))
                              : 0)),
             ...);
        }(std::make_index_sequence<16 / B>{});
        return result;
    }
}
template <unsigned Rows, unsigned Grain, bool Full, unsigned Mode, class Address>
[[gnu::always_inline]] inline packet gather(const detail::packet_placement &p, Address address,
                                            std::uint64_t active) {
    return join(gather_part<Rows, Grain, 0, Full, Mode>(p, address, active),
                gather_part<Rows, Grain, 1, Full, Mode>(p, address, active),
                gather_part<Rows, Grain, 2, Full, Mode>(p, address, active),
                gather_part<Rows, Grain, 3, Full, Mode>(p, address, active));
}
template <unsigned Rows, unsigned Grain, unsigned Part, bool Full, unsigned Mode, class Address>
[[gnu::always_inline]] inline void scatter_part(const detail::packet_placement &p, Address address,
                                                std::uint64_t active, vector16 value) {
    constexpr unsigned B = Grain;
    if constexpr (Part * 16 >= Rows * Grain)
        return;
    else if constexpr (B >= 16) {
        constexpr unsigned row = Part / (B / 16), piece = Part % (B / 16);
        if (Full || (active & (std::uint64_t(1) << row)))
            scatter16<piece, Mode>(p, address(row), value);
    } else {
        [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
            auto put = [&]<unsigned Lane>() __attribute__((always_inline)) {
                constexpr unsigned row = Part * (16 / B) + Lane;
                if constexpr (row < Rows)
                    if (Full || (active & (std::uint64_t(1) << row)))
                        store_word<B, Mode>(p, address(row), extract<B, Lane>(value));
            };
            (put.template operator()<I>(), ...);
        }(std::make_index_sequence<16 / B>{});
    }
}
template <unsigned Rows, unsigned Grain, bool Full, unsigned Mode, class Address>
[[gnu::always_inline]] inline void scatter(const detail::packet_placement &p, Address address,
                                           std::uint64_t active, packet value) {
    scatter_part<Rows, Grain, 0, Full, Mode>(p, address, active, split<0>(value));
    scatter_part<Rows, Grain, 1, Full, Mode>(p, address, active, split<1>(value));
    scatter_part<Rows, Grain, 2, Full, Mode>(p, address, active, split<2>(value));
    scatter_part<Rows, Grain, 3, Full, Mode>(p, address, active, split<3>(value));
}
template <bool Left>
[[gnu::always_inline]] inline packet transform_prefix(packet source, const detail::shuffle &p,
                                                      unsigned bytes) {
#if defined(__aarch64__)
    return {apply16<0>(source, p), bytes > 16 ? apply16<1>(source, p) : zero16(),
            bytes > 32 ? apply16<2>(source, p) : zero16(),
            bytes > 48 ? apply16<3>(source, p) : zero16()};
#elif defined(__AVX512VBMI__)
    (void)bytes;
    return native::transform<Left>(source, p);
#else
    return {apply32<0, Left>(source, p),
            bytes > 32 ? apply32<1, Left>(source, p) : _mm256_setzero_si256()};
#endif
}
template <unsigned Rows, bool Left>
[[gnu::always_inline]] inline packet transform(packet source, const detail::shuffle &p) {
    if constexpr (Rows < 4)
        return native::transform<Left>(source, p);
    else {
#if defined(__aarch64__)
        const auto index = vandq_u8(vld1q_u8(p.index.data()), vdupq_n_u8(15));
        auto apply = [&](auto value) __attribute__((always_inline)) {
            value = vqtbl1q_u8(value, index);
            if (p.shifting)
                value = vshlq_u8(value, vld1q_s8(p.shift.data()));
            if (p.masking)
                value = vandq_u8(value, vld1q_u8(p.mask.data()));
            return value;
        };
        return {apply(source.a), apply(source.b), apply(source.c), apply(source.d)};
#elif defined(__AVX512VBMI__)
        auto value = _mm512_shuffle_epi8(source, _mm512_loadu_si512(p.index.data()));
        if (p.shifting)
            value = _mm512_multishift_epi64_epi8(_mm512_loadu_si512(p.bit_index.data()), value);
        if (p.masking)
            value = _mm512_and_si512(value, _mm512_loadu_si512(p.mask.data()));
        return value;
#else
        auto apply = [&](auto value, unsigned half) __attribute__((always_inline)) {
            value = _mm256_shuffle_epi8(value, _mm256_loadu_si256(reinterpret_cast<const __m256i *>(
                                                   p.index.data() + half * 32)));
            if (p.shifting) {
                const auto lo = _mm256_set1_epi16(255), hi = _mm256_set1_epi16(short(0xff00));
                const auto ef = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i *>(p.even_factor.data() + half * 16));
                const auto of = _mm256_loadu_si256(
                    reinterpret_cast<const __m256i *>(p.odd_factor.data() + half * 16));
                auto even = _mm256_mullo_epi16(_mm256_and_si256(value, lo), ef);
                __m256i odd;
                if constexpr (Left) {
                    even = _mm256_and_si256(even, lo);
                    odd = _mm256_mullo_epi16(_mm256_and_si256(value, hi), of);
                } else {
                    even = _mm256_srli_epi16(even, 8);
                    odd = _mm256_mullo_epi16(_mm256_srli_epi16(value, 8), of);
                }
                value = _mm256_or_si256(even, _mm256_and_si256(odd, hi));
            }
            if (p.masking)
                value =
                    _mm256_and_si256(value, _mm256_loadu_si256(reinterpret_cast<const __m256i *>(
                                                p.mask.data() + half * 32)));
            return value;
        };
        return {apply(source.a, 0), apply(source.b, 1)};
#endif
    }
}
template <unsigned Rows, unsigned Grain, bool Full, class Address>
[[gnu::always_inline]] inline packet gather_selected(const detail::packet_placement &p,
                                                     Address address, std::uint64_t active) {
    // Grain is bit_ceil(count). One-byte transfers are always contiguous;
    // two-byte transfers cannot have a short tail. Preserve these facts through
    // lowering instead of cloning arbitrary tail logic into every tiny row.
    if constexpr (Grain == 1)
        return gather<Rows, Grain, Full, 0>(p, address, active);
    else {
        if (p.contiguous) {
            if constexpr (Grain == 2)
                return gather<Rows, Grain, Full, 0>(p, address, active);
            else {
                if (p.count == Grain)
                    return gather<Rows, Grain, Full, 0>(p, address, active);
                return gather<Rows, Grain, Full, 1>(p, address, active);
            }
        }
        if constexpr (Rows <= 4 && Grain > 8)
            __builtin_unreachable(); // Scattered wide maps use point lowering.
        else
            return gather<Rows, Grain, Full, 2>(p, address, active);
    }
}
template <unsigned Rows, unsigned Grain, bool Full, class Address>
[[gnu::always_inline]] inline void scatter_selected(const detail::packet_placement &p,
                                                    Address address, std::uint64_t active,
                                                    packet value) {
    if constexpr (Grain == 1)
        scatter<Rows, Grain, Full, 0>(p, address, active, value);
    else {
        if (p.contiguous) {
            if constexpr (Grain == 2)
                scatter<Rows, Grain, Full, 0>(p, address, active, value);
            else if (p.count == Grain)
                scatter<Rows, Grain, Full, 0>(p, address, active, value);
            else
                scatter<Rows, Grain, Full, 1>(p, address, active, value);
        } else if constexpr (Rows <= 4 && Grain > 8)
            __builtin_unreachable(); // Scattered wide maps use point lowering.
        else
            scatter<Rows, Grain, Full, 2>(p, address, active, value);
    }
}
template <unsigned Rows, unsigned Grain = 64 / Rows, class Address>
[[gnu::always_inline]] inline packet gather_grain(const detail::packet_placement &p,
                                                  Address address, std::uint64_t active) {
    if constexpr (Grain > 1)
        if (p.grain != Grain)
            return gather_grain<Rows, Grain / 2>(p, address, active);
    return active == all<Rows> ? gather_selected<Rows, Grain, true>(p, address, active)
                               : gather_selected<Rows, Grain, false>(p, address, active);
}
template <unsigned Rows, unsigned Grain = 64 / Rows, class Address>
[[gnu::always_inline]] inline void scatter_grain(const detail::packet_placement &p, Address address,
                                                 std::uint64_t active, packet value) {
    if constexpr (Grain > 1)
        if (p.grain != Grain) {
            scatter_grain<Rows, Grain / 2>(p, address, active, value);
            return;
        }
    if (active == all<Rows>)
        scatter_selected<Rows, Grain, true>(p, address, active, value);
    else
        scatter_selected<Rows, Grain, false>(p, address, active, value);
}
template <unsigned Rows, class Address>
[[gnu::always_inline]] inline packet gather_bound(const detail::packet_placement &p,
                                                  Address address, std::uint64_t active,
                                                  std::size_t stride) {
    if (active == all<Rows> && p.contiguous && p.count == p.grain && stride == p.grain)
        return native::load_unit(address(0) + p.first, Rows * p.grain);
#if defined(__AVX512VBMI__)
    if (p.contiguous && p.count == p.grain && stride == p.grain) {
        // The stride hint promises a common strided allocation. Start from an
        // active address; gathered-pointer callers need not supply inactive ones.
        const unsigned first_active = std::countr_zero(active);
        const auto base = address(first_active) - first_active * stride + p.first;
        const auto mask = [&]<unsigned G = 64 / Rows>(auto &&self)
                              __attribute__((always_inline)) -> std::uint64_t {
            if constexpr (G > 1)
                if (p.grain != G)
                    return self.template operator()<G / 2>(self);
            return native_detail::byte_mask<Rows, G>(active);
        };
        return _mm512_maskz_loadu_epi8(mask(mask), base);
    }
#endif
    return gather_grain<Rows>(p, address, active);
}
template <unsigned Rows, class Address>
[[gnu::always_inline]] inline void scatter_bound(const detail::packet_placement &p, Address address,
                                                 std::uint64_t active, std::size_t stride,
                                                 packet value) {
    if (active == all<Rows> && p.contiguous && p.count == p.grain && stride == p.grain) {
        const auto bytes = Rows * p.grain;
        auto row = address(0) + p.first;
        store_part<0>(row, bytes, value);
        store_part<1>(row, bytes, value);
        store_part<2>(row, bytes, value);
        store_part<3>(row, bytes, value);
        return;
    }
#if defined(__AVX512VBMI__)
    if (p.contiguous && p.count == p.grain && stride == p.grain) {
        const unsigned first_active = std::countr_zero(active);
        auto base = address(first_active) - first_active * stride + p.first;
        const auto mask = [&]<unsigned G = 64 / Rows>(auto &&self)
                              __attribute__((always_inline)) -> std::uint64_t {
            if constexpr (G > 1)
                if (p.grain != G)
                    return self.template operator()<G / 2>(self);
            return native_detail::byte_mask<Rows, G>(active);
        };
        _mm512_mask_storeu_epi8(base, mask(mask), value);
        return;
    }
#endif
    scatter_grain<Rows>(p, address, active, value);
}
[[gnu::always_inline]] inline void point_write(const detail::packet_write &p, byte *row,
                                               packet input) {
    store_selected(row, encode(p, row, input), p.writes);
}
template <unsigned Rows, class Address>
[[gnu::always_inline]] inline packet read(const detail::packet_read &p, Address address,
                                          std::uint64_t active = all<Rows>,
                                          std::size_t stride = 0) {
    if (!p.place.count || !active)
        return join(zero16(), zero16(), zero16(), zero16());
    static_assert(Rows > 1);
    if constexpr (Rows >= 4 && Rows <= 16)
        if (p.tight_bytes && active == all<Rows> && stride == p.bytes)
            return native::transform<false>(native::load_unit(address(0), p.tight_bytes),
                                            p.tight_route);
    if constexpr (Rows == 2) {
        if (!p.place.contiguous && p.place.count > 8) {
            auto a = active & 1 ? native::read_body(p.point, address(0))
                                : join(zero16(), zero16(), zero16(), zero16());
            auto b = active & 2 ? native::read_body(p.point, address(1))
                                : join(zero16(), zero16(), zero16(), zero16());
            const auto value = join(split<0>(a), split<1>(a), split<0>(b), split<1>(b));
            return p.ordering.row_major() ? value : native::transform<false>(value, p.route);
        }
    }
    if constexpr (Rows == 4) {
        if (!p.place.contiguous && p.place.count > 8) {
            auto part = [&](unsigned row) __attribute__((always_inline)) {
                return active & (std::uint64_t(1) << row)
                           ? split<0>(native::read_body(p.point, address(row)))
                           : zero16();
            };
            const auto value = join(part(0), part(1), part(2), part(3));
            return p.ordering.row_major() ? value : native::transform<false>(value, p.route);
        }
    }
    auto value = gather_bound<Rows>(p.place, address, active, stride);
    return p.ordering.row_major() && p.place.grain == 64 / Rows
               ? transform<Rows, false>(value, p.route)
               : native::transform<false>(value, p.route);
}
template <unsigned Rows, class Address>
[[gnu::always_inline]] inline void write(const detail::packet_write &p, Address address,
                                         packet value, std::uint64_t active = all<Rows>,
                                         std::size_t stride = 0) {
    if (!p.place.count || !active)
        return;
    static_assert(Rows > 1);
    if constexpr (Rows == 2 || Rows == 4) {
        if (!p.place.contiguous && p.place.count > 8) {
            if (!p.read.ordering.row_major())
                value = native::transform<false>(value, p.ungroup);
            [&]<std::size_t... I>(std::index_sequence<I...>) __attribute__((always_inline)) {
                auto apply = [&]<unsigned R>() __attribute__((always_inline)) {
                    if (!(active & (std::uint64_t(1) << R)))
                        return;
                    if constexpr (Rows == 2)
                        point_write(
                            p, address(R),
                            join(split<R * 2>(value), split<R * 2 + 1>(value), zero16(), zero16()));
                    else
                        point_write(p, address(R),
                                    join(split<R>(value), zero16(), zero16(), zero16()));
                };
                (apply.template operator()<I>(), ...);
            }(std::make_index_sequence<Rows>{});
            return;
        }
    }
    auto updated = join(zero16(), zero16(), zero16(), zero16());
    if (p.needs_old) {
        updated = gather_bound<Rows>(p.place, address, active, stride);
        updated = native::bit_and(updated, native::load_packet(p.preserve.data()));
    }
    for (unsigned round = 0; round < p.round_count; ++round)
        updated = native::bit_or(
            updated, p.read.ordering.row_major() && p.place.grain == 64 / Rows
                         ? transform<Rows, true>(value, p.rounds[round])
                         : transform_prefix<true>(value, p.rounds[round], Rows * p.place.grain));
    scatter_bound<Rows>(p.place, address, active, stride, updated);
}
} // namespace ikea::tuplepack::native::packet_detail

namespace ikea::tuplepack::native {
template <unsigned Rows, class Address>
[[gnu::always_inline]] inline packet read_body(const detail::packet_read &p, Address address,
                                               std::uint64_t active = packet_detail::all<Rows>,
                                               std::size_t stride = 0) {
    return packet_detail::read<Rows>(p, address, active, stride);
}
template <unsigned Rows, class Address>
[[gnu::always_inline]] inline void
write_body(const detail::packet_write &p, Address address, packet input,
           std::uint64_t active = packet_detail::all<Rows>, std::size_t stride = 0) {
    packet_detail::write<Rows>(p, address, input, active, stride);
}
} // namespace ikea::tuplepack::native
#endif
