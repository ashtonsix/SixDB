#pragma once
#include "packed-page.h"
#include <stdexcept>

namespace orbital_bench::packed {
// Private consumer accounting, resolved while the stable named sources live.
// This is neither a durable journal nor a public Ikea delta abstraction.
struct ConstructionDelta {
  std::uint64_t issued_write_bytes{}, issued_write_spans{};
  std::uint64_t index_write_bytes{}, index_write_spans{};
};
inline ConstructionDelta pack_writes(const Bindings& b, const ikea::source_write_journal& effects) {
  ConstructionDelta delta;
  for (const auto& effect : effects.entries()) {
    const auto limit = effect.source == &b.series ? series_bytes :
        effect.source == &b.tuples ? capacity * tuple_stride : 0;
    if (effect.bytes.plane || effect.bytes.offset > limit || effect.bytes.size > limit - effect.bytes.offset)
      throw std::runtime_error("unresolved packed effect");
    delta.issued_write_bytes += effect.bytes.size;
    ++delta.issued_write_spans;
  }
  return delta;
}
// Caller exclusively owns Page and the explicit stable writable footprint
// through completion. Schema and Page stay at stable addresses. Body has exactly
// page_bytes writable bytes; readable_padding describes an initialized
// contiguous suffix of at least 64 bytes, which construction leaves unchanged. No Allocation, Pool,
// lane, scheduler or live counter access occurs: pending VM backing can be
// constructed before an Allocation is published by its assigned owner.
// check_effects verifies full issued-body coverage without reading old bytes.
// After success, live/sum/bitmap are empty and all capacity fields initialized.
// Return deltas to the assigned owner for accounting when the page is adopted.
// Failure resets private bindings; body bytes can be partly initialized and
// remain unpublished. A noexcept preparation adapter catches the failure and
// settles backing through Orbital.
[[nodiscard]] ConstructionDelta construct_blank_page(Page&, const Schema&,
    std::span<std::byte> body, std::size_t readable_padding, bool check_effects = false);
} // namespace orbital_bench::packed
