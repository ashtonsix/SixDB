#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <ikea/detail/filter/compare.h>
#include <ikea/boundlist/detail/prefix.h>
#include <limits>
#include <type_traits>

namespace boundlist_spike {
enum class edge { at_or_after, after };
enum class method { binary, select_binary, scan, lerp, gallop, quad, hybrid, select_hybrid };
struct wide_key {
    std::uint64_t high, low;
    auto operator<=>(const wide_key&) const = default;
};
template <edge E, class T> inline bool meets(const T& end, const T& point) {
    if constexpr (E == edge::after) return point < end;
    else return !(end < point);
}
template <class T> [[gnu::always_inline]] T read_element(const T* data, unsigned index) {
    if constexpr (std::unsigned_integral<T>) {
        T result;
        std::memcpy(&result, reinterpret_cast<const std::byte*>(data) + index * sizeof(T), sizeof(T));
        return result;
    } else return data[index];
}
// One key-dependent position selection. Unlike a C++ ternary, the native-word
// candidate guarantees cmov/csel; loop control remains separate. Other ordered
// domains retain comparison semantics without requiring an integer encoding.
template <edge E, class T>
[[gnu::always_inline]] unsigned select_position(const T& value, const T& point, unsigned next, unsigned base) {
    if constexpr (std::unsigned_integral<T> && sizeof(T) <= 8) {
        const std::uint64_t a = value, b = point;
#if defined(__x86_64__)
        if constexpr (E == edge::at_or_after)
            asm("cmpq %[b], %[a]\n\tcmovbl %k[next], %k[base]"
                : [base] "+r"(base) : [a] "r"(a), [b] "r"(b), [next] "r"(next) : "cc");
        else
            asm("cmpq %[b], %[a]\n\tcmovbel %k[next], %k[base]"
                : [base] "+r"(base) : [a] "r"(a), [b] "r"(b), [next] "r"(next) : "cc");
        return base;
#elif defined(__aarch64__)
        if constexpr (E == edge::at_or_after)
            asm("cmp %x[a], %x[b]\n\tcsel %w[base], %w[next], %w[base], lo"
                : [base] "+r"(base) : [a] "r"(a), [b] "r"(b), [next] "r"(next) : "cc");
        else
            asm("cmp %x[a], %x[b]\n\tcsel %w[base], %w[next], %w[base], ls"
                : [base] "+r"(base) : [a] "r"(a), [b] "r"(b), [next] "r"(next) : "cc");
        return base;
#endif
    }
    return meets<E>(value, point) ? base : next;
}
inline std::uint64_t low_bits(unsigned n) {
    return n == 64 ? ~std::uint64_t{} : (std::uint64_t{1} << n) - 1;
}

// An interpolation hint, never a correctness proof. Scale wide coordinates
// before multiplication so a 64-bit point does not introduce a 128-bit divide
// helper in each search. Bisection/window verification repairs any rounding.
template <std::unsigned_integral T>
[[gnu::always_inline]] unsigned interpolation_probe(T low, T high, T point, unsigned n) {
    const auto span = std::uint64_t(high - low);
    const unsigned product_bits = std::bit_width(span) + std::bit_width(n - 1);
    const auto shift = product_bits > 64 ? product_bits - 64 : 0;
    return unsigned(((std::uint64_t(point - low) >> shift) * (n - 1)) / (span >> shift));
}

// Population is independent of endpoint equality. This view never decides that
// a zero delta is an empty population. The consumer supplies that interpretation.
struct population {
    const void* words = nullptr;
    bool inverted = false;
    std::uint64_t word(unsigned index) const {
        std::uint64_t result;
        std::memcpy(&result, static_cast<const std::byte*>(words) + index * 8, 8);
        return inverted ? ~result : result;
    }
    std::uint64_t mask(unsigned first, unsigned count) const {
        if (!words) return low_bits(count);
        auto result = word(first / 64) >> (first % 64);
        if (first % 64 && count > 64 - first % 64)
            result |= word(first / 64 + 1) << (64 - first % 64);
        return result & low_bits(count);
    }
    bool live(unsigned rank) const { return !words || (word(rank / 64) >> (rank % 64) & 1); }
};

template <edge E, class T> [[gnu::always_inline]] unsigned native_mask(const T* data, unsigned count, const T& query) {
    unsigned result = 0, i = 0;
    constexpr auto Op = E == edge::after ? ikea::comparison::greater : ikea::comparison::greater_equal;
    if constexpr (std::unsigned_integral<T>) {
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
      {
        constexpr unsigned lanes = 64 / sizeof(T);
        for (; i + lanes <= count; i += lanes)
            result |= ikea::detail::filter::bits<sizeof(T), Op>(_mm512_loadu_si512(data + i), query) << i;
      }
#endif
#if defined(__AVX2__)
      {
        constexpr unsigned lanes = 32 / sizeof(T);
        for (; i + lanes <= count; i += lanes)
            result |= ikea::detail::filter::bits<sizeof(T), Op>(
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i)), query) << i;
      }
      {
        constexpr unsigned lanes = 16 / sizeof(T);
        for (; i + lanes <= count; i += lanes)
            result |= ikea::detail::filter::bits<sizeof(T), Op>(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i)), query) << i;
      }
