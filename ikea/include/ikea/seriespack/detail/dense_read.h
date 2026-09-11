#pragma once
#include <ikea/seriespack/read_operation.h>
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
    // Shape selection is independent of format. The whole public range call is
    // measured, including these branches and arbitrary prefix/suffix handling.
    if (first % 16 == 0 && count == 16)
        return dense_region<F>(data, first, out);
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
    while (count--) {
        *out++ =
            point_payload<F>(data + (first / F::tile_rows) * F::tile_bytes, first % F::tile_rows);
        ++first;
    }
}
} // namespace detail
/// Copies the admitted dense pointer/count proof. Only bytes need remain live;
/// the temporary dense_source wrapper need not survive this binding.
template <class U, class F> decoder<U> bind_decoder(dense_source<F> source) {
    static_assert(sizeof(U) * 8 >= F::width);
    return {source.data, source.count, detail::dense_range<F, U>, detail::dense_read16<F, U>,
            detail::dense_point<F, U>};
}

struct dense_input {
    unsigned width;
    geometry storage;
    std::span<const std::uint8_t> bytes;
    std::size_t count;
};
/// Compiled binding for a runtime-described dense headless array. Validation and
/// format selection happen once; output type is part of the requested operation.
/// Compiled width/geometry selection for dense headless bytes. Admission checks
/// output type width, layout and accessible extent; no data is read. The returned
/// operation borrows the bytes, not this input descriptor. U is u8/u16/u32/u64.
template <class U>
[[nodiscard]] std::expected<decoder<U>, error> bind_decoder(const dense_input& source);
extern template std::expected<decoder<std::uint8_t>, error> bind_decoder(const dense_input&);
extern template std::expected<decoder<std::uint16_t>, error> bind_decoder(const dense_input&);
extern template std::expected<decoder<std::uint32_t>, error> bind_decoder(const dense_input&);
extern template std::expected<decoder<std::uint64_t>, error> bind_decoder(const dense_input&);
} // namespace ikea::seriespack
