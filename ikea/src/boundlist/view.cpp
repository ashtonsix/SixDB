#include <ikea/boundlist/author/read.h>
#include <ikea/boundlist/view.h>
#include <ikea/boundlist/detail/validation.h>
#include <limits>

namespace ikea::boundlist::sidecar::detail {
std::expected<void, error> admit_metadata(unsigned directory_bytes, unsigned control_bytes,
                                          unsigned lengths_alignment, const void* format,
                                          std::size_t format_bytes, storage<const byte> s) {
    if (s.directory.size() < directory_bytes || s.control.size() < control_bytes)
        return std::unexpected(error::capacity);
    if (s.lengths.size() && reinterpret_cast<std::uintptr_t>(s.lengths.data()) % lengths_alignment)
        return std::unexpected(error::alignment);
    const auto planes = s.planes();
    for (unsigned i = 0; i < planes.size(); ++i) {
        const auto p = planes[i];
        if (p.size() && (!p.data() || reinterpret_cast<std::uintptr_t>(p.data()) >
                                          std::numeric_limits<std::uintptr_t>::max() - p.size()))
            return std::unexpected(error::capacity);
        if (overlaps(p.data(), p.size(), format, format_bytes))
            return std::unexpected(error::alias);
        for (unsigned j = 0; j < i; ++j)
            if (overlaps(p.data(), p.size(), planes[j].data(), planes[j].size()))
                return std::unexpected(error::alias);
    }
    return {};
}
std::expected<void, error> admit_storage(const layout& l, storage<const byte> s) {
    return admit_metadata(l.directory_bytes(), l.control_bytes(), l.lengths_alignment(), &l,
                          sizeof(l), s);
}
} // namespace ikea::boundlist::sidecar::detail

namespace ikea::boundlist::sidecar {
template <class Byte> std::expected<void, error> basic_view<Byte>::validate() const {
    return detail::validate_geometry(*this, [](auto, unsigned, unsigned, bool) { return true; });
}
template class basic_view<byte>;
template class basic_view<const byte>;
} // namespace ikea::boundlist::sidecar
