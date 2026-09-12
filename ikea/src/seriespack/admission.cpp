#include <ikea/seriespack/detail/admission.h>
#include <ikea/seriespack/detail/mutation/admission_state.h>
#include <algorithm>
#include <limits>

namespace ikea::seriespack {
std::string_view describe(error reason) noexcept {
    switch (reason) {
    case error::capacity:
        return "insufficient accessible bytes, input/output extent, or effect capacity";
    case error::stride:
        return "tile stride is too small or violates the physical alignment rule";
    case error::alignment:
        return "payload base must be aligned to 64 bytes";
    case error::overlap:
        return "occupied writable fields overlap";
    case error::range:
        return "logical range or original-row selection extent is invalid";
    case error::value:
        return "a selected input value exceeds the unsigned field width";
    case error::overflow:
        return "physical extent arithmetic exceeds the addressable range";
    case error::description:
        return "unknown or inconsistent physical description";
    case error::unsupported:
        return "operation is unavailable in this execution profile";
    case error::allocation:
        return "temporary storage for cold alias admission could not be allocated";
    }
    return "unknown SeriesPack admission error";
}
namespace detail {
std::expected<void, error> admit_view(std::size_t count, std::size_t rows,
                                      std::array<std::size_t, 3> used,
                                      std::array<stream_extent, 3> streams, bool striped) {
    if (!count)
        return {};
    const auto tiles = count / rows + (count % rows != 0);
    std::array<occupied_run, 3> occupied{};
    for (unsigned p = 0; p < 3; ++p)
        if (used[p]) {
            const auto& stream = streams[p];
            if (stream.stride < used[p] || (p == 0 && striped && stream.stride % 32))
                return std::unexpected(error::stride);
            if (tiles - 1 > (SIZE_MAX - used[p]) / stream.stride)
                return std::unexpected(error::overflow);
            const auto extent = (tiles - 1) * stream.stride + used[p];
            if (stream.bytes < extent)
                return std::unexpected(error::capacity);
            const auto begin = reinterpret_cast<std::uintptr_t>(stream.data);
            if (p == 0 && begin % 64)
                return std::unexpected(error::alignment);
            if (begin > UINTPTR_MAX - extent)
                return std::unexpected(error::overflow);
            occupied[p] = {begin, stream.stride, used[p], tiles};
        }
    for (unsigned a = 0; a < 3; ++a)
        for (unsigned b = a + 1; b < 3; ++b)
            if (overlaps(occupied[a], occupied[b]))
                return std::unexpected(error::overlap);
    return {};
}
} // namespace detail
} // namespace ikea::seriespack

namespace ikea::seriespack::composition::detail {
void writable_fields::add(occupied_run next, const void* source, unsigned plane) {
    if (allocation_failed || aliased)
        return;
    for (const auto& old : occupied)
        if (ikea::seriespack::detail::overlaps(next, old.bytes)) {
            aliased = true;
            diagnostic = {source, old.source, plane, old.plane};
            return;
        }
    try {
        occupied.push_back({next, source, plane});
    } catch (const std::bad_alloc&) {
        allocation_failed = true;
        diagnostic = {source, nullptr, plane};
    }
}
} // namespace ikea::seriespack::composition::detail
