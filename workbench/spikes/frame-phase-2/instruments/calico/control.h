#pragma once
#include <engine/typebound_farray.h>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace calico_control {
using i128 = __int128;
using u128 = unsigned __int128;
struct range { const void* data; std::size_t bytes, capacity; std::string role; };

// The Type, codec and plans contain borrows. Owners stay at a stable address;
// construct them behind unique_ptr when collecting many independent images.
class signed_image {
    frame::Type type_;
    frame::RawCodec codec_;
    frame::RawStream stream_;
    std::vector<engine::raw_farray_sum_plan> plans_;
public:
    signed_image(frame::Type type, std::span<const std::span<const int64_t>> fields);
    signed_image(const signed_image&) = delete;
    signed_image& operator=(const signed_image&) = delete;
    bool sum(uint32_t field, i128& out) const;
    bool range_mask(uint32_t field, int64_t lo, int64_t hi, std::vector<uint8_t>& out) const;
    std::vector<range> sum_ranges(uint32_t field) const;
    const frame::RawStream& stream() const { return stream_; }
    const frame::RawCodec& codec() const { return codec_; }
    const engine::raw_farray_sum_plan& plan(uint32_t field) const { return plans_.at(field); }
};

// Actual Engine container: unsigned field widths, exact evidence, complete
// RawCodec frames plus the original literal tail. v1 refuses inline escapes.
class unsigned_image {
    frame::Type type_;
    std::vector<uint16_t> ids_;
    std::vector<uint8_t> widths_;
    engine::typebound_leaf_plan plan_;
    engine::typebound_leaf_binding binding_;
    std::vector<uint8_t> meta_, body_;
    engine::typebound_leaf_view view_;
public:
    unsigned_image(frame::Type type, std::span<const uint8_t> widths,
                   std::span<const std::span<const uint64_t>> fields);
    unsigned_image(const unsigned_image&) = delete;
    unsigned_image& operator=(const unsigned_image&) = delete;
    bool sum(uint32_t field, u128& out) const;
    bool range_mask(uint32_t field, uint64_t lo, uint64_t hi, std::vector<uint8_t>& out) const;
    std::vector<range> sum_ranges(uint32_t field) const;
    std::vector<range> retained_ranges() const;
    const engine::typebound_leaf_view& view() const { return view_; }
    const engine::typebound_leaf_binding& binding() const { return binding_; }
    std::span<const uint8_t> metadata() const { return meta_; }
    std::span<const uint8_t> body() const { return body_; }
};
} // namespace calico_control
