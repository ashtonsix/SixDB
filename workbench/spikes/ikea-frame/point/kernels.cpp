// Format preparation is shared across blocks. Existing SeriesPack kernels do
// all decoding; no packed-bit or ISA algorithm is copied here.
#include "kernels.h"
#include <ikea/seriespack.h>
#include <array>
#include <utility>
#include <cassert>
#include <algorithm>

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
template <unsigned K>
std::uint64_t prefix(const std::uint8_t *data, unsigned first, unsigned take) {
    // Same trusted owner handoff as point/range: the reader proved the row
    // and computes this aligned, bounded prefix inside its admitted block.
    __builtin_assume(first % 16 == 0 && first <= 256 && take <= 256 - first);
    if (!take) return 0;
    const sp::dense_source<sp::format<K>> source{data, 256};
#if defined(__aarch64__) || defined(__AVX2__)
    namespace cp = sp::composition;
    const auto expression = cp::describe(source);
    cp::native_ops ops;
    sp::native::sum_state total;
    for (unsigned done = 0; done < take; done += 16) {
        const unsigned n = std::min(16u, take - done);
        const auto active = static_cast<std::uint16_t>(0xffffu >> (16 - n));
        // Entire selected block is admitted. Reconstruct one native16 packet
        // and mask its contribution; no widened scratch array or scalar tail.
        const auto values = cp::read(ops, expression, first + done, std::uint16_t{0xffff});
        total.add(sp::native::sum(values, sp::native::mask16<K>(active)));
    }
    return total.finish();
#else
    const auto decoder = sp::bind_decoder<std::uint64_t>(source);
    std::uint64_t total = 0;
    for (unsigned i = 0; i < take; ++i) total += decoder.get_unchecked(first + i);
    return total;
#endif
}
template <unsigned K>
std::uint64_t materialized_prefix(const std::uint8_t *data, unsigned first, unsigned take) {
    __builtin_assume(first % 16 == 0 && first <= 256 && take <= 256 - first);
    if (!take) return 0;
    alignas(64) std::uint64_t values[256];
    range<K>(data, first, take, values);
    std::uint64_t total = 0;
    for (unsigned i = 0; i < take; ++i) total += values[i];
    return total;
}
template <std::size_t... K> constexpr auto catalogue(std::index_sequence<K...>) {
    return std::array<local_operations, 65>{{{},
        {point<K + 1>, range<K + 1>, {materialized_prefix<K + 1>, prefix<K + 1>}}...}};
}
} // namespace
const std::array<local_operations, 65> local_catalogue = catalogue(std::make_index_sequence<64>{});
} // namespace frame_points
