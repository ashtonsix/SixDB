#pragma once
#include <ikea/seriespack/reader.h>

namespace ikea::seriespack {
/// Checked convenience range; output is disjoint from occupied source bytes.
template <class L, class B, class U>
[[nodiscard]] std::expected<void, error>
read(const basic_view<L, B>& source, std::size_t first, std::size_t count, std::span<U> output) {
    if (first > source.size() || count > source.size() - first)
        return std::unexpected(error::range);
    if (output.size() < count)
        return std::unexpected(error::capacity);
    detail::erase_placed_reader<U>(source).read_unchecked(first, count, output.data());
    return {};
}
} // namespace ikea::seriespack
