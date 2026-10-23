#pragma once
#include <ikea/hashtable/types.h>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace ikea::hashtable {
template <unsigned K> constexpr word mask = ~word{0} >> (64 - K);
template <> inline constexpr word mask<0> = 0;
template <unsigned N> inline word load(const std::uint8_t* p) {
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
