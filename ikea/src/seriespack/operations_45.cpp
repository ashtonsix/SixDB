// Compiled range operations for dense widths 45..48; one unit per four
// widths bounds each unit's compile memory (narrow widths compile the most
// kernels).
#include <ikea/seriespack/detail/dense_operations.h>

namespace ikea::seriespack::detail {
const range_operations* compiled_operations_45(unsigned width, geometry storage) noexcept {
    const range_operations* out = nullptr;
    each<4>([&](auto i) {
        constexpr unsigned K = 45 + i;
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
