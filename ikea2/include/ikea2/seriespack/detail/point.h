#pragma once
#include <ikea2/seriespack/detail/wire.h>
#include <ikea2/seriespack/view.h>
#if defined(__BMI2__)
#include <immintrin.h>
#endif

namespace ikea2::seriespack::detail {
template <class F>
[[gnu::always_inline]] inline std::uint64_t point_tail(const std::uint8_t* tile, std::size_t lane) {
    constexpr unsigned R = F::tail;
    static_assert(R != 0);
    if constexpr (F::storage == geometry::local) {
        constexpr auto columns = 0x0101010101010101ULL >> (8 * (8 - R));
        const auto bits = load<R>(tile + 8 * F::body) >> lane;
#if defined(__BMI2__)
        return _pext_u64(bits, columns);
#else
        return ((bits & columns) * 0x0102040810204080ULL) >> 56;
#endif
    } else {
        const unsigned group = lane / 32;
        auto stripe = [&](unsigned s) -> unsigned { return tile[stripe_offset<F>(s) + lane % 32]; };
        if constexpr (R == 1 || R == 2 || R == 4)
            return (stripe(0) >> (group * R)) & ((1u << R) - 1);
        else if constexpr (R == 3) {
            static constexpr std::uint64_t descriptors = [] {
                std::uint64_t x = 0;
                for (unsigned g = 0; g < 8; ++g) {
                    const unsigned low = tail_bit<3>(g, 0), high = tail_bit<3>(g, 2);
                    x |= std::uint64_t((low / 8) * 32 | low % 8 | (low / 8 != high / 8 ? 8 : 0))
                         << (g * 8);
                }
                return x;
            }();
            const auto d = static_cast<unsigned>(descriptors >> (group * 8));
            const auto a = stripe((d >> 5) & 3);
            if (d & 8)
                return (a >> 6) | ((stripe(1) >> (4 + (group >> 2))) & 4);
            return (a >> (d & 7)) & 7;
        } else if constexpr (R == 6) {
            const auto edge = group & 2;
            const auto a = stripe(edge);
            if (((group + 1) & 2) == 0)
                return a & 63;
            return ((a >> 2) & 48) | ((stripe(1) >> (edge * 2)) & 15);
        } else {
            const unsigned start = group * R, s = start / 8, shift = start % 8;
            const auto a = stripe(s);
            if (shift <= 8 - R)
                return (a >> shift) & ((1u << R) - 1);
            const auto mask = (1u << (shift + R - 8)) - 1;
            return ((a >> (8 - R)) & ~mask) | (stripe(s + 1) & mask);
        }
    }
}
template <class F>
[[gnu::always_inline]] inline std::uint64_t point_payload(const std::uint8_t* tile,
                                                          std::size_t lane) {
    std::uint64_t high = 0;
    if constexpr (F::body)
        high = load<F::body>(tile + body_offset<F>(lane));
    if constexpr (F::tail == 0)
        return high;
    else
        return (high << F::tail) | point_tail<F>(tile, lane);
}
template <class F>
[[gnu::always_inline]] inline void put_tail(std::uint8_t* tile, std::size_t lane,
                                            std::uint64_t value) {
    if constexpr (F::tail)
        each<F::tail>([&](auto b) {
            const auto pos =
                F::storage == geometry::local ? b * 8 + lane : tail_bit<F::tail>(lane / 32, b);
            const auto offset = F::storage == geometry::local
                                    ? 8 * F::body + b
                                    : stripe_offset<F>(pos / 8) + lane % 32;
            const unsigned bit = pos % 8;
            auto& byte = tile[offset];
            byte = static_cast<std::uint8_t>((byte & ~(1u << bit)) | (((value >> b) & 1u) << bit));
        });
}
template <class F>
[[gnu::always_inline]] inline void put_payload(std::uint8_t* tile, std::size_t lane,
                                               std::uint64_t value) {
    if constexpr (F::body)
        store<F::body>(tile + body_offset<F>(lane), value >> F::tail);
    if constexpr (F::tail)
        put_tail<F>(tile, lane, value);
}
} // namespace ikea2::seriespack::detail

namespace ikea2::seriespack {
template <class F, class B>
[[gnu::always_inline]] inline std::uint64_t get_unchecked(const view<F, B>& v, std::size_t i) {
    const auto tile = i / F::tile_rows, lane = i % F::tile_rows;
    std::uint64_t x = 0;
    if constexpr (F::payload)
        x = detail::point_payload<F>(v.stream(0).bytes.data() + tile * v.stream(0).stride, lane);
    if constexpr (F::heads) {
        std::uint64_t high = v.stream(1).bytes[tile * v.stream(1).stride + lane];
        if constexpr (F::heads == 16)
            high = (high << 8) | v.stream(2).bytes[tile * v.stream(2).stride + lane];
        x |= high << F::payload;
    }
    return x;
}
template <class F, class B>
std::expected<std::uint64_t, error> get(const view<F, B>& v, std::size_t i) {
    if (i >= v.size())
        return std::unexpected(error::range);
    return get_unchecked(v, i);
}
template <class F>
inline void set_unchecked(const view<F, std::uint8_t>& v, std::size_t i, std::uint64_t x) {
    const auto tile = i / F::tile_rows, lane = i % F::tile_rows;
    if constexpr (F::payload)
        detail::put_payload<F>(v.stream(0).bytes.data() + tile * v.stream(0).stride, lane, x);
    if constexpr (F::heads) {
        v.stream(1).bytes[tile * v.stream(1).stride + lane] =
            static_cast<std::uint8_t>(x >> (F::width - 8));
        if constexpr (F::heads == 16)
            v.stream(2).bytes[tile * v.stream(2).stride + lane] =
                static_cast<std::uint8_t>(x >> F::payload);
    }
}
template <class F>
std::expected<void, error> set(const view<F, std::uint8_t>& v, std::size_t i, std::uint64_t x) {
    if (i >= v.size())
        return std::unexpected(error::range);
    if constexpr (F::width < 64)
        if (x >> F::width)
            return std::unexpected(error::value);
    set_unchecked(v, i, x);
    return {};
}
} // namespace ikea2::seriespack
