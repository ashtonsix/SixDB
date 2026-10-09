#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orbital_versioned_performance {
class Orbital;
enum class RangeRole { metadata, body };
struct LiveRange { const void* address; std::size_t bytes; RangeRole role; const char* record; };
struct RangeManifest {
  std::vector<LiveRange> ranges;
  std::vector<std::string> uncovered;
};
// Cold author diagnostics only. No normal allocation/read path tracks ranges.
RangeManifest diagnostic_ranges(const Orbital&);
} // namespace orbital_versioned_performance