#elif defined(__aarch64__)
        constexpr unsigned lanes = 16 / sizeof(T);
        for (; i + lanes <= count; i += lanes)
            result |= ikea::detail::filter::bits<sizeof(T), Op>(
                vld1q_u8(reinterpret_cast<const std::uint8_t*>(data + i)), query) << i;
#endif
    }
    for (; i < count; ++i) result |= unsigned(meets<E>(read_element(data, i), query)) << i;
    return result;
}

template <class T> constexpr unsigned convergence_lanes = [] {
    if constexpr (!std::unsigned_integral<T>) return 4u;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
    else return std::min(32u, unsigned(64 / sizeof(T)));
#elif defined(__AVX2__)
    else return unsigned(32 / sizeof(T));
#else
    else return unsigned(16 / sizeof(T));
#endif
}();

// lo..hi is the unresolved bisection window; hi itself is a known qualifying
// endpoint or the count sentinel. Wide reads stay within the actual array,
// shifting the final load left rather than asking for an unreadable tail.
template <edge E, class T>
[[gnu::always_inline]] unsigned transition_window(const T* data, unsigned n, unsigned lo, unsigned hi, const T& p) {
    if (lo == hi) return hi;
    constexpr auto lanes = convergence_lanes<T>;
    const auto take = std::min(n, lanes);
    const auto base = std::min(lo, n - take);
    auto mask = native_mask<E>(data + base, take, p);
    mask &= unsigned(low_bits(hi - base) & ~low_bits(lo - base));
    return mask ? base + std::countr_zero(mask) : hi;
}
template <edge E, bool Select = false, class T>
[[gnu::always_inline]] unsigned hybrid_array(const T* data, unsigned n, const T& p) {
    unsigned lo = 0, hi = n;
    if constexpr (Select) {
        unsigned remaining = n;
        while (remaining > convergence_lanes<T>) {
            const auto half = remaining / 2, mid = lo + half;
            lo = select_position<E>(read_element(data, mid), p, mid, lo);
            remaining -= half;
        }
        hi = lo + remaining;
    } else {
        while (hi - lo > convergence_lanes<T>) {
            const auto mid = lo + (hi - lo) / 2;
            if (meets<E>(read_element(data, mid), p)) hi = mid;
            else lo = mid + 1;
        }
    }
    return transition_window<E>(data, n, lo, hi, p);
}

