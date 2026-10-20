#pragma once
#include <ikea/boundlist/author/read.h>
#include <ikea/effects.h>
#include <ikea/boundlist/detail/rewrite.h>
#include <cstring>

namespace ikea::boundlist::sidecar::author {
template <class Source, class Effects>
inline void emit(const Source &dst, Effects &effects, plane stream, unsigned offset,
                 unsigned size) {
    if (size)
        effects.before(dst, byte_write{unsigned(stream), offset, size});
}
template <class Source, class Effects>
void initialize_unchecked(const Source &dst, Effects &effects) {
    const auto &l = dst.format();
    emit(dst, effects, plane::directory, 0, l.directory_bytes());
    emit(dst, effects, plane::control, 0, l.control_bytes());
    if (l.directory_bytes())
        std::memset(dst.windows().directory.data(), 0, l.directory_bytes());
    std::memset(dst.windows().control.data(), 0, l.control_bytes());
}
template <class Source, class Effects>
void initialize_unchecked(const Source &&, Effects &) = delete;
/// Admitted nonempty in-capacity run and present D bitmap. Only population bits
/// are changed; the enclosing owner supplies any payload and count work.
template <class Source, class Effects>
[[gnu::always_inline]] void make_live_unchecked(const Source &dst, unsigned first, unsigned count,
                                                Effects &effects) {
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
void retire_unchecked(const Source &dst, unsigned rank, Effects &effects) {
    emit(dst, effects, plane::directory, rank / 8, 1);
    dst.windows().directory[rank / 8] |= byte(1u << (rank % 8));
}
template <class Source, class Effects>
void retire_unchecked(const Source &&, unsigned, Effects &) = delete;
/// Admitted scalar field replacement. The caller has already checked the
/// complete edit; effects name the original source, never a projected column.
template <class T, class Source, class Effects>
[[gnu::always_inline]] void set_field(const Source &source, plane p, unsigned offset, T value,
                                      Effects &effects) {
    emit(source, effects, p, offset, sizeof(T));
    const auto window = p == plane::directory ? source.windows().directory
                        : p == plane::lengths ? source.windows().lengths
                                              : source.windows().control;
    detail::store(window.data() + offset, value);
}
template <class T, class Access, class Effects>
[[gnu::always_inline]] void add_column(Access a, plane p, unsigned offset, unsigned first,
                                       unsigned limit, unsigned padded, int delta,
                                       Effects &effects) {
    const auto &windows = a.source().windows();
    auto *data = (p == plane::directory ? windows.directory
                  : p == plane::lengths ? windows.lengths
                                        : windows.control)
                     .data() +
                 offset;
    boundlist::detail::add_fields<T>(
        data, first, limit, padded, delta,
        [&](unsigned from, unsigned size) { emit(a.source(), effects, p, offset + from, size); });
}
/// Change one end while preserving later ends. In contrast, resizing a slot
/// translates every subsequent endpoint. Both kernels leave rank/population alone.
template <class Access, class Effects>
[[gnu::always_inline]] void set_boundary_unchecked(Access a, unsigned rank, unsigned point,
                                                   int delta, Effects &effects) {
    using W = typename Access::width_type;
    using O = typename Access::offset_type;
    const auto &dst = a.source();
    const auto &l = a.format();
    if constexpr (Access::family == geometry::absolute_ends)
        set_field(dst, plane::directory, l.geometry_offset() + rank * sizeof(O), O(point), effects);
    else {
        const auto offset = a.width_offset(rank);
        const auto old = detail::load<W>(dst.windows().lengths.data() + offset);
        set_field(dst, plane::lengths, offset, W(old + delta), effects);
        if (rank + 1 < a.size()) {
            if constexpr (Access::family == geometry::sizes) {
                const auto next = a.width_offset(rank + 1);
                const auto width = detail::load<W>(dst.windows().lengths.data() + next);
                set_field(dst, plane::lengths, next, W(width - delta), effects);
            }
            if ((rank + 1) % 16 == 0) {
                const auto group = (rank + 1) / 16;
                const auto at = l.geometry_offset() + group * sizeof(O);
                const auto origin = detail::load<O>(dst.windows().directory.data() + at);
                set_field(dst, plane::directory, at, O(origin + delta), effects);
                if constexpr (Access::family == geometry::relative_ends)
                    add_column<W>(a, plane::lengths, a.width_offset(rank + 1), 0,
                                  std::min(16u, a.size() - rank - 1), 16, -delta, effects);
            }
        }
        if (rank + 1 == a.size())
            // The last endpoint is already the coherent extent. Do not make
            // Sizes callers reconstruct a prefix merely to publish it.
            set_field(dst, plane::control, 2, std::uint32_t(dst.extent() + delta), effects);
    }
}
template <class Access, class Effects>
[[gnu::always_inline]] void resize_unchecked(Access a, unsigned rank, unsigned size, int delta,
                                             Effects &effects) {
    using W = typename Access::width_type;
    using O = typename Access::offset_type;
    const auto &l = a.format();
    const auto &dst = a.source();
    if constexpr (Access::family == geometry::absolute_ends)
        add_column<O>(a, plane::directory, l.geometry_offset(), rank, a.size(), l.capacity(), delta,
                      effects);
    else {
        if constexpr (Access::family == geometry::sizes)
            set_field(dst, plane::lengths, a.width_offset(rank), W(size), effects);
        else {
            const auto first = rank / 16 * 16;
            add_column<W>(a, plane::lengths, a.width_offset(first), rank % 16,
                          std::min(16u, a.size() - first), 16, delta, effects);
        }
        add_column<O>(a, plane::directory, l.geometry_offset(), rank / 16 + 1, (a.size() + 15) / 16,
                      l.capacity() / 16, delta, effects);
        set_field(dst, plane::control, 2, std::uint32_t(dst.extent() + delta), effects);
    }
}
/// Sequential numeric encoder. Carries the new origin/local end across the
/// caller's chosen widths, avoiding a checkpoint or preceding-field reload per
/// slot. It chooses neither sizes nor movement/population policy. No count store.
template <class Access> class encoder {
    Access access_;
    unsigned rank_, position_, local_;

  public:
    encoder(Access a, unsigned first, unsigned begin)
        : access_(a), rank_(first), position_(begin), local_(0) {
        if constexpr (Access::family == geometry::relative_ends)
            if (!a.format().fixed() && first % 16)
                local_ = begin - detail::load<typename Access::offset_type>(
                                     a.geometry_data() +
                                     (first / 16) * sizeof(typename Access::offset_type));
    }
    Access access() const noexcept { return access_; }
    unsigned rank() const noexcept { return rank_; }
    unsigned position() const noexcept { return position_; }
    template <class Effects>
    [[gnu::always_inline]] void append(unsigned width, Effects &effects, bool covered = false) {
        if (!access_.format().fixed()) {
            using O = typename Access::offset_type;
            using W = typename Access::width_type;
            const auto &dst = access_.source();
            if constexpr (Access::family == geometry::absolute_ends) {
                const auto offset = access_.format().geometry_offset() + rank_ * sizeof(O);
                emit(dst, effects, plane::directory, offset, sizeof(O));
                detail::store(dst.windows().directory.data() + offset, O(position_ + width));
            } else {
                if (!(rank_ % 16)) {
                    local_ = 0;
                    const auto offset =
                        access_.format().geometry_offset() + (rank_ / 16) * sizeof(O);
                    emit(dst, effects, plane::directory, offset, sizeof(O));
                    detail::store(dst.windows().directory.data() + offset, O(position_));
                }
                local_ += width;
                const auto offset = access_.width_offset(rank_);
                if (!covered)
                    emit(dst, effects, plane::lengths, offset, sizeof(W));
                detail::store(dst.windows().lengths.data() + offset,
                              W(Access::family == geometry::relative_ends ? local_ : width));
            }
        }
        ++rank_;
        position_ += width;
    }
};
template <class Access, class Effects>
[[gnu::always_inline]] void finish_rewrite_unchecked(Access a, unsigned extent, Effects &effects) {
    if (a.format().has_extent()) {
        emit(a.source(), effects, plane::control, 2, 4);
        detail::store(a.source().windows().control.data() + 2, std::uint32_t(extent));
    }
}
template <class Access, class Effects>
[[gnu::always_inline]] void publish_size_unchecked(Access a, unsigned count, unsigned extent,
                                                   Effects &effects) {
    const auto &dst = a.source();
    emit(dst, effects, plane::control, 0, a.format().control_bytes());
    detail::store(dst.windows().control.data(), std::uint16_t(count));
    if (a.format().has_extent())
        detail::store(dst.windows().control.data() + 2, std::uint32_t(extent));
}
template <class Access, class Effects>
[[gnu::always_inline]] void append_unchecked(Access a, unsigned reservation, Effects &effects) {
    const auto n = a.size(), begin = a.source().extent();
    const bool group =
        !a.format().fixed() && Access::family != geometry::absolute_ends && !(n % 16);
    if (group) {
        const auto offset = a.width_offset(n);
        constexpr unsigned size = 16 * sizeof(typename Access::width_type);
        emit(a.source(), effects, plane::lengths, offset, size);
        std::memset(a.source().windows().lengths.data() + offset, 0, size);
    }
    if (!a.format().fixed())
        encoder(a, n, begin).append(reservation, effects, group);
    if (a.format().has_tombstones())
        make_live_unchecked(a.source(), n, 1, effects);
    publish_size_unchecked(a, n + 1, begin + reservation, effects);
}
} // namespace ikea::boundlist::sidecar::author
