#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ht::layout_solver {

// Physical recipes, not predicted performance. A legal recipe does not imply
// that a consumer can lower it. Consumers declare supported features and reject
// missing ones before execution; measurement can be keyed by (spec, shape).
// Consumers initialize the entire admitted unit before using its windows. The
// caller chooses representable descendant/reference domains; only the local
// occupancy count can be checked against this recipe's capacity.
enum class container { set, map, multimap };
enum class tier { list, primary, secondary };
enum class platform { neon, avx2_amd, avx2_intel, avx512_amd, avx512_intel };
enum class fingerprint { byte_tiles, exact_bytes, packed_bits };
enum class suffix { bytes, packed_bits };
enum class body { planes, rows };
enum class tags { none, byte_plane, bit_plane, row_byte };
enum class field_id { count, descendants, head, fingerprints, suffixes, values, tag, end };
enum class error {
    invalid_width, invalid_header, invalid_kind, invalid_alignment,
    invalid_capacity, invalid_selector, invalid_tags, occupancy_exceeds_capacity,
    insufficient_unit, invalid_slot, invalid_window, too_many_windows,
    candidate_limit
};

struct request {
    // Independent physical widths; the hash recipe owns K = Ka + Kb + Kc.
    unsigned fingerprint_bits = 8;
    unsigned suffix_bits = 0;
    unsigned value_bytes = 8;
    container kind = container::map;
    tier where = tier::primary;
    platform target = platform::neon;
    unsigned count_bytes = 1;
    unsigned descendant_bytes = 4;
    unsigned head_bytes = 0;
    unsigned chain_reference_bytes = 8;
    unsigned allocation_alignment = 64;
    unsigned cacheline_bytes = 64;
    std::optional<unsigned> occupancy = std::nullopt;
    unsigned tag_bits = 1; // Multimap discriminator states, independent of value width.
    constexpr bool operator==(const request&) const = default;
};

struct choice {
    unsigned slots = 16;
    unsigned unit_bytes = 256;
    fingerprint fp = fingerprint::byte_tiles;
    suffix suffix_encoding = suffix::bytes;
    body fields = body::planes;
    tags tag = tags::none;
    unsigned selector_lanes = 8;
    unsigned fingerprint_alignment = 1;
    unsigned value_alignment = 1;
    constexpr bool operator==(const choice&) const = default;
};

// Offsets/strides are in bits: byte and bit encodings use the same description.
// width_bits is owned storage per entry, logical_bits excludes field padding.
// Zero-width fields have no storage and never require a load or store.
struct field {
    std::uint64_t offset_bits = 0, stride_bits = 0, width_bits = 0, logical_bits = 0;
    unsigned entries = 0;
    constexpr bool present() const { return width_bits != 0 && entries != 0; }
    constexpr std::uint64_t end_bits() const {
        return offset_bits + (present() ? (entries - 1) * stride_bits + width_bits : 0);
    }
    constexpr bool byte_addressable() const {
        return (offset_bits | stride_bits | width_bits) % 8 == 0;
    }
    constexpr bool operator==(const field&) const = default;
};
struct window {
    std::uint64_t offset = 0, bytes = 0;
    constexpr bool operator==(const window&) const = default;
};
struct line_range {
    unsigned minimum = 0, maximum = 0;
    std::uint64_t sum = 0;
    unsigned phases = 0;
    // sum/phases is a geometric phase average, not a timing/cache-miss estimate.
};

