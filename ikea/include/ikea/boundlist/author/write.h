#pragma once
#include <ikea/boundlist/author/read.h>
#include <ikea/effects.h>
#include <cstring>

namespace ikea::boundlist::sidecar::author {
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
/// Admitted nonempty in-capacity run and present D bitmap. Only population bits
/// are changed; the enclosing owner supplies any payload and count work.
template <class Source, class Effects>
[[gnu::always_inline]] void make_live_unchecked(const Source& dst, unsigned first, unsigned count,
                                                Effects& effects) {
    if (count == 1) {
        emit(dst, effects, plane::directory, first / 8, 1);
        dst.windows().directory[first / 8] &= byte(~(1u << (first % 8)) & 255u);
        return;
    }
    const auto lo = first / 8, hi = (first + count - 1) / 8;
    const auto low = unsigned(255u << (first % 8)) & 255u;
    const auto high = 255u >> (7 - ((first + count - 1) % 8));
    emit(dst, effects, plane::directory, lo, hi - lo + 1);
    if (lo == hi)
        dst.windows().directory[lo] &= byte(~(low & high) & 255u);
    else {
        dst.windows().directory[lo] &= byte(~low & 255u);
        if (hi > lo + 1)
            std::memset(dst.windows().directory.data() + lo + 1, 0, hi - lo - 1);
        dst.windows().directory[hi] &= byte(~high & 255u);
    }
}
template <class Source, class Effects>
void retire_unchecked(const Source& dst, unsigned rank, Effects& effects) {
    emit(dst, effects, plane::directory, rank / 8, 1);
    dst.windows().directory[rank / 8] |= byte(1u << (rank % 8));
}
template <class Source, class Effects>
void retire_unchecked(const Source&&, unsigned, Effects&) = delete;
/// Encode a chosen interval during a caller-coordinated geometry rewrite.
/// Variable geometry is admitted by the caller, which has captured any old
/// boundaries still needed. No movement, shrinking or rank policy lives here.
template <class Access, class Effects>
[[gnu::always_inline]] void write_origin_unchecked(Access a, unsigned rank, unsigned to,
                                                   unsigned width, Effects& effects) {
    const auto& dst = a.source();
    const auto& l = a.format();
    using O = typename Access::offset_type;
    if constexpr (Access::family == geometry::width) {
        if (!(rank % 16)) {
            const auto offset = l.geometry_offset() + (rank / 16) * sizeof(O);
            emit(dst, effects, plane::directory, offset, sizeof(O));
            detail::store(dst.windows().directory.data() + offset, O(to));
        }
    }
    to += width;
    if constexpr (Access::family == geometry::direct) {
        const auto offset = l.geometry_offset() + rank * sizeof(O);
        emit(dst, effects, plane::directory, offset, sizeof(O));
        detail::store(dst.windows().directory.data() + offset, O(to));
    }
}
/// covered means the complete R store already has an issued write range in
/// this same operation. The caller admits variable geometry.
template <class Access, class Effects>
[[gnu::always_inline]] void write_width_unchecked(Access a, unsigned rank, unsigned width,
                                                  Effects& effects, bool covered = false) {
    if constexpr (Access::family == geometry::width) {
        using W = typename Access::width_type;
        if (!covered)
            emit(a.source(), effects, plane::lengths, a.width_offset(rank), sizeof(W));
        detail::store(a.source().windows().lengths.data() + a.width_offset(rank), W(width));
    }
}
template <class Access, class Effects>
[[gnu::always_inline]] void write_slot_unchecked(Access a, unsigned rank, unsigned to,
                                                 unsigned width, Effects& effects,
                                                 bool covered = false) {
    write_origin_unchecked(a, rank, to, width, effects);
    write_width_unchecked(a, rank, width, effects, covered);
}
template <class Access, class Effects>
[[gnu::always_inline]] void finish_rewrite_unchecked(Access a, unsigned extent, Effects& effects) {
    if (a.format().has_extent()) {
        emit(a.source(), effects, plane::control, 2, 4);
        detail::store(a.source().windows().control.data() + 2, std::uint32_t(extent));
    }
}
template <class Access, class Effects>
[[gnu::always_inline]] void publish_size_unchecked(Access a, unsigned count, unsigned extent,
                                                   Effects& effects) {
    const auto& dst = a.source();
    emit(dst, effects, plane::control, 0, a.format().control_bytes());
    detail::store(dst.windows().control.data(), std::uint16_t(count));
    if (a.format().has_extent())
        detail::store(dst.windows().control.data() + 2, std::uint32_t(extent));
}
template <class Access, class Effects>
[[gnu::always_inline]] void append_unchecked(Access a, unsigned reservation, Effects& effects) {
    const auto n = a.size(), begin = a.source().extent();
    const bool group = !a.format().fixed() && Access::family == geometry::width && !(n % 16);
    if (group) {
        const auto offset = a.width_offset(n);
        constexpr unsigned size = 16 * sizeof(typename Access::width_type);
        emit(a.source(), effects, plane::lengths, offset, size);
        std::memset(a.source().windows().lengths.data() + offset, 0, size);
    }
    if (!a.format().fixed())
        write_slot_unchecked(a, n, begin, reservation, effects, group);
    if (a.format().has_tombstones())
        make_live_unchecked(a.source(), n, 1, effects);
    publish_size_unchecked(a, n + 1, begin + reservation, effects);
}
} // namespace ikea::boundlist::sidecar::author
