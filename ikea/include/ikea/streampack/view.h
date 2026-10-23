#pragma once
#include <ikea/streampack/layout.h>
#include <ikea/streampack/detail/compact/detail/extent.h>
#include <type_traits>

namespace ikea::streampack {
/// Four independent logical windows; they may share one allocation, but their
/// supplied byte spans must be disjoint. Effects use these plane ordinals.
enum class plane : unsigned { payload, directory, lengths, control };
template <class Byte> struct storage {
    std::span<Byte> payload, directory, lengths, control;
    std::array<std::span<Byte>, 4> planes() const noexcept {
        return {payload, directory, lengths, control};
    }
};
namespace detail {
using ikea::streampack::detail::compact::detail::load;
using ikea::streampack::detail::compact::detail::overlaps;
using ikea::streampack::detail::compact::detail::store;
[[nodiscard]] std::expected<void, error> admit_storage(const layout &, storage<const byte>);
} // namespace detail

/// Named borrowed source identity. Keep this view, its layout and bytes stable
/// while operations/effects refer to it. Attachment admits placement only; initialize
/// new storage or validate encoded contents before any read/mutation.
template <class Byte> class basic_view {
    static_assert(std::is_same_v<std::remove_const_t<Byte>, byte>);
    const layout *layout_;
    storage<Byte> storage_;
    basic_view(const layout &l, storage<Byte> s) : layout_(&l), storage_(s) {}

  public:
    [[nodiscard]] static std::expected<basic_view, error> attach(const layout &l, storage<Byte> s) {
        if (auto ok = detail::admit_storage(l, {s.payload, s.directory, s.lengths, s.control}); !ok)
            return std::unexpected(ok.error());
        return basic_view(l, s);
    }
    static auto attach(const layout &&, storage<Byte>) = delete;
    const layout &format() const noexcept { return *layout_; }
    const storage<Byte> &windows() const noexcept { return storage_; }
    using plane_type = plane;
    std::span<const byte> plane_storage(plane p) const noexcept {
        return storage_.planes()[unsigned(p)];
    }
    auto as_const() const noexcept {
        return basic_view<const byte>::attach_unchecked(
            *layout_, {storage_.payload, storage_.directory, storage_.lengths, storage_.control});
    }
    /// Reuse an owner's established placement proof; same lifetime and content
    /// obligations as attach(). No encoded bytes are examined.
    static basic_view attach_unchecked(const layout &l, storage<Byte> s) { return {l, s}; }
    static auto attach_unchecked(const layout &&, storage<Byte>) = delete;
    unsigned size() const noexcept {
        return ikea::streampack::detail::compact::detail::load<std::uint16_t>(storage_.control.data());
    }
    bool live_unchecked(unsigned rank) const noexcept {
        return !layout_->has_tombstones() ||
               !(std::to_integer<unsigned>(storage_.directory[rank / 8]) & (1u << (rank % 8)));
    }
    std::uint32_t extent() const noexcept { return ikea::streampack::detail::compact::detail::extent(*this); }
    /// Cold recovery/admission: checks the complete assigned interpretation and
    /// zeroed SIMD tail. Successful checked writes preserve this proof.
    [[nodiscard]] std::expected<void, error> validate() const;
};
using view = basic_view<byte>;
using const_view = basic_view<const byte>;
} // namespace ikea::streampack