enum class feature : std::uint64_t {
    selector_tiles = 1ull << 0, selector_exact_tail = 1ull << 1,
    selector_packed = 1ull << 2, selector_32 = 1ull << 3,
    selector_64 = 1ull << 4, no_selector = 1ull << 5,
    suffix_bytes = 1ull << 6, suffix_packed = 1ull << 7,
    planes = 1ull << 8, rows = 1ull << 9,
    word_values = 1ull << 10, wide_values = 1ull << 11,
    byte_tags = 1ull << 12, bit_tags = 1ull << 13,
    row_tags = 1ull << 14, list = 1ull << 15,
    capacity_over_32 = 1ull << 16, custom_header = 1ull << 17,
    field_alignment = 1ull << 18, selector_over_64 = 1ull << 19
};
using features = std::uint64_t;
constexpr features bit(feature f) { return static_cast<features>(f); }
// Precisely the original bucket grammar's features. This is not a claim that
// arbitrary offsets are accepted by its old bindings. Root consumers must bind
// these descriptors (including physical windows) and assert their own support.
inline constexpr features initial_bucket_features =
    bit(feature::selector_tiles) | bit(feature::suffix_bytes) |
    bit(feature::planes) | bit(feature::word_values);

struct candidate {
    request spec;
    choice shape;
    std::array<field, static_cast<unsigned>(field_id::end)> fields{};
    window selector_storage;
    unsigned selector_groups = 0;
    unsigned header_bytes = 0;
    unsigned payload_bytes = 0; // Full inline or reference storage; tag is separate.
    std::uint64_t used_bytes = 0, padding_bytes = 0, logical_bits = 0;
    features requires_features = 0;
    line_range unit_lines;
    constexpr const field& get(field_id id) const {
        return fields[static_cast<unsigned>(id)];
    }
    constexpr features missing(features supported) const {
        return requires_features & ~supported;
    }
};

namespace detail {
constexpr bool pow2(unsigned n) { return n && (n & (n - 1)) == 0; }
constexpr std::uint64_t round(std::uint64_t n, unsigned a) {
    return (n + a - 1) / a * a;
}
constexpr unsigned native_lanes(platform p) {
    switch (p) {
    case platform::neon: return 16;
    case platform::avx2_amd: case platform::avx2_intel: return 32;
    case platform::avx512_amd: case platform::avx512_intel: return 64;
    }
    return 0;
}
constexpr bool header_width(unsigned n) { return n == 0 || n == 1 || n == 2 || n == 4 || n == 8; }
constexpr std::expected<void, error> valid_request(const request& r) {
    if (r.fingerprint_bits > 8 || r.suffix_bits > 64) return std::unexpected(error::invalid_width);
    if (!header_width(r.count_bytes) || !header_width(r.descendant_bytes) ||
        !header_width(r.head_bytes)) return std::unexpected(error::invalid_header);
    if ((r.where == tier::list && (r.descendant_bytes || r.head_bytes)) ||
        (r.where == tier::primary && r.head_bytes) ||
        (r.where != tier::list && (!r.count_bytes || !r.descendant_bytes)) ||
        (r.where == tier::secondary && !r.head_bytes)) return std::unexpected(error::invalid_header);
    if ((r.kind == container::set && r.value_bytes) ||
        (r.kind == container::multimap && !r.chain_reference_bytes)) return std::unexpected(error::invalid_kind);
    if (r.tag_bits!=1 && r.tag_bits!=2 && r.tag_bits!=4 && r.tag_bits!=8)
        return std::unexpected(error::invalid_tags);
    if (!pow2(r.allocation_alignment) || !pow2(r.cacheline_bytes) ||
        r.cacheline_bytes > 256) return std::unexpected(error::invalid_alignment);
    return {};
}
} // namespace detail

