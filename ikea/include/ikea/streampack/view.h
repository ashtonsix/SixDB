#pragma once
#include <ikea/streampack/description.h>
#include <ikea/boundlist/detail/extent.h>
#include <ikea/boundlist/search.h>
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
using boundlist::detail::load;
using boundlist::detail::overlaps;
using boundlist::detail::store;
[[nodiscard]] std::expected<void, error> admit_storage(const layout &, storage<const byte>);
} // namespace detail

/// Named borrowed source identity. Keep this view, its layout and bytes stable
/// while operations/effects refer to it. Binding admits placement only; initialize
/// new storage or validate encoded contents before any read/mutation.
template <class Byte> class basic_view {
    static_assert(std::is_same_v<std::remove_const_t<Byte>, byte>);
    const layout *layout_;
    storage<Byte> storage_;
    basic_view(const layout &l, storage<Byte> s) : layout_(&l), storage_(s) {}

  public:
    [[nodiscard]] static std::expected<basic_view, error> bind(const layout &l, storage<Byte> s) {
        if (auto ok = detail::admit_storage(l, {s.payload, s.directory, s.lengths, s.control}); !ok)
            return std::unexpected(ok.error());
        return basic_view(l, s);
    }
    static auto bind(const layout &&, storage<Byte>) = delete;
    const layout &format() const noexcept { return *layout_; }
    const storage<Byte> &windows() const noexcept { return storage_; }
    auto as_const() const noexcept {
        return basic_view<const byte>::assume_valid(
            *layout_, {storage_.payload, storage_.directory, storage_.lengths, storage_.control});
    }
    /// Reuse an owner's established placement proof; same lifetime and content
    /// obligations as bind(). No encoded bytes are examined.
    static basic_view assume_valid(const layout &l, storage<Byte> s) { return {l, s}; }
    static auto assume_valid(const layout &&, storage<Byte>) = delete;
    unsigned size() const noexcept {
        return boundlist::detail::load<std::uint16_t>(storage_.control.data());
    }
    bool live_unchecked(unsigned rank) const noexcept {
        return !layout_->has_tombstones() ||
               !(std::to_integer<unsigned>(storage_.directory[rank / 8]) & (1u << (rank % 8)));
    }
    unsigned next_eligible(unsigned first, unsigned n) const noexcept {
        return layout_->has_tombstones()
                   ? boundlist::population{storage_.directory.first(layout_->tombstone_bytes()),
                                           true}
                         .next(first, n)
                   : first;
    }
    std::uint32_t extent() const noexcept { return boundlist::sidecar::detail::extent(*this); }
    /// Cold recovery/admission: checks the complete assigned interpretation and
    /// zeroed SIMD tail. Successful checked writes preserve this proof.
    [[nodiscard]] std::expected<void, error> validate() const;
};
using view = basic_view<byte>;
using const_view = basic_view<const byte>;
} // namespace ikea::streampack
