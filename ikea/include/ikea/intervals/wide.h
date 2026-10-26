// Intervals representation: 64-bit fields decoded by a parent, with a
// checkpoint every `spacing` ranks. Values are rank ends: a checkpoint is the
// absolute end of its region's first rank, and fields reconstruct the rest
// modulo 2^64, so signed differences stored in two's complement reconstruct
// exactly. This borrowed source defines no persisted byte format.
//
// Reconstruction (at, start, visit_intervals) needs no ordering. Search
// (lower_bound, upper_bound) assumes the ends never decrease; the owner
// proves that before searching, as for every Intervals source.
//
// Trusted window: spacing >= 1; count readable fields and ceil(count /
// spacing) readable checkpoints; every rank and visited range within count.
// No readable halo is assumed beyond them, and the field at a checkpoint rank
// is never read.
//
// field_xor is applied to every field as it is read, so a parent can borrow
// fields still in its own encoding (Frame's offset binary). In
// visit_intervals a sink may overwrite the field of the rank it is given,
// so a parent can reconstruct in place over its decoded fields.
#pragma once
#include <ikea/intervals/detail/search.h>
#include <ikea/intervals/types.h>

#include <algorithm>
#include <concepts>
#include <cstdint>

namespace ikea::intervals {

/// Fields are each rank's difference from the previous rank's end; the
/// field at a checkpoint rank is ignored (its end is the checkpoint).
struct wide_sizes {
    const std::uint64_t *sizes;
    const std::uint64_t *checkpoints; // the end of each region's first rank
    unsigned count;
    unsigned spacing; // ranks per checkpoint, at least 1
    std::uint64_t field_xor = 0;

    std::uint64_t size_of(unsigned rank) const noexcept { return detail::read_element(sizes, rank) ^ field_xor; }
    unsigned size() const noexcept { return count; }
    unsigned groups() const noexcept { return count / spacing + unsigned(count % spacing != 0); }
    std::uint64_t at(unsigned rank) const noexcept {
        const unsigned first = rank - rank % spacing;
        std::uint64_t end = detail::read_element(checkpoints, rank / spacing);
        for (unsigned i = first + 1; i <= rank; ++i)
            end += size_of(i);
        return end;
    }
    std::uint64_t start(unsigned rank) const noexcept {
        return rank ? at(rank - 1) : detail::read_element(checkpoints, 0);
    }
    /// Consecutive ranks, region by region: one division per call, one
    /// checkpoint load per region, then one add per rank.
    template <class Sink> [[gnu::always_inline]] void visit_intervals(unsigned begin, unsigned take, Sink &&sink) const {
        if (!take)
            return;
        std::uint64_t position = start(begin), end = position;
        const unsigned stop = begin + take;
        unsigned region = begin / spacing, offset = begin % spacing;
        for (unsigned rank = begin; rank < stop; ++region, offset = 0) {
            const unsigned limit = rank + std::min(spacing - offset, stop - rank);
            if (!offset) {
                end = detail::read_element(checkpoints, region);
                [[clang::always_inline]] sink(rank, interval<std::uint64_t>{position, end});
                position = end, ++rank;
            }
            for (; rank < limit; ++rank) {
                end += size_of(rank);
                [[clang::always_inline]] sink(rank, interval<std::uint64_t>{position, end});
                position = end;
            }
        }
    }
    /// The first rank whose end meets point: the last region whose checkpoint
    /// does not, then a carried scan of that region.
    template <edge Edge, std::integral Query> [[gnu::always_inline]] unsigned boundary(Query point) const {
        const detail::prepared_query<std::uint64_t, Query> query(point);
        if (query.relation)
            return query.relation < 0 ? 0u : count;
        const unsigned next = detail::checkpoint_boundary<Edge, 0>(checkpoints, groups(), query.value);
        if (next == 0)
            return 0;
        const unsigned group = next - 1, first = group * spacing, last = first + std::min(spacing, count - first);
        std::uint64_t end = detail::read_element(checkpoints, group);
        for (unsigned rank = first + 1; rank < last; ++rank) {
            end += size_of(rank);
            if (detail::meets<Edge>(end, query.value))
                return rank;
        }
        return last;
    }
};

/// Fields are each rank's end relative to its region's checkpoint; the field
/// at a checkpoint rank is ignored (its end is the checkpoint).
struct wide_ends {
    const std::uint64_t *ends;
    const std::uint64_t *checkpoints;
    unsigned count;
    unsigned spacing;
    std::uint64_t field_xor = 0;

    std::uint64_t end_of(unsigned rank) const noexcept { return detail::read_element(ends, rank) ^ field_xor; }

    unsigned size() const noexcept { return count; }
    unsigned groups() const noexcept { return count / spacing + unsigned(count % spacing != 0); }
    std::uint64_t at(unsigned rank) const noexcept {
        const std::uint64_t base = detail::read_element(checkpoints, rank / spacing);
        return rank % spacing ? base + end_of(rank) : base;
    }
    std::uint64_t start(unsigned rank) const noexcept { return rank ? at(rank - 1) : at(0); }
    /// Consecutive ranks, region by region: one division per call, one
    /// checkpoint per region, then the checkpoint plus each contiguous field.
    template <class Sink> [[gnu::always_inline]] void visit_intervals(unsigned begin, unsigned take, Sink &&sink) const {
        if (!take)
            return;
        std::uint64_t position = start(begin);
        const unsigned stop = begin + take;
        unsigned region = begin / spacing, offset = begin % spacing;
        for (unsigned rank = begin; rank < stop; ++region, offset = 0) {
            const unsigned limit = rank + std::min(spacing - offset, stop - rank);
            const std::uint64_t base = detail::read_element(checkpoints, region);
            if (!offset) {
                [[clang::always_inline]] sink(rank, interval<std::uint64_t>{position, base});
                position = base, ++rank;
            }
            for (; rank < limit; ++rank) {
                const std::uint64_t end = base + end_of(rank);
                [[clang::always_inline]] sink(rank, interval<std::uint64_t>{position, end});
                position = end;
            }
        }
    }
    template <edge Edge, std::integral Query> [[gnu::always_inline]] unsigned boundary(Query point) const {
        const detail::prepared_query<std::uint64_t, Query> query(point);
        if (query.relation)
            return query.relation < 0 ? 0u : count;
        const unsigned next = detail::checkpoint_boundary<Edge, 0>(checkpoints, groups(), query.value);
        if (next == 0)
            return 0;
        const unsigned group = next - 1, first = group * spacing, last = first + std::min(spacing, count - first);
        const std::uint64_t base = detail::read_element(checkpoints, group);
        for (unsigned rank = first + 1; rank < last; ++rank)
            if (detail::meets<Edge>(base + end_of(rank), query.value))
                return rank;
        return last;
    }
};

} // namespace ikea::intervals
