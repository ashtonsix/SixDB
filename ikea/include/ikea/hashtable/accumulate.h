#pragma once
#include "capacity.h"
#include "detail/accumulation/engine.h"
namespace ikea::hashtable {
/// Accumulate a coherent, preserved source into an exclusive destination.
/// The initial destination contributes first; successive calls preserve each
/// key's caller-specified source order. Reduce(current,incoming) is invoked
/// once per duplicate. New keys copy their value. No associativity is assumed.
/// Sizing, copying and reserve_overflow are separate caller operations. Primary
/// and Swiss backing remain fixed; failure/exception requires discarding dst.
/// Sources must not alias dst. Reducers must not reenter or invalidate either
/// owner; external side effects remain caller-owned. This leaf acquires no
/// grants and publishes no MVCC damage: callers own exclusive construction and
/// final publication. It is not an independently guarded shared-owner mutation.
// Selection is outside the entry loop and depends on CPU and destination
// footprint, never on overlap or reducer results. Sources remain ordered by
// the caller's sequence of single-source calls.
template <class D, class S, class Reduce>
[[nodiscard]] accumulation_status accumulate(D& dst, const S& src, Reduce&& reduce) {
#if defined(__AVX512VBMI__) && defined(__GFNI__)
#if SIXDB_TUNE_ZEN5
    // Paired b10..15 trials: row deferral costs hot mixed throughput and earns
    // its cost on the two largest destinations (136/272MiB). Granite retains
    // immediate payloads throughout the same panel.
    constexpr bool defer = std::size_t(D::bucket_count) * D::stride >= 128u * 1024u * 1024u;
#else
    constexpr bool defer = false;
#endif
    return detail::accumulation::accumulate_images<defer>(dst, src, std::forward<Reduce>(reduce));
#else
    return detail::accumulation::accumulate_points(dst, src, std::forward<Reduce>(reduce));
#endif
}

template <class D, class S>
    requires(std::tuple_size_v<typename D::value_type> == 0)
[[nodiscard]] accumulation_status accumulate(D& dst, const S& src) {
    return accumulate(dst, src, [](auto a, auto) { return a; });
}
} // namespace ikea::hashtable
