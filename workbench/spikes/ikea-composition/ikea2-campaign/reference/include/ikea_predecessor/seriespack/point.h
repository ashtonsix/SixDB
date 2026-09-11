#pragma once

#include <ikea_predecessor/seriespack/operations.h>
#include <ikea_predecessor/seriespack/detail/physical.h>

namespace ikea_predecessor::seriespack {

/// Unchecked unsigned point read: requires index < size() and stable admitted storage.
/// Reader changes addressing over the same bytes; no materialized tile or width dispatch.
template<point_reader Reader = point_reader::arithmetic, class Format, class Byte>
[[gnu::always_inline]] inline typename Format::scalar_type
trusted_get(const basic_static_view<Format, Byte>& source, std::size_t index) {
    static_assert(Reader == point_reader::arithmetic || Reader == point_reader::constant_offsets);
    constexpr auto layout = Format::layout;
    constexpr auto W = payload_width(layout), H = layout.head_bits;
    constexpr auto G = layout.storage;
    constexpr auto T = Format::payload::tile_values;
    const auto tile = index / T, local = index % T;
    const auto& p = source.placement();
    std::uint64_t value = 0;
    if constexpr (W != 0) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(
            p.payload.bytes.data() + tile * p.payload.stride);
        if constexpr (Reader == point_reader::arithmetic) value = detail::get_arithmetic<W, G>(bytes, local);
        else value = detail::get<W, G>(bytes, local);
    }
    if constexpr (H != 0) {
        auto head = std::to_integer<std::uint64_t>(p.heads[0].bytes[tile * p.heads[0].stride + local]);
        if constexpr (H == 16) head = (head << 8) |
            std::to_integer<std::uint64_t>(p.heads[1].bytes[tile * p.heads[1].stride + local]);
        value |= head << W;
    }
    return static_cast<typename Format::scalar_type>(value);
}

/// Unchecked point update: requires index < size(), value fitting full K, and
/// isolation of shared RMW bytes. Optional effects need sufficient disjoint slots
/// (write_effect_capacity) and append after the existing prefix; nullptr omits them.
template<class Format>
[[gnu::always_inline]] inline void
trusted_set(const static_mutable_view<Format>& destination, std::size_t index,
            std::uint64_t value, effect_output* effects = nullptr) {
    constexpr auto layout = Format::layout;
    constexpr auto W = payload_width(layout), H = layout.head_bits;
    constexpr auto G = layout.storage;
    constexpr auto T = Format::payload::tile_values;
    const auto tile = index / T, local = index % T;
    const auto& p = destination.placement();
    const auto emit = [&](std::byte* data, std::size_t size) {
        if (effects != nullptr) effects->storage[effects->size++] = {data, size};
    };
    if constexpr (W != 0) {
        auto* bytes = p.payload.bytes.data() + tile * p.payload.stride;
        detail::set_low<W, G>(reinterpret_cast<std::uint8_t*>(bytes), local, value);
        if (effects != nullptr) detail::point_write_spans<W, G>(local,
            [&](std::size_t offset, std::size_t size) { emit(bytes + offset, size); });
    }
    detail::static_for<H / 8>([&](auto h) {
        auto* head = p.heads[h].bytes.data() + tile * p.heads[h].stride + local;
        *head = std::byte(value >> (layout.width - 8 * (h + 1)));
        emit(head, 1);
    });
}

} // namespace ikea_predecessor::seriespack
