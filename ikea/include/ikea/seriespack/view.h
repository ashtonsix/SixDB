#pragma once

#include <ikea/seriespack/layout.h>

#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <type_traits>

namespace ikea::seriespack {

/// Borrowed tile stream; bytes in stride gaps may belong to other components.
template<class Byte>
struct basic_plane {
    static_assert(std::is_same_v<std::remove_const_t<Byte>, std::byte>);
    std::span<Byte> bytes;
    /// Bytes between successive tile starts, not between logical values.
    std::size_t stride{};
};

/// Payload and optional leading-byte planes, each independently placed. Head 0
/// holds the most significant byte; head 1 holds the next. Owns no storage.
template<class Byte>
struct basic_placement {
    basic_plane<Byte> payload;
    std::array<basic_plane<Byte>, 2> heads;
};

/// Borrows the same placement read-only; does not copy bytes or create a snapshot.
template<class Byte>
[[nodiscard]] constexpr basic_placement<const std::byte>
as_const(basic_placement<Byte> placement) noexcept {
    return {{placement.payload.bytes, placement.payload.stride},
            {{{placement.heads[0].bytes, placement.heads[0].stride},
              {placement.heads[1].bytes, placement.heads[1].stride}}}};
}

/// Checks extents, alignment and occupied-range overlap for n logical values.
/// Envelopes may overlap through gaps. No byte scan: encoding, zero final slack,
/// lifetime and synchronization remain owner obligations.
[[nodiscard]] std::expected<void, error>
validate_placement(description layout, std::size_t n,
                   basic_placement<const std::byte> placement) noexcept;

/// Runtime description, logical length and borrowed placement. Copies share bytes;
/// the owner retains storage and synchronizes accesses. Length excludes final slack.
template<class Byte>
class basic_view {
public:
    using byte_type = Byte;

    /// Checks placement for n values; neither allocates nor initializes bytes.
    [[nodiscard]] static std::expected<basic_view, error>
    attach(description layout, std::size_t n, basic_placement<Byte> placement) noexcept {
        const auto checked = validate_placement(layout, n, seriespack::as_const(placement));
        if (!checked) return std::unexpected(checked.error());
        return assume_valid(layout, n, placement);
    }

    /// Unchecked construction; the caller proves attach's checks and owner obligations.
    [[nodiscard]] static constexpr basic_view
    assume_valid(description layout, std::size_t n, basic_placement<Byte> placement) noexcept {
        return basic_view(layout, n, placement);
    }

    [[nodiscard]] constexpr description layout() const noexcept { return layout_; }
    /// Logical value count, excluding initialized capacity and reserved bytes.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return n_; }
    [[nodiscard]] constexpr const basic_placement<Byte>& placement() const noexcept {
        return placement_;
    }

    /// Read-only access to the same bytes and coordinates; no snapshot or lease.
    [[nodiscard]] constexpr basic_view<const std::byte> as_const() const noexcept {
        return basic_view<const std::byte>::assume_valid(layout_, n_,
                                                        seriespack::as_const(placement_));
    }

private:
    constexpr basic_view(description layout, std::size_t n, basic_placement<Byte> placement) noexcept
        : layout_(layout), n_(n), placement_(placement) {}
    description layout_;
    std::size_t n_;
    basic_placement<Byte> placement_;
};

using const_view = basic_view<const std::byte>;
using mutable_view = basic_view<std::byte>;

/// Borrowed length/placement with the description in Format; shares basic_view's owner obligations.
template<class Format, class Byte>
class basic_static_view {
public:
    using format_type = Format;
    using byte_type = Byte;
    using scalar_type = typename Format::scalar_type;

    /// Checks placement for n values in Format; neither allocates nor initializes bytes.
    [[nodiscard]] static std::expected<basic_static_view, error>
    attach(std::size_t n, basic_placement<Byte> placement) noexcept {
        const auto checked = validate_placement(Format::layout, n, seriespack::as_const(placement));
        if (!checked) return std::unexpected(checked.error());
        return assume_valid(n, placement);
    }

    /// Unchecked construction; the caller proves attach's checks and owner obligations.
    [[nodiscard]] static constexpr basic_static_view
    assume_valid(std::size_t n, basic_placement<Byte> placement) noexcept {
        return basic_static_view(n, placement);
    }

    [[nodiscard]] static constexpr description layout() noexcept { return Format::layout; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return n_; }
    [[nodiscard]] constexpr const basic_placement<Byte>& placement() const noexcept {
        return placement_;
    }
    /// Exposes the description at runtime without moving bytes or repeating admission.
    [[nodiscard]] constexpr basic_view<Byte> as_dynamic() const noexcept {
        return basic_view<Byte>::assume_valid(Format::layout, n_, placement_);
    }
    [[nodiscard]] constexpr basic_static_view<Format, const std::byte> as_const() const noexcept {
        return basic_static_view<Format, const std::byte>::assume_valid(
            n_, seriespack::as_const(placement_));
    }

private:
    constexpr basic_static_view(std::size_t n, basic_placement<Byte> placement) noexcept
        : n_(n), placement_(placement) {}
    std::size_t n_;
    basic_placement<Byte> placement_;
};

template<class Format>
using static_const_view = basic_static_view<Format, const std::byte>;
template<class Format>
using static_mutable_view = basic_static_view<Format, std::byte>;

/// Recovers Format only on an exact description match; reuses placement admission.
template<class Format, class Byte>
[[nodiscard]] constexpr std::expected<basic_static_view<Format, Byte>, error>
specialize(const basic_view<Byte>& source) noexcept {
    if (source.layout() != Format::layout)
        return std::unexpected(error::invalid_description);
    return basic_static_view<Format, Byte>::assume_valid(source.size(), source.placement());
}

} // namespace ikea::seriespack
