#pragma once
#include <ikea/boundlist/author/read.h>
#include <ikea/effects.h>
#include <cstring>

namespace ikea::boundlist::sidecar::author {
/// Admitted writable source, exclusive authority and infallible effects. Hooks
/// cannot allocate, suspend or invalidate borrowed state. Source identity is
/// preserved even when these bodies are composed through StreamPack.
template <class Source, class Effects>
inline void emit(const Source& dst, Effects& effects, plane stream, unsigned offset,
                 unsigned size) {
    if (size)
        effects.before(dst, byte_write{unsigned(stream), offset, size});
}
template <class Source, class Effects>
void initialize_unchecked(const Source& dst, Effects& effects) {
    const auto& l = dst.format();
    emit(dst, effects, plane::directory, 0, l.directory_bytes());
    emit(dst, effects, plane::control, 0, l.control_bytes());
    if (l.directory_bytes())
        std::memset(dst.windows().directory.data(), 0, l.directory_bytes());
    std::memset(dst.windows().control.data(), 0, l.control_bytes());
}
template <class Source, class Effects> void initialize_unchecked(const Source&&, Effects&) = delete;
template <class Access, class Effects, class Place>
[[gnu::always_inline]] void append_unchecked(Access a, unsigned length, unsigned reservation,
                                             Effects& effects, Place&& place) {
    const auto& dst = a.source();
    const auto& l = a.format();
    const auto n = dst.size(), begin = dst.extent();
    using W = typename Access::width_type;
    using O = typename Access::offset_type;
    // Capture count/extent before additional stores: payload aliases cannot
    // force another dependent control load across this composition seam.
    [[clang::always_inline]] place(begin);
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
        detail::store(dst.windows().lengths.data() + slot + l.used_offset(), W(length));
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
[[gnu::always_inline]] void append_unchecked(Access a, unsigned length, unsigned reservation,
                                             Effects& effects) {
    append_unchecked(a, length, reservation, effects, [](unsigned) {});
}
template <class Access, class Effects, class Place>
[[gnu::always_inline]] void append_fixed_run_unchecked(Access a, unsigned count, Effects& effects,
                                                       Place&& place) {
    const auto& dst = a.source();
    const auto& l = a.format();
    const auto n = dst.size(), begin = dst.extent();
    [[clang::always_inline]] place(begin);
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
        detail::store(dst.windows().control.data() + 2,
                      std::uint32_t(begin + count * l.fixed_width()));
}
template <class Access, class Effects>
[[gnu::always_inline]] void append_fixed_run_unchecked(Access a, unsigned count, Effects& effects) {
    append_fixed_run_unchecked(a, count, effects, [](unsigned) {});
}
template <class Access, class Effects>
[[gnu::always_inline]] void set_used_unchecked(Access a, unsigned rank, unsigned length,
                                               Effects& effects) {
    using W = typename Access::width_type;
    const auto offset = a.format().used_offset() + a.width_offset(rank);
    emit(a.source(), effects, plane::lengths, offset, sizeof(W));
    detail::store(a.source().windows().lengths.data() + offset, W(length));
}
template <class Access, class Effects>
[[gnu::always_inline]] void replace_used_unchecked(Access a, unsigned rank, unsigned length,
                                                   Effects& effects) {
    if (a.format().has_used())
        set_used_unchecked(a, rank, length, effects);
}
template <class Source, class Effects>
void retire_unchecked(const Source& dst, unsigned rank, Effects& effects) {
    emit(dst, effects, plane::directory, rank / 8, 1);
    dst.windows().directory[rank / 8] |= byte(1u << (rank % 8));
}
template <class Source, class Effects>
void retire_unchecked(const Source&&, unsigned, Effects&) = delete;
template <class Access, class Effects>
[[gnu::always_inline]] void pack_slot_unchecked(Access a, unsigned rank, unsigned to,
                                                unsigned width, Effects& effects) {
    const auto& dst = a.source();
    const auto& l = a.format();
    using W = typename Access::width_type;
    using O = typename Access::offset_type;
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
}
template <class Access, class Effects>
[[gnu::always_inline]] void finish_pack_unchecked(Access a, unsigned extent, Effects& effects) {
    if (a.format().has_extent()) {
        emit(a.source(), effects, plane::control, 2, 4);
        detail::store(a.source().windows().control.data() + 2, std::uint32_t(extent));
    }
}
/// One packing law for metadata-only and payload compositions. Move performs
/// only the caller's extra work; metadata/effects use this same traversal.
template <class Access, class Effects, class Move>
[[gnu::always_inline]] void soft_pack_unchecked(Access a, bool shrink, Effects& effects,
                                                Move&& move) {
    if (a.format().fixed())
        return;
    using O = typename Access::offset_type;
    // The admitted move/effects hooks cannot change the assigned count. Keep
    // that invariant explicit across payload stores and journal writes.
    const auto n = a.size();
    unsigned from = 0, to = 0;
    for (unsigned rank = 0; rank < n; ++rank) {
        const auto reservation = [&] {
            if constexpr (Access::family == geometry::direct)
                return unsigned(detail::load<O>(a.geometry_data() + rank * sizeof(O))) - from;
            else
                return a.reserved(rank);
        }();
        const auto used = a.format().has_used() ? a.length(rank) : reservation;
        const auto width = a.live(rank) ? (shrink ? used : reservation) : 0;
        if (width && from != to) {
            [[clang::always_inline]] move(from, to, width);
        }
        pack_slot_unchecked(a, rank, to, width, effects);
        from += reservation;
        to += width;
    }
    finish_pack_unchecked(a, to, effects);
}
} // namespace ikea::boundlist::sidecar::author
