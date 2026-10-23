#include "writers.h"

namespace ikea::seriespack {
template std::expected<void, error> initialize(const dense_output&, std::span<const std::uint64_t>, journal&);
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
