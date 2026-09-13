#pragma once
#include <ikea/seriespack/format.h>
namespace ikea::seriespack {
namespace detail {
template <std::size_t N, class F> [[gnu::always_inline]] constexpr void each(F&& fn) {
    [&]<std::size_t... I> [[gnu::always_inline]] (std::index_sequence<I...>) {
        // Both levels must remain inline: outlining the callback loses its
        // constant index and can materialize native state in captured storage.
        auto invoke = [&]<std::size_t J> [[gnu::always_inline]] () {
            [[clang::always_inline]] fn(std::integral_constant<std::size_t, J>{});
        };
        (invoke.template operator()<I>(), ...);
    }(std::make_index_sequence<N>{});
}

template <unsigned N> [[gnu::always_inline]] inline std::uint64_t load(const std::uint8_t* p) {
    static_assert(N <= 8);
    if constexpr (N == 0)
        return 0;
    else if constexpr (std::has_single_bit(N)) {
        using U = uint_for<N * 8>;
        U x;
        std::memcpy(&x, p, N);
        if constexpr (std::endian::native == std::endian::big)
            x = std::byteswap(x);
        return x;
    } else {
        constexpr unsigned B = std::bit_floor(N);
        return load<B>(p) | (load<B>(p + N - B) << ((N - B) * 8));
    }
}
template <unsigned N> [[gnu::always_inline]] inline void store(std::uint8_t* p, std::uint64_t x) {
    if constexpr (N == 0)
        return;
    else if constexpr (std::has_single_bit(N)) {
        using U = uint_for<N * 8>;
        U y = static_cast<U>(x);
        if constexpr (std::endian::native == std::endian::big)
            y = std::byteswap(y);
        std::memcpy(p, &y, N);
    } else {
        constexpr unsigned B = std::bit_floor(N);
        store<B>(p, x);
        store<B>(p + N - B, x >> ((N - B) * 8));
    }
}
[[gnu::always_inline]] inline std::uint64_t transpose(std::uint64_t x) {
    auto t = (x ^ (x >> 7)) & 0x00aa00aa00aa00aaULL;
    x ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000cccc0000ccccULL;
    x ^= t ^ (t << 14);
    t = (x ^ (x >> 28)) & 0x00000000f0f0f0f0ULL;
    return x ^ t ^ (t << 28);
}
template <unsigned R> constexpr unsigned tail_bit(unsigned group, unsigned bit) {
    if constexpr (R == 3) {
        constexpr unsigned map[8][3] = {{0, 1, 2},    {3, 4, 5},    {6, 7, 14},   {8, 9, 10},
                                        {11, 12, 13}, {22, 23, 15}, {16, 17, 18}, {19, 20, 21}};
        return map[group][bit];
    } else if constexpr (R == 6) {
        constexpr unsigned map[4][6] = {{0, 1, 2, 3, 4, 5},
                                        {8, 9, 10, 11, 6, 7},
                                        {12, 13, 14, 15, 22, 23},
                                        {16, 17, 18, 19, 20, 21}};
        return map[group][bit];
    } else {
        const unsigned start = group * R, room = 8 - start % 8;
        if (R <= room)
            return start + bit;
        const unsigned low = R - room;
        return bit < low ? (start / 8 + 1) * 8 + bit : start + bit - low;
    }
}
/// Emit the one or two byte fields changed by one striped row group. This
/// wire description is shared by native stores and their coverage adapter.
template <unsigned R, class Emit>
[[gnu::always_inline]] inline void tail_fragments(unsigned g, Emit&& emit) {
    if constexpr (R == 1 || R == 2 || R == 4)
        emit(0, g * R, ((1u << R) - 1) << (g * R));
    else if constexpr (R == 3) {
        const unsigned first = tail_bit<3>(g, 0), last = tail_bit<3>(g, 2);
        if (first / 8 == last / 8)
            emit(first / 8, first % 8, 7u << (first % 8));
        else {
            emit(first / 8, 6, 192);
            emit(1, 4 + (g >> 2), 64u << (g >> 2));
        }
    } else if constexpr (R == 6) {
        const auto edge = g & 2;
        if (((g + 1) & 2) == 0)
            emit(edge, 0, 63);
        else {
            emit(edge, 2, 192);
            emit(1, edge * 2, 15u << (edge * 2));
        }
    } else {
        const unsigned start = g * R, s = start / 8, shift = start % 8;
        if (shift <= 8 - R)
            emit(s, shift, ((1u << R) - 1) << shift);
        else {
            emit(s, 8 - R, (255u << shift) & 255);
            emit(s + 1, 0, (1u << (shift + R - 8)) - 1);
        }
    }
}
template <class F> constexpr std::size_t body_offset(std::size_t i) {
    if constexpr (F::storage == geometry::local)
        return i * F::body;
    else if constexpr (F::payload == 10 || F::payload == 20)
        // Place one shared stripe between equal body halves; each half ends
        // on a native 32-row boundary. The same law covers one/two-byte bodies.
        return i * F::body + (i >= F::tile_rows / 2 ? 32 : 0);
    else if constexpr (F::payload == 12)
        return i;
    else if constexpr (F::payload == 14 || F::payload == 15)
        return (i / 32) * 64 + i % 32;
    else
        return 0;
}
template <class F> constexpr std::size_t stripe_offset(unsigned stripe) {
    if constexpr (F::payload < 8)
        return stripe * 32;
    else if constexpr (F::payload == 14 || F::payload == 15)
        return 32 + stripe * 64;
    else
        return 64;
}
} // namespace detail
} // namespace ikea::seriespack
