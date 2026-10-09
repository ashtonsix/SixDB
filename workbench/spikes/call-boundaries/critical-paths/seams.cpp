#include "access.h"
#include <algorithm>
#include <limits>

namespace critical_paths {
const packed::Page* Directory::locate(Key key) const noexcept {
  const auto found = std::lower_bound(source_.directory.begin(), source_.directory.end(), key,
      [](const packed::Entry& entry, Key wanted) { return entry.maximum < wanted; });
  return found == source_.directory.end() ? nullptr : found->page;
}
std::size_t View::size() const noexcept { return page_->live; }
std::size_t View::physical(std::size_t rank) const noexcept { return page_->order[rank]; }
Key View::key(std::size_t row) const noexcept { return packed::read_key(*page_->bindings, row); }
Record View::payload(std::size_t row) const noexcept { return packed::record(*page_->bindings, row); }
bool Scope::open() const noexcept { return open_; }
const Directory& Scope::directory() const noexcept { return directory_; }

std::expected<Scope, Admission> Scope::qualify(packed::Source source, Protection protection) {
  Scope scope(source);
  Key previous{};
  bool first = true;
  for (const auto& entry : source.directory) {
    const auto* page = entry.page;
    if (!page || !page->allocation || !page->bindings || page->live > packed::capacity ||
        page->allocation.size != packed::page_bytes || page->allocation.readable_padding < 64)
      return std::unexpected(Admission::backing);
    const auto& b = *page->bindings;
    const auto* base = reinterpret_cast<const std::uint8_t*>(page->allocation.address);
    const auto transfer = [](const auto& reader, unsigned offset, unsigned bytes) {
      const auto& t = reader.plan().controls().transfer;
      const auto mask = bytes == 8 ? ~std::uint64_t{0} : (std::uint64_t{1} << (8 * bytes)) - 1;
      return t.offset == offset && t.shift == 0 && t.bytes == bytes && t.mask == mask;
    };
    if (b.series.size() != packed::capacity || b.series.stream(0).bytes.data() != base ||
        b.series.stream(0).stride != packed::Format::tile_bytes ||
        b.tuples.size() != packed::capacity || b.tuples.stride() != packed::tuple_stride ||
        b.tuples.row_unchecked(0) != base + packed::tuple_offset ||
        &b.keys.source() != &b.tuples || &b.next.source() != &b.tuples || &b.tags.source() != &b.tuples ||
        !transfer(b.keys, 0, 8) || !transfer(b.next, 8, 8) || !transfer(b.tags, 16, 4) ||
        page->order != reinterpret_cast<const std::uint16_t*>(base + packed::order_offset))
      return std::unexpected(Admission::description);
    std::array<bool, packed::capacity> seen{};
    Key last{};
    for (std::size_t rank = 0; rank < page->live; ++rank) {
      const auto slot = page->order[rank];
      if (slot >= packed::capacity || seen[slot]) return std::unexpected(Admission::order);
      seen[slot] = true;
      const auto key = packed::read_key(b, slot);
      if ((!first && rank == 0 && key <= previous) || (rank && key <= last))
        return std::unexpected(Admission::order);
      last = key;
    }
    if ((!first && entry.maximum <= previous) || (page->live && entry.maximum != last))
      return std::unexpected(Admission::order);
    previous = entry.maximum; first = false;
    if (protection == Protection::retained_scope) {
      auto hold = packed::alloc::hold(page->allocation, {0, packed::page_bytes});
      if (!hold) return std::unexpected(Admission::backing);
      scope.holds_.push_back(std::move(*hold));
    }
  }
  return scope;
}
} // namespace critical_paths
