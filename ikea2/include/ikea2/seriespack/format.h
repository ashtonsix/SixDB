#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <type_traits>
#include <utility>

namespace ikea2::seriespack {
enum class geometry { local, striped };

constexpr bool striped_width(unsigned k) {
    return (k > 0 && k < 8) || k == 10 || k == 12 || k == 14 || k == 15 || k == 20;
}

template <unsigned K, geometry G = geometry::local, unsigned H = 0> struct format {
    static_assert(K >= 1 && K <= 64);
    static_assert((H == 0 || H == 8 || H == 16) && H <= K);
    static_assert(G == geometry::local || striped_width(K - H));
    static constexpr unsigned width = K, heads = H, payload = K - H;
    static constexpr unsigned body = payload / 8, tail = payload % 8;
    static constexpr geometry storage = G;
    static constexpr std::size_t tile_rows =
        G == geometry::local ? 8 : 32 * (8 / std::gcd(tail, 8u));
    static constexpr std::size_t tile_bytes = tile_rows * payload / 8;
};

template <unsigned K>
using uint_for = std::conditional_t<
    (K <= 8), std::uint8_t,
    std::conditional_t<(K <= 16), std::uint16_t,
                       std::conditional_t<(K <= 32), std::uint32_t, std::uint64_t>>>;

} // namespace ikea2::seriespack
