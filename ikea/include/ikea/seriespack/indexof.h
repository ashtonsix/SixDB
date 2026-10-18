#pragma once
#include <ikea/seriespack/author/indexof.h>

namespace ikea::seriespack {
/// First active equality match relative to first, or count when absent.
/// Trusted window of at most 64 values, within the admitted source's extent.
template <class Source>
[[gnu::always_inline]] inline unsigned
indexof_unchecked(const Source& source, std::size_t first, unsigned count, std::uint64_t rhs,
                  std::uint64_t active = ~std::uint64_t{0}) {
    return composition::indexof_unchecked(composition::describe(source), first, count, rhs, active);
}

template <class Source>
[[nodiscard]] inline std::expected<unsigned, error>
indexof(const Source& source, std::size_t first, unsigned count, std::uint64_t rhs,
        std::uint64_t active = ~std::uint64_t{0}) {
    const auto size = [&] {
        if constexpr (requires { source.count; })
            return source.count;
        else
            return source.size();
    }();
    if (count > 64 || first > size || count > size - first)
        return std::unexpected(error::range);
    return indexof_unchecked(source, first, count, rhs, active);
}
} // namespace ikea::seriespack