// Union of touched lines for up to 16 bounded physical windows. Bucket units
// repeat in an aligned allocation; a list is one independently aligned unit.
// All reachable start phases for the relevant ownership are included.
// Overlapping loads do not count the same cache line twice. Empty windows are
// valid and do not touch memory. This says nothing about spatial prefetch.
constexpr std::expected<line_range, error> line_touches(
        const candidate& c, std::span<const window> windows) {
    if (windows.size() > 16) return std::unexpected(error::too_many_windows);
    const unsigned line = c.spec.cacheline_bytes;
    const unsigned phase_step = c.spec.where == tier::list ?
        std::gcd(c.spec.allocation_alignment, line) :
        std::gcd(std::gcd(c.spec.allocation_alignment, c.shape.unit_bytes), line);
    line_range result{~0u, 0, 0, 0};
    for (unsigned phase = 0; phase < line; phase += phase_step) {
        std::array<std::pair<std::uint64_t, std::uint64_t>, 16> intervals{};
        unsigned size = 0;
        for (auto w : windows) {
            if (w.offset > c.shape.unit_bytes || w.bytes > c.shape.unit_bytes - w.offset)
                return std::unexpected(error::invalid_window);
            if (w.bytes) intervals[size++] = {(phase + w.offset) / line,
                                             (phase + w.offset + w.bytes - 1) / line};
        }
        std::sort(intervals.begin(), intervals.begin() + size);
        std::uint64_t count = 0, end = 0;
        for (unsigned i = 0; i < size; ++i) {
            auto [lo, hi] = intervals[i];
            if (i == 0 || lo > end) count += hi - lo + 1;
            else if (hi > end) count += hi - end;
            end = std::max(end, hi);
        }
        result.minimum = std::min(result.minimum, static_cast<unsigned>(count));
        result.maximum = std::max(result.maximum, static_cast<unsigned>(count));
        result.sum += count;
        ++result.phases;
    }
    return result;
}

