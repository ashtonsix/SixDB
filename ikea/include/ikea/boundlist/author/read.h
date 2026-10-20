#pragma once
#include <ikea/boundlist/search.h>
#include <algorithm>
#include <array>
#include <ikea/boundlist/detail/prefix.h>
#include <ikea/boundlist/view.h>
#include <type_traits>
#include <utility>

namespace ikea::boundlist::sidecar {
struct location {
    std::uint32_t offset;
    std::uint16_t reservation;
};
struct byte_owner {
    std::uint16_t rank;
    bool tombstone;
};

namespace author {
/// Trusted typed geometry body. The source has admitted contents, ranks and
/// storage. Keep it visible inside a useful consumer; dispatch outside its loop.
template <geometry Family, class Width, class Offset, class Source> class access {
    const Source* source_;
    using format_type = std::remove_cvref_t<decltype(std::declval<const Source&>().format())>;
    const format_type* format_;

  protected:
    access(const Source& source, const format_type& format) : source_(&source), format_(&format) {}

  public:
    static constexpr geometry family = Family;
    using width_type = Width;
    using offset_type = Offset;
    explicit access(const Source& source) : source_(&source), format_(&source.format()) {}
    access(const Source&&) = delete;
    const auto& source() const noexcept {
        return *source_;
    }
    const auto& format() const noexcept {
        return *format_;
    }
    /// Reuse shared schema preparation across a population. The owner proves
    /// the next binding has this exact shared layout identity; no schema loads
    /// depend on rank. All layouts are immutable for the operation's lifetime.
    access rebind(const Source& source) const {
        return {source, *format_};
    }
    access rebind(const Source&&) const = delete;
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
                   boundlist::detail::prefix16_native(widths(rank / 16), rank % 16);
    }
    unsigned reserved(unsigned rank) const noexcept {
        if (format().fixed())
            return format().fixed_width();
        if constexpr (Family == geometry::direct)
            return detail::load<Offset>(geometry_data() + rank * sizeof(Offset)) - start(rank);
        else
            return detail::load<Width>(source_->windows().lengths.data() + width_offset(rank));
    }
    [[gnu::always_inline]] location locate(unsigned rank) const noexcept {
        const auto begin = start(rank);
        const auto width = [&] {
            if (format().fixed())
                return format().fixed_width();
            if constexpr (Family == geometry::direct)
                return unsigned(detail::load<Offset>(geometry_data() + rank * sizeof(Offset))) -
                       begin;
            else
                return unsigned(
                    detail::load<Width>(source_->windows().lengths.data() + width_offset(rank)));
        }();
        return {begin, std::uint16_t(width)};
    }
    bool live(unsigned rank) const noexcept {
        return !format().has_tombstones() ||
               !(std::to_integer<unsigned>(source_->windows().directory[rank / 8]) &
                 (1u << (rank % 8)));
    }
    /// Start-only geometry reads. A value with separate U need not load R.
    template <class Visit> void visit_start_metadata(unsigned rank, Visit&& visit) const {
        if (format().fixed())
            return;
        if constexpr (Family == geometry::width) {
            visit(bytes(geometry_data() + (rank / 16) * sizeof(Offset), sizeof(Offset)));
            visit(bytes(reinterpret_cast<const byte*>(widths(rank / 16)), 16 * sizeof(Width)));
        } else if (rank)
            visit(bytes(geometry_data() + (rank - 1) * sizeof(Offset), sizeof(Offset)));
    }
    template <class Visit> void visit_liveness_metadata(unsigned rank, Visit&& visit) const {
        if (format().has_tombstones())
            visit(bytes(source_->windows().directory.data() + rank / 8, 1));
    }
    /// Issued read windows for reservation location/liveness, not value length.
    template <class Visit> void visit_metadata(unsigned rank, Visit&& visit) const {
        visit_start_metadata(rank, visit);
        if (!format().fixed() && Family == geometry::direct)
            visit(bytes(geometry_data() + rank * sizeof(Offset), sizeof(Offset)));
        visit_liveness_metadata(rank, visit);
    }
    /// Variable-layout geometric projection, without population. Fixed layouts
    /// use boundary() directly; they have no stored endpoint/width windows.
    auto bounds() const noexcept {
        if constexpr (Family == geometry::direct)
            return boundlist::direct_source<Offset>{
                reinterpret_cast<const Offset*>(geometry_data()), size()};
        else
            return boundlist::width_source<Width, Offset>{
                widths(0), reinterpret_cast<const Offset*>(geometry_data()), size(),
                format().group_stride() / unsigned(sizeof(Width)), Offset(source().extent())};
    }
    std::uint32_t at(unsigned rank) const noexcept {
        if (format().fixed())
            return (rank + 1) * format().fixed_width();
        if constexpr (Family == geometry::direct)
            return detail::load<Offset>(geometry_data() + rank * sizeof(Offset));
        else
            return bounds().at(rank);
    }
    template <boundlist::edge E, class Point>
    [[gnu::always_inline]] unsigned boundary(const Point& point) const noexcept {
        if (!format().fixed()) {
            if constexpr (Family == geometry::width && std::integral<Point>)
                return boundlist::detail::prepare_width<E, 16>(bounds(), point).finish();
            else
                return bounds().template boundary<E>(point);
        }
        const boundlist::detail::prepared_query<std::uint32_t, Point> q(point);
        const auto n = size(), width = format().fixed_width();
        if (q.relation)
            return q.relation < 0 ? 0 : n;
        if (!width)
            return boundlist::detail::meets<E>(0u, q.value) ? 0 : n;
        if constexpr (E == boundlist::edge::at_or_after) {
            if (!q.value)
                return 0;
            return std::min(n, (q.value - 1) / width);
        } else
            return std::min(n, q.value / width);
    }
    template <boundlist::edge E, class Point>
    [[gnu::always_inline]] auto prepare(const Point& point) const noexcept {
        if constexpr (Family == geometry::width) {
            if constexpr (std::integral<Point>)
                if (!format().fixed())
                    return boundlist::detail::prepare_width<E, 16>(bounds(), point);
            return boundlist::detail::width_search<Width, E>{nullptr, boundary<E>(point), 0};
        } else
            return boundlist::detail::ready_search{boundary<E>(point)};
    }
    unsigned next_eligible(unsigned first, unsigned n) const noexcept {
        if (!format().has_tombstones())
            return first;
        return boundlist::population{source().windows().directory.first(format().tombstone_bytes()),
                                     true}
            .next(first, n);
    }
    unsigned previous_eligible(unsigned limit) const noexcept {
        if (!format().has_tombstones())
            return limit ? limit - 1 : size();
        return boundlist::population{source().windows().directory.first(format().tombstone_bytes()),
                                     true}
            .previous(limit, size());
    }
    /// offset is below the coherent extent. Retired reservations and slack still
    /// have a physical owner; zero reservations do not. No population search.
    [[gnu::always_inline]] byte_owner reverse(unsigned offset) const noexcept {
        unsigned rank;
        if (format().fixed())
            rank = offset / format().fixed_width();
        else if constexpr (Family == geometry::direct)
            rank = boundlist::detail::hybrid_array<boundlist::edge::after, true>(
                reinterpret_cast<const Offset*>(geometry_data()), size(), Offset(offset));
        else {
            // The physical-owner admission proves C0 <= offset < extent. Do
            // not reload/check extent through the general search projection.
            const auto next = boundlist::detail::checkpoint_boundary<boundlist::edge::after, 16>(
                reinterpret_cast<const Offset*>(geometry_data()), (size() + 15) / 16,
                Offset(offset));
            const auto group = next - 1;
            const auto relative =
                offset - detail::load<Offset>(geometry_data() + group * sizeof(Offset));
            rank = group * 16 + boundlist::detail::owner16_native(widths(group), relative);
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
            boundlist::detail::starts16_native(
                widths(group), out, detail::load<Offset>(geometry_data() + group * sizeof(Offset)));
        } else {
#if defined(__aarch64__) || defined(__AVX2__)
            // Starts are the preceding endpoints. Only rank zero lacks one;
            // later chunks load their preceding window without a carry chain.
            unsigned i = 0;
            if (!first) {
                boundlist::native::store(out,
                                         boundlist::native::previous(
                                             boundlist::native::load<Offset>(geometry_data()), 0));
                i = boundlist::native::lanes;
            }
            for (; i < 16; i += boundlist::native::lanes)
                boundlist::native::store(out + i,
                                         boundlist::native::load<Offset>(
                                             geometry_data() + (first + i - 1) * sizeof(Offset)));
#else
            out[0] = start(first);
            for (unsigned i = 1; i < std::min(16u, size() - first); ++i)
                out[i] = detail::load<Offset>(geometry_data() + (first + i - 1) * sizeof(Offset));
#endif
        }
    }
    /// Traverse original geometry, including retired/zero reservations. The
    /// caller admits begin == original start(first) and the complete rank range.
    /// A sink may rewrite already-read geometry, but cannot change the format,
    /// count, or unread fields. Native scalar fields stay fused at the sink.
    template <class Sink>
    [[gnu::always_inline]] void slots(unsigned first, unsigned count, unsigned begin,
                                      Sink&& sink) const {
        auto walk = [&](auto get) __attribute__((always_inline)) {
            for (unsigned rank = first; rank < first + count; ++rank) {
                const auto reservation = get(rank, begin);
                [[clang::always_inline]] sink(rank, begin, reservation);
                begin += reservation;
            }
        };
        if (format().fixed()) {
            const auto width = format().fixed_width();
            walk([&](unsigned, unsigned) __attribute__((always_inline)) { return width; });
        } else if constexpr (Family == geometry::direct) {
            walk([&](unsigned rank, unsigned from) __attribute__((always_inline)) {
                return unsigned(detail::load<Offset>(geometry_data() + rank * sizeof(Offset))) -
                       from;
            });
        } else {
            walk([&](unsigned rank, unsigned) __attribute__((always_inline)) {
                return unsigned(
                    detail::load<Width>(source_->windows().lengths.data() + width_offset(rank)));
            });
        }
    }
    /// Live-range projection over the same carried geometry, without repeated
    /// point prefixes. Sinks cannot invalidate the traversal's admitted state.
    template <class Sink>
    [[gnu::always_inline]] void range(unsigned first, unsigned count, Sink&& sink) const {
        if (!count)
            return;
        slots(first, count, start(first),
              [&](unsigned rank, unsigned begin, unsigned reservation)
                  __attribute__((always_inline)) {
                      if (live(rank))
                          sink(rank, location{begin, std::uint16_t(reservation)});
                  });
    }
};

} // namespace author
namespace detail {
/// One schema switch for metadata and payload compositions. The factory creates
/// the concrete access at the consumer; no projected source or erased callback.
template <class Format, class Make> decltype(auto) dispatch(const Format& format, Make&& make) {
    auto offsets = [&]<geometry G, class W>() -> decltype(auto) {
        switch (format.offset_bytes()) {
        case 1:
            return make.template operator()<G, W, std::uint8_t>();
        case 2:
            return make.template operator()<G, W, std::uint16_t>();
        default:
            return make.template operator()<G, W, std::uint32_t>();
        }
    };
    auto widths = [&]<geometry G>() -> decltype(auto) {
        if (format.width_bytes() == 1)
            return offsets.template operator()<G, std::uint8_t>();
        return offsets.template operator()<G, std::uint16_t>();
    };
    if (format.schema().family == geometry::direct)
        return widths.template operator()<geometry::direct>();
    return widths.template operator()<geometry::width>();
}
} // namespace detail
namespace author {
/// Resolve schema-sized lanes once around a complete authored operation.
template <class Source, class Body> decltype(auto) dispatch(const Source& source, Body&& body) {
    return detail::dispatch(source.format(), [&]<geometry G, class W, class O>() -> decltype(auto) {
        return body(access<G, W, O, Source>(source));
    });
}
} // namespace author

template <class Byte>
template <boundlist::edge E, class Point>
unsigned basic_view<Byte>::boundary(const Point& point) const {
    return author::dispatch(*this, [&](auto a) { return a.template boundary<E>(point); });
}

} // namespace ikea::boundlist::sidecar
