// Format preparation is shared across blocks. Existing SeriesPack kernels do
// all decoding; no packed-bit or ISA algorithm is copied here.
#include "kernels.h"
#include <ikea/seriespack.h>
#include <array>
#include <utility>

namespace frame_points {
namespace {
namespace sp = ikea::seriespack;
template <unsigned K>
std::uint64_t point(const std::uint8_t *data, std::size_t row) {
    // Borrow the logical prefix through row. Its complete final tile is
    // already part of the admitted body; the wrapper need not survive binding.
    return sp::bind_decoder<std::uint64_t>(
        sp::dense_source<sp::format<K>>{data, row + 1}).get_unchecked(row);
}
template <unsigned K>
void range(const std::uint8_t *data, std::size_t first, std::size_t take,
           std::uint64_t *out) {
    sp::bind_decoder<std::uint64_t>(sp::dense_source<sp::format<K>>{data, first + take})
        .read_unchecked(first, take, out);
}
template <std::size_t... K> constexpr auto catalogue(std::index_sequence<K...>) {
    return std::array<local_operations, 65>{{{}, {point<K + 1>, range<K + 1>}...}};
}
} // namespace
const std::array<local_operations, 65> local_catalogue = catalogue(std::make_index_sequence<64>{});
} // namespace frame_points
