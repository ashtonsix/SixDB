#pragma once
#include <ikea/streampack/detail/compact/author/write.h>

namespace ikea::streampack::detail::compact {
namespace detail {
template <class Effects> bool room(const Effects &effects, std::size_t n) {
    if constexpr (requires {
                      effects.used;
                      effects.storage;
                  })
        if (effects.used > effects.storage.size())
            return false;
    if constexpr (requires { effects.remaining(); })
        return effects.remaining() >= n;
    else
        return true;
}
template <class Format> inline bool accepts(const Format &l, unsigned reservation) {
    return reservation <= l.schema().maximum && (!l.fixed() || reservation == l.fixed_width());
}
inline unsigned append_effects(const layout &l, unsigned n) {
    return (!l.fixed() && (l.schema().family == geometry::absolute_ends || !(n % 16))) +
           (!l.fixed() && l.schema().family != geometry::absolute_ends) + l.has_tombstones() + 1;
}
} // namespace detail
/// Admitted contents and caller-held exclusive authority. Source/format/effect
/// output controls cannot alias writable windows; hooks are infallible. Refusal
/// preserves all bytes and effects. No allocation or publication occurs here.
template <class Effects>
[[nodiscard]] std::expected<void, error> initialize(const view &dst, Effects &effects) {
    if (!detail::room(effects, 1 + (dst.format().directory_bytes() != 0)))
        return std::unexpected(error::effects);
    author::initialize_unchecked(dst, effects);
    return {};
}
template <class Effects>
[[nodiscard]] std::expected<void, error> append(const view &dst, unsigned reservation,
                                                Effects &effects) {
    const auto &l = dst.format();
    if (!detail::accepts(l, reservation))
        return std::unexpected(error::value);
    const auto n = dst.size(), begin = dst.extent();
    if (n >= l.capacity() || l.lengths_bytes(n + 1) > dst.windows().lengths.size() ||
        begin > l.extent_limit() || reservation > l.extent_limit() - begin)
        return std::unexpected(error::capacity);
    if (!detail::room(effects, detail::append_effects(l, n)))
        return std::unexpected(error::effects);
    author::dispatch(dst, [&](auto a) { author::append_unchecked(a, reservation, effects); });
    return {};
}
template <class Effects>
[[nodiscard]] std::expected<void, error> retire(const view &dst, unsigned rank, Effects &effects) {
    if (rank >= dst.size())
        return std::unexpected(error::range);
    if (!dst.format().has_tombstones())
        return std::unexpected(error::description);
    if (!detail::room(effects, 1))
        return std::unexpected(error::effects);
    author::retire_unchecked(dst, rank, effects);
    return {};
}
template <class Effects> void initialize(const view &&, Effects &) = delete;
template <class Effects> void append(const view &&, unsigned, Effects &) = delete;
template <class Effects> void retire(const view &&, unsigned, Effects &) = delete;

/// Numeric geometry edits; the owner coordinates any associated payload/child
/// movement. These calls are intentionally absent from StreamPack's value API.
template <class Effects>
[[nodiscard]] std::expected<void, error> set_boundary(const view &dst, unsigned rank,
                                                      std::uint32_t point, Effects &effects) {
    if (rank >= dst.size())
        return std::unexpected(error::range);
    return author::dispatch(dst, [&](auto a) -> std::expected<void, error> {
        const auto old = a.at(rank);
        if (point == old)
            return {};
        if (dst.format().fixed())
            return std::unexpected(error::description);
        const auto before = rank ? a.at(rank - 1) : 0;
        const bool next = rank + 1 < a.size();
        const auto after = next ? a.at(rank + 1) : point;
        if (point < before || point > after || point - before > dst.format().schema().maximum ||
            (next && after - point > dst.format().schema().maximum) ||
            point > dst.format().extent_limit())
            return std::unexpected(error::value);
        unsigned writes = 1;
        if constexpr (decltype(a)::family != geometry::absolute_ends) {
            writes += !next;
            if constexpr (decltype(a)::family == geometry::sizes)
                writes += next;
            if (next && !((rank + 1) % 16)) {
                ++writes;
                if constexpr (decltype(a)::family == geometry::relative_ends)
                    ++writes;
            }
        }
        if (!detail::room(effects, writes))
            return std::unexpected(error::effects);
        author::set_boundary_unchecked(a, rank, point, int(point) - int(old), effects);
        return {};
    });
}
template <class Effects>
[[nodiscard]] std::expected<void, error> resize(const view &dst, unsigned rank, unsigned size,
                                                Effects &effects) {
    if (rank >= dst.size())
        return std::unexpected(error::range);
    if (!detail::accepts(dst.format(), size))
        return std::unexpected(error::value);
    return author::dispatch(dst, [&](auto a) -> std::expected<void, error> {
        const int delta = int(size) - int(a.slot_size(rank));
        if (!delta)
            return {};
        const auto end = std::int64_t(dst.extent()) + delta;
        if (end < 0 || end > dst.format().extent_limit())
            return std::unexpected(error::capacity);
        const unsigned writes = decltype(a)::family == geometry::absolute_ends
                                    ? 1
                                    : 2 + unsigned(rank / 16 + 1 < (a.size() + 15) / 16);
        if (!detail::room(effects, writes))
            return std::unexpected(error::effects);
        author::resize_unchecked(a, rank, size, delta, effects);
        return {};
    });
}
template <class Effects>
void set_boundary(const view &&, unsigned, std::uint32_t, Effects &) = delete;
template <class Effects> void resize(const view &&, unsigned, unsigned, Effects &) = delete;
} // namespace ikea::streampack::detail::compact
