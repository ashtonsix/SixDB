#pragma once
#include <ikea/seriespack/detail/wire.h>
#include <ikea/seriespack/view.h>
#if defined(__BMI2__)
#include <immintrin.h>
#endif

namespace ikea::seriespack::detail {
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
        else {
            // Each row group's residual is at most two byte fields; a table
            // places them, so a point takes two loads and shifts without a
            // branch on its random group (that branch mispredicted: Zen 5
            // K = 5 points 3.7 ns against 0.26 native).
            static constexpr auto fields = [] {
                struct field {
                    std::uint8_t stripe, shift, mask, place;
                };
                struct {
                    std::array<std::array<field, 2>, 8> t{};
                    bool fits = true;
                } out;
                auto& t = out.t;
                for (unsigned g = 0; g < F::tile_rows / 32; ++g) {
                    unsigned n = 0, last = 0;
                    for (unsigned b = 0; b < R; ++b) {
                        const unsigned at = tail_bit<R>(g, b);
                        auto& f = t[g][n ? n - 1 : 0];
                        if (n && at == last + 1 && at / 8 == f.stripe)
                            f.mask = static_cast<std::uint8_t>(f.mask << 1 | 1);
                        else {
                            if (n == 2) {
                                out.fits = false;
                                break;
                            }
                            t[g][n++] = {static_cast<std::uint8_t>(at / 8), static_cast<std::uint8_t>(at % 8), 1,
                                         static_cast<std::uint8_t>(b)};
                        }
                        last = at;
                    }
                    if (n == 1)
                        t[g][1] = {t[g][0].stripe, 0, 0, 0};
                }
                return out;
            }();
            static_assert(fields.fits, "a row group spans at most two byte fields");
            const auto& f = fields.t[group];
            return ((stripe(f[0].stripe) >> f[0].shift) & f[0].mask) << f[0].place |
                   ((stripe(f[1].stripe) >> f[1].shift) & f[1].mask) << f[1].place;
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
} // namespace ikea::seriespack::detail
