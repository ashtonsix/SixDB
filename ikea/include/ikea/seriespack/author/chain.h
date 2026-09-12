#pragma once
#include <ikea/seriespack/detail/dense_read.h>
#include <ikea/detail/native_chain.h>

namespace ikea::seriespack {
#if defined(__aarch64__) || defined(__AVX2__)

/// Provisional straight-through shell, with a type-specific native carrier.
/// Different carriers require a checked bridge; incompatible function casts are
/// not an interface. Stopping/suspension occurs after returning to the owner.
template <unsigned K, unsigned Slots = 8>
using chain = ikea::detail::native_chain<native::values<K>, std::uint16_t, Slots, error,
                                         std::make_index_sequence<native::values<K>::parts>>;

/// A native packet is an execution grain, independent of physical tiling. Keep
/// the flattened carrier within the target's register argument budget (the
/// initial operating points use at most eight vector arguments).
template <unsigned K, unsigned N> struct pipeline_packet {
    static_assert(N >= 16 && N <= 64 && N % 16 == 0);
    using region = native::values<K>;
    using vector = typename region::vector;
    static constexpr unsigned parts = region::parts * (N / 16), rows = N;
    static_assert(parts <= 8, "Choose a smaller pipeline grain to retain register handoff");
    std::array<vector, parts> v;
    template <unsigned P> [[gnu::always_inline]] region get() const {
        region value;
        detail::each<region::parts>([&](auto i) { value.v[i] = v[P * region::parts + i]; });
        return value;
    }
    template <unsigned P> [[gnu::always_inline]] void set(region value) {
        detail::each<region::parts>([&](auto i) { v[P * region::parts + i] = value.v[i]; });
    }
};
template <unsigned K, unsigned N, unsigned Slots = 8>
using packet_chain =
    ikea::detail::native_chain<pipeline_packet<K, N>, std::uint64_t, Slots, error,
                               std::make_index_sequence<pipeline_packet<K, N>::parts>>;
#endif
} // namespace ikea::seriespack
