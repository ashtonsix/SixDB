#include <ikea/tuplepack/construction.h>

namespace ikea::tuplepack {
std::expected<constructor, error> constructor::make(const layout& format) {
    std::array<byte, 64> lo, hi;
    lo.fill(hole);
    hi.fill(hole);
    for (unsigned i = 0; i < format.codes().size(); ++i)
        (i < 64 ? lo[i] : hi[i - 64]) = i;
    auto first = writer<64>::make(format, lo);
    if (!first)
        return std::unexpected(first.error());
    auto second = writer<64>::make(format, hi);
    if (!second)
        return std::unexpected(second.error());
    return constructor(std::move(*first), std::move(*second), format.codes().size());
}
} // namespace ikea::tuplepack
