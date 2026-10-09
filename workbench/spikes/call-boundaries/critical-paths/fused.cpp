#include "access.h"
#include "wire.h"
#include <algorithm>

namespace critical_paths {
static_assert(packed::Format::width == 20 && packed::Format::heads == 0 &&
    packed::Format::storage == packed::sp::geometry::local &&
    packed::Format::tile_rows == 8 && packed::Format::tile_bytes == 20);
static_assert(packed::tuple_stride == 20);

template<Decode D> [[gnu::always_inline]] inline Record decode(const std::uint8_t* base, std::size_t row) noexcept {
  const auto* tuple = base + packed::tuple_offset + row * packed::tuple_stride;
  // Three independent payload loads/address chains precede the unavoidable
  // serial semantic checksum. No wide overread or speculative neighbour load.
  const auto next = load<std::uint64_t>(tuple + 8);
  const auto tag = load<std::uint32_t>(tuple + 16);
  const auto value = local20<D>(base, row);
  return {next, value, tag};
}
template<Decode D> [[gnu::always_inline]] inline void payload(const packed::Page& page,
    std::size_t row, Key key, Outcome& output) noexcept {
  // Use the admitted source, not Allocation.address: a retained Scope remains
  // usable after the actual Allocation handle is released.
  const auto* base = page.bindings->series.stream(0).bytes.data();
  Outcome result{};
  orbital_bench::consume_row(result, key, decode<D>(base, row));
  output = result;
}
template<Decode D> [[gnu::always_inline]] inline Status query(std::span<const packed::Entry> directory,
    bool open, Key key, Outcome& output) noexcept {
  if (!open) return Status::closed;
  const auto entry = std::lower_bound(directory.begin(), directory.end(), key,
      [](const packed::Entry& item, Key wanted) { return item.maximum < wanted; });
  Outcome result{};
  if (entry == directory.end()) {
    orbital_bench::consume_absent(result, key); output = result; return Status::absent;
  }
  const auto& page = *entry->page;
  const auto* base = page.bindings->series.stream(0).bytes.data();
  const auto* tuples = base + packed::tuple_offset;
  std::size_t first = 0, last = page.live;
  while (first < last) {
    const auto mid = first + (last - first) / 2;
    const auto row = page.order[mid];
    const auto found = load<std::uint64_t>(tuples + row * packed::tuple_stride);
    if (found == key) {
      orbital_bench::consume_row(result, key, decode<D>(base, row));
      output = result; return Status::found;
    }
    if (found < key) first = mid + 1;
    else last = mid;
  }
  orbital_bench::consume_absent(result, key); output = result; return Status::absent;
}
Status fused(const Scope& scope, Key key, Outcome& out) noexcept {
  return query<Decode::native>(scope.directory_.source_.directory, scope.open_, key, out);
}
Status fused_multiply(const Scope& scope, Key key, Outcome& out) noexcept {
  return query<Decode::multiply>(scope.directory_.source_.directory, scope.open_, key, out);
}
Status fused_simd(const Scope& scope, Key key, Outcome& out) noexcept {
  return query<Decode::simd>(scope.directory_.source_.directory, scope.open_, key, out);
}
Status fused_fp(const Scope& scope, Key key, Outcome& out) noexcept {
  return query<Decode::fp>(scope.directory_.source_.directory, scope.open_, key, out);
}
void payload_fused(const packed::Page& p, std::size_t row, Key key, Outcome& out) noexcept { payload<Decode::native>(p,row,key,out); }
void payload_multiply(const packed::Page& p, std::size_t row, Key key, Outcome& out) noexcept { payload<Decode::multiply>(p,row,key,out); }
void payload_simd(const packed::Page& p, std::size_t row, Key key, Outcome& out) noexcept { payload<Decode::simd>(p,row,key,out); }
void payload_fp(const packed::Page& p, std::size_t row, Key key, Outcome& out) noexcept { payload<Decode::fp>(p,row,key,out); }
} // namespace critical_paths
