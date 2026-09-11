#include "points.h"

#include <ikea/seriespack/point.h>

namespace seriespack_measurement {
using namespace ikea::seriespack;
namespace {

template<class Format, point_reader Reader>
std::uint64_t sum_region(const const_view& source, const std::size_t* indices,
                         std::size_t count) {
    const auto values = static_const_view<Format>::assume_valid(source.size(), source.placement());
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < count; ++i)
        sum += trusted_get<Reader>(values, indices[i]);
    return sum;
}

template<unsigned K, unsigned H, geometry G>
point_region choose_strategy(point_reader reader) {
    using F = static_format<K, G, H>;
    if (reader == point_reader::arithmetic) return {sum_region<F, point_reader::arithmetic>};
    return {sum_region<F, point_reader::constant_offsets>};
}

template<unsigned K, unsigned H>
point_region choose_geometry(geometry storage, point_reader reader) {
    if (storage == geometry::local8) return choose_strategy<K, H, geometry::local8>(reader);
    if constexpr (supports_stripes(K - H)) return choose_strategy<K, H, geometry::striped>(reader);
    __builtin_unreachable(); // Description was admitted before choosing code.
}

template<unsigned K>
point_region choose_head(description layout, point_reader reader) {
    if (layout.head_bits == 0) return choose_geometry<K, 0>(layout.storage, reader);
    if constexpr (K >= 8)
        if (layout.head_bits == 8) return choose_geometry<K, 8>(layout.storage, reader);
    if constexpr (K >= 16)
        if (layout.head_bits == 16) return choose_geometry<K, 16>(layout.storage, reader);
    __builtin_unreachable();
}

} // namespace

point_region static_points(description layout, point_reader strategy) {
    if (!validate(layout) ||
        (strategy != point_reader::arithmetic && strategy != point_reader::constant_offsets)) return {};
    return detail::dispatch_group<64>(layout.width - 1, [&](auto index) {
        return choose_head<index + 1>(layout, strategy);
    });
}

} // namespace seriespack_measurement
