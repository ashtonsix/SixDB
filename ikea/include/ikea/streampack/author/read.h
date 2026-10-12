#pragma once
#include <algorithm>
#include <array>
#include <ikea/streampack/detail/prefix.h>
#include <ikea/streampack/view.h>
#include <utility>

namespace ikea::streampack {
struct location {
    std::uint32_t offset;
    std::uint16_t length, reservation;
};
struct byte_owner {
    std::uint16_t rank;
    bool tombstone;
};

namespace author {
/// Trusted typed geometry body. The source has admitted contents, ranks and
/// storage. Keep it visible inside a useful consumer; dispatch outside its loop.
template <geometry Family, class Width, class Offset, class Byte> class access {
    const basic_view<Byte>* source_;
    const layout* format_;
    access(const basic_view<Byte>& source, const layout& format)
        : source_(&source), format_(&format) {}

  public:
    static constexpr geometry family = Family;
    using width_type = Width;
    using offset_type = Offset;
    explicit access(const basic_view<Byte>& source) : source_(&source), format_(&source.format()) {}
    access(const basic_view<Byte>&&) = delete;
    const auto& source() const noexcept {
        return *source_;
    }
    const auto& format() const noexcept {
        return *format_;
    }
    /// Reuse shared schema preparation across a population. The owner proves
    /// the next binding has this exact shared layout identity; no schema loads
    /// depend on rank. All layouts are immutable for the operation's lifetime.
    access rebind(const basic_view<Byte>& source) const {
        return {source, *format_};
    }
    access rebind(const basic_view<Byte>&&) const = delete;
    unsigned size() const noexcept {
        return source_->size();
    }
    unsigned width_offset(unsigned rank) const noexcept {
        return (rank / 16) * format().group_stride() + (rank % 16) * sizeof(Width);
    }
    const byte* geometry_data() const noexcept {
        return source_->windows().directory.data() + format().geometry_offset();
    }
    const Width* widths(unsigned group) const noexcept {
        return reinterpret_cast<const Width*>(source_->windows().lengths.data() +
                                              group * format().group_stride());
    }
    std::uint32_t start(unsigned rank) const noexcept {
        if (format().fixed())
            return rank * format().fixed_width();
        if constexpr (Family == geometry::direct)
            return rank ? detail::load<Offset>(geometry_data() + (rank - 1) * sizeof(Offset)) : 0;
        else
            return detail::load<Offset>(geometry_data() + (rank / 16) * sizeof(Offset)) +
                   detail::prefix16_native(widths(rank / 16), rank % 16);
    }
    unsigned reserved(unsigned rank) const noexcept {
        if (format().fixed())
            return format().fixed_width();
        if constexpr (Family == geometry::direct)
            return detail::load<Offset>(geometry_data() + rank * sizeof(Offset)) - start(rank);
        else
            return detail::load<Width>(source_->windows().lengths.data() + width_offset(rank));
    }
    unsigned length(unsigned rank) const noexcept {
        if (!format().has_used())
            return reserved(rank);
        return detail::load<Width>(source_->windows().lengths.data() + format().used_offset() +
                                   width_offset(rank));
    }
    location locate(unsigned rank) const noexcept {
        const auto begin = start(rank);
        unsigned reservation;
        if (format().fixed())
            reservation = format().fixed_width();
        else if constexpr (Family == geometry::direct)
            reservation = detail::load<Offset>(geometry_data() + rank * sizeof(Offset)) - begin;
        else
            reservation =
                detail::load<Width>(source_->windows().lengths.data() + width_offset(rank));
        const auto used = format().has_used()
                              ? detail::load<Width>(source_->windows().lengths.data() +
                                                    format().used_offset() + width_offset(rank))
                              : reservation;
        return {begin, std::uint16_t(used), std::uint16_t(reservation)};
    }
    // Value is the fused hot-path boundary, just like get: outlining even this
    // span return adds a call per lookup and prevents schema facts from hoisting.
    [[gnu::always_inline]] bytes value(unsigned rank) const noexcept {
        if (format().has_used())
            return source_->windows().payload.subspan(start(rank), length(rank));
        const auto p = locate(rank);
        return source_->windows().payload.subspan(p.offset, p.length);
    }
    bool live(unsigned rank) const noexcept {
        return !format().has_tombstones() ||
               !(std::to_integer<unsigned>(source_->windows().directory[rank / 8]) &
                 (1u << (rank % 8)));
    }
    /// Keep cheap admission with its consumer: outlining expected<span> creates
    /// a hidden result buffer and prevents shared schema facts from hoisting.
    [[nodiscard, gnu::always_inline]] std::expected<bytes, error> get(unsigned rank) const {
        if (rank >= size())
            return std::unexpected(error::rank);
        if (!live(rank))
            return std::unexpected(error::retired);
        [[clang::always_inline]] return value(rank);
    }
    /// Exact metadata read windows for a trusted value lookup and liveness. This
    /// exposes addresses to an owner prefetch policy without issuing hints or
    /// retaining another binding. Rank/count have already been admitted.
    template <class Visit> void visit_value_metadata(unsigned rank, Visit&& visit) const {
        const auto& l = format();
        if (!l.fixed()) {
            if constexpr (Family == geometry::width) {
                visit(bytes(geometry_data() + (rank / 16) * sizeof(Offset), sizeof(Offset)));
                visit(bytes(reinterpret_cast<const byte*>(widths(rank / 16)), 16 * sizeof(Width)));
            } else {
                if (rank)
                    visit(bytes(geometry_data() + (rank - 1) * sizeof(Offset), sizeof(Offset)));
                if (!l.has_used())
                    visit(bytes(geometry_data() + rank * sizeof(Offset), sizeof(Offset)));
            }
        }
        if (l.has_used())
            visit(bytes(source_->windows().lengths.data() + l.used_offset() + width_offset(rank),
                        sizeof(Width)));
        if (l.has_tombstones())
            visit(bytes(source_->windows().directory.data() + rank / 8, 1));
    }
    /// The offset is below the coherent occupied extent. Zero reservations have
    /// no byte owner; retired reservations/slack still have one until packing.
    byte_owner reverse(unsigned offset) const noexcept {
        unsigned rank;
        if (format().fixed())
            rank = offset / format().fixed_width();
        else if constexpr (Family == geometry::direct) {
            unsigned lo = 0, hi = size();
            while (lo < hi) {
                const auto mid = (lo + hi) / 2;
                if (detail::load<Offset>(geometry_data() + mid * sizeof(Offset)) <= offset)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            rank = lo;
        } else {
            unsigned lo = 0, hi = (size() + 15) / 16;
            while (lo < hi) {
                const auto mid = (lo + hi) / 2;
                if (detail::load<Offset>(geometry_data() + mid * sizeof(Offset)) <= offset)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            const auto group = lo - 1;
            const auto relative =
                offset - detail::load<Offset>(geometry_data() + group * sizeof(Offset));
            rank = group * 16 + detail::owner16_native(widths(group), relative);
        }
        return {std::uint16_t(rank), !live(rank)};
    }
    /// One group decode, with bounded initialized padding, for dense selection.
    /// Partial tails are readable but are never presented to a value consumer.
    void starts16(unsigned group, std::uint32_t* out) const noexcept {
        const auto first = group * 16;
        if (format().fixed()) {
            for (unsigned i = 0; i < 16; ++i)
                out[i] = (first + i) * format().fixed_width();
        } else if constexpr (Family == geometry::width) {
            detail::starts16_native(widths(group), out,
                                    detail::load<Offset>(geometry_data() + group * sizeof(Offset)));
        } else {
#if defined(__aarch64__) || defined(__AVX2__)
            // Starts are the preceding endpoints. Only rank zero lacks one;
            // later chunks load their preceding window without a carry chain.
            unsigned i = 0;
            if (!first) {
                native::store(out, native::previous(native::load<Offset>(geometry_data()), 0));
                i = native::lanes;
            }
            for (; i < 16; i += native::lanes)
                native::store(out + i, native::load<Offset>(geometry_data() +
                                                            (first + i - 1) * sizeof(Offset)));
#else
            out[0] = start(first);
            for (unsigned i = 1; i < std::min(16u, size() - first); ++i)
                out[i] = detail::load<Offset>(geometry_data() + (first + i - 1) * sizeof(Offset));
#endif
        }
    }
    /// Ordered traversal carries the previous endpoint/prefix. It never pays a
    /// point prefix for each slot. Sink receives live ranks in original order.
    template <class Sink> void range(unsigned first, unsigned count, Sink&& sink) const {
        if (!count)
            return;
        auto begin = start(first);
        for (unsigned rank = first; rank < first + count; ++rank) {
            unsigned reservation;
            if (format().fixed())
                reservation = format().fixed_width();
            else if constexpr (Family == geometry::direct)
                reservation = detail::load<Offset>(geometry_data() + rank * sizeof(Offset)) - begin;
            else
                reservation =
                    detail::load<Width>(source_->windows().lengths.data() + width_offset(rank));
            if (live(rank)) {
                const auto used = format().has_used() ? length(rank) : reservation;
                sink(rank, source_->windows().payload.subspan(begin, used));
            }
            begin += reservation;
        }
    }
    /// Sparse selections retain a point path; duplicates and order are retained.
    template <class Sink> void select(std::span<const std::uint16_t> ranks, Sink&& sink) const {
        for (auto rank : ranks)
            if (live(rank))
                sink(rank, value(rank));
    }
};

/// Resolve schema-sized lanes once around a complete authored operation. The
/// caller's body receives a concrete geometry; no erased per-rank callback.
template <class Byte, class Body>
decltype(auto) dispatch(const basic_view<Byte>& source, Body&& body) {
    auto offsets = [&]<geometry G, class W>() -> decltype(auto) {
        switch (source.format().offset_bytes()) {
        case 1:
            return body(access<G, W, std::uint8_t, Byte>(source));
        case 2:
            return body(access<G, W, std::uint16_t, Byte>(source));
        default:
            return body(access<G, W, std::uint32_t, Byte>(source));
        }
    };
    auto widths = [&]<geometry G>() -> decltype(auto) {
        if (source.format().width_bytes() == 1)
            return offsets.template operator()<G, std::uint8_t>();
        return offsets.template operator()<G, std::uint16_t>();
    };
    if (source.format().schema().family == geometry::direct)
        return widths.template operator()<geometry::direct>();
    return widths.template operator()<geometry::width>();
}
} // namespace author

template <class Byte> std::uint32_t basic_view<Byte>::extent() const noexcept {
    const auto n = size();
    if (format().fixed())
        return n * format().fixed_width();
    if (format().has_extent())
        return detail::load<std::uint32_t>(storage_.control.data() + 2);
    if (!n)
        return 0;
    const auto* p =
        storage_.directory.data() + format().geometry_offset() + (n - 1) * format().offset_bytes();
    switch (format().offset_bytes()) {
    case 1:
        return detail::load<std::uint8_t>(p);
    case 2:
        return detail::load<std::uint16_t>(p);
    default:
        return detail::load<std::uint32_t>(p);
    }
}
} // namespace ikea::streampack
