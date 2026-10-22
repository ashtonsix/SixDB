#pragma once
#include <ikea/streampack/detail/compact/description.h>
#include <ikea/streampack/detail/compact/detail/extent.h>
#include <type_traits>
#include <ikea/intervals/search.h>

namespace ikea::streampack::detail::compact {
/// Three independent logical windows; they may share one allocation, but their
/// supplied byte spans must be disjoint. Effects use these plane ordinals.
enum class plane : unsigned { directory, lengths, control };
template <class Byte> struct storage {
    std::span<Byte> directory, lengths, control;
    std::array<std::span<Byte>, 3> planes() const noexcept {
        return {directory, lengths, control};
    }
};
namespace detail {
[[nodiscard]] std::expected<void, error>
admit_metadata(unsigned directory_bytes, unsigned control_bytes, unsigned lengths_alignment,
               const void* format, std::size_t format_bytes, storage<const byte>);
[[nodiscard]] std::expected<void, error> admit_storage(const layout&, storage<const byte>);
} // namespace detail

/// Named borrowed source identity. Keep this view, its layout and bytes stable
/// while operations/effects refer to it. Binding admits placement only; initialize
/// new storage or validate encoded contents before any read/mutation.
template <class Byte> class basic_view {
    static_assert(std::is_same_v<std::remove_const_t<Byte>, byte>);
    const layout* layout_;
    storage<Byte> storage_;
    basic_view(const layout& l, storage<Byte> s) : layout_(&l), storage_(s) {}

  public:
    [[nodiscard]] static std::expected<basic_view, error> bind(const layout& l, storage<Byte> s) {
        if (auto ok = detail::admit_storage(l, {s.directory, s.lengths, s.control}); !ok)
            return std::unexpected(ok.error());
        return basic_view(l, s);
    }
    static auto bind(const layout&&, storage<Byte>) = delete;
    const layout& format() const noexcept {
        return *layout_;
    }
    const storage<Byte>& windows() const noexcept {
        return storage_;
    }
    auto as_const() const noexcept {
        return basic_view<const byte>::assume_valid(
            *layout_, {storage_.directory, storage_.lengths, storage_.control});
    }
    /// Reuse an owner's established placement proof; same lifetime and content
    /// obligations as bind(). No encoded bytes are examined.
    static basic_view assume_valid(const layout& l, storage<Byte> s) {
        return {l, s};
    }
    static auto assume_valid(const layout&&, storage<Byte>) = delete;
    unsigned size() const noexcept {
        return detail::load<std::uint16_t>(storage_.control.data());
    }
    bool live_unchecked(unsigned rank) const noexcept {
        return !layout_->has_tombstones() ||
               !(std::to_integer<unsigned>(storage_.directory[rank / 8]) & (1u << (rank % 8)));
    }
    template <compact::edge E, class Point> unsigned boundary(const Point& point) const;
    std::uint32_t origin() const noexcept { return 0; }
    std::uint32_t at(unsigned rank) const;
    /// Optional retirement companion, selected only when the caller attaches it.
    rank_selection live_ranks() const noexcept {
        return layout_->has_tombstones()
                   ? rank_selection{storage_.directory.first(layout_->tombstone_bytes()), true}
                   : rank_selection::all();
    }
    template <class Body> decltype(auto) with_geometry(Body &&body) const;
    std::uint32_t extent() const noexcept {
        return detail::extent(*this);
    }
    /// Cold recovery/admission: checks the complete assigned interpretation and
    /// zeroed SIMD tail. Successful checked writes preserve this proof.
    [[nodiscard]] std::expected<void, error> validate() const;
};
using view = basic_view<byte>;
using const_view = basic_view<const byte>;
} // namespace ikea::streampack::detail::compact
