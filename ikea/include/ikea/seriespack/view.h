#pragma once
#include <ikea/seriespack/format.h>
#include <ikea/seriespack/detail/admission.h>
#include <algorithm>
#include <expected>
#include <limits>
#include <span>

namespace ikea::seriespack {
template <class Byte> struct plane {
    /// Borrowed accessible bytes from this plane's origin, including stride
    /// gaps. Presence in this span does not grant ownership of those gaps.
    std::span<Byte> bytes;
    /// Bytes between successive physical tiles; heads follow their own stride.
    std::size_t stride;
};

/// Admitted storage and logical extent. Copies borrow the same bytes; an owner
/// must keep them live and synchronize access. Gaps can belong to other objects.
template <class F, class Byte = const std::uint8_t> class view {
    std::array<plane<Byte>, 3> planes_;
    std::size_t count_;
    view(std::array<plane<Byte>, 3> p, std::size_t n) : planes_(p), count_(n) {}

  public:
    using format_type = F;
    using byte_type = Byte;
    std::size_t size() const {
        return count_;
    }
    const plane<Byte>& stream(unsigned s) const {
        return planes_[s];
    }
    auto as_const() const {
        return view<F>::assume_valid(count_, {{{planes_[0].bytes, planes_[0].stride},
                                               {planes_[1].bytes, planes_[1].stride},
                                               {planes_[2].bytes, planes_[2].stride}}});
    }
    /// Trusted adapter entry. The caller has already proved attach()'s extent,
    /// alignment, stride, overflow and occupied-field non-overlap requirements.
    static view assume_valid(std::size_t n, std::array<plane<Byte>, 3> p) {
        return {p, n};
    }
    /// Metadata-only admission. n counts logical values; planes are payload,
    /// highest byte, next byte. Include every occupied final-tile byte. Payload
    /// starts at 64B alignment; striped payload stride is a multiple of 32B.
    /// Empty input needs no storage. Failure reads/writes no data or effects.
    [[nodiscard]] static std::expected<view, error> attach(std::size_t n,
                                                           std::array<plane<Byte>, 3> p) {
        constexpr std::array<std::size_t, 3> used{F::tile_bytes, F::heads >= 8 ? F::tile_rows : 0,
                                                  F::heads == 16 ? F::tile_rows : 0};
        std::array<detail::stream_extent, 3> streams;
        for (unsigned i = 0; i < 3; ++i)
            streams[i] = {p[i].bytes.data(), p[i].bytes.size(), p[i].stride};
        if (auto admitted =
                detail::admit_view(n, F::tile_rows, used, streams, F::storage == geometry::striped);
            !admitted)
            return std::unexpected(admitted.error());
        return view{p, n};
    }
};

/// Placement proof for a headless dense payload; no view-object dependency.
template <class F> struct dense_source {
    static_assert(F::heads == 0);
    using format_type = F;
    const std::uint8_t* data;
    std::size_t count;
};
template <class F, class B>
std::expected<dense_source<F>, error> dense(const view<F, B>& source)
    requires(F::heads == 0)
{
    if (source.stream(0).stride != F::tile_bytes)
        return std::unexpected(error::stride);
    return dense_source<F>{source.stream(0).bytes.data(), source.size()};
}
} // namespace ikea::seriespack
