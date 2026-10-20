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
inline bool accepts(const layout& l, unsigned length, unsigned reservation) {
    return length <= reservation && reservation <= l.schema().maximum &&
           (!l.fixed() || reservation == l.fixed_width()) &&
           (l.has_used() || length == reservation);
}
inline unsigned append_effects(const layout& l, unsigned n) {
    const bool r = !l.fixed() && l.schema().family == geometry::width;
    const unsigned fields =
        n % 16 ? unsigned(r) + l.has_used()
               : unsigned(r || l.has_used()) +
                     (r && l.has_used() && l.schema().lengths == placement::separated);
    return (!l.fixed() && (l.schema().family == geometry::direct || !(n % 16))) + fields +
           l.has_tombstones() + 1;
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
[[nodiscard]] std::expected<void, error> append(const view& dst, unsigned length,
                                                unsigned reservation, Effects& effects) {
    const auto& l = dst.format();
    if (!detail::accepts(l, length, reservation))
        return std::unexpected(error::value);
    const auto n = dst.size(), begin = dst.extent();
    if (n >= l.capacity() || l.lengths_bytes(n + 1) > dst.windows().lengths.size() ||
        begin > l.schema().byte_limit || reservation > l.schema().byte_limit - begin)
        return std::unexpected(error::capacity);
    if (!detail::room(effects, detail::append_effects(l, n)))
        return std::unexpected(error::effects);
    author::dispatch(dst,
                     [&](auto a) { author::append_unchecked(a, length, reservation, effects); });
    return {};
}
template <class Effects>
[[nodiscard]] std::expected<void, error> replace(const view& dst, unsigned rank, unsigned length,
                                                 Effects& effects) {
    if (rank >= dst.size())
        return std::unexpected(error::rank);
    if (!dst.live_unchecked(rank))
        return std::unexpected(error::retired);
    return author::dispatch(dst, [&](auto a) -> std::expected<void, error> {
        if (!detail::accepts(a.format(), length, a.reserved(rank)))
            return std::unexpected(error::value);
        if (!detail::room(effects, a.format().has_used()))
            return std::unexpected(error::effects);
        author::replace_used_unchecked(a, rank, length, effects);
        return {};
    });
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
/// Rewrites metadata while preserving ranks. The owner must move/rebuild any
/// corresponding payload/children in the same private version before publication.
template <class Effects>
[[nodiscard]] std::expected<void, error> soft_pack(const view& dst, bool shrink, Effects& effects) {
    return author::dispatch(dst, [&](auto a) -> std::expected<void, error> {
        const auto& l = a.format();
        if (l.fixed()) {
            for (unsigned r = 0; r < dst.size(); ++r)
                if ((l.fixed_width() && !a.live(r)) || (shrink && a.length(r) != a.reserved(r)))
                    return std::unexpected(error::description);
            return {};
        }
        if (!detail::room(effects, l.pack_effect_capacity(dst.size())))
            return std::unexpected(error::effects);
        author::soft_pack_unchecked(a, shrink, effects, [](unsigned, unsigned, unsigned) {});
        return {};
    });
}
template <class Effects> void initialize(const view&&, Effects&) = delete;
template <class Effects> void append(const view&&, unsigned, unsigned, Effects&) = delete;
template <class Effects> void replace(const view&&, unsigned, unsigned, Effects&) = delete;
template <class Effects> void retire(const view&&, unsigned, Effects&) = delete;
template <class Effects> void soft_pack(const view&&, bool, Effects&) = delete;
} // namespace ikea::boundlist::sidecar