// Interleave independent point searches. Each takes one optional interpolation
// probe, then fixed-step conditional-select bisection, then one bounded native
// comparison window. Q can also be two bounds into one directory. This is an
// experiment in stage composition, not an interface or universal search policy.
template <edge E, unsigned Q, bool Lerp, class T>
[[gnu::always_inline]] void shotgun_array(const std::array<const T*, Q>& data,
    const std::array<unsigned, Q>& counts, const std::array<T, Q>& points,
    std::array<unsigned, Q>& result) {
    std::array<unsigned, Q> lo{}, remaining{};
    for (unsigned i = 0; i < Q; ++i) {
        const auto n = counts[i];
        remaining[i] = n;
        if constexpr (Lerp && std::unsigned_integral<T>) if (n > convergence_lanes<T>) {
            const auto first = read_element(data[i], 0), last = read_element(data[i], n - 1);
            if (meets<E>(first, points[i])) { remaining[i] = 0; continue; }
            if (!meets<E>(last, points[i])) { lo[i] = n; remaining[i] = 0; continue; }
            // The two admission tests imply first < last and first <= p <= last.
            const auto mid = interpolation_probe(first, last, points[i], n);
            if (meets<E>(read_element(data[i], mid), points[i])) remaining[i] = mid;
            else { lo[i] = mid + 1; remaining[i] = n - lo[i]; }
        }
    }
    bool pending;
    do {
        pending = false;
        for (unsigned i = 0; i < Q; ++i) if (remaining[i] > convergence_lanes<T>) {
            pending = true;
            const auto half = remaining[i] / 2, mid = lo[i] + half;
            lo[i] = select_position<E>(read_element(data[i], mid), points[i], mid, lo[i]);
            remaining[i] -= half;
        }
    } while (pending);
    for (unsigned i = 0; i < Q; ++i)
        result[i] = transition_window<E>(data[i], counts[i], lo[i], lo[i] + remaining[i], points[i]);
}
[[gnu::always_inline]] inline unsigned eligible_from(population live, unsigned first, unsigned n) {
    if (!live.words) return first;
    while (first < n) {
        const auto take = std::min(64u - first % 64, n - first);
        const auto mask = live.mask(first, take);
        if (mask) return first + std::countr_zero(mask);
        first += take;
    }
    return n;
}

template <class T, unsigned Group = 16, bool Starts = false> struct direct_view {
    static_assert(Group <= 32 && Group > 0);
    using point_type = T;
    static constexpr unsigned group_size = Group;
    const T* ends;
    unsigned count;
    population eligible;
    T origin{};
    unsigned groups() const { return (count + Group - 1) / Group; }
    T end(unsigned rank) const {
        if constexpr (Starts) return rank ? read_element(ends, rank - 1) : origin;
        else return read_element(ends, rank);
    }
    T group_end(unsigned g) const { return end(std::min(count, (g + 1) * Group) - 1); }
    template <edge E> [[gnu::always_inline]] unsigned in_group(unsigned g, const T& query) const {
        const auto first = g * Group, n = std::min(Group, count - first);
        auto mask = [&] {
            if constexpr (!Starts) return native_mask<E>(ends + first, n, query);
            else {
                if (first) return native_mask<E>(ends + first - 1, n, query);
                return unsigned(meets<E>(origin, query)) | (native_mask<E>(ends, n - 1, query) << 1);
            }
        }();
        mask &= eligible.mask(first, n);
        return mask ? first + std::countr_zero(mask) : count;
    }
    template <edge E> [[gnu::always_inline]] unsigned scan_group(const T& p) const {
        auto raw = *this; raw.eligible = {};
        for (unsigned g = 0; g < groups(); ++g)
            if (raw.template in_group<E>(g, p) != count) return g;
        return groups();
    }
    template <edge E, bool Select = false> [[gnu::always_inline]] unsigned hybrid_seek(const T& p) const {
        if constexpr (!Starts) return eligible_from(eligible, hybrid_array<E, Select>(ends, count, p), count);
        else {
            if (!count) return count;
            if (meets<E>(origin, p)) return eligible_from(eligible, 0, count);
            return eligible_from(eligible, hybrid_array<E, Select>(ends, count - 1, p) + 1, count);
        }
    }
};

