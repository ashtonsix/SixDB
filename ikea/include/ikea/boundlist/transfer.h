#pragma once
#include <ikea/boundlist/read.h>
#include <ikea/boundlist/write.h>

namespace ikea::boundlist::sidecar {
/// Caller repairs rank holders/children and publishes the whole replacement.
/// Sources, destination, selected ranks and journal storage are disjoint.
template <class Byte, class Effects>
[[nodiscard]] std::expected<void, error> transfer(const basic_view<Byte>& source,
                                                  std::span<const std::uint16_t> ranks,
                                                  const view& dst, bool shrink, Effects& effects) {
    const auto& l = dst.format();
    if (ranks.size() > l.capacity() || l.lengths_bytes(ranks.size()) > dst.windows().lengths.size())
        return std::unexpected(error::capacity);
    for (auto d : dst.windows().planes()) {
        for (auto s : source.windows().planes())
            if (detail::overlaps(d.data(), d.size(), s.data(), s.size()))
                return std::unexpected(error::alias);
        if (detail::overlaps(d.data(), d.size(), ranks.data(), ranks.size_bytes()))
            return std::unexpected(error::alias);
    }
    if constexpr (requires { effects.storage; }) {
        const auto* data = effects.storage.data();
        const auto bytes = effects.storage.size_bytes();
        if (detail::overlaps(data, bytes, ranks.data(), ranks.size_bytes()))
            return std::unexpected(error::alias);
        for (auto s : source.windows().planes())
            if (detail::overlaps(data, bytes, s.data(), s.size()))
                return std::unexpected(error::alias);
    }
    std::array<location, slot_capacity> values;
    const auto admitted = author::dispatch(source, [&](auto a) -> std::expected<void, error> {
        unsigned total = 0, i = 0;
        for (auto rank : ranks) {
            if (rank >= source.size())
                return std::unexpected(error::rank);
            if (!a.live(rank))
                return std::unexpected(error::retired);
            auto p = a.locate(rank);
            if (shrink)
                p.reservation = p.length;
            if (!detail::accepts(l, p.length, p.reservation))
                return std::unexpected(error::value);
            total += p.reservation;
            values[i++] = p;
        }
        if (total > l.schema().byte_limit)
            return std::unexpected(error::capacity);
        return {};
    });
    if (!admitted)
        return admitted;
    if (!detail::room(effects, 2 + ranks.size() * l.append_effect_capacity()))
        return std::unexpected(error::effects);
    author::initialize_unchecked(dst, effects);
    author::dispatch(dst, [&](auto a) {
        for (unsigned i = 0; i < ranks.size(); ++i)
            author::append_unchecked(a, values[i].length, values[i].reservation, effects);
    });
    return {};
}
template <class Byte, class Effects>
void transfer(const basic_view<Byte>&, std::span<const std::uint16_t>, const view&&, bool,
              Effects&) = delete;
} // namespace ikea::boundlist::sidecar
