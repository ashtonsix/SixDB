#include <ikea/streampack/view.h>
#include <ikea/boundlist/view.h>
#include <ikea/boundlist/detail/validation.h>
#include <limits>

namespace ikea::streampack::detail {
std::expected<void, error> admit_storage(const layout& l, storage<const byte> s) {
    if (auto ok = boundlist::sidecar::detail::admit_metadata(l.directory_bytes(), l.control_bytes(),
                                                             l.lengths_alignment(), &l, sizeof(l),
                                                             {s.directory, s.lengths, s.control});
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
    auto ok = boundlist::sidecar::detail::validate_geometry(
        *this, [&](auto a, unsigned rank, unsigned reservation, bool assigned) {
            if (!format().has_used())
                return true;
            using W = typename decltype(a)::width_type;
            const auto u = boundlist::detail::load<W>(s.lengths.data() +
                                                      detail::used_position<W>(format(), rank));
            return assigned ? u <= reservation : u == 0;
        });
    if (!ok)
        return ok;
    if (extent() > s.payload.size())
        return std::unexpected(error::contents);
    return {};
}
template class basic_view<byte>;
template class basic_view<const byte>;
} // namespace ikea::streampack
