#pragma once
#include <ikea/seriespack/erased_reader.h>
#include <ikea/seriespack/author/native.h>
namespace ikea::seriespack {
namespace detail {
template <class F, class U> U dense_point(const void* source, std::size_t i) {
    return point_payload<F>(static_cast<const std::uint8_t*>(source) +
                                (i / F::tile_rows) * F::tile_bytes,
                            i % F::tile_rows);
}
template <class F, class U>
[[gnu::always_inline]] inline void dense_region(const std::uint8_t* data, std::size_t i,
                                                U* __restrict out) {
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    native_group::store(out, native_group::read<F, true, 16>(data, F::tile_bytes, i));
#elif defined(__aarch64__) || defined(__AVX2__)
    native::store16(out, native::read16<F, true>(data, F::tile_bytes, i));
#else
    each<16>([&](auto j) {
        const auto p = i + j;
        out[j] = point_payload<F>(data + (p / F::tile_rows) * F::tile_bytes, p % F::tile_rows);
    });
#endif
}
template <class F, class U> void dense_read16(const void* data, std::size_t i, U* __restrict out) {
    __builtin_assume(i % 16 == 0);
    dense_region<F>(static_cast<const std::uint8_t*>(data), i, out);
}
template <class F, class U>
void dense_range(const void* source, std::size_t first, std::size_t count, U* __restrict out) {
    const auto* data = static_cast<const std::uint8_t*>(source);
    // Shape selection is independent of layout. The whole public range call is
    // measured, including these branches and arbitrary prefix/suffix handling.
    if (first % 16 == 0 && count == 16)
        return dense_region<F>(data, first, out);
#if defined(__aarch64__) || defined(__AVX2__)
    // A partial packet whose tiles all hold requested rows exists in full, so
    // it is decoded whole and the requested part copied (short reads would
    // otherwise take up to 30 scalar points). The containing tiles run from
    // the first requested row's tile to the last's; nothing outside is read.
    const std::size_t lowest = first / F::tile_rows * F::tile_rows;
    const std::size_t backed = (first + count + F::tile_rows - 1) / F::tile_rows * F::tile_rows;
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
    // A short read inside its range's tiles: the covering native groups,
    // stored by lane masks (no scalar or 16-row edges). Only where a group
    // stays inside one tile: a Local group would start before the first
    // requested row's tile (Zen 5 compact short reads then lost 5-10%).
    constexpr unsigned short_grain = F::width <= 8 ? 64 : F::width <= 16 ? 32 : 16;
    if constexpr (native_group::lane_store<U> && F::tile_rows >= short_grain) {
        const std::size_t g = first - first % short_grain, end = (first + count + short_grain - 1) / short_grain * short_grain;
        if (count && count <= 64 && g >= lowest && end <= backed) {
            for (std::size_t at = g; at < first + count; at += short_grain) {
                const auto from = at < first ? unsigned(first - at) : 0u;
                const auto n = unsigned(std::min<std::size_t>(at + short_grain, first + count) - (at + from));
                native_group::store_lanes(out, native_group::read<F, true, short_grain>(data, F::tile_bytes, at), from, n);
                out += n;
            }
            return;
        }
    }
#endif
    const auto partial = [&](std::size_t from, std::size_t n) __attribute__((always_inline)) {
        const auto p = first - from;
        U packet[16];
        dense_region<F>(data, p, packet);
        for (std::size_t j = 0; j < n; ++j)
            out[j] = packet[from + j];
        first += n, out += n, count -= n;
    };
    if (count && first % 16 && first - first % 16 >= lowest && first - first % 16 + 16 <= backed)
        partial(first % 16, std::min<std::size_t>(count, 16 - first % 16));
#endif
    while (count && first % 16) {
        *out++ =
            point_payload<F>(data + (first / F::tile_rows) * F::tile_bytes, first % F::tile_rows);
        ++first;
        --count;
    }
    if constexpr (F::storage == geometry::striped) {
        // A complete-tile traversal fixes stripe groups at compile time. Random
        // short reads still use their dynamic group endpoint; scans do not pay
        // those choices for every fragment.
        while (count >= 16 && first % F::tile_rows) {
            dense_region<F>(data, first, out);
            first += 16;
            out += 16;
            count -= 16;
        }
        while (count >= F::tile_rows) {
            const auto* tile = data + (first / F::tile_rows) * F::tile_bytes;
#if defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__)
            each<F::tile_rows / 32>([&](auto p) {
                native_group::store(out + p * 32,
                                    native_group::read<F, true, 32>(tile, F::tile_bytes, p * 32));
            });
#elif defined(__AVX2__)
            each<F::tile_rows / 32>([&](auto p) {
                native::store32(out + p * 32,
                                native::read_striped32<F, true>(tile, F::tile_bytes, p * 32));
            });
#else
            each<F::tile_rows / 16>([&](auto p) { dense_region<F>(tile, p * 16, out + p * 16); });
#endif
            first += F::tile_rows;
            out += F::tile_rows;
            count -= F::tile_rows;
        }
    }
#if defined(__aarch64__) ||                                                                        \
    (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
    constexpr unsigned grain = F::width <= 8 ? 64 : F::width <= 16 ? 32 : 16;
    while (count >= grain) {
        native_group::store(out, native_group::read<F, true, grain>(data, F::tile_bytes, first));
        first += grain;
        out += grain;
        count -= grain;
    }
#endif
    while (count >= 16) {
        dense_region<F>(data, first, out);
        first += 16;
        out += 16;
        count -= 16;
    }
#if defined(__aarch64__) || defined(__AVX2__)
    if (count && first + 16 <= backed)
        partial(0, count);
#endif
    while (count--) {
        *out++ =
            point_payload<F>(data + (first / F::tile_rows) * F::tile_bytes, first % F::tile_rows);
        ++first;
    }
}
} // namespace detail
namespace detail {
/// Copies the admitted dense pointer/count proof. Only bytes need remain live;
/// the temporary dense_source wrapper need not survive this binding.
template <class U, class F> erased_reader<U> erase_dense_reader(dense_source<F> source) {
    static_assert(sizeof(U) * 8 >= F::width);
    return {source.data, source.count, detail::dense_range<F, U>, detail::dense_read16<F, U>,
            detail::dense_point<F, U>};
}

} // namespace detail
} // namespace ikea::seriespack
