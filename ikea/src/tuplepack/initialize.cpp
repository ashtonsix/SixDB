#include <ikea/tuplepack/initialize.h>

namespace ikea::tuplepack {
std::expected<write_plan<128>, error> write_plan<128>::make(const layout& format) {
    std::array<std::uint8_t, 64> lo, hi;
    lo.fill(hole);
    hi.fill(hole);
    for (unsigned i = 0; i < format.codes().size(); ++i)
        (i < 64 ? lo[i] : hi[i - 64]) = i;
    auto first = write_plan<64>::make(format, lo);
    if (!first)
        return std::unexpected(first.error());
    auto second = write_plan<64>::make(format, hi);
    if (!second)
        return std::unexpected(second.error());
    return write_plan<128>(std::move(*first), std::move(*second), format.codes().size());
}
} // namespace ikea::tuplepack
