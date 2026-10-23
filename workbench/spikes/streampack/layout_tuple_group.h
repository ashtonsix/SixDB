#include <ikea/tuplepack/author/execution.h>
#pragma once
#include "layout.h"

namespace streampack_layout {
// One TuplePack unit per 16 logical slots. Low bytes are contiguous; packed
// high bits follow. For k=12 its prefix projection is already 16 native u16s.
// This is an internal research adapter, not another public geometry family.
template<unsigned Maximum, unsigned Spacing = 0> class TupleGroup {
public:
  static constexpr bool widths = true, valid = true;
  static constexpr unsigned capacity = 256, group = 16, bits = std::bit_width(Maximum);
  static constexpr unsigned plane_bytes = 2 * bits, group_bytes = 4 + 2 * plane_bytes + 2;
  static constexpr unsigned stride = Spacing ? Spacing : group_bytes;
  using W = std::conditional_t<bits == 8, std::uint8_t, std::uint16_t>;
private:
  struct Field {
    std::array<std::uint8_t, 3> codes{};
    std::array<unsigned, 3> shifts{}, masks{};
    unsigned size{};
  };
  unsigned rows_;
  std::uint8_t* data_{};
  std::vector<tp::code> codes_;
  std::array<Field, 16> geometry_fields_, used_fields_;
  std::array<unsigned, 16> prefix_offsets_{};
  std::array<std::optional<tp::read_plan<8>>, 16> geometry_, state_, used_, dead_;
  std::array<std::optional<tp::write_plan<8>>, 16> writes_;
  std::optional<tp::layout> layout_;
  std::optional<tp::read_plan<64>> prefix_, used_group_;
  std::optional<tp::view> view_;
  std::optional<tp::author::native_reader<64, std::byte>> native_, native_used_;

  Field field(unsigned origin, unsigned rank) {
    Field out;
    auto add = [&](unsigned byte, unsigned shift, unsigned width, unsigned result_shift) {
      out.codes[out.size] = codes_.size(); out.shifts[out.size] = result_shift;
      out.masks[out.size++] = (1u << width) - 1;
      codes_.push_back({std::uint8_t(byte), std::uint8_t(shift), std::uint8_t(width)});
    };
    add(origin + rank, 0, 8, 0);
    if constexpr (bits > 8) {
      unsigned done = 0;
      while (done < bits - 8) {
        const auto bit = rank * (bits - 8) + done, take = std::min(bits - 8 - done, 8 - bit % 8);
        add(origin + 16 + bit / 8, bit % 8, take, 8 + done); done += take;
      }
    }
    return out;
  }
  static unsigned unpack(std::uint64_t packet, const Field& f) {
    if constexpr (bits == 8) return std::uint8_t(packet);
    if constexpr (bits == 12) return unsigned(packet & 0xffff);
    unsigned out = 0;
    for (unsigned i = 0; i < f.size; ++i) out |= unsigned(std::uint8_t(packet >> (8 * i))) << f.shifts[i];
    return out;
  }
  static std::uint64_t pack(unsigned value, const Field& f) {
    if constexpr (bits == 8 || bits == 12) return value;
    std::uint64_t out = 0;
    for (unsigned i = 0; i < f.size; ++i) out |= std::uint64_t((value >> f.shifts[i]) & f.masks[i]) << (8 * i);
    return out;
  }
  static unsigned code_count(const Field& f) {
    if constexpr (bits == 8 || bits == 12) return sizeof(W);
    else return f.size;
  }
  std::uint8_t* unit(unsigned row) const { return data_ + row / group * stride; }
  void encode_field(std::uint8_t* p, const Field& f, unsigned value) {
    // Construction oracle uses the declared physical bits, independently from
    // TuplePack's prepared writer controls used by mutation.
    for (unsigned i = 0; i < f.size; ++i) {
      const auto code = codes_[f.codes[i]];
      const auto mask = unsigned(f.masks[i] << code.shift);
      p[code.offset] = (p[code.offset] & ~mask) | (((value >> f.shifts[i]) & f.masks[i]) << code.shift);
    }
  }
public:
  explicit TupleGroup(unsigned rows) : rows_(rows) {
    static_assert((bits == 8 || bits == 12 || bits == 13) && group_bytes <= 64 && stride >= group_bytes);
    require(rows % capacity == 0, "whole capacity fixture");
    for (unsigned i = 0; i < 4; ++i) codes_.push_back({std::uint8_t(i), 0, 8});
    for (unsigned r = 0; r < group; ++r) geometry_fields_[r] = field(4, r);
    for (unsigned r = 0; r < group; ++r) used_fields_[r] = field(4 + plane_bytes, r);
    const auto dead_first = codes_.size();
    for (unsigned r = 0; r < group; ++r) codes_.push_back({std::uint8_t(4 + 2 * plane_bytes + r / 8), std::uint8_t(r % 8), 1});
    auto layout = tp::layout::make(group_bytes, codes_); require(bool(layout), "packed group description"); layout_ = *layout;
    std::vector<std::uint8_t> prefix_map;
    for (unsigned r = 0; r < group; ++r) {
      const auto& g = geometry_fields_[r]; const auto& u = used_fields_[r];
      const auto gm = std::span(g.codes).first(g.size);
      prefix_offsets_[r] = prefix_map.size(); prefix_map.insert(prefix_map.end(), gm.begin(), gm.end());
      geometry_[r] = *tp::read_plan<8>::make(*layout_, gm);
      std::array<std::uint8_t, 4> map{}; std::copy_n(u.codes.begin(), u.size, map.begin()); map[u.size] = dead_first + r;
      state_[r] = *tp::read_plan<8>::make(*layout_, std::span(map).first(u.size + 1));
      used_[r] = *tp::read_plan<8>::make(*layout_, std::span(map).first(u.size));
      writes_[r] = *tp::write_plan<8>::make(*layout_, std::span(map).first(u.size + 1));
      dead_[r] = *tp::read_plan<8>::make(*layout_, std::span(map).subspan(u.size, 1));
    }
    auto prefix = tp::read_plan<64>::make(*layout_, prefix_map); require(bool(prefix), "packed group prefix map"); prefix_ = *prefix;
    std::vector<std::uint8_t> used_map;
    for (const auto& f : used_fields_) used_map.insert(used_map.end(), f.codes.begin(), f.codes.begin() + f.size);
    used_group_ = *tp::read_plan<64>::make(*layout_, used_map);
  }
  TupleGroup(const TupleGroup&) = delete;
  TupleGroup& operator=(const TupleGroup&) = delete;
  TupleGroup(TupleGroup&&) = delete;
  TupleGroup& operator=(TupleGroup&&) = delete;
  std::size_t bytes() const { return std::size_t(rows_) / group * stride; }
  std::size_t owned_bytes() const { return std::size_t(rows_) / group * group_bytes; }
  bool gaps_unchanged() const {
    for (unsigned row = 0; row < rows_; row += group)
      for (unsigned i = group_bytes; i < stride; ++i) if (unit(row)[i] != 0xa5) return false;
    return true;
  }
  void attach(std::uint8_t* data) {
    data_ = data;
    auto view = tp::view::attach(*layout_, std::as_writable_bytes(std::span{data_, bytes()}), rows_ / group, stride);
    require(bool(view), "packed group actual placement"); view_ = *view;
    auto read = tp::bind_reader(*prefix_, *view_); require(bool(read), "packed group native binding"); native_.emplace(*read);
    native_used_.emplace(*tp::bind_reader(*used_group_, *view_));
  }
  unsigned geometry(unsigned row) const {
    const auto r = row % group;
    return unpack(tp::native::read_body(geometry_[r]->controls(), unit(row)), geometry_fields_[r]);
  }
  std::pair<unsigned, bool> state(unsigned row) const {
    const auto r = row % group; const auto p = tp::native::read_body(state_[r]->controls(), unit(row));
    return {unpack(p, used_fields_[r]), bool((p >> (8 * code_count(used_fields_[r]))) & 1)};
  }
  bool dead(unsigned row) const { return bool(tp::native::read_body(dead_[row % group]->controls(), unit(row)) & 1); }
  unsigned used_length(unsigned row) const {
    return unpack(tp::native::read_body(used_[row % group]->controls(), unit(row)), used_fields_[row % group]);
  }
  unsigned base(unsigned g) const { return load<std::uint32_t>(data_ + g * stride); }
  [[gnu::always_inline]] void decode_widths(unsigned first, W* decoded) const {
    alignas(64) std::array<std::uint8_t, 64> packet;
    tp::native::store_packet(packet.data(), native_->get_unchecked(first / group));
    if constexpr (bits == 8 || bits == 12)
      std::memcpy(decoded, packet.data(), 16 * sizeof(W));
    else {
      for (unsigned r = 0; r < group; ++r) {
        decoded[r] = 0;
        const auto& f = geometry_fields_[r];
        for (unsigned j = 0; j < f.size; ++j) decoded[r] |= unsigned(packet[prefix_offsets_[r] + j]) << f.shifts[j];
      }
    }
  }
  unsigned start(unsigned row) const {
    alignas(64) W decoded[16]; decode_widths(row, decoded);
    return base(row / group) + streampack_next::prefix16_native(decoded, row % group);
  }
  [[gnu::always_inline]] void decode_used(unsigned first, W* decoded) const {
    alignas(64) std::array<std::uint8_t, 64> packet;
    tp::native::store_packet(packet.data(), native_used_->get_unchecked(first / group));
    if constexpr (bits == 8 || bits == 12) std::memcpy(decoded, packet.data(), 16 * sizeof(W));
    else for (unsigned r = 0; r < group; ++r) {
      decoded[r] = 0; const auto& f = used_fields_[r];
      for (unsigned j = 0; j < f.size; ++j) decoded[r] |= unsigned(packet[prefix_offsets_[r] + j]) << f.shifts[j];
    }
  }
  [[gnu::always_inline]] void decode_bases(unsigned block, std::uint32_t* decoded) const {
    for (unsigned i = 0; i < 16; ++i) decoded[i] = base(block * 16 + i);
  }
  unsigned count_dead(unsigned block) const {
    unsigned n = 0; for (unsigned i = 0; i < 16; ++i)
      n += std::popcount(load<std::uint16_t>(unit(block * 256 + i * 16) + 4 + 2 * plane_bytes));
    return n;
  }
  std::uint16_t dead_mask16(unsigned first) const { return load<std::uint16_t>(unit(first) + 4 + 2 * plane_bytes); }
  unsigned reserved(unsigned row) const { return geometry(row); }
  Entry get(unsigned row) const { const auto [n, d] = state(row); return {start(row), geometry(row), n, d}; }
  void encode(unsigned row, unsigned begin, unsigned reservation, unsigned used, bool dead) {
    auto* p = unit(row); const auto r = row % group;
    if (!r) store<std::uint32_t>(p, begin);
    encode_field(p, geometry_fields_[r], reservation); encode_field(p, used_fields_[r], used);
    auto& d = p[4 + 2 * plane_bytes + r / 8]; const auto mask = std::uint8_t(1u << (r % 8)); d = (d & ~mask) | (dead ? mask : 0);
  }
  template<class Journal> void update(unsigned row, unsigned used, bool dead, Journal& effects) {
    const auto r = row % group; auto* p = unit(row); const auto& f = used_fields_[r];
    const auto packet = pack(used, f) | (std::uint64_t(dead) << (code_count(f) * 8));
    const auto mask = writes_[r]->write_bytes();
    for (unsigned i = 0; i < group_bytes;) {
      if (!(mask & (1ull << i))) { ++i; continue; }
      const auto first = i; while (i < group_bytes && (mask & (1ull << i))) ++i;
      effects.before(*this, 0, std::size_t(p - data_) + first, i - first);
    }
    tp::native::write_body(writes_[r]->controls(), p, packet);
  }
  std::optional<std::pair<unsigned, bool>> reverse(unsigned block, unsigned count, unsigned extent, unsigned offset) const {
    if (offset >= extent || !count) return std::nullopt;
    const auto first = block * capacity;
    unsigned lo = 0, hi = (count + group - 1) / group;
    while (lo < hi) { const auto mid = (lo + hi) / 2; if (base(first / group + mid) <= offset) lo = mid + 1; else hi = mid; }
    const auto g = lo - 1; auto end = base(first / group + g);
    alignas(64) W decoded[16]; decode_widths(first + g * group, decoded);
    if constexpr (sizeof(W) == 1) {
      const auto local = streampack_next::owner16_native(decoded, offset - end);
      const auto rank = g * group + local;
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
