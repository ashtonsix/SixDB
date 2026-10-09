#include "access.h"
#include <algorithm>

namespace critical_paths {
Status visible(const Scope& scope, Key key, Outcome& output) noexcept {
  if (!scope.open_) return Status::closed;
  const auto directory = scope.directory_.source_.directory;
  const auto entry = std::lower_bound(directory.begin(), directory.end(), key,
      [](const packed::Entry& item, Key wanted) { return item.maximum < wanted; });
  Outcome result{};
  if (entry == directory.end()) {
    orbital_bench::consume_absent(result, key); output = result; return Status::absent;
  }
  const auto& page = *entry->page;
  const auto& binding = *page.bindings;
  std::size_t first = 0, last = page.live;
  while (first < last) {
    const auto mid = first + (last - first) / 2;
    const auto row = page.order[mid];
    const auto found = packed::read_key(binding, row);
    if (found == key) {
      orbital_bench::consume_row(result, key, packed::record(binding, row));
      output = result; return Status::found;
    }
    if (found < key) first = mid + 1;
    else last = mid;
  }
  orbital_bench::consume_absent(result, key); output = result; return Status::absent;
}
void payload_visible(const packed::Page& page, std::size_t row, Key key, Outcome& output) noexcept {
  Outcome result{};
  orbital_bench::consume_row(result, key, packed::record(*page.bindings, row));
  output = result;
}
} // namespace critical_paths
