#pragma once
#include <cstddef>
#include <cstdint>

namespace ikea::heterogeneous {
enum class MetadataKind { direct32, local16, scan128 };
constexpr unsigned max_blocks = 256;
constexpr unsigned checkpoint_values = 16;
constexpr unsigned metadata_capacity(MetadataKind kind, unsigned count) {
  const unsigned grain = kind == MetadataKind::scan128 ? 128 : 16;
  return (count + grain - 1) / grain * grain;
}
constexpr std::size_t metadata_bytes(MetadataKind kind, unsigned capacity) {
  return capacity * (kind == MetadataKind::direct32 ? 4u : 2u);
}
constexpr std::size_t scan_pop_offset(unsigned capacity) {
  return capacity / 8;
}
constexpr std::size_t scan_length_offset(unsigned capacity) {
  return capacity * 10 / 8;
}
constexpr const char *metadata_name(MetadataKind kind) {
  switch (kind) {
  case MetadataKind::direct32:
    return "direct32";
  case MetadataKind::local16:
    return "local16";
  case MetadataKind::scan128:
    return "scan128";
  }
  return "invalid";
}
// Inline scalar carrier accessors; this spelling does not belong to range
// authors.
constexpr unsigned entry_population(std::uint32_t entry) {
  return entry & 0xffff;
}
constexpr unsigned entry_offset(std::uint32_t entry) { return entry >> 16; }

struct Entry {
  std::uint16_t population;
  std::uint16_t offset;
  std::uint8_t length;
  bool operator==(const Entry &) const = default;
};
} // namespace ikea::heterogeneous
