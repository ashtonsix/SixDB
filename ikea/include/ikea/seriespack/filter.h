#pragma once
#include <ikea/seriespack/author/filter.h>

namespace ikea::seriespack {
/// Trusted window of at most 64 values. Bit i names first+i. Empty activity
/// reads nothing; a nonempty window may read all of its occupied lanes.
/// Multiples of eight rows use native groups; byte origins need no alignment.
/// The complete window is readable and within the admitted source's extent.
template <comparison Op, class Source>
[[gnu::always_inline]] inline std::uint64_t
filter_unchecked(const Source& source, std::size_t first, unsigned count, std::uint64_t rhs,
                 std::uint64_t active = ~std::uint64_t{0}) {
    return composition::filter_unchecked<Op>(composition::describe(source), first, count, rhs,
                                             active);
}

template <comparison Op, class Source>
[[nodiscard]] inline std::expected<std::uint64_t, error>
filter(const Source& source, std::size_t first, unsigned count, std::uint64_t rhs,
       std::uint64_t active = ~std::uint64_t{0}) {
    const auto size = [&] {
        if constexpr (requires { source.count; })
            return source.count;
        else
            return source.size();
    }();
    if (count > 64 || first > size || count > size - first)
        return std::unexpected(error::range);
    return filter_unchecked<Op>(source, first, count, rhs, active);
}
} // namespace ikea::seriespack
