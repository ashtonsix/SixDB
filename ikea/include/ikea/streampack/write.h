#pragma once
#include <ikea/streampack/author/write.h>
#include <ikea/boundlist/write.h>

namespace ikea::streampack {
namespace detail {
using boundlist::sidecar::detail::accepts;
using boundlist::sidecar::detail::room;
/// Payload aliases are supported while the binding stays live. Metadata and
/// effect-output aliases would let issued stores destroy remaining input.
template <class Effects> bool input_alias(const view& dst, bytes input, const Effects& effects) {
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
/// Checked commands preserve supplied bytes and effects on refusal. Contents
/// have been admitted; effect storage and source/view/format controls are disjoint
/// from outputs. The owner establishes exclusive authority before entry.
template <class Effects>
[[nodiscard]] std::expected<void, error> initialize(const view& dst, Effects& effects) {
    if (!detail::room(effects, 1 + (dst.format().directory_bytes() != 0)))
        return std::unexpected(error::effects);
    author::initialize_unchecked(dst, effects);
    return {};
}
template <class Effects>
[[nodiscard]] std::expected<void, error> append(const view& dst, bytes input, unsigned reservation,
                                                Effects& effects) {
    const auto& l = dst.format();
    if (input.size() > l.schema().maximum || !detail::accepts(l, input.size(), reservation))
        return std::unexpected(error::value);
    const auto n = dst.size(), begin = dst.extent();
    if (n >= l.capacity() || l.lengths_bytes(n + 1) > dst.windows().lengths.size() ||
        begin > l.schema().byte_limit || reservation > l.schema().byte_limit - begin ||
        begin > dst.windows().payload.size() || reservation > dst.windows().payload.size() - begin)
        return std::unexpected(error::capacity);
    if (detail::input_alias(dst, input, effects))
        return std::unexpected(error::alias);
    const unsigned count = (reservation != 0) + boundlist::sidecar::detail::append_effects(l, n);
    if (!detail::room(effects, count))
        return std::unexpected(error::effects);
    author::dispatch(dst,
                     [&](auto a) { author::append_unchecked(a, input, reservation, effects); });
    return {};
}
template <class Effects>
[[nodiscard]] std::expected<void, error> replace(const view& dst, unsigned rank, bytes input,
                                                 Effects& effects) {
    if (rank >= dst.size())
        return std::unexpected(error::rank);
    if (!dst.live_unchecked(rank))
        return std::unexpected(error::retired);
    if (input.size() > dst.format().schema().maximum)
        return std::unexpected(error::value);
    if (detail::input_alias(dst, input, effects))
        return std::unexpected(error::alias);
    if (!detail::room(effects, (!input.empty()) + dst.format().has_used()))
        return std::unexpected(error::effects);
    return author::dispatch(dst, [&](auto a) -> std::expected<void, error> {
        if (!detail::accepts(a.format(), input.size(), a.reserved(rank)))
            return std::unexpected(error::value);
        author::replace_unchecked(a, rank, input, effects);
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
/// Stable in-place packing. All ranks remain; retired reservations become zero.
/// Optional shrinking discards live slack. Fixed nonzero geometry can only pack
/// when that would leave every reservation unchanged.
template <class Effects>
[[nodiscard]] std::expected<void, error> soft_pack(const view& dst, bool shrink, Effects& effects) {
    return author::dispatch(dst, [&](auto a) -> std::expected<void, error> {
        if (a.format().fixed()) {
            for (unsigned r = 0; r < dst.size(); ++r)
                if ((a.format().fixed_width() && !a.live(r)) ||
                    (shrink && a.length(r) != a.reserved(r)))
                    return std::unexpected(error::description);
            return {};
        }
        if (!detail::room(effects, a.format().pack_effect_capacity(dst.size())))
            return std::unexpected(error::effects);
        author::soft_pack_unchecked(a, shrink, effects);
        return {};
    });
}
// Journal entries borrow the destination identity beyond this expression.
template <class Effects> void initialize(const view&&, Effects&) = delete;
template <class Effects> void append(const view&&, bytes, unsigned, Effects&) = delete;
template <class Effects> void replace(const view&&, unsigned, bytes, Effects&) = delete;
template <class Effects> void retire(const view&&, unsigned, Effects&) = delete;
template <class Effects> void soft_pack(const view&&, bool, Effects&) = delete;
} // namespace ikea::streampack
