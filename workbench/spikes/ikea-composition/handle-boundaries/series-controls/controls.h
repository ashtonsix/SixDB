#pragma once
#include <ikea/seriespack/read.h>
#include <ikea/effects.h>
#include <array>
#include <bit>
#include <limits>

namespace series_controls {
namespace sp = ikea::seriespack;
using word = std::uint64_t;
constexpr unsigned keys = 128;
enum class operation { point, range, read16 };
struct extent { std::uint16_t tile_bytes; std::uint8_t row_shift; };
extern const std::array<extent, keys> extent_columns;

// A study-owned immutable shape value, not a placement or content proof.
struct shape {
    std::uint16_t index;
    unsigned width() const { return index % 64 + 1; }
    sp::geometry geometry() const { return index < 64 ? sp::geometry::local : sp::geometry::striped; }
    sp::layout_value law() const { return {width(), 0, geometry()}; }
    std::size_t tile_rows() const {
        return std::size_t{1} << extent_columns[index].row_shift;
    }
    std::size_t tile_bytes() const { return extent_columns[index].tile_bytes; }
    static std::expected<shape, sp::error> make(sp::layout_value law, unsigned carrier_bits = 64) {
        if (!sp::valid(law) || law.heads || law.width > carrier_bits)
            return std::unexpected(sp::error::description);
        return shape{std::uint16_t(law.width - 1 + (law.storage == sp::geometry::striped ? 64 : 0))};
    }
    std::expected<std::size_t, sp::error> bytes(std::size_t count) const {
        const auto info = extent_columns[index];
        const auto tile = std::size_t(info.tile_bytes);
        const auto n = (count >> info.row_shift) + bool(count & ((std::size_t{1} << info.row_shift) - 1));
        if (n > std::numeric_limits<std::size_t>::max() / tile)
            return std::unexpected(sp::error::overflow);
        return n * tile;
    }
};
template <class U> struct columns {
    using point = typename sp::erased_reader<U>::point_function;
    using range = typename sp::erased_reader<U>::function;
    using region = typename sp::erased_reader<U>::region_function;
    std::array<point, keys> points{};
    std::array<range, keys> ranges{};
    std::array<region, keys> regions{};
};
template <class U> extern const columns<U> read_columns;
extern template const columns<std::uint8_t> read_columns<std::uint8_t>;
extern template const columns<std::uint16_t> read_columns<std::uint16_t>;
extern template const columns<std::uint32_t> read_columns<std::uint32_t>;
extern template const columns<word> read_columns<word>;

template <class U> struct shared_reader {
    const std::byte* data;
    std::size_t count;
    shape control;
    U get_unchecked(std::size_t row) const { return read_columns<U>.points[control.index](data, row); }
    void read_unchecked(std::size_t first, std::size_t take, U* out) const {
        if (take) read_columns<U>.ranges[control.index](data, first, take, out);
    }
    void read16_unchecked(std::size_t first, U* out) const {
        read_columns<U>.regions[control.index](data, first, out);
    }
    std::expected<U, sp::error> get(std::size_t row) const {
        if (row >= count) return std::unexpected(sp::error::range);
        return get_unchecked(row);
    }
    std::expected<void, sp::error> read(std::size_t first, std::span<U> out) const {
        if (first > count || out.size() > count - first) return std::unexpected(sp::error::range);
        read_unchecked(first, out.size(), out.data()); return {};
    }
    std::expected<void, sp::error> read16(std::size_t first, std::span<U> out) const {
        if (first % 16 || first > count || count - first < 16) return std::unexpected(sp::error::range);
        if (out.size() < 16) return std::unexpected(sp::error::capacity);
        read16_unchecked(first, out.data()); return {};
    }
};
template <class U>
std::expected<shared_reader<U>, sp::error> bind_reader(shape control, ikea::bytes bytes, std::size_t count) {
    if (control.width() > sizeof(U) * 8) return std::unexpected(sp::error::description);
    auto need = control.bytes(count);
    if (!need) return std::unexpected(need.error());
    if (bytes.size() < *need) return std::unexpected(sp::error::capacity);
    return shared_reader<U>{bytes.data(), count, control};
}

// Every command keeps this named owner and its placement stationary until the
// whole journal has been translated/resolved. No temporary typed view escapes.
struct destination {
    shape control;
    ikea::mutable_bytes bytes;
    std::size_t count;
};
using initialize_function = std::expected<void, sp::error> (*)(const destination&, std::span<const word>, ikea::journal&);
extern const std::array<initialize_function, keys> initialize_columns;
std::expected<void, sp::error> initialize_dispatch(const destination&, std::span<const word>, ikea::journal&);

// Small complete-operation loops over one runtime-selected typed law. These
// reuse maintained bodies; they do not clone a Frame query or packed algorithm.
struct command {
    const std::byte* data;
    std::size_t rows;
    unsigned id;
};
inline std::size_t point_row(const command& c, unsigned q, word epoch) { return (epoch + c.id * 17 + q * 13) % c.rows; }
inline std::pair<std::size_t, std::size_t> range_rows(const command& c, unsigned q, word epoch) {
    const auto take = std::min<std::size_t>((c.id & 1) ? 31 : 13, c.rows);
    const auto first = (epoch + c.id * 7 + q * 3) % (c.rows - take + 1);
    return {first, take};
}
inline std::size_t region_row(const command& c, unsigned q, word epoch) {
    return ((epoch + c.id * 7 + q * 3) % (c.rows / 16)) * 16;
}
inline word consume(std::span<const word> out) {
    word total = 0;
    for (auto x : out) total = std::rotl(total, 3) ^ x;
    return total;
}
using typed_function = word (*)(operation, const command&, unsigned, word);
extern const std::array<typed_function, keys> typed_columns;
using typed_initialize_function = word (*)(const destination&, std::span<const word>, std::span<const word>, unsigned, word, ikea::journal&);
extern const std::array<typed_initialize_function, keys> typed_initialize_columns;
word initialized(const destination&, ikea::journal&);
void check();
void facts();
} // namespace series_controls
