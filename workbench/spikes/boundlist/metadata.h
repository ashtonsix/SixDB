#pragma once
#include "boundlist.h"
#include "geometry.h"
#include <ikea/streampack.h>

namespace boundlist_spike {
namespace sp = ikea::streampack;
// Compatibility extraction experiment, deliberately using the existing format
// rather than choosing a new Boundlist schema. Metadata alone has no payload
// requirement. The caller supplies its admitted encoded/readable windows and
// keeps the shared layout and selection alive. Mutable effects still name the
// actual original StreamPack source; this temporary read projection is not a
// replacement source identity or an allocation/publication authority.
template <ikea::streampack::geometry Family, class W, class O> struct metadata {
    const sp::layout* format;
    sp::bytes directory, lengths, control;
    template <class B> static metadata from(const sp::basic_view<B>& source) {
        return {&source.format(), source.windows().directory, source.windows().lengths, source.windows().control};
    }
    template <class B> metadata rebind(const sp::basic_view<B>& source) const {
        // Caller admits the same shared immutable layout identity, as for the
        // current typed StreamPack prototype. Selection/lifetime stays external.
        return {format, source.windows().directory, source.windows().lengths, source.windows().control};
    }
    unsigned size() const { return read_element(reinterpret_cast<const std::uint16_t*>(control.data()), 0); }
    unsigned width_offset(unsigned rank) const {
        return rank / 16 * format->group_stride() + rank % 16 * sizeof(W);
    }
    const O* positions() const { return reinterpret_cast<const O*>(directory.data() + format->geometry_offset()); }
    const W* widths(unsigned group) const { return reinterpret_cast<const W*>(lengths.data() + group * format->group_stride()); }
    population eligible(bool only_live) const {
        return only_live && format->has_tombstones() ? population{directory.data(), true} : population{};
    }
    bool live(unsigned rank) const {
        return !format->has_tombstones() || !(std::to_integer<unsigned>(directory[rank / 8]) >> (rank % 8) & 1);
    }
    unsigned extent() const {
        if (format->has_extent()) return read_element(reinterpret_cast<const std::uint32_t*>(control.data() + 2), 0);
        if (format->fixed()) return size() * format->fixed_width();
        return size() ? unsigned(read_element(positions(), size() - 1)) : 0;
    }
    auto bounds(bool only_live = false) const {
        // Fixed recipes use their arithmetic methods below, not a fabricated
        // endpoint/checkpoint array. Both return types remain compile-time facts.
        if constexpr (Family == sp::geometry::direct)
            return direct_view<O>{positions(), size(), eligible(only_live)};
        else return delta_view<W, O>{widths(0), positions(), size(), format->group_stride() / unsigned(sizeof(W)), O(extent()), eligible(only_live)};
    }
    [[gnu::always_inline]] unsigned start(unsigned rank) const {
        if (format->fixed()) return rank * format->fixed_width();
        if constexpr (Family == sp::geometry::direct) return rank ? read_element(positions(), rank - 1) : 0;
        else {
            unsigned prefix;
            [[clang::always_inline]] prefix = ikea::boundlist::detail::prefix16_native(widths(rank / 16), rank % 16);
            return read_element(positions(), rank / 16) + prefix;
        }
    }
    unsigned reserved(unsigned rank) const {
        if (format->fixed()) return format->fixed_width();
        if constexpr (Family == sp::geometry::direct) return read_element(positions(), rank) - start(rank);
        else return read_element(reinterpret_cast<const W*>(lengths.data() + width_offset(rank)), 0);
    }
    unsigned length(unsigned rank) const {
        return format->has_used() ? read_element(reinterpret_cast<const W*>(lengths.data() + format->used_offset() + width_offset(rank)), 0)
                                  : reserved(rank);
    }
    [[gnu::always_inline]] sp::location locate(unsigned rank) const {
        const auto begin = start(rank), r = reserved(rank);
        const auto used = format->has_used() ? length(rank) : r;
        return {begin, std::uint16_t(used), std::uint16_t(r)};
    }
    template <method M = method::select_hybrid> unsigned physical(unsigned point) const {
        if (point >= extent()) return size();
        if (format->fixed()) return point / format->fixed_width();
        return seek<edge::after, M>(bounds(), O(point));
    }
    // Caller supplies the actual bytes after resolving this metadata selection.
    [[gnu::always_inline]] sp::bytes value(sp::bytes payload, unsigned rank) const {
        const auto begin = start(rank);
        return payload.subspan(begin, length(rank));
    }
    template <class Sink> [[gnu::always_inline]] void range(sp::bytes payload, unsigned first, unsigned count, Sink&& sink) const {
        if (!count) return;
        auto position = start(first);
        for (unsigned r = first; r < first + count; ++r) {
            unsigned reserved;
            if (format->fixed()) reserved = format->fixed_width();
            else if constexpr (Family == sp::geometry::direct) reserved = read_element(positions(), r) - position;
            else reserved = read_element(reinterpret_cast<const W*>(lengths.data() + width_offset(r)), 0);
            if (live(r)) sink(r, payload.subspan(position, format->has_used() ? length(r) : reserved));
            position += reserved;
        }
    }
    // Grants describe issued reads, including full initialized width groups.
    // Logical assigned rank bounds do not make those padding bytes reusable by
    // a writer while an older reader retains this selection.
    template <class Visit> void visit_value_metadata(unsigned rank, Visit&& visit) const {
        if (!format->fixed()) {
            if constexpr (Family == sp::geometry::direct) {
                if (rank) visit(sp::bytes(reinterpret_cast<const sp::byte*>(positions() + rank - 1), sizeof(O)));
                if (!format->has_used()) visit(sp::bytes(reinterpret_cast<const sp::byte*>(positions() + rank), sizeof(O)));
            } else {
                visit(sp::bytes(reinterpret_cast<const sp::byte*>(positions() + rank / 16), sizeof(O)));
                visit(sp::bytes(reinterpret_cast<const sp::byte*>(widths(rank / 16)), 16 * sizeof(W)));
            }
        }
        if (format->has_used()) visit(lengths.subspan(format->used_offset() + width_offset(rank), sizeof(W)));
        if (format->has_tombstones()) visit(directory.subspan(rank / 8, 1));
    }
};
} // namespace boundlist_spike
