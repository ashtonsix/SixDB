#pragma once
#include <ikea/streampack/author/write.h>
#include <ikea/streampack/detail/compact/write.h>

namespace ikea::streampack {
template <class Effects> requires ikea::effects<Effects, view>
void initialize_unchecked(const view &dst, Effects &effects) {
    author::metadata_effects mapped{effects};
    ikea::streampack::detail::compact::author::initialize_unchecked(dst, mapped);
}
template <class Effects> requires ikea::effects<Effects, view>
void initialize_unchecked(const view &&, Effects &) = delete;

template <class Access, class Effects>
    requires ikea::effects<Effects, view> && requires(const Access &a) { a.source(); }
[[gnu::always_inline]] void append_unchecked(Access a, bytes input, unsigned reservation,
                                             Effects &effects) {
    author::value_encoder encoder(a, a.size(), a.source().extent(), effects);
    encoder.append(input, reservation);
    encoder.finish();
}

template <class Access, class Effects>
    requires ikea::effects<Effects, view> && requires(const Access &a) { a.source(); }
void replace_unchecked(Access a, unsigned rank, bytes input, Effects &effects) {
    const auto &dst = a.source();
    const auto begin = a.start(rank);
    author::emit(dst, effects, plane::payload, begin, input.size());
    if (!input.empty())
        std::memmove(dst.windows().payload.data() + begin, input.data(), input.size());
    author::replace_used_unchecked(a, rank, input.size(), effects);
}

template <class Effects> requires ikea::effects<Effects, view>
void retire_unchecked(const view &dst, unsigned rank, Effects &effects) {
    author::metadata_effects mapped{effects};
    ikea::streampack::detail::compact::author::retire_unchecked(dst, rank, mapped);
}
template <class Effects> requires ikea::effects<Effects, view>
void retire_unchecked(const view &&, unsigned, Effects &) = delete;

/// One fused value traversal. Capture original R before rewriting preceding
/// endpoints; U determines shrinking only here, above the geometry kernels.
template <class Access, class Effects>
    requires ikea::effects<Effects, view> && requires(const Access &a) { a.source(); }
[[gnu::always_inline]] void soft_pack_unchecked(Access a, bool shrink, Effects &effects) {
    const auto &dst = a.source();
    author::metadata_effects mapped{effects};
    if (a.format().fixed())
        return;
    const auto n = a.size();
    ikea::streampack::detail::compact::author::encoder encoder(a, 0, 0);
    a.slots(0, n, 0,
            [&](unsigned rank, unsigned from, unsigned reservation) __attribute__((always_inline)) {
                const auto used = a.format().has_used() ? a.length(rank) : reservation;
                const auto width = a.live(rank) ? (shrink ? used : reservation) : 0;
                const auto to = encoder.position();
                if (width && from != to) {
                    author::emit(dst, effects, plane::payload, to, width);
                    std::memmove(dst.windows().payload.data() + to,
                                 dst.windows().payload.data() + from, width);
                }
                encoder.append(width, mapped);
                if (a.format().has_used() && !a.live(rank))
                    author::set_used_unchecked(a, rank, 0, effects);
            });
    ikea::streampack::detail::compact::author::finish_rewrite_unchecked(a, encoder.position(), mapped);
}
/// Reuse a complete owner-proved command without repeating admission.
/// Typed author access overloads retain an already selected schema body.
template <class Effects> requires ikea::effects<Effects, view>
void append_unchecked(const view &dst, bytes input, unsigned reservation, Effects &effects) {
    author::visit(dst, [&](auto a) { append_unchecked(a, input, reservation, effects); });
}
template <class Effects> requires ikea::effects<Effects, view>
void replace_unchecked(const view &dst, unsigned rank, bytes input, Effects &effects) {
    author::visit(dst, [&](auto a) { replace_unchecked(a, rank, input, effects); });
}
template <class Effects> requires ikea::effects<Effects, view>
void soft_pack_unchecked(const view &dst, bool shrink, Effects &effects) {
    author::visit(dst, [&](auto a) { soft_pack_unchecked(a, shrink, effects); });
}
template <class Effects> void append_unchecked(const view &&, bytes, unsigned, Effects &) = delete;
template <class Effects> void replace_unchecked(const view &&, unsigned, bytes, Effects &) = delete;
template <class Effects> void soft_pack_unchecked(const view &&, bool, Effects &) = delete;

namespace detail {
inline bool accepts(const layout &l, unsigned length, unsigned reservation) {
    return length <= reservation && ikea::streampack::detail::compact::detail::accepts(l, reservation) &&
           (l.has_used() || length == reservation);
}
inline unsigned append_effects(const layout &l, unsigned n) {
    const bool r = !l.fixed() && l.schema().family != geometry::absolute_ends;
    const unsigned fields =
        n % 16 ? unsigned(r) + l.has_used()
               : unsigned(r || l.has_used()) +
                     (r && l.has_used() && l.schema().lengths == placement::separated);
    return (!l.fixed() && (l.schema().family == geometry::absolute_ends || !(n % 16))) + fields +
           l.has_tombstones() + 1;
}
using ikea::streampack::detail::compact::detail::room;
/// Payload aliases are supported while the binding stays live. Metadata and
/// effect-output aliases would let issued stores destroy remaining input.
template <class Effects> bool input_alias(const view &dst, bytes input, const Effects &effects) {
    const auto planes = dst.windows().planes();
    for (unsigned i = 1; i < planes.size(); ++i)
        if (overlaps(input.data(), input.size(), planes[i].data(), planes[i].size()))
            return true;
    if constexpr (requires { effects.storage; })
        if (overlaps(input.data(), input.size(), effects.storage.data(),
                     effects.storage.size_bytes()))
            return true;
    return overlaps(input.data(), input.size(), &dst, sizeof(dst)) ||
           overlaps(input.data(), input.size(), &dst.format(), sizeof(layout));
}
} // namespace detail
/// Conservative additional journal records for one admitted command. These do
/// not reserve records or validate its coordinates, input or owner authority.
inline std::size_t effect_capacity_initialize(const view &dst) noexcept {
    return 1 + (dst.format().directory_bytes() != 0);
}
inline std::size_t effect_capacity_append(const view &dst, bytes, unsigned reservation) noexcept {
    return (reservation != 0) + detail::append_effects(dst.format(), dst.size());
}
inline std::size_t effect_capacity_replace(const view &dst, unsigned, bytes input) noexcept {
    return (!input.empty()) + dst.format().has_used();
}
inline std::size_t effect_capacity_retire(const view &, unsigned) noexcept { return 1; }
inline std::size_t effect_capacity_soft_pack(const view &dst, bool) noexcept {
    return dst.format().fixed() ? 0 : std::size_t(dst.size()) * 4 + 1;
}
inline std::size_t effect_capacity_transfer(const view &, std::size_t count) noexcept {
    return 2 + count * 8;
}

/// Checked commands accept exactly journal; the constrained template keeps
/// schema bodies lazy and visible for caller inlining. They preserve supplied
/// bytes and effects on refusal. Existing-content mutations require admitted
/// contents; initialize establishes contents in fresh storage. Effect storage
/// and source/view/format controls are disjoint from outputs. The owner
/// establishes exclusive authority before entry.
template <class Effects> requires std::same_as<Effects, journal>
[[nodiscard]] std::expected<void, error> initialize(const view &dst, Effects &effects) {
    if (!detail::room(effects, effect_capacity_initialize(dst)))
        return std::unexpected(error::effects);
    initialize_unchecked(dst, effects);
    return {};
}
template <class Effects> requires std::same_as<Effects, journal>
[[nodiscard]] std::expected<void, error> append(const view &dst, bytes input, unsigned reservation,
                                                Effects &effects) {
    const auto &l = dst.format();
    if (input.size() > l.schema().maximum || !detail::accepts(l, input.size(), reservation))
        return std::unexpected(error::value);
    const auto n = dst.size(), begin = dst.extent();
    if (n >= l.capacity() || l.lengths_bytes(n + 1) > dst.windows().lengths.size() ||
        begin > l.schema().byte_limit || reservation > l.schema().byte_limit - begin ||
        begin > dst.windows().payload.size() || reservation > dst.windows().payload.size() - begin)
        return std::unexpected(error::capacity);
    if (detail::input_alias(dst, input, effects))
        return std::unexpected(error::overlap);
    const auto count = effect_capacity_append(dst, input, reservation);
    if (!detail::room(effects, count))
        return std::unexpected(error::effects);
    author::visit(dst,
                     [&](auto a) { append_unchecked(a, input, reservation, effects); });
    return {};
}
template <class Effects> requires std::same_as<Effects, journal>
[[nodiscard]] std::expected<void, error> replace(const view &dst, unsigned rank, bytes input,
                                                 Effects &effects) {
    if (rank >= dst.size())
        return std::unexpected(error::range);
    if (!dst.live_unchecked(rank))
        return std::unexpected(error::retired);
    if (input.size() > dst.format().schema().maximum)
        return std::unexpected(error::value);
    if (detail::input_alias(dst, input, effects))
        return std::unexpected(error::overlap);
    if (!detail::room(effects, effect_capacity_replace(dst, rank, input)))
        return std::unexpected(error::effects);
    return author::visit(dst, [&](auto a) -> std::expected<void, error> {
        if (!detail::accepts(a.format(), input.size(), a.reserved(rank)))
            return std::unexpected(error::value);
        replace_unchecked(a, rank, input, effects);
        return {};
    });
}
template <class Effects> requires std::same_as<Effects, journal>
[[nodiscard]] std::expected<void, error> retire(const view &dst, unsigned rank, Effects &effects) {
    if (rank >= dst.size())
        return std::unexpected(error::range);
    if (!dst.format().has_tombstones())
        return std::unexpected(error::description);
    if (!detail::room(effects, 1))
        return std::unexpected(error::effects);
    retire_unchecked(dst, rank, effects);
    return {};
}
/// Stable in-place packing. All ranks remain; retired reservations become zero.
/// Optional shrinking discards live slack. Fixed nonzero geometry can only pack
/// when that would leave every reservation unchanged.
template <class Effects> requires std::same_as<Effects, journal>
[[nodiscard]] std::expected<void, error> soft_pack(const view &dst, bool shrink, Effects &effects) {
    return author::visit(dst, [&](auto a) -> std::expected<void, error> {
        if (a.format().fixed()) {
            for (unsigned r = 0; r < dst.size(); ++r)
                if ((a.format().fixed_width() && !a.live(r)) ||
                    (shrink && a.length(r) != a.reserved(r)))
                    return std::unexpected(error::description);
            return {};
        }
        if (!detail::room(effects, effect_capacity_soft_pack(dst, shrink)))
            return std::unexpected(error::effects);
        soft_pack_unchecked(a, shrink, effects);
        return {};
    });
}
// Journal entries borrow the destination identity beyond this expression.
void initialize(const view &&, journal &) = delete;
void append(const view &&, bytes, unsigned, journal &) = delete;
void replace(const view &&, unsigned, bytes, journal &) = delete;
void retire(const view &&, unsigned, journal &) = delete;
void soft_pack(const view &&, bool, journal &) = delete;
} // namespace ikea::streampack