// The first arithmetic candidate reuses current StreamPack native prefix bodies
// while opening the schema/access boundary. Fused controls do not include or call
// this header or any Ikea kernel. Native 8/16-bit deltas are a measured slice,
// not the boundlist coordinate contract: endpoints can be wider than deltas.
template <class Width, class Point = std::uint32_t, unsigned Group = 16, bool Starts = false> struct delta_view {
    using point_type = Point;
    static constexpr unsigned group_size = Group;
    const Width* widths;
    const Point* checkpoints;
    unsigned count, stride; // stride in Width elements; sibling fields can interleave.
    Point extent;
    population eligible;
    unsigned groups() const { return (count + Group - 1) / Group; }
    const Width* group(unsigned g) const { return widths + g * stride; }
    Point group_end(unsigned g) const {
        auto result = g + 1 == groups() ? extent : read_element(checkpoints, g + 1);
        if constexpr (Starts) result -= read_element(group(g), std::min(Group, count - g * Group) - 1);
        return result;
    }
    Point end(unsigned rank) const {
        Point result = read_element(checkpoints, rank / Group);
        const auto take = rank % Group + unsigned(!Starts);
        for (unsigned i = 0; i < take; ++i) result += read_element(group(rank / Group), i);
        return result;
    }
    template <edge E> [[gnu::always_inline]] unsigned scan_group(const Point& p) const {
        const auto n = groups();
        if constexpr (!Starts) {
            // Include C0 in the load: sixteen uint16 checkpoints then fit one
            // YMM operation rather than a shifted fifteen-lane tail. The first
            // qualifying start selects the preceding end group (clamped at 0).
            for (unsigned g = 0; g < n; g += 32) {
                const auto take = std::min(32u, n - g);
                const auto mask = native_mask<E>(checkpoints + g, take, p);
                if (mask) {
                    const auto next = g + std::countr_zero(mask);
                    return next ? next - 1 : 0;
                }
            }
            return n && meets<E>(extent, p) ? n - 1 : n;
        } else {
            // Start-of-rank group maxima also need the final delta. Keep this
            // distinct from the contiguous checkpoint scan in the end view.
            for (unsigned g = 0; g < n; ++g) if (meets<E>(group_end(g), p)) return g;
            return n;
        }
    }
    template <edge E, bool Select = false> [[gnu::always_inline]] unsigned hybrid_group(const Point& p) const {
        const auto n = groups();
        if constexpr (!Starts) {
            const auto next = hybrid_array<E, Select>(checkpoints, n, p);
            if (next == n) return n && meets<E>(extent, p) ? n - 1 : n;
            return next ? next - 1 : 0;
        } else {
            // This projection lacks a contiguous array of group maxima. Scalar
            // probes reach a small bracket; explicitly compare those pivots.
            unsigned lo = 0, hi = n;
            while (hi - lo > convergence_lanes<Point>) {
                const auto mid = lo + (hi - lo) / 2;
                if (meets<E>(group_end(mid), p)) hi = mid; else lo = mid + 1;
            }
            if (lo == hi) return hi;
            std::array<Point, convergence_lanes<Point>> local;
            for (unsigned i = lo; i < hi; ++i) local[i - lo] = group_end(i);
            const auto mask = native_mask<E>(local.data(), hi - lo, p);
            return mask ? lo + std::countr_zero(mask) : hi;
        }
    }
    template <edge E> [[gnu::always_inline]] unsigned in_group(unsigned g, Point query) const {
        namespace sn = ikea::boundlist::native;
        const auto first = g * Group, n = std::min(Group, count - first);
        auto active = eligible.mask(first, n);
        if (!active) return count;
        const auto base = read_element(checkpoints, g);
        if (query < base) return first + std::countr_zero(active);
        const Point relative = Point(query - base);
        if constexpr (std::is_same_v<Width, std::uint8_t> && Group == 16) {
            // Preserve StreamPack's stronger byte-width kernel: sixteen byte
            // deltas sum to <=4080, so its NEON/YMM prefix stays in uint16 lanes
            // instead of needlessly widening to four-word prefix chunks.
            if (relative > 4080) return count;
            unsigned lane;
            if constexpr (E == edge::at_or_after) {
                if (!relative) lane = 0;
                else {
                    [[clang::always_inline]] lane = ikea::boundlist::detail::owner16_native(group(g), relative - 1);
                    if constexpr (Starts) ++lane;
                }
            } else {
                [[clang::always_inline]] lane = ikea::boundlist::detail::owner16_native(group(g), relative);
                if constexpr (Starts) ++lane;
            }
            if (!eligible.words) return lane < n ? first + lane : count;
            active &= ~low_bits(std::min(lane, n));
            return active ? first + std::countr_zero(active) : count;
        }
        unsigned carry = 0;
#if defined(__AVX2__) || defined(__aarch64__)
        if constexpr ((sizeof(Width) == 1 || sizeof(Width) == 2) && Group % sn::lanes == 0) {
            // Admitted local sum fits uint32 for this narrow-delta recipe.
            if (relative > std::numeric_limits<std::uint32_t>::max()) return count;
            for (unsigned i = 0; i < Group; i += sn::lanes) {
                const auto values = sn::load<Width>(reinterpret_cast<const std::byte*>(group(g) + i));
                const auto sums = sn::prefix(values);
                const auto positions = [&] { if constexpr (Starts) return sn::sub(sums, values); else return sums; }();
                unsigned mask;
                if constexpr (E == edge::at_or_after)
                    mask = relative == 0 ? unsigned(low_bits(sn::lanes))
                                         : sn::greater(sn::add(positions, sn::splat(carry)), relative - 1);
                else mask = sn::greater(sn::add(positions, sn::splat(carry)), relative);
                mask &= unsigned(active >> i) & unsigned(low_bits(sn::lanes));
                if (mask) return first + i + std::countr_zero(mask);
                carry += sn::last(sums);
            }
            return count;
        }
#endif
        Point position = base;
        for (unsigned i = 0; i < n; ++i) {
            if constexpr (!Starts) position += read_element(group(g), i);
            if ((active >> i & 1) && meets<E>(position, query)) return first + i;
            if constexpr (Starts) position += read_element(group(g), i);
        }
        return count;
    }
};

