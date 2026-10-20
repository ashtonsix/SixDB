#include <ikea/boundlist/author/read.h>
#include <ikea/boundlist/view.h>
#include <limits>

namespace ikea::boundlist::sidecar::detail {
std::expected<void, error> admit_storage(const layout& l, storage<const byte> s) {
    if (s.directory.size() < l.directory_bytes() || s.control.size() < l.control_bytes())
        return std::unexpected(error::capacity);
    if (s.lengths.size() &&
        reinterpret_cast<std::uintptr_t>(s.lengths.data()) % l.lengths_alignment())
        return std::unexpected(error::alignment);
    const auto planes = s.planes();
    for (unsigned i = 0; i < planes.size(); ++i) {
        const auto p = planes[i];
        if (p.size() && (!p.data() || reinterpret_cast<std::uintptr_t>(p.data()) >
                                          std::numeric_limits<std::uintptr_t>::max() - p.size()))
            return std::unexpected(error::capacity);
        if (overlaps(p.data(), p.size(), &l, sizeof(l)))
            return std::unexpected(error::alias);
        for (unsigned j = 0; j < i; ++j)
            if (overlaps(p.data(), p.size(), planes[j].data(), planes[j].size()))
                return std::unexpected(error::alias);
    }
    return {};
}
} // namespace ikea::boundlist::sidecar::detail

namespace ikea::boundlist::sidecar {
template <class Byte> std::expected<void, error> basic_view<Byte>::validate() const {
    const auto& l = format();
    const auto n = size();
    if (n > l.capacity() || l.lengths_bytes(n) > storage_.lengths.size())
        return std::unexpected(error::contents);
    if (extent() > l.schema().byte_limit)
        return std::unexpected(error::contents);
    return author::dispatch(*this, [&](auto a) -> std::expected<void, error> {
        using O = typename decltype(a)::offset_type;
        using W = typename decltype(a)::width_type;
        unsigned position = 0;
        for (unsigned rank = 0; rank < n; ++rank) {
            unsigned reservation;
            if (l.fixed())
                reservation = l.fixed_width();
            else if (l.schema().family == geometry::direct) {
                const auto end = detail::load<O>(a.geometry_data() + rank * sizeof(O));
                if (end < position)
                    return std::unexpected(error::contents);
                reservation = end - position;
            } else {
                if (!(rank % 16) &&
                    detail::load<O>(a.geometry_data() + (rank / 16) * sizeof(O)) != position)
                    return std::unexpected(error::contents);
                reservation = detail::load<W>(storage_.lengths.data() + a.width_offset(rank));
            }
            if (reservation > l.schema().maximum || (l.has_used() && a.length(rank) > reservation))
                return std::unexpected(error::contents);
            position += reservation;
        }
        if (position != extent())
            return std::unexpected(error::contents);
        if (n % 16)
            for (unsigned rank = n; rank < ((n + 15) / 16) * 16; ++rank) {
                if (!l.fixed() && l.schema().family == geometry::width &&
                    detail::load<W>(storage_.lengths.data() + a.width_offset(rank)))
                    return std::unexpected(error::contents);
                if (l.has_used() && detail::load<W>(storage_.lengths.data() + l.used_offset() +
                                                    a.width_offset(rank)))
                    return std::unexpected(error::contents);
            }
        return {};
    });
}
template class basic_view<byte>;
template class basic_view<const byte>;
} // namespace ikea::boundlist::sidecar
