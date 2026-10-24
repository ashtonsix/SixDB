#pragma once

#include "binding.h"
#include "manifest.h"

namespace frame3_owner {
struct borrowed_part {
    std::uint32_t owner_part{};
    std::span<const std::byte> bytes;
};

// Immutable binding over explicitly available sections of one material. The
// descriptor, manifest and available-key set are copied. Payload is borrowed:
// only explicitly available sections require initialized, readable, isolated
// bytes; a whole Part span is a placement bound, not a grant over every byte.
// Admitted section addresses and backing stay fixed through
// every call; the owner retains its Scope/Parts. borrowed_part wrappers and the
// caller's descriptor/manifest containers need not survive attach.
//
// Metadata and available-key/Part bounds are checked before resolving any
// section address. Available intervals root groups whose checkpoint dependency
// is missing may attach structurally, but cannot answer queries. With complete
// dependencies, no-shortcut decode proves last/monotone/exact evidence once.
// The resulting per-root-group closure/content proof is cached: queries admit
// the complete request before entering a stage or writing output. A false
// monotone/exact flag may conservatively disable a law; a false last value or
// an unsound true flag refuses attachment. There is no mutation/point-plan API.
class stream_binding {
    std::unique_ptr<frame3::stage> root_;
    description description_;
    std::vector<stream_manifest> streams_;
    std::vector<section_key> available_;
    std::size_t rows_{};
    std::uint16_t ready_groups_{};

    stream_binding() = default;
    struct fine_request { frame3::bitset256 active; std::size_t count{}; };
    std::expected<void, binding_error> valid_blocks(const frame3::bitset256 &) const;
    std::expected<fine_request, binding_error> selected_rows(const frame3::bitset256 &,
        const frame3::block_rows_set &) const;
    std::expected<void, binding_error> admit_range(std::size_t, std::size_t) const;
    std::expected<void, binding_error> admit_blocks(const frame3::bitset256 &) const;

  public:
    static std::expected<std::unique_ptr<stream_binding>, binding_error> attach(const description &,
        std::size_t root_rows, std::span<const std::size_t> part_extents,
        std::span<const stream_manifest>, std::span<const borrowed_part>, std::span<const section_key> available);
    std::size_t size() const noexcept { return rows_; }
    std::span<const section_key> available_sections() const noexcept { return available_; }
    // Conservative point/read closure for an enclosing acquisition driver;
    // this is not minimal for every operation (COUNT/FILTER may settle from
    // the logical UInt64 domain). Keys grant neither bytes nor plan lifetime.
    std::expected<std::vector<section_key>, binding_error> required(std::span<const row_range>) const;
    std::expected<frame3::u64, binding_error> get(unsigned row) const;
    std::expected<void, binding_error> read(unsigned first, std::span<frame3::u64>) const;
    std::expected<frame3::u128, binding_error> sum(const frame3::bitset256 &) const;
    std::expected<frame3::u64, binding_error> count(ikea::comparison, const frame3::bitset256 &, frame3::u64 threshold) const;
    // COUNT/FILTER can settle >=0, <=MAX, <0 and >MAX using the non-null UInt64
    // logical contract, even when every payload section is unavailable.
    // FILTER writes only selected coarse slots and clears their tail high bits.
    std::expected<void, binding_error> filter(ikea::comparison, const frame3::bitset256 &,
        frame3::u64 threshold, frame3::block_rows_set &output) const;
    std::expected<frame3::u128, binding_error> sum_selected(const frame3::bitset256 &,
        const frame3::block_rows_set &) const;
    // Gather admitted fine rows in original row order. Output beyond the
    // returned count is untouched; shortage refuses with resources. As with
    // read/filter, the caller proves output disjoint from source, this binding
    // and input masks. All domain/availability/capacity refusal precedes writes.
    std::expected<std::size_t, binding_error> select(const frame3::bitset256 &,
        const frame3::block_rows_set &, std::span<frame3::u64> output) const;
};
} // namespace frame3_owner
