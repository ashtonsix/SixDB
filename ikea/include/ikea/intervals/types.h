#pragma once
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <type_traits>

namespace ikea::intervals {
/// Each source has at most UINT_MAX assigned ranks, with count also the absent
/// sentinel. Larger directories compose local sources with wider outer indices.
inline constexpr unsigned maximum_count = std::numeric_limits<unsigned>::max();
enum class edge { at_or_after, after };
struct rank_window {
    unsigned first, limit;
    bool empty() const noexcept {
        return first == limit;
    }
    auto operator<=>(const rank_window&) const = default;
};
/// Coordinates of one contiguous half-open interval.
template <class Point> struct interval {
    Point first, limit;
};
enum class error { description, capacity, alignment, alias, contents, rank, value, effects, range };
/// Cold diagnostic text for this module's typed errors.
std::string_view describe(error) noexcept;
namespace detail {
template <std::integral A, std::integral B> constexpr bool integer_less(A a, B b) {
    if constexpr (std::is_signed_v<A> == std::is_signed_v<B>)
        return a < b;
    else if constexpr (std::is_signed_v<A>)
        return a < 0 || std::make_unsigned_t<A>(a) < b;
    else
        return b >= 0 && a < std::make_unsigned_t<B>(b);
}
template <edge E, class T, class Q> constexpr bool meets(const T& value, const Q& point) {
    if constexpr (std::integral<T> && std::integral<Q>) {
        if constexpr (E == edge::after)
            return integer_less(point, value);
        else
            return !integer_less(value, point);
    } else {
        if constexpr (E == edge::after)
            return point < value;
        else
            return !(value < point);
    }
}
inline std::uint64_t low_bits(unsigned n) {
    return n == 64 ? ~std::uint64_t{} : (std::uint64_t{1} << n) - 1;
}
/// Classification precedes conversion: queries outside a stored lane's domain
/// must keep their order. The returned value is used only when relation == 0.
template <std::unsigned_integral T, std::integral Q> struct prepared_query {
    int relation;
    T value;
    explicit prepared_query(Q point)
        : relation(integer_less(point, 0)                               ? -1
                   : integer_less(std::numeric_limits<T>::max(), point) ? 1
                                                                        : 0),
          value(relation ? T{} : T(point)) {}
};
} // namespace detail
} // namespace ikea::intervals
