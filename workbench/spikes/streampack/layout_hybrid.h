#pragma once
#include "layout.h"

namespace streampack_layout {
// Width geometry: R/U data is grouped or split, while each block's tombstones
// and checkpoints share a dense control table. Checkpoint width is a separate
// control so locality is not confused with narrowing the stored offset.
template<unsigned Maximum, bool Split = false, bool WideCheckpoints = false, unsigned ControlSpacing = 0, bool BlockLocal = false>
class HybridWidth {
public:
  static constexpr bool widths = true, valid = true;
  static constexpr unsigned capacity = 256, group = 16;
  using W = std::conditional_t<(Maximum <= 255), std::uint8_t, std::uint16_t>;
  using C = std::conditional_t<(Maximum <= 255 && !WideCheckpoints), std::uint16_t, std::uint32_t>;
  static constexpr unsigned control_bytes = 32 + 16 * sizeof(C);
  static constexpr unsigned control_stride = ControlSpacing ? ControlSpacing : control_bytes;
  static_assert(control_stride >= control_bytes);
  static constexpr unsigned length_origin = align64(control_stride);
  static constexpr unsigned block_stride = length_origin + capacity * 2 * sizeof(W);
private:
  unsigned rows_;
  std::uint8_t* data_{};
  std::uint8_t* control(unsigned block) const {
    if constexpr (BlockLocal) return data_ + block * block_stride;
    return data_ + std::size_t(rows_) * 2 * sizeof(W) + block * control_stride;
  }
  std::uint8_t* reservation_address(unsigned row) const {
    if constexpr (BlockLocal) {
      auto* first = control(row / capacity) + length_origin; row %= capacity;
      if constexpr (Split) return first + row * sizeof(W);
      return first + (row / group * 2 * group + row % group) * sizeof(W);
    }
    if constexpr (Split) return data_ + row * sizeof(W);
    else return data_ + (row / group * 2 * group + row % group) * sizeof(W);
  }
  std::uint8_t* used_address(unsigned row) const {
    if constexpr (BlockLocal && Split) return control(row / capacity) + length_origin + (capacity + row % capacity) * sizeof(W);
    if constexpr (Split) return data_ + (std::size_t(rows_) + row) * sizeof(W);
    else return reservation_address(row) + group * sizeof(W);
  }
  std::uint8_t* dead_address(unsigned row) const { return control(row / capacity) + row % capacity / 8; }
public:
  explicit HybridWidth(unsigned rows) : rows_(rows) { require(rows % capacity == 0, "whole capacity fixture"); }
  std::size_t bytes() const {
    if constexpr (BlockLocal) return rows_ / capacity * block_stride;
    return std::size_t(rows_) * 2 * sizeof(W) + rows_ / capacity * control_stride;
  }
  std::size_t owned_bytes() const { return std::size_t(rows_) * 2 * sizeof(W) + rows_ / capacity * control_bytes; }
  bool gaps_unchanged() const {
    for (unsigned b = 0; b < rows_ / capacity; ++b)
      for (unsigned i = control_bytes; i < (BlockLocal ? length_origin : control_stride); ++i) if (control(b)[i] != 0xa5) return false;
    return true;
  }
  void attach(std::uint8_t* data) { data_ = data; }
  unsigned geometry(unsigned row) const { return load<W>(reservation_address(row)); }
  unsigned used_length(unsigned row) const { return load<W>(used_address(row)); }
  bool dead(unsigned row) const { return bool((*dead_address(row) >> (row % 8)) & 1); }
  std::pair<unsigned, bool> state(unsigned row) const { return {used_length(row), dead(row)}; }
  unsigned base(unsigned group_index) const {
    return load<C>(control(group_index / 16) + 32 + group_index % 16 * sizeof(C));
  }
  [[gnu::always_inline]] void decode_widths(unsigned first, W* decoded) const { streampack_next::copy16_native(decoded, reinterpret_cast<const W*>(reservation_address(first))); }
  [[gnu::always_inline]] void decode_used(unsigned first, W* decoded) const { streampack_next::copy16_native(decoded, reinterpret_cast<const W*>(used_address(first))); }
  [[gnu::always_inline]] void decode_bases(unsigned block, std::uint32_t* decoded) const {
    for (unsigned i = 0; i < 16; ++i) decoded[i] = load<C>(control(block) + 32 + i * sizeof(C));
  }
  unsigned count_dead(unsigned block) const {
    unsigned n = 0; for (unsigned i = 0; i < 4; ++i) n += std::popcount(load<std::uint64_t>(control(block) + 8 * i)); return n;
  }
  std::uint16_t dead_mask16(unsigned first) const { return load<std::uint16_t>(dead_address(first)); }
  unsigned start(unsigned row) const {
    return base(row / group) + streampack_next::prefix16_native(reinterpret_cast<const W*>(reservation_address(row / group * group)), row % group);
  }
  unsigned reserved(unsigned row) const { return geometry(row); }
  Entry get(unsigned row) const { return {start(row), geometry(row), used_length(row), dead(row)}; }
  void encode(unsigned row, unsigned begin, unsigned reservation, unsigned used, bool dead) {
    store<W>(reservation_address(row), reservation); store<W>(used_address(row), used);
    auto* p = dead_address(row); const auto mask = std::uint8_t(1u << (row % 8)); *p = (*p & ~mask) | (dead ? mask : 0);
    if (row % group == 0) store<C>(control(row / capacity) + 32 + row % capacity / group * sizeof(C), begin);
  }
  template<class Journal> void update(unsigned row, unsigned used, bool dead, Journal& effects) {
    auto* u = used_address(row); auto* d = dead_address(row);
    effects.before(*this, 0, std::size_t(u - data_), sizeof(W)); store<W>(u, used);
    effects.before(*this, 0, std::size_t(d - data_), 1);
    const auto mask = std::uint8_t(1u << (row % 8)); *d = (*d & ~mask) | (dead ? mask : 0);
  }
  std::optional<std::pair<unsigned, bool>> reverse(unsigned block, unsigned count, unsigned extent, unsigned offset) const {
    if (offset >= extent || !count) return std::nullopt;
    const auto first = block * capacity;
    unsigned lo = 0, hi = (count + group - 1) / group;
    while (lo < hi) { const auto mid = (lo + hi) / 2; if (base(first / group + mid) <= offset) lo = mid + 1; else hi = mid; }
    const auto g = lo - 1; auto end = base(first / group + g);
    alignas(64) W decoded[16]; decode_widths(first + g * group, decoded);
    if constexpr (sizeof(W) == 1) {
      const auto rank = g * group + streampack_next::owner16_native(decoded, offset - end);
      if (rank < count) return std::pair{rank, dead(first + rank)};
      return std::nullopt;
    }
    for (unsigned r = g * group; r < std::min(count, (g + 1) * group); ++r) {
      end += decoded[r % group]; if (offset < end) return std::pair{r, dead(first + r)};
    }
    return std::nullopt;
  }
};
} // namespace streampack_layout
