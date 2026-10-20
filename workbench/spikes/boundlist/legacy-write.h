// Pre-extraction complete mutation control, frozen from 4fb8fcea0ae309f2cf19d69ee1f6159e3fa57dae.
#pragma once
#include <cstring>
#include <ikea/effects.h>
#include <ikea/streampack/author/read.h>

namespace boundlist_legacy {
namespace detail = ikea::boundlist::detail;
namespace native = ikea::boundlist::native;
using namespace ikea::streampack;
/// These bodies borrow an admitted writable named view. Command, capacity,
/// aliasing, exclusion and infallible effects are proved before entry. Hooks
/// cannot allocate, fail, suspend or invalidate any borrowed input.
template <class Effects>
inline void emit(const view& dst, Effects& effects, plane stream, unsigned offset, unsigned size) {
    if (size)
        effects.before(dst, ikea::byte_write{unsigned(stream), offset, size});
}
template <class Effects> void initialize_unchecked(const view& dst, Effects& effects) {
    const auto& l = dst.format();
    emit(dst, effects, plane::directory, 0, l.directory_bytes());
    emit(dst, effects, plane::control, 0, l.control_bytes());
    if (l.directory_bytes())
        std::memset(dst.windows().directory.data(), 0, l.directory_bytes());
    std::memset(dst.windows().control.data(), 0, l.control_bytes());
}
template <class Effects> void initialize_unchecked(const view&&, Effects&) = delete;
template <class Access, class Effects>
void append_unchecked(Access a, bytes input, unsigned reservation, Effects& effects) {
    const auto& dst = a.source();
    const auto& l = a.format();
    const auto n = dst.size(), begin = dst.extent();
    using W = typename Access::width_type;
    using O = typename Access::offset_type;
    emit(dst, effects, plane::payload, begin, reservation);
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    if (reservation > input.size())
        std::memset(dst.windows().payload.data() + begin + input.size(), 0,
                    reservation - input.size());
    if (!l.fixed()) {
        if constexpr (Access::family == geometry::direct) {
            const auto offset = l.geometry_offset() + n * sizeof(O);
            emit(dst, effects, plane::directory, offset, sizeof(O));
            detail::store(dst.windows().directory.data() + offset, O(begin + reservation));
        } else if (!(n % 16)) {
            const auto offset = l.geometry_offset() + (n / 16) * sizeof(O);
            emit(dst, effects, plane::directory, offset, sizeof(O));
            detail::store(dst.windows().directory.data() + offset, O(begin));
        }
    }
    const bool r = !l.fixed() && Access::family == geometry::width;
    const auto slot = a.width_offset(n);
    if (!(n % 16) && (r || l.has_used())) {
        const auto group = (n / 16) * l.group_stride();
        emit(dst, effects, plane::lengths, group, l.group_stride());
        std::memset(dst.windows().lengths.data() + group, 0, l.group_stride());
        if (r && l.has_used() && l.schema().lengths == placement::separated) {
            emit(dst, effects, plane::lengths, group + l.used_offset(), 16 * sizeof(W));
            std::memset(dst.windows().lengths.data() + group + l.used_offset(), 0, 16 * sizeof(W));
        }
    } else {
        if (r)
            emit(dst, effects, plane::lengths, slot, sizeof(W));
        if (l.has_used())
            emit(dst, effects, plane::lengths, slot + l.used_offset(), sizeof(W));
    }
    if (r)
        detail::store(dst.windows().lengths.data() + slot, W(reservation));
    if (l.has_used())
        detail::store(dst.windows().lengths.data() + slot + l.used_offset(), W(input.size()));
    if (l.has_tombstones()) {
        emit(dst, effects, plane::directory, n / 8, 1);
        dst.windows().directory[n / 8] &= byte(~(1u << (n % 8)) & 255);
    }
    emit(dst, effects, plane::control, 0, l.control_bytes());
    detail::store(dst.windows().control.data(), std::uint16_t(n + 1));
    if (l.has_extent())
        detail::store(dst.windows().control.data() + 2, std::uint32_t(begin + reservation));
}
template <class Access, class Effects>
void append_fixed_run_unchecked(Access a, bytes input, unsigned count, Effects& effects) {
    // Admitted fixed() && !has_used() ranks in one real view: count is nonzero, the entire
    // run fits, and input.size() == count * fixed_width(). All source/effect
    // aliases are admitted before entry. The cursor/recipe may be snapshots;
    // effects still identify the original named view through a.source().
    const auto& dst = a.source();
    const auto& l = a.format();
    const auto n = dst.size(), begin = dst.extent();
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    if (l.has_tombstones()) {
        const auto first = n / 8, last = (n + count - 1) / 8;
        const auto low = unsigned(255u << (n % 8)) & 255u;
        const auto high = 255u >> (7 - ((n + count - 1) % 8));
        emit(dst, effects, plane::directory, first, last - first + 1);
        if (first == last) {
            dst.windows().directory[first] &= byte(~(low & high) & 255u);
        } else {
            dst.windows().directory[first] &= byte(~low & 255u);
            if (count > 8 && last > first + 1)
                std::memset(dst.windows().directory.data() + first + 1, 0, last - first - 1);
            dst.windows().directory[last] &= byte(~high & 255u);
        }
    }
    emit(dst, effects, plane::control, 0, l.control_bytes());
    detail::store(dst.windows().control.data(), std::uint16_t(n + count));
    if (l.has_extent())
        detail::store(dst.windows().control.data() + 2, std::uint32_t(begin + input.size()));
}
template <class Access, class Effects>
void replace_fixed_run_unchecked(Access a, unsigned rank, bytes input, Effects& effects) {
    // Admitted nonempty contiguous live fixed() && !has_used() ranks. Input contains whole
    // values and fits this run; replacement does not alter its rank count.
    const auto& dst = a.source();
    const auto begin = a.start(rank);
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
}
template <class Access, class Effects>
void extend_unchecked(Access a, unsigned rank, bytes suffix, Effects& effects) {
    // An admitted live rank with has_used(), U <= R, and suffix.size() <= R-U.
    // U+suffix fits the schema maximum and width carrier. Source/effect alias
    // admission and both write ranges precede entry; hooks cannot invalidate
    // borrowed input. The rank's reservation, geometry and liveness stay fixed.
    if (suffix.empty())
        return;
    const auto& dst = a.source();
    const auto used = a.length(rank), begin = a.start(rank);
    const auto offset = a.format().used_offset() + a.width_offset(rank);
    using W = typename Access::width_type;
    emit(dst, effects, plane::payload, begin + used, suffix.size());
    std::memmove(dst.windows().payload.data() + begin + used, suffix.data(), suffix.size());
    emit(dst, effects, plane::lengths, offset, sizeof(W));
    detail::store(dst.windows().lengths.data() + offset, W(used + suffix.size()));
}
template <class Access, class Effects>
void replace_unchecked(Access a, unsigned rank, bytes input, Effects& effects) {
    const auto& dst = a.source();
    const auto begin = a.start(rank);
    emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    if (a.format().has_used()) {
        using W = typename Access::width_type;
        const auto offset = a.format().used_offset() + a.width_offset(rank);
        emit(dst, effects, plane::lengths, offset, sizeof(W));
        detail::store(dst.windows().lengths.data() + offset, W(input.size()));
    }
}
template <class Effects> void retire_unchecked(const view& dst, unsigned rank, Effects& effects) {
    emit(dst, effects, plane::directory, rank / 8, 1);
    dst.windows().directory[rank / 8] |= byte(1u << (rank % 8));
}
template <class Effects> void retire_unchecked(const view&&, unsigned, Effects&) = delete;
template <class Access, class Effects>
void soft_pack_unchecked(Access a, bool shrink, Effects& effects) {
    const auto& dst = a.source();
    const auto& l = a.format();
    if (l.fixed())
        return; // admitted fixed packing is a no-op
    using W = typename Access::width_type;
    using O = typename Access::offset_type;
    unsigned from = 0, to = 0;
    for (unsigned rank = 0; rank < dst.size(); ++rank) {
        const auto reservation = [&] {
            if constexpr (Access::family == geometry::direct)
                return unsigned(detail::load<O>(a.geometry_data() + rank * sizeof(O))) - from;
            else
                return a.reserved(rank);
        }();
        const auto used = l.has_used() ? a.length(rank) : reservation;
        const auto width = a.live(rank) ? (shrink ? used : reservation) : 0;
        if (width && from != to) {
            emit(dst, effects, plane::payload, to, width);
            std::memmove(dst.windows().payload.data() + to, dst.windows().payload.data() + from,
                         width);
        }
        if constexpr (Access::family == geometry::width) {
            if (!(rank % 16)) {
                const auto offset = l.geometry_offset() + (rank / 16) * sizeof(O);
                emit(dst, effects, plane::directory, offset, sizeof(O));
                detail::store(dst.windows().directory.data() + offset, O(to));
            }
            emit(dst, effects, plane::lengths, a.width_offset(rank), sizeof(W));
            detail::store(dst.windows().lengths.data() + a.width_offset(rank), W(width));
        }
        to += width;
        if constexpr (Access::family == geometry::direct) {
            const auto offset = l.geometry_offset() + rank * sizeof(O);
            emit(dst, effects, plane::directory, offset, sizeof(O));
            detail::store(dst.windows().directory.data() + offset, O(to));
        }
        if (l.has_used() && !a.live(rank)) {
            const auto offset = l.used_offset() + a.width_offset(rank);
            emit(dst, effects, plane::lengths, offset, sizeof(W));
            detail::store(dst.windows().lengths.data() + offset, W(0));
        }
        from += reservation;
    }
    if (l.has_extent()) {
        emit(dst, effects, plane::control, 2, 4);
        detail::store(dst.windows().control.data() + 2, std::uint32_t(to));
    }
}
} // namespace boundlist_legacy
