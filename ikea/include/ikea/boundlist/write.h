#pragma once
#include <ikea/boundlist/author/write.h>

namespace ikea::boundlist::sidecar {
namespace detail {
template <class Effects> bool room(const Effects& effects, unsigned n) {
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
template <class Format> inline bool accepts(const Format& l, unsigned reservation) {
    return reservation <= l.schema().maximum && (!l.fixed() || reservation == l.fixed_width());
}
inline unsigned append_effects(const layout& l, unsigned n) {
    return (!l.fixed() && (l.schema().family == geometry::direct || !(n % 16))) +
           (!l.fixed() && l.schema().family == geometry::width) + l.has_tombstones() + 1;
}
} // namespace detail
/// Admitted contents and caller-held exclusive authority. Source/format/effect
/// output controls cannot alias writable windows; hooks are infallible. Refusal
/// preserves all bytes and effects. No allocation or publication occurs here.
template <class Effects>
[[nodiscard]] std::expected<void, error> initialize(const view& dst, Effects& effects) {
    if (!detail::room(effects, 1 + (dst.format().directory_bytes() != 0)))
        return std::unexpected(error::effects);
    author::initialize_unchecked(dst, effects);
    return {};
}
template <class Effects>
[[nodiscard]] std::expected<void, error> append(const view& dst, unsigned reservation,
                                                Effects& effects) {
    const auto& l = dst.format();
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
[[nodiscard]] std::expected<void, error> retire(const view& dst, unsigned rank, Effects& effects) {
    if (rank >= dst.size())
        return std::unexpected(error::rank);
    if (!dst.format().has_tombstones())
        return std::unexpected(error::description);
    if (!detail::room(effects, 1))
        return std::unexpected(error::effects);
    author::retire_unchecked(dst, rank, effects);
    return {};
}
template <class Effects> void initialize(const view&&, Effects&) = delete;
template <class Effects> void append(const view&&, unsigned, Effects&) = delete;
template <class Effects> void retire(const view&&, unsigned, Effects&) = delete;
} // namespace ikea::boundlist::sidecar
