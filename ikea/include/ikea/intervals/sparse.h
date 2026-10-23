// Intervals representation: sparse directories, native 16-field kernels.
// This borrowed source defines no persisted byte format. The owner admits coherent checkpoints,
// nonnegative widths, sums fitting Point and initialized final native padding.
#pragma once
#include <ikea/intervals/geometry.h>
#include <ikea/intervals/detail/native.h>
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace ikea::intervals {

template <unsigned Spacing, class Width, std::unsigned_integral Point = std::uint32_t,
          unsigned CheckpointCapacity = 0, bool Independent = false>
struct sparse_sizes {
    static_assert(Spacing >= 16 && Spacing <= 256 && Spacing % 16 == 0);
    static_assert(std::same_as<Width, std::uint8_t> || std::same_as<Width, std::uint16_t>);
    using coordinate = std::common_type_t<Point, std::uint32_t>;
    const Width *widths;
    const Point *checkpoints; // Absolute start of each Spacing-rank region.
    unsigned count;
    Point first, limit;
    unsigned maximum = std::numeric_limits<Width>::max();
    unsigned stride = 16; // Width elements between native groups, including siblings.
    const Width *native_group(unsigned g) const { return widths + std::size_t(g) * stride; }

    unsigned size() const noexcept { return count; }
    coordinate origin() const noexcept { return first; }
    coordinate extent() const noexcept { return limit; }
    unsigned groups() const noexcept { return count / Spacing + unsigned(count % Spacing != 0); }

    // Prefix execution policy for throughput-oriented rank/range callers.
    // Inverse routing keeps the measured serial implementation.
    auto for_independent_reads() const {
        return sparse_sizes<Spacing,Width,Point,CheckpointCapacity,true>{
            widths,checkpoints,count,first,limit,maximum,stride};
    }
    template<bool Start = false>
    coordinate prefix(unsigned group,unsigned take) const {
        coordinate result=detail::read_element(checkpoints,group);
        const Width *base=native_group(group*(Spacing/16));
#if defined(__aarch64__) || defined(__AVX2__)
        if constexpr(Spacing==64 && Independent) {
            if(count-group*Spacing>48) return result+native_detail::prefix64(base,stride,take);
        }
#endif
        // start(rank) admits rank<count: its final native window exists even
        // at a zero residual. Endpoints instead use a one-based final window,
        // so exact16/32/48/64 tails never read the following native group.
        if constexpr(!Start) if(!take) return result;
        const unsigned complete=Start ? take/16 : (take-1)/16;
        for(unsigned i=0;i<complete;++i)
            result+=detail::prefix16_native(base+std::size_t(i)*stride,16);
        return result+detail::prefix16_native(base+std::size_t(complete)*stride,
                                                 Start ? take%16 : (take-1)%16+1);
    }
    coordinate at(unsigned rank) const { return prefix(rank / Spacing, rank % Spacing + 1); }
    coordinate start(unsigned rank) const { return prefix<true>(rank / Spacing, rank % Spacing); }
    Width slot_size(unsigned rank) const { return detail::read_element(native_group(rank / 16), rank % 16); }

    template <class Sink>
    [[gnu::always_inline]] void visit_intervals(unsigned begin, unsigned take, Sink &&sink) const {
        if (!take) return;
        coordinate position = start(begin);
        for (unsigned i = 0; i < take; ++i) {
            const unsigned rank = begin + i;
            const coordinate next = position + slot_size(rank);
            [[clang::always_inline]] sink(rank, interval<coordinate>{position, next});
            position = next;
        }
    }

    // Public reverse has already checked origin <= point < extent. Preserve
    // that admitted handoff instead of repeating checked boundary preparation.
    template <std::integral Query>
    [[gnu::always_inline]] unsigned owner_rank(Query point) const {
        const Point value = static_cast<Point>(point);
        const unsigned next = detail::checkpoint_boundary<edge::after,CheckpointCapacity>(checkpoints,groups(),value);
        const unsigned group = next - 1, begin = group * Spacing;
        const Point base = detail::read_element(checkpoints, group);
        unsigned offset = static_cast<unsigned>(value - base);
        Point position = base;
        const unsigned end = begin + std::min(Spacing, count - begin);
        for (unsigned window = 0; window < end - begin; window += 16) {
            const unsigned rank = begin + window;
            const auto *local = native_group(rank / 16);
            unsigned span_proof = static_cast<unsigned>(std::min<std::uint64_t>(
                16ull * maximum, static_cast<std::uint64_t>(limit - position)));
            if (window + 16 < end - begin) {
                const unsigned span = detail::prefix16_native(local, 16);
                if (offset >= span) {
                    offset -= span;
                    position += span;
                    continue;
                }
                span_proof = span;
            }
            const auto found = [&] {
                if constexpr (sizeof(Width) == 1)
                    return detail::owner16_native(local, offset);
                else
                    return detail::owner16_native(local, offset, span_proof);
            }();
            return rank + found;
        }
        return count;
    }

    template <edge Edge> struct pending {
        const Width *fields;
        unsigned first, take, stride, maximum;
        Point offset, remaining;
        bool has_local_read() const noexcept { return fields != nullptr; }
        std::span<const std::byte> local_window() const noexcept {
            if (!fields) return {};
            const unsigned windows = take / 16 + (take % 16 != 0);
            return {reinterpret_cast<const std::byte *>(fields),
                    (std::size_t(windows - 1) * stride + 16) * sizeof(Width)};
        }
        [[gnu::always_inline]] unsigned finish() const noexcept {
            if (!fields) return first;
            Point relative = offset, left = remaining;
            for (unsigned window = 0; window < take; window += 16) {
                const auto *local = fields + std::size_t(window / 16) * stride;
                const unsigned count = std::min(16u, take - window);
                unsigned span_proof = static_cast<unsigned>(std::min<std::uint64_t>(16ull * maximum, left));
                if (window + 16 < take) {
                    const unsigned span = detail::prefix16_native(local, 16);
                    if (relative >= span) {relative -= span; left -= span; continue;}
                    span_proof = span;
                }
                if (relative > 16u * maximum) return first + take;
                const unsigned found = [&] {
                    if constexpr (sizeof(Width) == 1)
                        return detail::owner16_native(local, unsigned(relative));
                    else
                        return detail::owner16_native(local, unsigned(relative), span_proof);
                }();
                return first + window + std::min(found, count);
            }
            return first + take;
        }
    };
    template <edge Edge, std::integral Query>
    [[gnu::always_inline]] pending<Edge> prepare(Query point) const {
        auto ready = [](unsigned rank) -> pending<Edge> {return {nullptr,rank,0,0,0,0,0};};
        const detail::prepared_query<Point, Query> query(point);
        if (query.relation) return ready(query.relation < 0 ? 0u : count);
        if (!count || !detail::meets<Edge>(limit, query.value)) return ready(count);
        const unsigned next = detail::checkpoint_boundary<Edge,CheckpointCapacity>(checkpoints,groups(),query.value);
        const unsigned group = next ? next - 1 : 0, begin = group * Spacing;
        const Point base = detail::read_element(checkpoints, group);
        if (query.value < base || (Edge == edge::at_or_after && query.value == base)) return ready(begin);
        return {native_group(begin / 16),begin,std::min(Spacing,count-begin),stride,maximum,
                Point(query.value-base-unsigned(Edge==edge::at_or_after)),Point(limit-base)};
    }
    template <edge Edge, class Query>
    [[gnu::always_inline]] unsigned boundary(const Query &point) const {
        if constexpr (std::integral<Query>) return prepare<Edge>(point).finish();
        else {
            unsigned low = 0, high = count;
            while (low < high) {
                const unsigned middle = low + (high - low) / 2;
                if (detail::meets<Edge>(at(middle), point)) high = middle;
                else low = middle + 1;
            }
            return low;
        }
    }
};

// A competing representation: each field is the cumulative end relative to
// its sparse checkpoint. Local must admit Spacing * maximum, not just maximum.
template <unsigned Spacing, std::unsigned_integral Local,
          std::unsigned_integral Point = std::uint32_t, unsigned CheckpointCapacity = 0>
struct sparse_ends {
    static_assert(Spacing >= 16 && Spacing <= 256 && Spacing % 16 == 0);
    using coordinate = std::common_type_t<Point, std::uint32_t>;
    const Local *ends;
    const Point *checkpoints;
    unsigned count;
    Point first, limit;

    unsigned size() const noexcept { return count; }
    coordinate origin() const noexcept { return first; }
    coordinate extent() const noexcept { return limit; }
    unsigned groups() const noexcept { return count / Spacing + unsigned(count % Spacing != 0); }
    coordinate at(unsigned rank) const {
        return detail::read_element(checkpoints, rank / Spacing) +
               detail::read_element(ends, rank);
    }
    coordinate start(unsigned rank) const {
        return detail::read_element(checkpoints, rank / Spacing) +
               (rank % Spacing ? detail::read_element(ends, rank - 1) : 0);
    }
    Local slot_size(unsigned rank) const {
        return detail::read_element(ends, rank) -
               (rank % Spacing ? detail::read_element(ends, rank - 1) : 0);
    }
    template <class Sink>
    [[gnu::always_inline]] void visit_intervals(unsigned begin, unsigned take, Sink &&sink) const {
        if (!take) return;
        coordinate position = start(begin);
        for (unsigned i = 0; i < take; ++i) {
            const unsigned rank = begin + i;
            const coordinate next = at(rank);
            [[clang::always_inline]] sink(rank, interval<coordinate>{position, next});
            position = next;
        }
    }
    template <std::integral Query>
    [[gnu::always_inline]] unsigned owner_rank(Query point) const {
        const Point value = static_cast<Point>(point);
        const unsigned next = detail::checkpoint_boundary<edge::after, CheckpointCapacity>(
            checkpoints, groups(), value);
        const unsigned group = next - 1, begin = group * Spacing;
        const Point base = detail::read_element(checkpoints, group);
        return begin + detail::hybrid_array<edge::after, true>(
            ends + begin, std::min(Spacing, count - begin), Local(value - base));
    }
    template <edge Edge> struct pending {
        const Local *fields;
        unsigned first, take;
        Local relative;
        bool has_local_read() const noexcept {return fields != nullptr;}
        std::span<const std::byte> local_window() const noexcept {
            return fields ? std::span<const std::byte>{reinterpret_cast<const std::byte *>(fields), std::size_t(take)*sizeof(Local)}
                          : std::span<const std::byte>{};
        }
        unsigned finish() const {
            return fields ? first + detail::hybrid_array<Edge,true>(fields,take,relative) : first;
        }
    };
    template <edge Edge, std::integral Query>
    pending<Edge> prepare(Query point) const {
        auto ready=[](unsigned rank)->pending<Edge>{return {nullptr,rank,0,0};};
        const detail::prepared_query<Point, Query> query(point);
        if (query.relation) return ready(query.relation < 0 ? 0u : count);
        if (!count || !detail::meets<Edge>(limit,query.value)) return ready(count);
        const unsigned next=detail::checkpoint_boundary<Edge,CheckpointCapacity>(checkpoints,groups(),query.value);
        const unsigned group=next?next-1:0,begin=group*Spacing;
        const Point base=detail::read_element(checkpoints,group);
        if(query.value<base||(Edge==edge::at_or_after&&query.value==base)) return ready(begin);
        const Point relative=query.value-base;
        if(relative>std::numeric_limits<Local>::max()) return ready(count);
        return {ends+begin,begin,std::min(Spacing,count-begin),Local(relative)};
    }
    template <edge Edge, class Query>
    [[gnu::always_inline]] unsigned boundary(const Query &point) const {
        if constexpr (std::integral<Query>) return prepare<Edge>(point).finish();
        else {
            unsigned low = 0, high = count;
            while (low < high) {
                const unsigned middle = low + (high - low) / 2;
                if (detail::meets<Edge>(at(middle), point)) high = middle;
                else low = middle + 1;
            }
            return low;
        }
    }
};
} // namespace ikea::intervals
