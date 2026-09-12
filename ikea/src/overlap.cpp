#include <ikea/detail/overlap.h>
#include <algorithm>
namespace ikea::detail {
bool overlaps(occupied_run a, occupied_run b) noexcept {
    if (!a.count || !b.count)
        return false;
    if (a.base + (a.count - 1) * a.stride + a.bytes <= b.base ||
        b.base + (b.count - 1) * b.stride + b.bytes <= a.base)
        return false;
    if (a.stride == b.stride && a.bytes <= a.stride && b.bytes <= b.stride) {
        if (a.base > b.base)
            std::swap(a, b);
        const auto delta = b.base - a.base;
        const auto tile = delta / a.stride, offset = delta % a.stride;
        // Equal-stride fields repeat the same gap. Checking every row made
        // cold TuplePack alias admission scale with the entire array length.
        return tile < a.count &&
               (offset < a.bytes || (tile + 1 < a.count && b.bytes > a.stride - offset));
    }
    std::size_t i = 0, j = 0;
    while (i < a.count && j < b.count) {
        const auto x = a.base + i * a.stride, y = b.base + j * b.stride;
        if (x < y + b.bytes && y < x + a.bytes)
            return true;
        if (x + a.bytes <= y)
            i += std::min(a.count - i, (y - x - a.bytes) / a.stride + 1);
        else
            j += std::min(b.count - j, (x - y - b.bytes) / b.stride + 1);
    }
    return false;
}
} // namespace ikea::detail
