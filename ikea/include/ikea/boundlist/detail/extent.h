#pragma once
#include <ikea/boundlist/description.h>
#include <ikea/boundlist/detail/bytes.h>

namespace ikea::boundlist::detail {
/// Shared coherent count/extent law; no search or native-reader dependency.
template <class Source> [[gnu::always_inline]] std::uint32_t extent(const Source& source) noexcept {
    const auto n = source.size();
    const auto& l = source.format();
    const auto& s = source.windows();
    if (l.fixed())
        return n * l.fixed_width();
    if (l.has_extent())
        return load<std::uint32_t>(s.control.data() + 2);
    if (!n)
        return 0;
    const auto* p = s.directory.data() + l.geometry_offset() + (n - 1) * l.offset_bytes();
    switch (l.offset_bytes()) {
    case 1:
        return load<std::uint8_t>(p);
    case 2:
        return load<std::uint16_t>(p);
    default:
        return load<std::uint32_t>(p);
    }
}
} // namespace ikea::boundlist::detail
