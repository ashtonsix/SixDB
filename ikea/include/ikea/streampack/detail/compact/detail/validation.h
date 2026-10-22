#pragma once
#include <ikea/streampack/detail/compact/author/read.h>

namespace ikea::streampack::detail::compact::detail {
/// One geometry admission walk. The enclosing recipe may check sibling fields
/// in the same pass; the callback does not alter geometry or borrowed controls.
template <class Source, class Check>
std::expected<void, error> validate_geometry(const Source &source, Check &&check) {
    const auto &l = source.format();
    const auto n = source.size();
    if (n > l.capacity() || l.lengths_bytes(n) > source.windows().lengths.size())
        return std::unexpected(error::contents);
    if (source.extent() > l.extent_limit())
        return std::unexpected(error::contents);
    return author::dispatch(source, [&](auto a) -> std::expected<void, error> {
        using O = typename decltype(a)::offset_type;
        using W = typename decltype(a)::width_type;
        unsigned position = 0, previous_local = 0;
        for (unsigned rank = 0; rank < n; ++rank) {
            unsigned reservation;
            if (l.fixed())
                reservation = l.fixed_width();
            else if (l.schema().family == geometry::absolute_ends) {
                const auto end = detail::load<O>(a.geometry_data() + rank * sizeof(O));
                if (end < position)
                    return std::unexpected(error::contents);
                reservation = end - position;
            } else {
                if (!(rank % 16) &&
                    detail::load<O>(a.geometry_data() + (rank / 16) * sizeof(O)) != position)
                    return std::unexpected(error::contents);
                reservation =
                    detail::load<W>(source.windows().lengths.data() + a.width_offset(rank));
                if constexpr (decltype(a)::family == geometry::relative_ends) {
                    if (!(rank % 16))
                        previous_local = 0;
                    if (reservation < previous_local)
                        return std::unexpected(error::contents);
                    const auto end = reservation;
                    reservation -= previous_local;
                    previous_local = end;
                }
            }
            if (reservation > l.schema().maximum || !check(a, rank, reservation, true))
                return std::unexpected(error::contents);
            position += reservation;
        }
        if (position != source.extent())
            return std::unexpected(error::contents);
        if (n % 16)
            for (unsigned rank = n; rank < ((n + 15) / 16) * 16; ++rank) {
                if (!l.fixed() && l.schema().family != geometry::absolute_ends &&
                    detail::load<W>(source.windows().lengths.data() + a.width_offset(rank)))
                    return std::unexpected(error::contents);
                if (!check(a, rank, 0, false))
                    return std::unexpected(error::contents);
            }
        return {};
    });
}
} // namespace ikea::streampack::detail::compact::detail
