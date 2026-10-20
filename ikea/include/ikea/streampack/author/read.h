#pragma once
#include <ikea/boundlist/author/read.h>
#include <ikea/streampack/view.h>

namespace ikea::streampack {
using byte_owner = boundlist::sidecar::position_owner;
struct location {
    std::uint32_t offset;
    std::uint16_t length, reservation;
};
namespace author {
template <geometry Family, class Width, class Offset, class Byte, class Used = Width>
class access : public boundlist::sidecar::author::access<Family, Width, Offset, basic_view<Byte>> {
    using base = boundlist::sidecar::author::access<Family, Width, Offset, basic_view<Byte>>;
    access(const basic_view<Byte> &source, const layout &format) : base(source, format) {}

  public:
    using used_type = Used;
    using base::base;
    using base::format;
    using base::geometry_data;
    using base::live;
    [[gnu::always_inline]] unsigned reserved(unsigned rank) const noexcept {
        return base::slot_size(rank);
    }
    using base::size;
    using base::source;
    using base::start;
    using base::width_offset;
    using base::widths;
    /// Companion addressing is owned here; it need not follow a geometry
    /// field's native carrier width.
    unsigned used_offset(unsigned rank) const noexcept {
        const auto stride = [&] {
            if constexpr (sizeof(Width) == sizeof(Used))
                return format().group_stride();
            else
                return format().used_group_stride();
        }();
        return format().used_offset() + (rank / 16) * stride + (rank % 16) * sizeof(Used);
    }
    unsigned length(unsigned rank) const noexcept {
        return format().has_used()
                   ? detail::load<Used>(source().windows().lengths.data() + used_offset(rank))
                   : reserved(rank);
    }
    [[gnu::always_inline]] location locate(unsigned rank) const noexcept {
        const auto p = base::locate(rank);
        const auto used =
            format().has_used()
                ? detail::load<Used>(source().windows().lengths.data() + used_offset(rank))
                : p.size;
        return {p.offset, std::uint16_t(used), p.size};
    }

    access rebind(const basic_view<Byte> &source) const { return {source, format()}; }
    access rebind(const basic_view<Byte> &&) const = delete;
    /// Value read windows retain Direct's start+U fast path. Geometry owns
    /// its read projection; the companion contributes only its own field.
    template <class Visit> void visit_value_metadata(unsigned rank, Visit &&visit) const {
        if (!format().has_used()) {
            base::visit_metadata(rank, visit);
            return;
        }
        base::visit_start_metadata(rank, visit);
        visit(bytes(source().windows().lengths.data() + used_offset(rank), sizeof(Used)));
        base::visit_liveness_metadata(rank, visit);
    }
    // Value is the fused hot-path boundary, just like get: outlining even this
    // span return adds a call per lookup and prevents schema facts from hoisting.
    [[gnu::always_inline]] bytes value(unsigned rank) const noexcept {
        if (format().has_used())
            return source().windows().payload.subspan(start(rank), length(rank));
        const auto p = locate(rank);
        return source().windows().payload.subspan(p.offset, p.length);
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
    /// Compose value projection inside the single geometry traversal.
    template <class Sink>
    [[gnu::always_inline]] void range(unsigned first, unsigned count, Sink &&sink) const {
        if (!count)
            return;
        base::slots(first, count, start(first),
                    [&](unsigned rank, unsigned begin, unsigned reservation)
                        __attribute__((always_inline)) {
                            if (live(rank)) {
                                const auto used = format().has_used() ? length(rank) : reservation;
                                sink(rank, source().windows().payload.subspan(begin, used));
                            }
                        });
    }
    /// Sparse selections retain a point path; duplicates and order are retained.
    template <class Sink> void select(std::span<const std::uint16_t> ranks, Sink &&sink) const {
        for (auto rank : ranks)
            if (live(rank))
                sink(rank, value(rank));
    }
};
/// Resolve schema-sized lanes once around a complete authored operation. The
/// caller's body receives a concrete geometry; no erased per-rank callback.
template <class Byte, class Body>
decltype(auto) dispatch(const basic_view<Byte> &source, Body &&body) {
    return boundlist::sidecar::detail::dispatch(
        source.format(), [&]<geometry G, class W, class O>() -> decltype(auto) {
            if constexpr (G != geometry::relative_ends || sizeof(W) == 1)
                return body(access<G, W, O, Byte, W>(source));
            else if constexpr (sizeof(W) == 4)
                return body(access<G, W, O, Byte, std::uint16_t>(source));
            else {
                if (source.format().used_bytes() == 1)
                    return body(access<G, W, O, Byte, std::uint8_t>(source));
                return body(access<G, W, O, Byte, std::uint16_t>(source));
            }
        });
}
} // namespace author

} // namespace ikea::streampack
