#pragma once
#include <ikea/seriespack/view.h>

namespace ikea::seriespack {
/// A physical description, independent of construction presets and execution
/// target. Engine separately retains the mapping from planes to storage owners.
struct representation {
    unsigned width, heads;
    geometry storage;
    std::uint64_t count;
    std::array<std::uint64_t, 3> strides;
    bool operator==(const representation&) const = default;
};
constexpr bool valid(const representation& r) {
    if (r.width < 1 || r.width > 64 || (r.heads != 0 && r.heads != 8 && r.heads != 16) ||
        r.heads > r.width)
        return false;
    if (r.storage != geometry::local && r.storage != geometry::striped)
        return false;
    if (r.storage == geometry::striped && !striped_width(r.width - r.heads))
        return false;
    const auto rows =
        r.storage == geometry::local ? 8 : 32 * (8 / std::gcd((r.width - r.heads) % 8, 8u));
    const std::array<std::uint64_t, 3> used{rows * (r.width - r.heads) / 8, r.heads >= 8 ? rows : 0,
                                            r.heads == 16 ? rows : 0};
    for (unsigned p = 0; p < 3; ++p) {
        if (!used[p]) {
            if (r.strides[p])
                return false;
            continue;
        }
        if (r.strides[p] < used[p])
            return false;
        if (p == 0 && r.storage == geometry::striped && r.strides[p] % 32)
            return false;
        const auto tiles = r.count / rows + (r.count % rows != 0);
        if (tiles && tiles - 1 > (UINT64_MAX - used[p]) / r.strides[p])
            return false;
    }
    return true;
}
template <class F, class B> representation describe_storage(const view<F, B>& source) {
    return {F::width,
            F::heads,
            F::storage,
            source.size(),
            {F::payload ? source.stream(0).stride : 0, F::heads >= 8 ? source.stream(1).stride : 0,
             F::heads == 16 ? source.stream(2).stride : 0}};
}
/// Versioned little-endian physical descriptor. No enum representation, pointer,
/// preset, ISA, C++ type name or process-local plan identity is persisted.
using descriptor_bytes = std::array<std::uint8_t, 40>;
std::expected<descriptor_bytes, error> encode_descriptor(const representation& r);
std::expected<representation, error> decode_descriptor(std::span<const std::uint8_t> bytes);
template <class F, class B>
std::expected<view<F, B>, error> attach_representation(const representation& r,
                                                       std::array<std::span<B>, 3> bytes) {
    if (!valid(r) || r.width != F::width || r.heads != F::heads || r.storage != F::storage)
        return std::unexpected(error::description);
    if (r.count > SIZE_MAX)
        return std::unexpected(error::overflow);
    std::array<plane<B>, 3> planes;
    for (unsigned p = 0; p < 3; ++p) {
        if (r.strides[p] > SIZE_MAX)
            return std::unexpected(error::overflow);
        planes[p] = {bytes[p], static_cast<std::size_t>(r.strides[p])};
    }
    return view<F, B>::attach(r.count, planes);
}
} // namespace ikea::seriespack
