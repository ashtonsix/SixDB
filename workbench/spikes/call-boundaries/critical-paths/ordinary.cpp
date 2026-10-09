#include "access.h"

namespace critical_paths {
Status ordinary(const Scope& scope, Key key, Outcome& output) noexcept {
  if (!scope.open()) return Status::closed;
  Outcome result{};
  const auto* page = scope.directory().locate(key);
  if (!page) { orbital_bench::consume_absent(result, key); output = result; return Status::absent; }
  const View view(*page);
  std::size_t first = 0, last = view.size();
  while (first < last) {
    const auto mid = first + (last - first) / 2;
    const auto row = view.physical(mid);
    const auto found = view.key(row);
    if (found == key) {
      orbital_bench::consume_row(result, key, view.payload(row));
      output = result; return Status::found;
    }
    if (found < key) first = mid + 1;
    else last = mid;
  }
  orbital_bench::consume_absent(result, key); output = result; return Status::absent;
}
void payload_ordinary(const View& view, std::size_t row, Key key, Outcome& output) noexcept {
  Outcome result{};
  orbital_bench::consume_row(result, key, view.payload(row));
  output = result;
}
} // namespace critical_paths