constexpr std::expected<candidate, error> make(request r, choice s) {
    if (auto valid = detail::valid_request(r); !valid) return std::unexpected(valid.error());
    if (s.slots == 0 || s.slots > 256 || s.unit_bytes == 0)
        return std::unexpected(error::invalid_capacity);
    if (r.count_bytes == 1 && s.slots == 256) return std::unexpected(error::invalid_header);
    if (r.occupancy && *r.occupancy > s.slots) return std::unexpected(error::occupancy_exceeds_capacity);
    const unsigned selector_width = s.fp == fingerprint::packed_bits ? r.fingerprint_bits : 8;
    if (!detail::pow2(s.selector_lanes) || s.selector_lanes < 8 || s.selector_lanes > 256 ||
        (r.fingerprint_bits && s.selector_lanes * selector_width > detail::native_lanes(r.target) * 8))
        return std::unexpected(error::invalid_selector);
    for (auto alignment : {s.fingerprint_alignment, s.value_alignment}) {
        if (!detail::pow2(alignment) || r.allocation_alignment < alignment ||
            (r.where != tier::list && s.unit_bytes % alignment))
            return std::unexpected(error::invalid_alignment);
    }
    if ((r.kind != container::multimap && s.tag != tags::none) ||
        (r.kind == container::multimap && s.tag == tags::none)) return std::unexpected(error::invalid_tags);

    candidate c{};
    c.spec = r; c.shape = s;
    c.header_bytes = r.count_bytes + r.descendant_bytes + r.head_bytes;
    c.payload_bytes = r.kind == container::multimap ?
        std::max(r.value_bytes, r.chain_reference_bytes) : r.value_bytes;
    auto put = [&](field_id id, std::uint64_t at, std::uint64_t stride,
                   std::uint64_t width, std::uint64_t logical, unsigned entries) {
        c.fields[static_cast<unsigned>(id)] = {at, stride, width, logical, entries};
    };
    unsigned header = 0;
    for (auto [id, bytes] : {std::pair{field_id::count, r.count_bytes},
                            std::pair{field_id::descendants, r.descendant_bytes},
                            std::pair{field_id::head, r.head_bytes}}) {
        put(id, header * 8, 0, bytes * 8, bytes * 8, 1); header += bytes;
    }
    std::uint64_t cursor = detail::round(header, s.fingerprint_alignment);
    const unsigned fp_width = !r.fingerprint_bits ? 0 :
        s.fp == fingerprint::packed_bits ? r.fingerprint_bits : 8;
    const unsigned fp_slots = fp_width && s.fp == fingerprint::byte_tiles ?
        static_cast<unsigned>(detail::round(s.slots, s.selector_lanes)) : s.slots;
    put(field_id::fingerprints, cursor * 8, fp_width, fp_width, r.fingerprint_bits, s.slots);
    c.selector_storage = {cursor, (std::uint64_t(fp_slots) * fp_width + 7) / 8};
    c.selector_groups = fp_width ? (s.slots + s.selector_lanes - 1) / s.selector_lanes : 0;
    cursor += c.selector_storage.bytes;
    const unsigned suffix_width = s.suffix_encoding == suffix::bytes ?
        ((r.suffix_bits + 7) / 8) * 8 : r.suffix_bits;
    const std::uint64_t payload_width = std::uint64_t(c.payload_bytes) * 8;
    if (s.fields == body::planes) {
        put(field_id::suffixes, cursor * 8, suffix_width, suffix_width, r.suffix_bits, s.slots);
        cursor += (std::uint64_t(s.slots) * suffix_width + 7) / 8;
        cursor = detail::round(cursor, s.value_alignment);
        const std::uint64_t value_stride = detail::round(
            std::uint64_t(c.payload_bytes) + (s.tag == tags::row_byte), s.value_alignment);
        put(field_id::values, cursor * 8, value_stride * 8, payload_width, payload_width, s.slots);
        if (s.tag == tags::row_byte)
            put(field_id::tag, (cursor + c.payload_bytes) * 8, value_stride * 8, 8, r.tag_bits, s.slots);
        cursor += s.slots * value_stride;
    } else {
        cursor = detail::round(cursor, s.value_alignment);
        const std::uint64_t value_at = detail::round((suffix_width + 7) / 8, s.value_alignment);
        const std::uint64_t row_bytes = detail::round(value_at + c.payload_bytes +
            (s.tag == tags::row_byte), s.value_alignment);
        put(field_id::suffixes, cursor * 8, row_bytes * 8, suffix_width, r.suffix_bits, s.slots);
        put(field_id::values, (cursor + value_at) * 8, row_bytes * 8, payload_width, payload_width, s.slots);
        if (s.tag == tags::row_byte)
            put(field_id::tag, (cursor + value_at + c.payload_bytes) * 8, row_bytes * 8, 8, r.tag_bits, s.slots);
        cursor += s.slots * row_bytes;
    }
    if (s.tag == tags::byte_plane || s.tag == tags::bit_plane) {
        const unsigned width = s.tag == tags::byte_plane ? 8 : r.tag_bits;
        put(field_id::tag, cursor * 8, width, width, r.tag_bits, s.slots);
        cursor += (std::uint64_t(s.slots) * width + 7) / 8;
    }
    if (cursor > s.unit_bytes) return std::unexpected(error::insufficient_unit);
    c.used_bytes = cursor;
    c.padding_bytes = s.unit_bytes - cursor;
    c.logical_bits = std::uint64_t(c.header_bytes) * 8 + std::uint64_t(s.slots) *
        (r.fingerprint_bits + r.suffix_bits + payload_width + (r.kind == container::multimap?r.tag_bits:0));
    auto need = [&](feature f) { c.requires_features |= bit(f); };
    if (!fp_width) need(feature::no_selector);
    else {
        need(s.fp == fingerprint::byte_tiles ? feature::selector_tiles :
             s.fp == fingerprint::exact_bytes ? feature::selector_exact_tail : feature::selector_packed);
        if (s.selector_lanes == 32) need(feature::selector_32);
        if (s.selector_lanes == 64) need(feature::selector_64);
        if (s.selector_lanes > 64) need(feature::selector_over_64);
    }
    need(s.suffix_encoding == suffix::bytes ? feature::suffix_bytes : feature::suffix_packed);
    need(s.fields == body::planes ? feature::planes : feature::rows);
    need(c.payload_bytes <= 8 ? feature::word_values : feature::wide_values);
    if (s.tag == tags::byte_plane) need(feature::byte_tags);
    if (s.tag == tags::bit_plane) need(feature::bit_tags);
    if (s.tag == tags::row_byte) need(feature::row_tags);
    if (r.where == tier::list) need(feature::list);
    if (s.slots > 32) need(feature::capacity_over_32);
    if (r.count_bytes != 1 || r.descendant_bytes != 4 ||
        r.head_bytes != (r.where == tier::secondary ? 4u : 0u)) need(feature::custom_header);
    if (s.fingerprint_alignment > 1 || s.value_alignment > 1) need(feature::field_alignment);
    const std::array unit{window{0, s.unit_bytes}};
    c.unit_lines = *line_touches(c, unit);
    return c;
}

