#pragma once
#include "boundlist.h"

namespace boundlist_spike {
// A deliberately narrower geometry control: adjacent half-open intervals share
// endpoints. It does not apply to occupied extrema with gaps/duplicates. Separate
// and paired versions use identical semantics to isolate sharing from that fact.
template <class T, unsigned G> T origin(const direct_view<T, G>& v) { return v.origin; }
template <class W, class T, unsigned G> T origin(const delta_view<W, T, G>& v) { return read_element(v.checkpoints, 0); }
template <class T, unsigned G> T extent(const direct_view<T, G>& v) { return read_element(v.ends, v.count - 1); }
template <class W, class T, unsigned G> T extent(const delta_view<W, T, G>& v) { return v.extent; }
template <class T, unsigned G> bool positive(const direct_view<T, G>& v, unsigned r) {
    return (r ? read_element(v.ends, r - 1) : v.origin) < read_element(v.ends, r);
}
template <class W, class T, unsigned G> bool positive(const delta_view<W, T, G>& v, unsigned r) {
    return read_element(v.group(r / G), r % G) != 0;
}

template <edge E, edge F, class T>
[[gnu::always_inline]] std::array<unsigned, 2> native_pair_mask(const T* data, unsigned n, const T& p, const T& q) {
    constexpr auto A = E == edge::after ? ikea::comparison::greater : ikea::comparison::greater_equal;
    constexpr auto B = F == edge::after ? ikea::comparison::greater : ikea::comparison::greater_equal;
    std::array<unsigned, 2> result{};
    unsigned i = 0;
    if constexpr (std::unsigned_integral<T>) {
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        for (; i + 64 / sizeof(T) <= n; i += 64 / sizeof(T)) {
            const auto x = _mm512_loadu_si512(data + i);
            result[0] |= ikea::detail::filter::bits<sizeof(T), A>(x, p) << i;
            result[1] |= ikea::detail::filter::bits<sizeof(T), B>(x, q) << i;
        }
#endif
#if defined(__AVX2__)
        for (; i + 32 / sizeof(T) <= n; i += 32 / sizeof(T)) {
            const auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
            result[0] |= ikea::detail::filter::bits<sizeof(T), A>(x, p) << i;
            result[1] |= ikea::detail::filter::bits<sizeof(T), B>(x, q) << i;
        }
        for (; i + 16 / sizeof(T) <= n; i += 16 / sizeof(T)) {
            const auto x = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
            result[0] |= ikea::detail::filter::bits<sizeof(T), A>(x, p) << i;
            result[1] |= ikea::detail::filter::bits<sizeof(T), B>(x, q) << i;
        }
#elif defined(__aarch64__)
        for (; i + 16 / sizeof(T) <= n; i += 16 / sizeof(T)) {
            const auto x = vld1q_u8(reinterpret_cast<const std::uint8_t*>(data + i));
            result[0] |= ikea::detail::filter::bits<sizeof(T), A>(x, p) << i;
            result[1] |= ikea::detail::filter::bits<sizeof(T), B>(x, q) << i;
        }
#endif
    }
    for (; i < n; ++i) {
        result[0] |= unsigned(meets<E>(read_element(data, i), p)) << i;
        result[1] |= unsigned(meets<F>(read_element(data, i), q)) << i;
    }
    return result;
}

template <edge E, edge F, class T, unsigned G>
[[gnu::always_inline]] std::array<unsigned, 2> pair_in_group(const direct_view<T, G>& v, unsigned g, const T& p, const T& q) {
    const auto first = g * G, n = std::min(G, v.count - first);
    auto mask = native_pair_mask<E, F>(v.ends + first, n, p, q);
    mask[0] &= v.eligible.mask(first, n);
    return {mask[0] ? first + std::countr_zero(mask[0]) : v.count,
            mask[1] ? first + std::countr_zero(mask[1]) : v.count};
}

template <edge E, edge F, class W, class T, unsigned G>
[[gnu::always_inline]] std::array<unsigned, 2> pair_in_group(const delta_view<W, T, G>& v, unsigned g, T p, T q) {
    namespace sn = ikea::boundlist::native;
    const auto first = g * G, n = std::min(G, v.count - first);
    const auto base = read_element(v.checkpoints, g);
    const std::array<T, 2> points{p, q};
    std::array<unsigned, 2> result{v.count, v.count};
    const std::array<unsigned, 2> active{unsigned(v.eligible.mask(first, n)), unsigned(low_bits(n))};
    unsigned carry = 0;
#if defined(__AVX2__) || defined(__aarch64__)
    if constexpr ((sizeof(W) == 1 || sizeof(W) == 2) && G % sn::lanes == 0) {
        for (unsigned i = 0; i < G; i += sn::lanes) {
            const auto x = sn::load<W>(reinterpret_cast<const std::byte*>(v.group(g) + i));
            const auto prefix = sn::prefix(x);
            const auto positions = sn::add(prefix, sn::splat(carry));
            auto one = [&]<edge Policy>(unsigned k) {
                if (result[k] != v.count) return;
                unsigned mask = 0;
                if (points[k] < base) mask = unsigned(low_bits(sn::lanes));
                else {
                    const T relative = T(points[k] - base);
                    if (relative <= UINT32_MAX) {
                        if constexpr (Policy == edge::at_or_after)
                            mask = relative == 0 ? unsigned(low_bits(sn::lanes)) : sn::greater(positions, relative - 1);
                        else mask = sn::greater(positions, relative);
                    }
                }
                mask &= active[k] >> i & unsigned(low_bits(sn::lanes));
                if (mask) result[k] = first + i + std::countr_zero(mask);
            };
            one.template operator()<E>(0); one.template operator()<F>(1);
            if (result[0] != v.count && result[1] != v.count) break;
            carry += sn::last(prefix);
        }
        return result;
    }
#endif
    T position = base;
    for (unsigned i = 0; i < n; ++i) {
        position += read_element(v.group(g), i);
        if (result[0] == v.count && (active[0] >> i & 1) && meets<E>(position, p)) result[0] = first + i;
        if (result[1] == v.count && meets<F>(position, q)) result[1] = first + i;
    }
    return result;
}

template <edge E, edge F, class V>
[[gnu::always_inline]] std::array<unsigned, 2> pair_seek(const V& v, const typename V::point_type& p, const typename V::point_type& q) {
    std::array<unsigned, 2> lo{}, hi{v.groups(), v.groups()};
    while (lo[0] < hi[0] || lo[1] < hi[1]) {
        const auto a = lo[0] + (hi[0] - lo[0]) / 2;
        const auto b = lo[1] + (hi[1] - lo[1]) / 2;
        // One checkpoint load if the two probe paths still coincide.
        if (lo[0] < hi[0]) {
            const auto x = v.group_end(a);
            if (meets<E>(x, p)) hi[0] = a; else lo[0] = a + 1;
            if (lo[1] < hi[1] && a == b) {
                if (meets<F>(x, q)) hi[1] = b; else lo[1] = b + 1;
                continue;
            }
        }
        if (lo[1] < hi[1]) {
            if (meets<F>(v.group_end(b), q)) hi[1] = b; else lo[1] = b + 1;
        }
    }
    std::array<unsigned, 2> result{v.count, v.count};
    if (lo[0] == lo[1] && lo[0] < v.groups()) result = pair_in_group<E, F>(v, lo[0], p, q);
    else {
        if (lo[0] < v.groups()) result[0] = v.template in_group<E>(lo[0], p);
        auto raw = v; raw.eligible = {};
        if (lo[1] < v.groups()) result[1] = raw.template in_group<F>(lo[1], q);
    }
    // Eligibility may remove the entire initially selected group. Upper routing
    // is physical and ignores population; lower resolution skips inactive ranks.
    if (result[0] == v.count) for (auto g = lo[0] + 1; g < v.groups(); ++g) {
        result[0] = v.template in_group<E>(g, p);
        if (result[0] != v.count) break;
    }
    return result;
}

template <bool Paired, method M = method::binary, class V>
[[gnu::always_inline]] rank_window geometry_overlap(const V& v, const query_range<typename V::point_type>& q) {
    if (!v.count || q.empty() || !(origin(v) < q.high)) return {v.count, v.count};
    // This adapter describes half-open physical reservations and half-open
    // query ranges. It must not mistake tombstones or zero reservations for
    // populated equal logical extrema (the latter use extrema_overlap).
    const auto end = extent(v);
    if (!(q.low < end)) return {v.count, v.count};
    const auto low = std::max(origin(v), q.low), high = std::min(end, q.high);
    if (!(low < high)) return {v.count, v.count};
    auto raw = v; raw.eligible = {};
    std::array<unsigned, 2> ranks;
    if constexpr (Paired) ranks = pair_seek<edge::after, edge::at_or_after>(raw, low, high);
    else {
        ranks = {seek<edge::after, M>(raw, low), seek<edge::at_or_after, M>(raw, high)};
    }
    const auto limit = ranks[1] == v.count ? v.count : ranks[1] + 1;
    if (ranks[0] >= limit) return {v.count, v.count};
    // Raw strict/closed boundary searches identify positive physical owners
    // once clamped. Only retirement skipping can land on zero reservations.
    if (!v.eligible.words) return {ranks[0], limit};
    auto first = eligible_from(v.eligible, ranks[0], limit);
    while (first < limit && !positive(v, first)) first = eligible_from(v.eligible, first + 1, limit);
    if (first >= limit) return {v.count, v.count};
    auto last = live_before(v, limit);
    while (last != v.count && !positive(v, last)) last = live_before(v, last);
    return last == v.count || last < first ? rank_window{v.count, v.count} : rank_window{first, last + 1};
}
} // namespace boundlist_spike
