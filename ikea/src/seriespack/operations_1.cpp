// Compiled range operations for dense widths 1..8; one unit per eight
// widths bounds each unit's compile memory.
#include <ikea/seriespack/detail/dense_operations.h>

namespace ikea::seriespack::detail {
const range_operations* compiled_operations_1(unsigned width, geometry storage) noexcept {
    const range_operations* out = nullptr;
    each<8>([&](auto i) {
        constexpr unsigned K = 1 + i;
        if (width != K)
            return;
        if (storage == geometry::local)
            out = &dense_operations<layout<K>>::table;
        else if constexpr (striped_width(K))
            out = &dense_operations<layout<K, geometry::striped>>::table;
    });
    return out;
}
} // namespace ikea::seriespack::detail