template <class T, unsigned G> auto starts(const direct_view<T, G>& v) {
    return direct_view<T, G, true>{v.ends, v.count, v.eligible, v.origin};
}
template <class W, class T, unsigned G> auto starts(const delta_view<W, T, G>& v) {
    return delta_view<W, T, G, true>{v.widths, v.checkpoints, v.count, v.stride, v.extent, v.eligible};
}

template <edge E, class View>
[[gnu::always_inline]] unsigned binary_groups(const View& view, const typename View::point_type& p, unsigned lo, unsigned hi) {
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        if (meets<E>(view.group_end(mid), p)) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}
template <edge E, method M, class View>
[[gnu::always_inline]] unsigned group_for(const View& v, const typename View::point_type& p) {
    const auto n = v.groups();
    if constexpr (M == method::scan) {
        return v.template scan_group<E>(p);
    } else if constexpr (M == method::hybrid || M == method::select_hybrid) {
        return v.template hybrid_group<E, M == method::select_hybrid>(p);
    } else if constexpr (M == method::gallop) {
        unsigned lo = 0, hi = std::min(1u, n);
        while (hi < n && !meets<E>(v.group_end(hi - 1), p)) {
            lo = hi;
            hi = std::min(n, hi * 2);
        }
        return binary_groups<E>(v, p, lo, hi);
    } else if constexpr (M == method::quad) {
        unsigned lo = 0, hi = n;
        while (hi - lo > 3) {
            const auto step = (hi - lo) / 4;
            const auto a = lo + step, b = lo + 2 * step, c = lo + 3 * step;
            const auto x = v.group_end(a), y = v.group_end(b), z = v.group_end(c);
            if (meets<E>(x, p)) hi = a + 1;
            else if (meets<E>(y, p)) { lo = a + 1; hi = b + 1; }
            else if (meets<E>(z, p)) { lo = b + 1; hi = c + 1; }
            else lo = c + 1;
        }
        return binary_groups<E>(v, p, lo, hi);
    } else if constexpr (M == method::lerp && std::unsigned_integral<typename View::point_type>) {
        if (!n) return n;
        const auto a = v.group_end(0), b = v.group_end(n - 1);
        if (p <= a || p > b || a == b) return binary_groups<E>(v, p, 0, n);
        const auto mid = interpolation_probe(a, b, p, n);
        if (meets<E>(v.group_end(mid), p)) return binary_groups<E>(v, p, 0, mid + 1);
        return binary_groups<E>(v, p, mid + 1, n);
    } else if constexpr (M == method::select_binary) {
        if (!n) return n;
        unsigned base = 0, remaining = n;
        while (remaining > 1) {
            const auto half = remaining / 2, mid = base + half;
            base = select_position<E>(v.group_end(mid), p, mid, base);
            remaining -= half;
        }
        return select_position<E>(v.group_end(base), p, base + 1, base);
    } else return binary_groups<E>(v, p, 0, n);
}
template <edge E, method M, class View>
[[gnu::always_inline]] unsigned seek(const View& v, const typename View::point_type& p) {
    if constexpr ((M == method::hybrid || M == method::select_hybrid) && requires { v.template hybrid_seek<E>(p); })
        return v.template hybrid_seek<E, M == method::select_hybrid>(p);
    else {
        for (auto g = group_for<E, M>(v, p); g < v.groups(); ++g) {
            const auto rank = v.template in_group<E>(g, p);
            if (rank != v.count) return rank;
        }
        return v.count;
    }
}
template <edge E, unsigned Q, class View>
void shotgun(const std::array<View, Q>& views,
             const std::array<typename View::point_type, Q>& points, std::array<unsigned, Q>& result) {
    std::array<unsigned, Q> lo{}, hi{};
    for (unsigned i = 0; i < Q; ++i) hi[i] = views[i].groups();
    bool pending;
    do {
        pending = false;
        for (unsigned i = 0; i < Q; ++i) if (lo[i] < hi[i]) {
            pending = true;
            const auto mid = lo[i] + (hi[i] - lo[i]) / 2;
            const bool before = !meets<E>(views[i].group_end(mid), points[i]);
            lo[i] = before ? mid + 1 : lo[i];
            hi[i] = before ? hi[i] : mid;
        }
    } while (pending);
    for (unsigned i = 0; i < Q; ++i) {
        result[i] = views[i].count;
        for (auto g = lo[i]; g < views[i].groups(); ++g) {
            const auto r = views[i].template in_group<E>(g, points[i]);
            if (r != views[i].count) { result[i] = r; break; }
        }
    }
}

