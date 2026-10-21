#pragma once
#include <ikea/boundlist/detail/search.h>
#include <ikea/boundlist/detail/prefix.h>
#include <ikea/boundlist/detail/bytes.h>
#include <cassert>
#include <span>

namespace ikea::boundlist {
/// Borrowed, ordered endpoints. The owner admits ordering, initialized elements
/// and lifetime. Native unsigned loads may be unaligned; other T are real objects.
template <class T> struct direct_source {
    const T *data;
    unsigned count;
    unsigned size() const noexcept { return count; }
    decltype(auto) at(unsigned rank) const { return detail::read_element(data, rank); }
    template <edge E, class Q> [[gnu::always_inline]] unsigned boundary(const Q &point) const {
        if constexpr (std::unsigned_integral<T> && std::integral<Q>) {
            const detail::prepared_query<T, Q> q(point);
            if (q.relation)
                return q.relation < 0 ? 0 : count;
            return detail::hybrid_array<E, true>(data, count, q.value);
        } else {
            unsigned lo = 0, hi = count;
            while (lo < hi) {
                const auto mid = lo + (hi - lo) / 2;
                if (detail::meets<E>(at(mid), point))
                    hi = mid;
                else
                    lo = mid + 1;
            }
            return lo;
        }
    }
};

template <class Width, std::unsigned_integral Point> struct delta_source;
namespace detail {
struct ready_search {
    unsigned rank;
    bool has_local_read() const noexcept { return false; }
    std::span<const std::byte> local_window() const noexcept { return {}; }
    unsigned finish() const noexcept { return rank; }
};
// Canonical local handoff: checkpoint width/global Point no longer participates
// after routing. Keep the local threshold in uint32 (16*u16 fits), not Point.
template <class Width, edge E> struct width_search {
    const Width *widths;
    unsigned first, relative;
    bool has_local_read() const noexcept { return widths != nullptr; }
    std::span<const std::byte> local_window() const noexcept {
        return widths ? std::span<const std::byte>(reinterpret_cast<const std::byte *>(widths),
                                                   16 * sizeof(Width))
                      : std::span<const std::byte>{};
    }
    [[gnu::always_inline]] unsigned finish() const noexcept {
        if (!widths)
            return first;
        if constexpr (sizeof(Width) == 2) {
            const auto offset = (relative & 0x7fffffffu) - unsigned(E == edge::at_or_after);
            return first + owner16_native(widths, offset, relative >> 31 ? 65535u : 16u * 65535);
        } else
            return first + owner16_native(widths, relative - unsigned(E == edge::at_or_after));
    }
};
template <edge E, unsigned Capacity = 0, class W, std::unsigned_integral P, std::integral Q>
[[gnu::always_inline]] width_search<W, E>
prepare_width(const delta_source<W, P> &, Q,
              unsigned span_limit = 16u * std::numeric_limits<W>::max());
} // namespace detail

/// Narrow deltas with one checkpoint per 16 ranks, including the origin, and no
/// terminal checkpoint. Each width group has 16 initialized/readable elements;
/// stride is in Width elements, allowing interleaved siblings. Sums must fit Point.
template <class Width, std::unsigned_integral Point = std::uint32_t> struct delta_source {
    static_assert(std::same_as<Width, std::uint8_t> || std::same_as<Width, std::uint16_t>);
    const Width *widths;
    const Point *checkpoints;
    unsigned count, stride;
    Point extent;
    unsigned size() const noexcept { return count; }
    unsigned groups() const noexcept { return count / 16 + unsigned(count % 16 != 0); }
    const Width *group(unsigned g) const { return widths + std::size_t(g) * stride; }
    Point at(unsigned rank) const {
        return detail::read_element(checkpoints, rank / 16) +
               detail::prefix16_native(group(rank / 16), rank % 16 + 1);
    }
    /// Resolves the available checkpoints, leaving at most one qualified width
    /// window. The owner retains its coherent backing across a wait.
    template <edge E, std::integral Q>
    [[gnu::always_inline]] detail::width_search<Width, E> prepare(Q point) const {
        return detail::prepare_width<E>(*this, point);
    }
    template <edge E, class Q> [[gnu::always_inline]] unsigned boundary(const Q &point) const {
        if constexpr (std::integral<Q>)
            return prepare<E>(point).finish();
        else {
            unsigned lo = 0, hi = count;
            while (lo < hi) {
                const auto mid = lo + (hi - lo) / 2;
                if (detail::meets<E>(at(mid), point))
                    hi = mid;
                else
                    lo = mid + 1;
            }
            return lo;
        }
    }
};

namespace detail {
template <edge E, unsigned Capacity, class W, std::unsigned_integral P, std::integral Q>
[[gnu::always_inline]] width_search<W, E> prepare_width(const delta_source<W, P> &source, Q point,
                                                        unsigned span_limit) {
    const prepared_query<P, Q> q(point);
    if (q.relation)
        return {nullptr, q.relation < 0 ? 0u : source.count, 0};
    const auto n = source.groups();
    if (!n || !meets<E>(source.extent, q.value))
        return {nullptr, source.count, 0};
    const auto next = checkpoint_boundary<E, Capacity>(source.checkpoints, n, q.value);
    const auto g = next ? next - 1 : 0, first = g * 16;
    const auto base = read_element(source.checkpoints, g);
    if (q.value < base || (E == edge::at_or_after && q.value == base))
        return {nullptr, first, 0};
    const P relative = q.value - base;
    if (relative > 16u * std::numeric_limits<W>::max())
        return {nullptr, source.count, 0};
    // A local delta needs at most20 bits. Keep the span proof in a spare bit,
    // preserving the16B pending frame instead of adding another bound word.
    const auto narrow = sizeof(W) == 2 && (span_limit <= 65535 || source.extent - base <= 65535);
    return {source.group(g), first, unsigned(relative) | (unsigned(narrow) << 31)};
}
} // namespace detail

/// Group-local cumulative ends. One origin per G16 group; local ends reset at
/// each group. The last group has sixteen readable fields, with zero padding.
/// Global sums fit Point and local spans fit Local. No payload or eligibility.
template <std::unsigned_integral Local, std::unsigned_integral Point = std::uint32_t>
struct relative_end_source {
    const Local *ends;
    const Point *checkpoints;
    unsigned count, stride;
    Point extent;
    unsigned size() const noexcept { return count; }
    unsigned groups() const noexcept { return count / 16 + unsigned(count % 16 != 0); }
    const Local *group(unsigned g) const noexcept { return ends + std::size_t(g) * stride; }
    Point at(unsigned rank) const noexcept {
        return detail::read_element(checkpoints, rank / 16) +
               detail::read_element(group(rank / 16), rank % 16);
    }
    template <edge E> struct pending {
        const Local *fields;
        // Group starts are multiples of16. Its four spare bits retain take-1,
        // keeping u8/u16/u32 local handoffs in two words without limiting rank.
        unsigned first_count;
        Local threshold;
        bool has_local_read() const noexcept { return fields != nullptr; }
        std::span<const std::byte> local_window() const noexcept {
            return fields ? std::span<const std::byte>(reinterpret_cast<const std::byte *>(fields),
                                                       16 * sizeof(Local))
                          : std::span<const std::byte>{};
        }
        [[gnu::always_inline]] unsigned finish() const noexcept {
            return fields ? (first_count & ~15u) + detail::hybrid_array<E, true>(
                                                       fields, (first_count & 15u) + 1, threshold)
                          : first_count;
        }
    };
    template <edge E, unsigned Capacity = 0, std::integral Q>
    [[gnu::always_inline]] pending<E> prepare(Q point) const noexcept {
        const detail::prepared_query<Point, Q> q(point);
        if (q.relation)
            return {nullptr, q.relation < 0 ? 0u : count, 0};
        if (!count || !detail::meets<E>(extent, q.value))
            return {nullptr, count, 0};
        const auto next = detail::checkpoint_boundary<E, Capacity>(checkpoints, groups(), q.value);
        const auto g = next ? next - 1 : 0, first = g * 16;
        const auto base = detail::read_element(checkpoints, g);
        if (q.value < base || (E == edge::at_or_after && q.value == base))
            return {nullptr, first, 0};
        const auto relative = q.value - base;
        if (detail::integer_less(std::numeric_limits<Local>::max(), relative))
            return {nullptr, count, 0};
        return {group(g), first | (std::min(16u, count - first) - 1), Local(relative)};
    }
    template <edge E, class Q> [[gnu::always_inline]] unsigned boundary(const Q &point) const {
        if constexpr (std::integral<Q>)
            return prepare<E>(point).finish();
        else {
            unsigned lo = 0, hi = count;
            while (lo < hi) {
                const auto mid = lo + (hi - lo) / 2;
                if (detail::meets<E>(at(mid), point))
                    hi = mid;
                else
                    lo = mid + 1;
            }
            return lo;
        }
    }
};

/// Exact byte-readable rank-selection window. Unlike word-padded bitmaps, the final
/// partial byte/word needs no extra readable grant. Set bits mean eligible unless
/// inverted (a tombstone plane). The owner supplies ceil(count/8) bytes.
struct rank_selection {
    static rank_selection all() noexcept { return {{}, true}; }
    std::span<const std::byte> bytes;
    bool inverted = false;
    [[gnu::always_inline]] bool contains(unsigned rank) const {
        return bytes.empty()
                   ? inverted
                   : bool(std::to_integer<unsigned>(bytes[rank / 8]) & (1u << (rank % 8))) != inverted;
    }
    [[gnu::always_inline]] std::uint64_t mask(unsigned first, unsigned count) const {
        assert(count <= 64 && first % 8 == 0);
        if (bytes.empty()) return inverted ? detail::low_bits(count) : 0;
        const auto take = (count + 7) / 8;
        assert(first / 8 + take <= bytes.size());
        std::uint64_t word = 0;
        const auto *p = bytes.data() + first / 8;
        if (take == 8)
            word = detail::load<std::uint64_t>(p);
        else {
            unsigned i = 0;
            if (take >= 4) {
                word = detail::load<std::uint32_t>(p);
                i = 4;
            }
            if (take - i >= 2) {
                word |= std::uint64_t(detail::load<std::uint16_t>(p + i)) << (i * 8);
                i += 2;
            }
            if (take - i)
                word |= std::uint64_t(std::to_integer<unsigned>(p[i])) << (i * 8);
        }
        return (inverted ? ~word : word) & detail::low_bits(count);
    }
    [[gnu::always_inline]] unsigned next(unsigned first, unsigned n) const {
        if (bytes.empty()) return inverted ? std::min(first, n) : n;
        while (first < n) {
            const auto base = first / 64 * 64, take = std::min(64u, n - base);
            const auto bits = mask(base, take) & ~detail::low_bits(first - base);
            if (bits)
                return base + std::countr_zero(bits);
            first = base + take;
        }
        return n;
    }
    [[gnu::always_inline]] unsigned previous(unsigned limit, unsigned n) const {
        if (bytes.empty()) return inverted && limit ? limit - 1 : n;
        while (limit) {
            const auto base = (limit - 1) / 64 * 64;
            const auto bits = mask(base, limit - base);
            if (bits)
                return base + 63 - std::countl_zero(bits);
            limit = base;
        }
        return n;
    }
};
template <class Source> struct selected_source : Source {
    rank_selection eligible;
    [[gnu::always_inline]] bool is_selected(unsigned rank) const {
        if (!eligible.contains(rank)) return false;
        if constexpr (requires { static_cast<const Source &>(*this).is_selected(rank); })
            return static_cast<const Source &>(*this).is_selected(rank);
        else
            return true;
    }
    [[gnu::always_inline]] unsigned next_eligible(unsigned first, unsigned n) const {
        auto rank = eligible.next(first, n);
        if constexpr (requires { static_cast<const Source &>(*this).next_eligible(first, n); }) {
            while (rank < n) {
                const auto base = static_cast<const Source &>(*this).next_eligible(rank, n);
                if (base == rank) break;
                rank = eligible.next(base, n);
            }
        }
        return rank;
    }
    [[gnu::always_inline]] unsigned previous_eligible(unsigned limit) const {
        auto rank = eligible.previous(limit, this->size());
        if constexpr (requires { static_cast<const Source &>(*this).previous_eligible(limit); }) {
            while (rank != this->size()) {
                const auto base = static_cast<const Source &>(*this).previous_eligible(rank + 1);
                if (base == rank) break;
                if (base == this->size()) return base;
                rank = eligible.previous(base + 1, this->size());
            }
        }
        return rank;
    }
    [[gnu::always_inline]] std::uint64_t selection_mask(unsigned first, unsigned count) const {
        auto mask = eligible.mask(first, count);
        if constexpr (requires { static_cast<const Source &>(*this).selection_mask(first, count); })
            mask &= static_cast<const Source &>(*this).selection_mask(first, count);
        return mask;
    }
    template <class Body> decltype(auto) with_geometry(Body &&body) const
        requires requires { static_cast<const Source &>(*this).with_geometry(body); }
    {
        return static_cast<const Source &>(*this).with_geometry([&](auto access) {
            const selected_source<decltype(access)> selected{access, eligible};
            return body(selected);
        });
    }
};
namespace detail {
template <class Source> unsigned next(const Source &source, unsigned first, unsigned n) {
    if constexpr (requires { source.next_eligible(first, n); })
        return source.next_eligible(first, n);
    else
        return first;
}
template <class Source> unsigned previous(const Source &source, unsigned limit) {
    if constexpr (requires { source.previous_eligible(limit); })
        return source.previous_eligible(limit);
    else
        return limit ? limit - 1 : source.size();
}
template <edge E, class Source, class Q>
[[gnu::always_inline]] auto prepare(const Source &source, const Q &point) {
    static_assert(std::same_as<std::remove_cvref_t<decltype(source.size())>, unsigned>,
                  "Boundlist projections use unsigned local counts/ranks; bind wider directory "
                  "indices outside the source");
    if constexpr (requires { source.template prepare<E>(point); })
        return source.template prepare<E>(point);
    else if constexpr (requires { source.template boundary<E>(point); })
        return ready_search{source.template boundary<E>(point)};
    else {
        unsigned lo = 0, hi = source.size();
        while (lo < hi) {
            const auto mid = lo + (hi - lo) / 2;
            if (meets<E>(source.at(mid), point))
                hi = mid;
            else
                lo = mid + 1;
        }
        return ready_search{lo};
    }
}
template <edge E, class Source, class Q>
[[gnu::always_inline]] unsigned boundary(const Source &source, const Q &point) {
    static_assert(std::same_as<std::remove_cvref_t<decltype(source.size())>, unsigned>,
                  "Boundlist projections use unsigned local counts/ranks; bind wider directory "
                  "indices outside the source");
    if constexpr (requires { source.template boundary<E>(point); })
        return source.template boundary<E>(point);
    else
        return prepare<E>(source, point).finish();
}
} // namespace detail
template <class Source, class Q>
[[gnu::always_inline]] unsigned lower_bound(const Source &source, const Q &point) {
    return detail::next(source, detail::boundary<edge::at_or_after>(source, point), source.size());
}
template <class Source, class Q>
[[gnu::always_inline]] unsigned upper_bound(const Source &source, const Q &point) {
    return detail::next(source, detail::boundary<edge::after>(source, point), source.size());
}
} // namespace ikea::boundlist