// Smallest byte window that contains one field. For packed stores it is an RMW
// ownership requirement; callers must preserve neighbouring bits. Padding lanes
// are not active slots: every match mask must be intersected with occupancy.
constexpr std::expected<window, error> field_window(const candidate& c, field_id id, unsigned slot = 0) {
    if (id == field_id::end) return std::unexpected(error::invalid_slot);
    const auto& f = c.get(id);
    if (slot >= f.entries) return std::unexpected(error::invalid_slot);
    const auto bit_at = f.offset_bits + slot * f.stride_bits;
    return window{bit_at / 8, f.width_bits ? (bit_at % 8 + f.width_bits + 7) / 8 : 0};
}
constexpr std::expected<window, error> selector_window(const candidate& c, unsigned group) {
    if (group >= c.selector_groups) return std::unexpected(error::invalid_slot);
    const auto& f = c.get(field_id::fingerprints);
    const unsigned first = group * c.shape.selector_lanes;
    const unsigned lanes = c.shape.fp == fingerprint::byte_tiles ? c.shape.selector_lanes :
        std::min(c.shape.selector_lanes, c.shape.slots - first);
    const auto at = f.offset_bits + first * f.stride_bits;
    return window{at / 8, (at % 8 + lanes * f.width_bits + 7) / 8};
}

// The frontier covers the caller's finite capacity/unit/alignment search space,
// including explicitly unsupported grammar features. make() also accepts exact
// unit sizes absent from this convenience list. Ordering is enumeration order,
// never a fastest-layout ranking.
struct limits {
    std::vector<unsigned> capacities{1, 2, 4, 8, 12, 14, 16, 24, 32, 64, 128, 256};
    std::vector<unsigned> units{1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 128, 192,
                                256, 384, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
    std::vector<unsigned> alignments{1, 8, 16};
    std::vector<unsigned> fingerprint_alignments{1, 16};
    std::vector<unsigned> selector_groups{8, 16, 32, 64, 128, 256};
    std::size_t max_candidates = 250000;
};
inline std::expected<std::vector<candidate>, error> enumerate(request r, const limits& l = {}) {
    if (auto valid = detail::valid_request(r); !valid) return std::unexpected(valid.error());
    std::vector<candidate> out;
    for (auto n : l.capacities) for (auto unit : l.units)
    for (auto fp : {fingerprint::byte_tiles, fingerprint::exact_bytes, fingerprint::packed_bits})
    for (auto sf : {suffix::bytes, suffix::packed_bits})
    for (auto arrangement : {body::planes, body::rows})
    for (auto tag : {tags::none, tags::byte_plane, tags::bit_plane, tags::row_byte})
    for (auto group : l.selector_groups) for (auto alignment : l.alignments)
    for (auto fp_alignment : l.fingerprint_alignments) {
        if ((r.kind == container::multimap) == (tag == tags::none)) continue;
        auto c = make(r, choice{n, unit, fp, sf, arrangement, tag, group, fp_alignment, alignment});
        if (!c) continue; // Capacity, alignment and unit fit are candidate constraints.
        if (out.size() == l.max_candidates) return std::unexpected(error::candidate_limit);
        out.push_back(*c);
    }
    return out;
}

} // namespace ht::layout_solver
