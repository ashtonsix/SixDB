#include "algebra.h"
#include "metadata_point.h"

namespace ikea::heterogeneous {
template<MetadataKind Kind>
__attribute__((noinline)) std::uint32_t
resolve_point(const std::uint8_t *base, unsigned index) noexcept {
  return read_metadata_point<Kind>(base, index);
}
template<MetadataKind Kind>
__attribute__((noinline)) void resolve_frame(const std::uint8_t *base,
                                            unsigned group, EntryLanes16 *out) noexcept {
  *out = read_metadata16<Kind>(base, 256, group);
}
ResolverBinding bind_resolver(MetadataKind kind, Resolution resolution) {
  switch (kind) {
  case MetadataKind::direct32:
    return {resolve_point<MetadataKind::direct32>, resolve_frame<MetadataKind::direct32>, resolution};
  case MetadataKind::local16:
    return {resolve_point<MetadataKind::local16>, resolve_frame<MetadataKind::local16>, resolution};
  case MetadataKind::scan128:
    return {resolve_point<MetadataKind::scan128>, resolve_frame<MetadataKind::scan128>, resolution};
  }
  __builtin_unreachable();
}
} // namespace ikea::heterogeneous
