#pragma once
#include <ikea/seriespack/layout.h>
#include <ikea/seriespack/layout_value.h>
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
template <class F, class Byte = const ikea::byte> class basic_view {
    static_assert(std::is_same_v<std::remove_const_t<Byte>, ikea::byte>);
    std::array<plane<Byte>, 3> planes_;
    std::size_t count_;
    basic_view(std::array<plane<Byte>, 3> p, std::size_t n) : planes_(p), count_(n) {}

  public:
    using layout_type = F;
    using byte_type = Byte;
    std::size_t size() const {
        return count_;
    }
    const plane<Byte>& stream(unsigned s) const {
        return planes_[s];
    }
    /// Effect plane p: 0 payload, 1 and 2 head bytes. Includes stride gaps.
    std::span<const ikea::byte> plane_storage(unsigned p) const noexcept {
        return planes_[p].bytes;
    }
    auto as_const() const {
        return basic_view<F, const ikea::byte>::attach_unchecked(count_, {{{planes_[0].bytes, planes_[0].stride},
                                               {planes_[1].bytes, planes_[1].stride},
                                               {planes_[2].bytes, planes_[2].stride}}});
    }
    /// Trusted adapter entry. The caller has already proved attach()'s extent,
    /// stride, overflow and occupied-field non-overlap requirements.
    static basic_view attach_unchecked(std::size_t n, std::array<plane<Byte>, 3> p) {
        return {p, n};
    }
    /// Metadata-only admission. n counts logical values; planes are payload,
    /// highest byte, next byte. Include every occupied final-tile byte. Payload
    /// origins have byte alignment; striped payload stride is a multiple of 32B.
    /// Empty input needs no storage. Failure reads/writes no data or effects.
    [[nodiscard]] static std::expected<basic_view, error> attach(std::size_t n,
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
        return basic_view{p, n};
    }
    /// Match a recovered shared byte law, then admit the parent's placement.
    [[nodiscard]] static std::expected<basic_view, error>
    attach(const layout_value& law, std::size_t n, std::array<plane<Byte>, 3> p) {
        if (law != layout_value::of<F>())
            return std::unexpected(error::description);
        return attach(n, p);
    }
};
template <class L> using view = basic_view<L, ikea::byte>;
template <class L> using const_view = basic_view<L, const ikea::byte>;

/// Placement proof for a headless dense payload; no view-object dependency.
template <class F> struct dense_source {
    static_assert(F::heads == 0);
    using layout_type = F;
    const ikea::byte* data;
    std::size_t count;
};
template <class F, class B>
std::expected<dense_source<F>, error> dense(const basic_view<F, B>& source)
    requires(F::heads == 0)
{
    if (source.stream(0).stride != F::tile_bytes)
        return std::unexpected(error::stride);
    return dense_source<F>{source.stream(0).bytes.data(), source.size()};
}
} // namespace ikea::seriespack
