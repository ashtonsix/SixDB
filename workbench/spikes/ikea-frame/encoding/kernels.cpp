// Width-specialised plane operations, compiled once for the shared catalogue.
#include "kernels.h"

#include <ikea/seriespack.h>

#include <algorithm>
#include <bit>
#include <utility>

namespace frame_encoding {
namespace {
namespace sp = ikea::seriespack;

template <unsigned K> std::uint64_t point(const std::uint8_t *data, std::size_t row) {
    return sp::bind_decoder<std::uint64_t>(sp::dense_source<sp::format<K>>{data, row + 1})
        .get_unchecked(row);
}

template <unsigned K, class U = std::uint64_t>
void range(const std::uint8_t *data, std::size_t first, std::size_t take, U *out) {
    sp::bind_decoder<U>(sp::dense_source<sp::format<K>>{data, first + take})
        .read_unchecked(first, take, out);
}

// The narrow-lane range for K, or null when the lane can't hold K bits.
template <unsigned K, class U> constexpr auto lane() {
    using function = void (*)(const std::uint8_t *, std::size_t, std::size_t, U *);
    if constexpr (K <= 8 * sizeof(U))
        return static_cast<function>(range<K, U>);
    else
        return static_cast<function>(nullptr);
}

template <unsigned K> std::uint64_t sum(const std::uint8_t *data, unsigned take) {
    if (!take)
        return 0;
    const sp::dense_source<sp::format<K>> source{data, (take + 15) / 16 * 16};
#if defined(__aarch64__) || defined(__AVX2__)
    // Reconstruct native 16-row packets and reduce them in registers; no
    // widened scratch array.
    namespace cp = sp::composition;
    const auto expression = cp::describe(source);
    cp::native_ops ops;
    sp::native::sum_state total;
    for (unsigned done = 0; done < take; done += 16) {
        const unsigned n = std::min(16u, take - done);
        const auto active = static_cast<std::uint16_t>(0xffffu >> (16 - n));
        const auto values = cp::read(ops, expression, done, std::uint16_t{0xffff});
        total.add(sp::native::sum(values, sp::native::mask16<K>(active)));
    }
    return total.finish();
#else
    const auto decoder = sp::bind_decoder<std::uint64_t>(source);
    std::uint64_t total = 0;
    for (unsigned i = 0; i < take; ++i)
        total += decoder.get_unchecked(i);
    return total;
#endif
}

template <unsigned K>
std::uint64_t count_ge(const std::uint8_t *data, unsigned take, std::uint64_t rhs) {
    const sp::dense_source<sp::format<K>> source{data, take};
    std::uint64_t total = 0;
    for (unsigned first = 0; first < take; first += 64) {
        const unsigned n = std::min(64u, take - first);
        const std::uint64_t active = n == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << n) - 1;
        total += std::popcount(sp::filter_unchecked<ikea::comparison::greater_equal>(
            source, first, n, rhs, active));
    }
    return total;
}

template <std::size_t... K> constexpr auto catalogue(std::index_sequence<K...>) {
    return std::array<plane_operations, 65>{
        {{},
         {point<K + 1>, range<K + 1>, sum<K + 1>, count_ge<K + 1>, lane<K + 1, std::uint8_t>(),
          lane<K + 1, std::uint16_t>(), lane<K + 1, std::uint32_t>()}...}};
}
}  // namespace

const std::array<plane_operations, 65> plane_catalogue = catalogue(std::make_index_sequence<64>{});
}  // namespace frame_encoding