struct rank_window {
    unsigned first, limit; // half-open rank window; holes inside are not made live.
    bool empty() const { return first == limit; }
    auto operator<=>(const rank_window&) const = default;
};
template <class T> struct query_range {
    T low, high;
    bool low_closed = true, high_closed = false;
    bool empty() const { return high < low || (!(low_closed && high_closed) && !(low < high)); }
};
template <class View> [[gnu::always_inline]] unsigned live_before(const View& v, unsigned limit) {
    while (limit) {
        const auto first = (limit - 1) / 64 * 64;
        const auto mask = v.eligible.mask(first, limit - first);
        if (mask) return first + 63 - std::countl_zero(mask);
        limit = first;
    }
    return v.count;
}
// Max/min views can describe occupied extrema, physical intervals or owner
// projections. Eligibility is independent. The caller chooses equality semantics
// and admits matching ordered views; this is not a fixed public module API.
template <edge Lower, edge Stop, method M, class MaxView, class MinView>
[[gnu::always_inline]] rank_window overlap(const MaxView& maxima, const MinView& minima,
                    const query_range<typename MaxView::point_type>& q) {
    if (q.empty()) return {maxima.count, maxima.count};
    const auto first = seek<Lower, M>(maxima, q.low);
    const auto stop = seek<Stop, M>(minima, q.high);
    if (first >= stop) return {maxima.count, maxima.count};
    const auto last = live_before(maxima, stop);
    return {first, last + 1};
}

// Occupied min/max consumer: endpoint equality belongs to the query, while
// eligibility remains independent. This dispatch is outside either search loop.
template <method M, class MaxView, class MinView>
[[gnu::always_inline]] rank_window extrema_overlap(const MaxView& maxima, const MinView& minima,
                                      const query_range<typename MaxView::point_type>& q) {
    if (q.low_closed) {
        if (q.high_closed) return overlap<edge::at_or_after, edge::after, M>(maxima, minima, q);
        return overlap<edge::at_or_after, edge::at_or_after, M>(maxima, minima, q);
    }
    if (q.high_closed) return overlap<edge::after, edge::after, M>(maxima, minima, q);
    return overlap<edge::after, edge::at_or_after, M>(maxima, minima, q);
}

template <edge Lower, edge Stop, unsigned Q, class MaxView, class MinView>
std::array<rank_window, Q> overlap_shotgun(const std::array<MaxView, Q>& maxima,
                                         const std::array<MinView, Q>& minima,
                                         const std::array<query_range<typename MaxView::point_type>, Q>& queries) {
    std::array<typename MaxView::point_type, Q> low, high;
    std::array<unsigned, Q> first, stop;
    for (unsigned i = 0; i < Q; ++i) { low[i] = queries[i].low; high[i] = queries[i].high; }
    shotgun<Lower, Q>(maxima, low, first);
    shotgun<Stop, Q>(minima, high, stop);
    std::array<rank_window, Q> result;
    for (unsigned i = 0; i < Q; ++i) {
        result[i] = {maxima[i].count, maxima[i].count};
        if (!queries[i].empty() && first[i] < stop[i]) {
            const auto last = live_before(maxima[i], stop[i]);
            result[i] = {first[i], last + 1};
        }
    }
    return result;
}
} // namespace boundlist_spike
