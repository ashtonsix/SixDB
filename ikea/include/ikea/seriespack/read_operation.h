#pragma once
#include <ikea/seriespack/status.h>
#include <cstddef>
#include <expected>
#include <span>
#include <type_traits>

namespace ikea::seriespack {
/// Borrowed read operation. The source bytes and any named view used at binding
/// remain alive and unchanged in placement through each call. Coordinates count
/// original logical values, not bytes or filtered ranks. Output is disjoint from
/// occupied source bytes and binding metadata. No call allocates or suspends.
template <class U> class decoder {
    static_assert(std::is_integral_v<U> && std::is_unsigned_v<U> && !std::is_same_v<U, bool> &&
                  sizeof(U) <= 8);

  public:
    using function = void (*)(const void*, std::size_t, std::size_t, U*);
    using point_function = U (*)(const void*, std::size_t);
    using region_function = void (*)(const void*, std::size_t, U*);
    /// Adapter construction after placement, readable extents and kernel
    /// contracts have been admitted. Prefer bind_decoder at ordinary call sites.
    decoder(const void* resource, std::size_t count, function range, region_function region,
            point_function point)
        : resource_(resource), range_(range), region_(region), point_(point), count_(count) {}
    std::size_t size() const {
        return count_;
    }
    /// Checked point read. An out-of-range coordinate reads no source bytes.
    [[nodiscard]] std::expected<U, error> get(std::size_t row) const {
        if (row >= count_)
            return std::unexpected(error::range);
        return get_unchecked(row);
    }
    /// Checked range; output length determines count. Failure leaves output
    /// unchanged. Does not prove output/source disjointness or owner lifetime.
    [[nodiscard]] std::expected<void, error> read(std::size_t first, std::span<U> output) const {
        if (first > count_ || output.size() > count_ - first)
            return std::unexpected(error::range);
        read_unchecked(first, output.size(), output.data());
        return {};
    }
    /// Trusted range: first+count is within size(), and count output slots are
    /// writable and disjoint. Arbitrary scalar edges are supported.
    void read_unchecked(std::size_t first, std::size_t count, U* __restrict output) const {
        range_(resource_, first, count, output);
    }
    /// Trusted point: row<size(); no range or lifetime check is repeated.
    U get_unchecked(std::size_t row) const {
        return point_(resource_, row);
    }
    /// Trusted native region: first is divisible by 16 and all sixteen logical
    /// values are in range. All sixteen output slots are writable and disjoint.
    void read16_unchecked(std::size_t first, U* __restrict output) const {
        region_(resource_, first, output);
    }

  private:
    const void* resource_;
    function range_;
    region_function region_;
    point_function point_;
    std::size_t count_;
};
} // namespace ikea::seriespack
