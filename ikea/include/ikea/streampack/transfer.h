#pragma once
#include <ikea/streampack/read.h>
#include <ikea/streampack/write.h>

namespace ikea::streampack {
/// Constructs a disjoint destination in the requested live-rank order, preserving
/// duplicates. All commands are admitted before initialization. The owner chooses
/// deduplication, sorting/repartitioning, remaps and coherent publication.
template <class Byte>
[[nodiscard]] std::expected<void, error> transfer(const basic_view<Byte> &source,
                                                  std::span<const std::uint16_t> ranks,
                                                  const view &dst, bool shrink, journal &effects) {
    const auto &l = dst.format();
    if (ranks.size() > l.capacity() || l.lengths_bytes(ranks.size()) > dst.windows().lengths.size())
        return std::unexpected(error::capacity);
    if constexpr (requires { effects.storage; }) {
        const auto *records = effects.storage.data();
        const auto size = effects.storage.size_bytes();
        if (detail::overlaps(records, size, ranks.data(), ranks.size_bytes()))
            return std::unexpected(error::overlap);
        for (const auto s : source.windows().planes())
            if (detail::overlaps(records, size, s.data(), s.size()))
                return std::unexpected(error::overlap);
    }
    for (const auto d : dst.windows().planes()) {
        for (const auto s : source.windows().planes())
            if (detail::overlaps(d.data(), d.size(), s.data(), s.size()))
                return std::unexpected(error::overlap);
        if (detail::overlaps(d.data(), d.size(), ranks.data(), ranks.size_bytes()))
            return std::unexpected(error::overlap);
    }
    // Reuse admitted coordinates during writing, and keep source/destination
    // dispatch additive rather than instantiating every codec pair. The bounded
    // 2 KiB scratch belongs to this complete rewrite operation, not point reads.
    std::array<location, slot_capacity> values;
    const auto admitted = author::visit(source, [&](auto a) -> std::expected<void, error> {
        std::uint32_t total = 0;
        unsigned index = 0;
        for (auto rank : ranks) {
            if (rank >= source.size())
                return std::unexpected(error::range);
            if (!a.live(rank))
                return std::unexpected(error::retired);
            auto value = a.locate(rank);
            if (shrink)
                value.reservation = value.length;
            if (!detail::accepts(l, value.length, value.reservation))
                return std::unexpected(error::value);
            total += value.reservation;
            values[index++] = value;
        }
        if (total > l.schema().byte_limit || total > dst.windows().payload.size())
            return std::unexpected(error::capacity);
        return {};
    });
    if (!admitted)
        return admitted;
    if (!detail::room(effects, effect_capacity_transfer(dst, ranks.size())))
        return std::unexpected(error::effects);
    initialize_unchecked(dst, effects);
    if (ranks.empty())
        return {};
    author::visit(dst, [&](auto to) {
        author::value_encoder encoder(to, 0, 0, effects);
        for (unsigned i = 0; i < ranks.size(); ++i) {
            const auto value = values[i];
            encoder.append(source.windows().payload.subspan(value.offset, value.length),
                           value.reservation);
        }
        encoder.finish();
    });
    return {};
}
template <class Byte>
void transfer(const basic_view<Byte> &, std::span<const std::uint16_t>, const view &&, bool,
              journal &) = delete;
} // namespace ikea::streampack
