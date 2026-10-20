#include <ikea/streampack/view.h>
#include <ikea/boundlist/view.h>
#include <limits>

namespace ikea::streampack::detail {
std::expected<void, error> admit_storage(const layout& l, storage<const byte> s) {
    if (auto ok = boundlist::sidecar::detail::admit_storage(l, {s.directory, s.lengths, s.control});
        !ok)
        return ok;
    if (!s.payload.empty() &&
        (!s.payload.data() || reinterpret_cast<std::uintptr_t>(s.payload.data()) >
                                  std::numeric_limits<std::uintptr_t>::max() - s.payload.size()))
        return std::unexpected(error::capacity);
    if (detail::overlaps(s.payload.data(), s.payload.size(), &l, sizeof(l)))
        return std::unexpected(error::alias);
    for (auto m : {s.directory, s.lengths, s.control})
        if (detail::overlaps(s.payload.data(), s.payload.size(), m.data(), m.size()))
            return std::unexpected(error::alias);
    return {};
}
} // namespace ikea::streampack::detail

namespace ikea::streampack {
template <class Byte> std::expected<void, error> basic_view<Byte>::validate() const {
    const auto& s = windows();
    const auto metadata =
        boundlist::sidecar::const_view::assume_valid(format(), {s.directory, s.lengths, s.control});
    if (auto ok = metadata.validate(); !ok)
        return ok;
    if (extent() > s.payload.size())
        return std::unexpected(error::contents);
    return {};
}
template class basic_view<byte>;
template class basic_view<const byte>;
} // namespace ikea::streampack
