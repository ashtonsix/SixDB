#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace ikea::hashtable {
using word = std::uint64_t;
using byte = std::uint8_t;
template <unsigned K> constexpr word mask = ~word{0} >> (64 - K);
template <> inline constexpr word mask<0> = 0;
template <unsigned N> inline word load(const byte* p) {
    static_assert(N <= 8);
    word v = 0;
    std::memcpy(&v, p, N);
    return v;
}
template <unsigned N, class Fn> inline void each(Fn&& f) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (f(std::integral_constant<unsigned, I>{}), ...);
    }(std::make_index_sequence<N>{});
}
} // namespace ikea::hashtable
