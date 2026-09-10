#pragma once
#include "analyse.h"
#include "metadata_format.h"

namespace ikea::heterogeneous {
enum class ByteScope { readable_extent, aligned_allocation };

// This spike owns a separate 64-aligned directory and body allocation. The
// latter admits one 64-readable-byte suffix. Object/control-block overhead is
// outside both scopes; enclosing storage can have a different accounting rule.
// body_bytes is an actual or predicted value in [0,12032], never capacity proof.
constexpr unsigned bec_storage_bytes(unsigned body_bytes, MetadataKind kind,
                                     ByteScope scope = ByteScope::aligned_allocation) {
  const unsigned readable = body_bytes + 64;
  const unsigned body = scope == ByteScope::aligned_allocation
      ? (readable + 63) & ~63u : readable;
  return unsigned(metadata_bytes(kind, 256)) + body;
}
constexpr bool prefer_bec(unsigned candidate_bytes, unsigned minimum_saving = 0) {
  return candidate_bytes < analyse_plain_bytes &&
      minimum_saving <= analyse_plain_bytes - candidate_bytes;
}

// A heuristic byte-worth operator: native extraction/model evaluation is the
// expensive step, followed by this layout-specific storage obligation. A caller
// requiring actual savings must confirm using the actual encoded byte count.
inline unsigned predict_bec_storage_bytes(const std::uint8_t *plain,
    AnalyseModel model, AnalyseScan scan, MetadataKind kind,
    ByteScope scope = ByteScope::aligned_allocation) noexcept {
  return bec_storage_bytes(predict_body_bytes(plain, model, scan), kind, scope);
}
static_assert(bec_storage_bytes(0, MetadataKind::local16) == 576);
static_assert(bec_storage_bytes(7552, MetadataKind::local16) == 8128);
static_assert(bec_storage_bytes(7553, MetadataKind::local16) == 8192);
static_assert(!prefer_bec(bec_storage_bytes(7553, MetadataKind::local16)));
static_assert(prefer_bec(bec_storage_bytes(7553, MetadataKind::local16, ByteScope::readable_extent)));
static_assert(prefer_bec(7680, 512) && !prefer_bec(7680, 513));
} // namespace ikea::heterogeneous
