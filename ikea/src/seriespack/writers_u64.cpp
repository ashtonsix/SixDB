#include "writers.h"

namespace ikea::seriespack {
template std::expected<void, error> initialize(const dense_output&, std::span<const std::uint64_t>, journal&);
template std::expected<void, error> set(const dense_output&, std::size_t, std::uint64_t, journal&);
} // namespace ikea::seriespack

namespace ikea::seriespack {
std::expected<std::size_t, error> effect_capacity_initialize(const dense_output& target) {
    if (auto rows = writers_detail::rows(target); !rows)
        return std::unexpected(rows.error());
    return writers_detail::dispatch(target, 64, [&]<class L>() -> std::expected<std::size_t, error> {
        const auto destination = writers_detail::attach<L>(target);
        if (!destination)
            return std::unexpected(destination.error());
        std::size_t needed = 0;
        author::visit_storage(*destination, [&](ikea::detail::write_span) { ++needed; });
        return needed;
    });
}
} // namespace ikea::seriespack

namespace ikea::seriespack {
std::expected<point_footprint, error> footprint(const dense_output& target, std::size_t row) {
    if (auto rows = writers_detail::rows(target); !rows)
        return std::unexpected(rows.error());
    return writers_detail::dispatch(target, 64, [&]<class L>() -> std::expected<point_footprint, error> {
        const auto destination = writers_detail::attach<L>(target);
        if (!destination)
            return std::unexpected(destination.error());
        if (row >= target.count)
            return std::unexpected(error::range);
        point_footprint out;
        author::visit_writes_point(*destination, row, [&](ikea::detail::write_span w) {
            out.stores[out.store_count++] = {w.offset, w.size};
        });
        const auto* base = destination->stream(0).bytes.data();
        visit_point_reads(*destination, row, [&](const ikea::byte* at, std::size_t n) {
            out.reads[out.read_count++] = {static_cast<std::size_t>(at - base), n};
        });
        return out;
    });
}
} // namespace ikea::seriespack
