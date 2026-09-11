#include <ikea/seriespack/operations.h>
#include <ikea/seriespack/detail/physical.h>
#include "native_dispatch.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>

namespace ikea::seriespack {
namespace {

using status = std::expected<void, error>;

struct interval { std::uintptr_t begin = 0, end = 0; };

std::expected<interval, error> memory_range(const void* pointer, std::size_t count,
                                          std::size_t element_size) {
    if (count == 0) return interval{};
    if (pointer == nullptr) return std::unexpected(error::insufficient_storage);
    constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
    if (count > maximum / element_size) return std::unexpected(error::overflow);
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    const auto bytes = count * element_size;
    if (bytes > maximum - begin) return std::unexpected(error::overflow);
    return interval{begin, begin + bytes};
}

bool overlaps(interval a, interval b) {
    return a.begin < b.end && b.begin < a.end;
}

// A contiguous external range against the occupied intervals of one stream.
// No enumeration of possibly billions of tiles or rejection of foreign gaps.
bool overlaps_stream(interval other, basic_plane<const std::byte> plane,
                     std::size_t bytes, std::size_t tiles) {
    if (tiles == 0 || bytes == 0 || other.begin == other.end) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(plane.bytes.data());
    const auto end = base + (tiles - 1) * plane.stride + bytes;
    if (other.end <= base || other.begin >= end) return false;
    const auto tile = other.begin <= base ? 0 : (other.begin - base) / plane.stride;
    if (tile < tiles && overlaps(other, {base + tile * plane.stride,
                                        base + tile * plane.stride + bytes})) return true;
    return tile + 1 < tiles && other.end > base + (tile + 1) * plane.stride;
}

bool overlaps_view(interval other, const const_view& view) {
    const auto layout = view.layout();
    const auto values = tile_values(layout);
    const auto tiles = view.size() / values + (view.size() % values != 0);
    const auto& p = view.placement();
    if (overlaps_stream(other, p.payload, tile_bytes(layout), tiles)) return true;
    for (unsigned h = 0; h < layout.head_bits / 8; ++h)
        if (overlaps_stream(other, p.heads[h], values, tiles)) return true;
    return false;
}

std::expected<unsigned, error> element_bytes(element_width width) {
    switch (width) {
        case element_width::u8: return 1;
        case element_width::u16: return 2;
        case element_width::u32: return 4;
        case element_width::u64: return 8;
    }
    return std::unexpected(error::invalid_output_type);
}

template<class F> decltype(auto) with_element(element_width width, F&& f) {
    switch (width) {
        case element_width::u8: return f.template operator()<std::uint8_t>();
        case element_width::u16: return f.template operator()<std::uint16_t>();
        case element_width::u32: return f.template operator()<std::uint32_t>();
        case element_width::u64: return f.template operator()<std::uint64_t>();
    }
    __builtin_unreachable(); // Width admitted before dispatch.
}

template<class F> decltype(auto) with_payload(description layout, F&& f) {
    const unsigned width = payload_width(layout);
    if (layout.storage == geometry::local8) {
        return detail::dispatch_group<65>(width, [&](auto w) -> decltype(auto) {
            return f.template operator()<w, geometry::local8>();
        });
    }
    switch (width) {
#define SERIESPACK_STRIPE_CASE(W) case W: return f.template operator()<W, geometry::striped>();
        SERIESPACK_STRIPE_CASE(1) SERIESPACK_STRIPE_CASE(2) SERIESPACK_STRIPE_CASE(3)
        SERIESPACK_STRIPE_CASE(4) SERIESPACK_STRIPE_CASE(5) SERIESPACK_STRIPE_CASE(6)
        SERIESPACK_STRIPE_CASE(7) SERIESPACK_STRIPE_CASE(10) SERIESPACK_STRIPE_CASE(12)
        SERIESPACK_STRIPE_CASE(14) SERIESPACK_STRIPE_CASE(15) SERIESPACK_STRIPE_CASE(20)
#undef SERIESPACK_STRIPE_CASE
    }
    __builtin_unreachable(); // Description is an attached view's fact.
}

template<class F> void selected_indices(index_range rows, selection selected, F&& f) {
    if (selected.is_all()) {
        for (auto i = rows.begin; i < rows.end; ++i) f(i);
        return;
    }
    for (auto i = rows.begin; i < rows.end;) {
        const auto bit = i - selected.origin();
        const auto first = bit % 64;
        const auto count = std::min<std::size_t>(64 - first, rows.end - i);
        auto bits = selected.words()[bit / 64] >> first;
        if (count < 64) bits &= (std::uint64_t{1} << count) - 1;
        while (bits != 0) {
            f(i + std::countr_zero(bits));
            bits &= bits - 1;
        }
        i += count;
    }
}

std::size_t selected_count(index_range rows, selection selected) {
    if (selected.is_all()) return rows.size();
    std::size_t result = 0;
    for (auto i = rows.begin; i < rows.end;) {
        const auto bit = i - selected.origin(), first = bit % 64;
        const auto count = std::min<std::size_t>(64 - first, rows.end - i);
        auto bits = selected.words()[bit / 64] >> first;
        if (count < 64) bits &= (std::uint64_t{1} << count) - 1;
        result += std::popcount(bits);
        i += count;
    }
    return result;
}

bool fits(std::uint64_t value, unsigned width) {
    return width == 64 || (value >> width) == 0;
}

template<class UInt> UInt input_at(const void* data, std::size_t i) {
    UInt value;
    std::memcpy(&value, static_cast<const std::byte*>(data) + i * sizeof(UInt), sizeof(UInt));
    return value;
}

status validate_input(input_values input, index_range rows, selection selected,
                      unsigned width) {
    bool valid = true;
    with_element(input.width, [&]<class UInt> {
        if (sizeof(UInt) * 8 <= width) return;
        selected_indices(rows, selected, [&](std::size_t i) {
            valid &= fits(input_at<UInt>(input.data, i - rows.begin), width);
        });
    });
    return valid ? status{} : status{std::unexpected(error::invalid_value)};
}

status validate_effects(const const_view& destination, effect_output* effects,
                        std::size_t capacity, interval input = {}, interval mask = {}) {
    if (effects == nullptr) return {};
    if (effects->size > effects->storage.size() ||
        capacity > effects->storage.size() - effects->size)
        return std::unexpected(error::insufficient_effect_storage);
    if (capacity == 0) return {};
    const auto storage = memory_range(effects->storage.data(), effects->storage.size(), sizeof(byte_span));
    const auto state = memory_range(effects, 1, sizeof(effect_output));
    if (!storage) return std::unexpected(storage.error());
    if (!state) return std::unexpected(state.error());
    const interval records{storage->begin + effects->size * sizeof(byte_span),
                           storage->begin + (effects->size + capacity) * sizeof(byte_span)};
    if (overlaps(*storage, *state) || overlaps_view(records, destination) ||
        overlaps_view(*state, destination) || overlaps(records, input) ||
        overlaps(records, mask) || overlaps(*state, input) || overlaps(*state, mask))
        return std::unexpected(error::overlapping_storage);
    return {};
}

void emit(effect_output* effects, std::byte* data, std::size_t bytes) {
    if (effects != nullptr && bytes != 0) effects->storage[effects->size++] = {data, bytes};
}

template<unsigned W, geometry G, unsigned H, point_reader Reader = point_reader::constant_offsets>
std::uint64_t point(const const_view& source, std::size_t index) {
    constexpr auto T = payload_layout<W, G>::tile_values;
    const auto tile = index / T, local = index % T;
    const auto& p = source.placement();
    std::uint64_t value = 0;
    if constexpr (W != 0) {
        const auto* payload = reinterpret_cast<const std::uint8_t*>(
            p.payload.bytes.data() + tile * p.payload.stride);
        if constexpr (Reader == point_reader::arithmetic) value = detail::get_arithmetic<W, G>(payload, local);
        else value = detail::get<W, G>(payload, local);
    }
    if constexpr (H != 0) {
        auto high = std::to_integer<std::uint64_t>(p.heads[0].bytes[tile * p.heads[0].stride + local]);
        if constexpr (H == 16) high = (high << 8) |
            std::to_integer<std::uint64_t>(p.heads[1].bytes[tile * p.heads[1].stride + local]);
        value |= high << W;
    }
    return value;
}

template<unsigned W, geometry G>
void point_write(mutable_view& destination, std::size_t index, std::uint64_t value,
                 effect_output* effects) {
    constexpr auto T = payload_layout<W, G>::tile_values;
    const auto tile = index / T, local = index % T;
    const auto& p = destination.placement();
    if constexpr (W != 0) {
        auto* payload = p.payload.bytes.data() + tile * p.payload.stride;
        detail::set_low<W, G>(reinterpret_cast<std::uint8_t*>(payload), local, value);
        if (effects != nullptr) detail::point_write_spans<W, G>(local, [&](std::size_t offset, std::size_t bytes) {
            emit(effects, payload + offset, bytes);
        });
    }
    const auto layout = destination.layout();
    for (unsigned h = 0; h < layout.head_bits / 8; ++h) {
        auto* head = p.heads[h].bytes.data() + tile * p.heads[h].stride + local;
        *head = std::byte(value >> (layout.width - 8 * (h + 1)));
        emit(effects, head, 1);
    }
}

template<unsigned W, geometry G, unsigned H, class UInt>
void scalar_decode(const const_view& source, index_range rows, UInt* output) {
    // Type and physical width are selected outside this loop. A bound point
    // function is likewise monomorphic; it returns a scalar without status.
    for (auto i = rows.begin; i < rows.end; ++i) {
        const UInt value = static_cast<UInt>(point<W, G, H>(source, i));
        std::memcpy(reinterpret_cast<std::byte*>(output) + (i - rows.begin) * sizeof(UInt), &value, sizeof(UInt));
    }
}

template<unsigned W, geometry G, unsigned H>
void decode_bound_scalar(const const_view& source, index_range rows, void* output, element_width width) {
    with_element(width, [&]<class UInt> {
        scalar_decode<W, G, H>(source, rows, static_cast<UInt*>(output));
    });
}

template<unsigned W, geometry G, unsigned H>
bound_reader::point_function point_function(point_reader strategy) {
    return strategy == point_reader::arithmetic ? point<W, G, H, point_reader::arithmetic>
                                               : point<W, G, H, point_reader::constant_offsets>;
}

template<unsigned W, geometry G, unsigned H>
bound_reader scalar_reader(const_view source, point_reader strategy) {
    return bound_reader::assume_valid(source, execution_target::scalar,
        point_function<W, G, H>(strategy), decode_bound_scalar<W, G, H>, strategy);
}

template<unsigned W, geometry G>
bound_reader choose_scalar_reader(const_view source, point_reader strategy) {
    const auto h = source.layout().head_bits;
    if constexpr (W >= 1) if (h == 0) return scalar_reader<W, G, 0>(source, strategy);
    if constexpr (W <= 56) if (h == 8) return scalar_reader<W, G, 8>(source, strategy);
    if constexpr (W <= 48) if (h == 16) return scalar_reader<W, G, 16>(source, strategy);
    __builtin_unreachable();
}

template<unsigned W, geometry G, class UInt>
void scalar_encode(const mutable_view& destination, const UInt* input) {
    constexpr auto T = payload_layout<W, G>::tile_values;
    const auto& p = destination.placement();
    const auto layout = destination.layout();
    const auto n = destination.size();
    const auto full = n / T;
    const auto encode_one = [&](const UInt* values, std::size_t tile) {
        if constexpr (W != 0) detail::encode_low_tile<W, G>(values,
            reinterpret_cast<std::uint8_t*>(p.payload.bytes.data() + tile * p.payload.stride));
        for (unsigned h = 0; h < layout.head_bits / 8; ++h) {
            auto* head = p.heads[h].bytes.data() + tile * p.heads[h].stride;
            for (std::size_t i = 0; i < T; ++i)
                head[i] = std::byte(std::uint64_t(values[i]) >> (layout.width - 8 * (h + 1)));
        }
    };
    for (std::size_t tile = 0; tile < full; ++tile) encode_one(input + tile * T, tile);
    if (const auto left = n % T; left != 0) {
        std::array<UInt, T> boundary{};
        std::memcpy(boundary.data(), input + full * T, left * sizeof(UInt));
        encode_one(boundary.data(), full);
    }
}

void collect_encode_effects(const mutable_view& destination, effect_output* effects) {
    if (effects == nullptr || destination.size() == 0) return;
    const auto layout = destination.layout();
    const auto T = tile_values(layout), B = tile_bytes(layout);
    const auto tiles = destination.size() / T + (destination.size() % T != 0);
    const auto payload = destination.placement().payload;
    auto* const records = effects->storage.data();
    auto count = effects->size;
    // This nonsuspending collector has no intermediate observer. Keep its
    // append cursor local, then update the output length after all streams.
    const auto stream = [&](basic_plane<std::byte> plane, std::size_t bytes) {
        if (bytes == 0) return;
        if (plane.stride == bytes) records[count++] = {plane.bytes.data(), tiles * bytes};
        else for (std::size_t tile = 0; tile < tiles; ++tile)
            records[count++] = {plane.bytes.data() + tile * plane.stride, bytes};
    };
    stream(payload, B);
    if (layout.head_bits != 0) stream(destination.placement().heads[0], T);
    if (layout.head_bits == 16) stream(destination.placement().heads[1], T);
    effects->size = count;
}

template<unsigned W, geometry G>
void encode_bound_scalar(const mutable_view& destination, const void* input,
                         element_width width, effect_output* effects) {
    if (destination.size() == 0) return;
    with_element(width, [&]<class UInt> {
        scalar_encode<W, G>(destination, static_cast<const UInt*>(input));
    });
    collect_encode_effects(destination, effects);
}

std::expected<execution_target, error> admitted_target(execution_target target) {
    if (target == execution_target::scalar) return target;
    if (target == execution_target::automatic) {
        for (auto candidate : {execution_target::avx512, execution_target::avx2,
                               execution_target::neon})
            if (detail::native_target_available(candidate)) return candidate;
        return execution_target::scalar;
    }
    if (detail::native_target_available(target)) return target;
    return std::unexpected(error::unsupported);
}

} // namespace

void detail::encode_effects(const mutable_view& destination, effect_output* effects) {
    collect_encode_effects(destination, effects);
}

bound_reader::point_function detail::select_point_function(description layout, point_reader strategy) {
    return with_payload(layout, [&]<unsigned W, geometry G> {
        if constexpr (W >= 1) if (layout.head_bits == 0) return point_function<W, G, 0>(strategy);
        if constexpr (W <= 56) if (layout.head_bits == 8) return point_function<W, G, 8>(strategy);
        if constexpr (W <= 48) if (layout.head_bits == 16) return point_function<W, G, 16>(strategy);
        __builtin_unreachable();
    });
}

std::expected<std::size_t, error> encode_effect_capacity(const_view destination) {
    const auto layout = destination.layout();
    const auto T = tile_values(layout), B = tile_bytes(layout);
    const auto tiles = destination.size() / T + (destination.size() % T != 0);
    if (tiles == 0) return 0;
    std::size_t result = 0;
    const auto add = [&](std::size_t count) -> bool {
        if (count > std::numeric_limits<std::size_t>::max() - result) return false;
        result += count;
        return true;
    };
    if (B != 0 && !add(destination.placement().payload.stride == B ? 1 : tiles))
        return std::unexpected(error::overflow);
    for (unsigned h = 0; h < layout.head_bits / 8; ++h)
        if (!add(destination.placement().heads[h].stride == T ? 1 : tiles))
            return std::unexpected(error::overflow);
    return result;
}

std::expected<std::size_t, error> write_effect_capacity(const_view destination,
                                                       index_range rows, selection selected) {
    if (auto s = validate_range(rows, destination.size()); !s) return std::unexpected(s.error());
    if (auto s = selected.validate(rows); !s) return std::unexpected(s.error());
    const auto layout = destination.layout();
    const unsigned w = payload_width(layout), r = w % 8;
    const std::size_t per_value = (w >= 8) + layout.head_bits / 8 +
        (r == 0 ? 0 : layout.storage == geometry::local8 || r == 1 || r == 2 || r == 4 ? 1 : 2);
    const auto count = selected_count(rows, selected);
    if (per_value != 0 && count > std::numeric_limits<std::size_t>::max() / per_value)
        return std::unexpected(error::overflow);
    return count * per_value;
}

std::expected<bound_reader, error> bind_reader(const_view source, execution_target target,
                                               point_reader strategy) {
    if (strategy != point_reader::arithmetic && strategy != point_reader::constant_offsets)
        return std::unexpected(error::unsupported);
    auto selected = admitted_target(target);
    if (!selected) return std::unexpected(selected.error());
    if (*selected != execution_target::scalar)
        return detail::bind_native_reader(source, *selected, strategy);
    return with_payload(source.layout(), [&]<unsigned W, geometry G> {
        return choose_scalar_reader<W, G>(source, strategy);
    });
}

std::expected<std::uint64_t, error> get(const_view source, std::size_t index) {
    if (index >= source.size()) return std::unexpected(error::invalid_range);
    auto reader = bind_reader(source, execution_target::scalar);
    return reader->get(index);
}

std::expected<bound_encoder, error> bind_encoder(mutable_view destination,
                                                execution_target target) {
    const auto selected = admitted_target(target);
    if (!selected) return std::unexpected(selected.error());
    if (*selected != execution_target::scalar)
        return detail::bind_native_encoder(destination, *selected);
    return with_payload(destination.layout(), [&]<unsigned W, geometry G> {
        return bound_encoder::assume_valid(destination, execution_target::scalar,
                                          encode_bound_scalar<W, G>);
    });
}

std::expected<void, error> decode(const_view source, index_range rows,
                                  output_values output, execution_target target) {
    if (auto s = validate_range(rows, source.size()); !s) return s;
    const auto bytes = element_bytes(output.width);
    if (!bytes) return std::unexpected(bytes.error());
    if (*bytes * 8 < source.layout().width) return std::unexpected(error::invalid_output_type);
    if (output.size < rows.size()) return std::unexpected(error::insufficient_storage);
    const auto memory = memory_range(output.data, rows.size(), *bytes);
    if (!memory) return std::unexpected(memory.error());
    if (overlaps_view(*memory, source)) return std::unexpected(error::overlapping_storage);
    const auto reader = bind_reader(source, target);
    if (!reader) return std::unexpected(reader.error());
    if (!rows.empty()) reader->decode(rows, output);
    return {};
}

std::expected<void, error> encode(mutable_view destination, input_values input,
                                  effect_output* effects, execution_target target) {
    const auto selected_target = admitted_target(target);
    if (!selected_target) return std::unexpected(selected_target.error());
    const auto bytes = element_bytes(input.width);
    if (!bytes) return std::unexpected(bytes.error());
    if (input.size < destination.size()) return std::unexpected(error::insufficient_storage);
    const auto memory = memory_range(input.data, destination.size(), *bytes);
    if (!memory) return std::unexpected(memory.error());
    const auto view = destination.as_const();
    if (overlaps_view(*memory, view)) return std::unexpected(error::overlapping_storage);
    const auto capacity = effects ? encode_effect_capacity(view) : std::expected<std::size_t, error>{0};
    if (!capacity) return std::unexpected(capacity.error());
    if (auto s = validate_effects(view, effects, *capacity, *memory); !s) return s;
    if (auto s = validate_input(input, {0, destination.size()}, selection::all(), destination.layout().width); !s) return s;
    if (destination.size() == 0) return {};
    if (*selected_target == execution_target::scalar) {
        with_payload(destination.layout(), [&]<unsigned W, geometry G> {
            with_element(input.width, [&]<class UInt> {
                scalar_encode<W, G>(destination, static_cast<const UInt*>(input.data));
            });
        });
    } else detail::native_encode(destination, input, *selected_target);
    collect_encode_effects(destination, effects);
    return {};
}

std::expected<void, error> write(mutable_view destination, index_range rows,
                                 input_values input, selection selected, effect_output* effects) {
    if (auto s = validate_range(rows, destination.size()); !s) return s;
    if (auto s = selected.validate(rows); !s) return s;
    const auto bytes = element_bytes(input.width);
    if (!bytes) return std::unexpected(bytes.error());
    if (input.size < rows.size()) return std::unexpected(error::insufficient_storage);
    const auto memory = memory_range(input.data, rows.size(), *bytes);
    if (!memory) return std::unexpected(memory.error());
    const auto view = destination.as_const();
    if (overlaps_view(*memory, view)) return std::unexpected(error::overlapping_storage);
    auto mask = memory_range(selected.words().data(), selected.words().size(), sizeof(std::uint64_t));
    if (!mask) return std::unexpected(mask.error());
    if (overlaps_view(*mask, view)) return std::unexpected(error::overlapping_storage);
    const auto capacity = effects ? write_effect_capacity(view, rows, selected) : std::expected<std::size_t, error>{0};
    if (!capacity) return std::unexpected(capacity.error());
    if (auto s = validate_effects(view, effects, *capacity, *memory, *mask); !s) return s;
    if (auto s = validate_input(input, rows, selected, destination.layout().width); !s) return s;
    with_payload(destination.layout(), [&]<unsigned W, geometry G> {
        with_element(input.width, [&]<class UInt> {
            selected_indices(rows, selected, [&](std::size_t i) {
                point_write<W, G>(destination, i, input_at<UInt>(input.data, i - rows.begin), effects);
            });
        });
    });
    return {};
}

std::expected<void, error> set(mutable_view destination, std::size_t index,
                               std::uint64_t value, effect_output* effects) {
    if (index >= destination.size()) return std::unexpected(error::invalid_range);
    if (!fits(value, destination.layout().width)) return std::unexpected(error::invalid_value);
    const auto view = destination.as_const();
    const auto capacity = effects ? write_effect_capacity(view, {index, index + 1}, selection::all())
                                  : std::expected<std::size_t, error>{0};
    if (!capacity) return std::unexpected(capacity.error());
    if (auto s = validate_effects(view, effects, *capacity); !s) return s;
    with_payload(destination.layout(), [&]<unsigned W, geometry G> {
        point_write<W, G>(destination, index, value, effects);
    });
    return {};
}

} // namespace ikea::seriespack
