#include <ikea/seriespack/author/store.h>
#pragma once
#include "prefix.h"
#include <ikea/seriespack.h>
#include <ikea/seriespack/author/native.h>
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/native.h>
#include <ikea/tuplepack/author/execution.h>
#include <ikea/effects.h>
#include <array>
#include <optional>
#include <vector>
#include <cstdlib>
#include <cstdio>

// Experimental physical adapters. This is not the public StreamPack interface.
// Every adapter uses group STARTS, with authoritative occupied extent supplied
// by the caller. No terminal checkpoint is stored; capacity is never extent.
namespace streampack_layout {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
enum class Placement { planes, groups, series, tuple, block_planes };
inline void require(bool value, const char* why) {
  if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); std::abort(); }
}
template<class T> T load(const std::uint8_t* p) { T v; std::memcpy(&v, p, sizeof(v)); return v; }
template<class T> void store(std::uint8_t* p, T v) { std::memcpy(p, &v, sizeof(v)); }
constexpr std::size_t align64(std::size_t x) { return (x + 63) & ~std::size_t(63); }
struct Entry { unsigned start, reserved, used; bool dead; bool operator==(const Entry&) const = default; };

template<bool Widths, unsigned Maximum, Placement P, unsigned Spacing = 0, bool WideUsed = false>
class Metadata {
public:
  static constexpr bool widths = Widths;
  static constexpr unsigned capacity = 256, group = 16;
  static constexpr unsigned length_bits = std::bit_width(Maximum);
  static constexpr unsigned geometry_bits = Widths ? length_bits : std::bit_width(capacity * Maximum);
  static constexpr unsigned base_bits = std::bit_width((capacity - group) * Maximum);
  using W = std::conditional_t<(Maximum <= 255), std::uint8_t, std::uint16_t>;
  using G = std::conditional_t<(geometry_bits <= 16), std::uint16_t, std::uint32_t>;
  using U = std::conditional_t<WideUsed, std::uint16_t, W>;
  static constexpr unsigned geometry_bytes = Widths ? sizeof(W) : sizeof(G);
  static constexpr unsigned group_bytes = 4 + group * geometry_bytes + group * sizeof(U) + 2;
  static constexpr unsigned stride = Spacing ? Spacing : group_bytes;
  static constexpr unsigned block_fields = capacity * (geometry_bytes + sizeof(U)) + capacity / 8 + (Widths ? capacity / group * 4 : 0);
  static constexpr unsigned block_stride = align64(block_fields);
  static constexpr bool valid = P != Placement::groups || stride >= group_bytes;
  using GF = sp::layout<geometry_bits, sp::geometry::local>;
  using UF = sp::layout<length_bits, sp::geometry::local>;
  using BF = sp::layout<base_bits, sp::geometry::local>;
  static constexpr unsigned tuple_bytes = (geometry_bits + length_bits + 1 + 7) / 8;
private:
  std::uint8_t* data_{};
  std::size_t rows_{}, used_offset_{}, dead_offset_{}, base_offset_{}, bytes_{};
  std::optional<sp::view<GF>> geometry_;
  std::optional<sp::view<UF>> used_;
  std::optional<sp::view<BF>> bases_;
  std::optional<tp::read_plan<8>> tuple_read_, tuple_geometry_, tuple_used_, tuple_length_, tuple_dead_;
  std::optional<tp::write_plan<8>> tuple_write_;
  std::optional<tp::read_plan<64, 16>> tuple_group_, tuple_used_group_, tuple_dead_group_;
  std::optional<tp::view> tuple_view_;
  std::optional<tp::author::native_reader<64, std::byte, 16>> tuple_native_, tuple_used_native_, tuple_dead_native_;
  std::optional<tp::layout> tuple_layout_;
  // Each output byte is one byte-contained code. Reassembly shifts are static
  // for this schema but plans remain ordinary TuplePack preparation products.
  std::array<unsigned, 8> field_shift_{}, field_id_{};
  unsigned fields_{}, geom_fields_{}, used_fields_{};

  static void field(std::vector<tp::code>& codes, unsigned first, unsigned bits) {
    for (unsigned done = 0; done < bits;) {
      const auto bit = first + done, take = std::min(bits - done, 8 - bit % 8);
      codes.push_back({std::uint8_t(bit / 8), std::uint8_t(bit % 8), std::uint8_t(take)}); done += take;
    }
  }
  void prepare_tuple() {
    std::vector<tp::code> codes;
    field(codes, 0, geometry_bits); geom_fields_ = codes.size();
    field(codes, geometry_bits, length_bits); used_fields_ = codes.size() - geom_fields_;
    field(codes, geometry_bits + length_bits, 1); fields_ = codes.size();
    require(fields_ <= 8, "TuplePack point packet fits");
    auto layout = tp::layout::make(tuple_bytes, codes); require(bool(layout), "TuplePack layout");
    std::array<std::uint8_t, 8> all{}, geom{}, used{}, update{};
    unsigned shift = 0, id = 0;
    for (unsigned i = 0; i < fields_; ++i) {
      if (i == geom_fields_) { shift = 0; id = 1; }
      if (i == geom_fields_ + used_fields_) { shift = 0; id = 2; }
      field_shift_[i] = shift; field_id_[i] = id; shift += codes[i].width; all[i] = i;
      if (i < geom_fields_) geom[i] = i;
      else { update[i - geom_fields_] = i; used[i - geom_fields_] = i; }
    }
    tuple_read_ = *tp::read_plan<8>::make(*layout, std::span(all).first(fields_));
    tuple_geometry_ = *tp::read_plan<8>::make(*layout, std::span(geom).first(geom_fields_));
    tuple_used_ = *tp::read_plan<8>::make(*layout, std::span(used).first(used_fields_ + 1));
    tuple_length_ = *tp::read_plan<8>::make(*layout, std::span(used).first(used_fields_));
    tuple_dead_ = *tp::read_plan<8>::make(*layout, std::span(all).subspan(fields_ - 1, 1));
    tuple_write_ = *tp::write_plan<8>::make(*layout, std::span(update).first(used_fields_ + 1));
    tuple_layout_ = *layout;
    // One group per code yields sixteen-byte code planes. The explicit native
    // assembly handles fields beginning mid-byte and 3-code 13-bit lengths.
    const std::array<unsigned, 4> groups{1, 1, 1, 1};
    tuple_group_ = *tp::read_plan<64, 16>::make(*layout, std::span(geom).first(geom_fields_), std::span(groups).first(geom_fields_));
    tuple_used_group_ = *tp::read_plan<64, 16>::make(*layout, std::span(used).first(used_fields_), std::span(groups).first(used_fields_));
    tuple_dead_group_ = *tp::read_plan<64, 16>::make(*layout, std::span(all).subspan(fields_ - 1, 1));
  }
  std::array<unsigned, 3> unpack(std::uint64_t packet, unsigned first, unsigned count) const {
    std::array<unsigned, 3> values{};
    for (unsigned i = 0; i < count; ++i)
      values[field_id_[first + i]] |= unsigned(std::uint8_t(packet >> (8 * i))) << field_shift_[first + i];
    return values;
  }
  std::uint8_t* unit(unsigned row) const { return data_ + (row / group) * stride; }
  std::uint8_t* geometry_address(unsigned row) const {
    if constexpr (P == Placement::block_planes) return data_ + row / capacity * block_stride + row % capacity * geometry_bytes;
    if constexpr (P == Placement::groups) return unit(row) + 4 + row % group * geometry_bytes;
    return data_ + row * geometry_bytes;
  }
  std::uint8_t* used_address(unsigned row) const {
    if constexpr (P == Placement::block_planes) return data_ + row / capacity * block_stride + capacity * geometry_bytes + row % capacity * sizeof(U);
    if constexpr (P == Placement::groups) return unit(row) + 4 + group * geometry_bytes + row % group * sizeof(U);
    return data_ + used_offset_ + row * sizeof(U);
  }
  std::uint8_t* dead_address(unsigned row) const {
    if constexpr (P == Placement::block_planes) return data_ + row / capacity * block_stride + capacity * (geometry_bytes + sizeof(U)) + row % capacity / 8;
    if constexpr (P == Placement::groups) return unit(row) + 4 + group * (geometry_bytes + sizeof(U)) + row % group / 8;
    return data_ + dead_offset_ + row / 8;
  }
  template<class Journal> void before(Journal& effects, std::uint8_t* p, std::size_t n) const {
    if (n) effects.before(*this, 0, std::size_t(p - data_), n);
  }
public:
  explicit Metadata(unsigned rows) : rows_(rows) {
    static_assert(Maximum > 0 && Maximum <= 65535);
    require(rows % capacity == 0, "whole capacity fixture");
    if constexpr (P == Placement::block_planes) bytes_ = rows / capacity * block_stride;
    else if constexpr (P == Placement::groups) bytes_ = rows / group * stride;
    else if constexpr (P == Placement::series) {
      used_offset_ = align64(rows / 8 * GF::tile_bytes);
      dead_offset_ = used_offset_ + align64(rows / 8 * UF::tile_bytes);
      base_offset_ = align64(dead_offset_ + rows / 8);
      bytes_ = base_offset_ + (Widths ? rows / group / 8 * BF::tile_bytes : 0);
    } else if constexpr (P == Placement::tuple) {
      base_offset_ = align64(rows * tuple_bytes); bytes_ = base_offset_ + (Widths ? rows / group * 4 : 0);
      prepare_tuple();
    } else {
      used_offset_ = rows * geometry_bytes; dead_offset_ = used_offset_ + rows * sizeof(U);
      base_offset_ = align64(dead_offset_ + rows / 8); bytes_ = base_offset_ + (Widths ? rows / group * 4 : 0);
    }
  }
  Metadata(const Metadata&) = delete;
  Metadata& operator=(const Metadata&) = delete;
  Metadata(Metadata&&) = delete;
  Metadata& operator=(Metadata&&) = delete;
  std::size_t bytes() const { return bytes_; }
  std::size_t owned_bytes() const {
    if constexpr (P == Placement::block_planes) return rows_ / capacity * block_fields;
    if constexpr (P == Placement::groups) return rows_ / group * group_bytes;
    if constexpr (P == Placement::series) return rows_ / 8 * (GF::tile_bytes + UF::tile_bytes) + rows_ / 8 + (Widths ? rows_ / group / 8 * BF::tile_bytes : 0);
    if constexpr (P == Placement::tuple) return rows_ * tuple_bytes + (Widths ? rows_ / group * 4 : 0);
    return rows_ * (geometry_bytes + sizeof(U)) + rows_ / 8 + (Widths ? rows_ / group * 4 : 0);
  }
  bool gaps_unchanged() const {
    if constexpr (P == Placement::block_planes)
      for (std::size_t b = 0; b < rows_ / capacity; ++b)
        for (unsigned i = block_fields; i < block_stride; ++i) if (data_[b * block_stride + i] != 0xa5) return false;
    if constexpr (P == Placement::groups)
      for (std::size_t g = 0; g < rows_ / group; ++g)
        for (unsigned i = group_bytes; i < stride; ++i) if (data_[g * stride + i] != 0xa5) return false;
    return true;
  }
  void attach(std::uint8_t* data) {
    data_ = data;
    if constexpr (P == Placement::tuple) {
      auto view = tp::view::attach(*tuple_layout_, std::as_writable_bytes(std::span{data_, rows_ * tuple_bytes}), rows_, tuple_bytes);
      require(bool(view), "TuplePack actual strided view"); tuple_view_ = *view;
      auto read = tp::bind_reader(*tuple_group_, *tuple_view_); require(bool(read), "TuplePack native group binding");
      tuple_native_.emplace(*read);
      tuple_used_native_.emplace(*tp::bind_reader(*tuple_used_group_, *tuple_view_));
      tuple_dead_native_.emplace(*tp::bind_reader(*tuple_dead_group_, *tuple_view_));
    }
    if constexpr (P == Placement::series) {
      auto g = sp::view<GF>::attach(rows_, {{{std::as_writable_bytes(std::span{data_, used_offset_}), GF::tile_bytes},{},{}}});
      auto u = sp::view<UF>::attach(rows_, {{{std::as_writable_bytes(std::span{data_ + used_offset_, dead_offset_ - used_offset_}), UF::tile_bytes},{},{}}});
      require(bool(g) && bool(u), "SeriesPack real placement admission"); geometry_ = *g; used_ = *u;
      if constexpr (Widths) {
        auto b = sp::view<BF>::attach(rows_ / group, {{{std::as_writable_bytes(std::span{data_ + base_offset_, bytes_ - base_offset_}), BF::tile_bytes},{},{}}});
        require(bool(b), "SeriesPack checkpoint admission"); bases_ = *b;
      }
    }
  }
  unsigned geometry(unsigned row) const {
    if constexpr (P == Placement::series) return sp::get_unchecked(*geometry_, row);
    else if constexpr (P == Placement::tuple) return unpack(tp::native::read_body(tuple_geometry_->controls(), data_ + row * tuple_bytes), 0, geom_fields_)[0];
    else if constexpr (Widths) return load<W>(geometry_address(row));
    else return load<G>(geometry_address(row));
  }
  std::pair<unsigned, bool> state(unsigned row) const {
    if constexpr (P == Placement::tuple) {
      const auto v = unpack(tp::native::read_body(tuple_used_->controls(), data_ + row * tuple_bytes), geom_fields_, used_fields_ + 1);
      return {v[1], bool(v[2])};
    } else {
      const auto n = [&] { if constexpr (P == Placement::series) return unsigned(sp::get_unchecked(*used_, row)); else return unsigned(load<U>(used_address(row))); }();
      return {n, bool((*dead_address(row) >> (row % 8)) & 1)};
    }
  }
  bool dead(unsigned row) const {
    if constexpr (P == Placement::tuple)
      return bool(tp::native::read_body(tuple_dead_->controls(), data_ + row * tuple_bytes) & 1);
    else return bool((*dead_address(row) >> (row % 8)) & 1);
  }
  unsigned used_length(unsigned row) const {
    if constexpr (P == Placement::tuple)
      return unpack(tp::native::read_body(tuple_length_->controls(), data_ + row * tuple_bytes), geom_fields_, used_fields_)[1];
    else if constexpr (P == Placement::series) return sp::get_unchecked(*used_, row);
    else return load<U>(used_address(row));
  }
  unsigned base(unsigned group_index) const {
    if constexpr (P == Placement::block_planes) return load<std::uint32_t>(data_ + group_index / 16 * block_stride + capacity * (geometry_bytes + sizeof(U)) + capacity / 8 + group_index % 16 * 4);
    else if constexpr (P == Placement::groups) return load<std::uint32_t>(data_ + group_index * stride);
    else if constexpr (P == Placement::series) return sp::get_unchecked(*bases_, group_index);
    else return load<std::uint32_t>(data_ + base_offset_ + group_index * 4);
  }
  [[gnu::always_inline]] void decode_widths(unsigned first, W* decoded) const requires(Widths) {
    if constexpr (P == Placement::series) {
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
      sp::native_group::store(decoded, sp::native_group::read<GF, true, 16>(data_, GF::tile_bytes, first));
#else
      sp::native::store16(decoded, sp::native::read16<GF, true>(data_, GF::tile_bytes, first));
#endif
    } else if constexpr (P == Placement::tuple) {
      alignas(64) std::array<std::uint8_t, 64> packet;
      tp::native::store_packet(packet.data(), tuple_native_->get_unchecked(first));
      streampack_next::assemble16_native<W, geometry_bits, 8>(packet.data(), decoded);
    } else streampack_next::copy16_native(decoded, reinterpret_cast<const W*>(geometry_address(first)));
  }
  [[gnu::always_inline]] void decode_endpoints(unsigned first, G* decoded) const requires(!Widths) {
    if constexpr (P == Placement::series) {
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
      sp::native_group::store(decoded, sp::native_group::read<GF, true, 16>(data_, GF::tile_bytes, first));
#else
      sp::native::store16(decoded, sp::native::read16<GF, true>(data_, GF::tile_bytes, first));
#endif
    } else if constexpr (P == Placement::tuple) {
      alignas(64) std::array<std::uint8_t, 64> packet;
      tp::native::store_packet(packet.data(), tuple_native_->get_unchecked(first));
      streampack_next::assemble16_native<G, geometry_bits, 8>(packet.data(), decoded);
    } else streampack_next::copy16_native(decoded, reinterpret_cast<const G*>(geometry_address(first)));
  }
  unsigned start(unsigned row) const {
    const auto rank = row % capacity;
    if constexpr (!Widths) {
      if constexpr (P == Placement::groups) if (row % group == 0) return base(row / group);
      return rank ? geometry(row - 1) : 0;
    } else {
      const auto first = row / group * group, local = row % group;
      alignas(64) W decoded[16]; decode_widths(first, decoded);
      return base(row / group) + streampack_next::prefix16_native(decoded, local);
    }
  }
  [[gnu::always_inline]] void decode_used(unsigned first, W* decoded) const {
    if constexpr (P == Placement::series) {
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
      sp::native_group::store(decoded, sp::native_group::read<UF, true, 16>(data_ + used_offset_, UF::tile_bytes, first));
#else
      sp::native::store16(decoded, sp::native::read16<UF, true>(data_ + used_offset_, UF::tile_bytes, first));
#endif
    } else if constexpr (P == Placement::tuple) {
      alignas(64) std::array<std::uint8_t, 64> packet;
      tp::native::store_packet(packet.data(), tuple_used_native_->get_unchecked(first));
      streampack_next::assemble16_native<W, length_bits, 8 - geometry_bits % 8>(packet.data(), decoded);
    } else if constexpr (WideUsed) {
      for (unsigned i = 0; i < 16; ++i) decoded[i] = used_length(first + i);
    } else streampack_next::copy16_native(decoded, reinterpret_cast<const W*>(used_address(first)));
  }
  [[gnu::always_inline]] void decode_bases(unsigned block, std::uint32_t* decoded) const requires(Widths) {
    if constexpr (P == Placement::series) {
      using B = std::conditional_t<(base_bits <= 16), std::uint16_t, std::uint32_t>;
      alignas(64) B values[16];
#if defined(__aarch64__) || (defined(__AVX512BW__) && defined(__AVX512DQ__) && defined(__AVX512VL__))
      sp::native_group::store(values, sp::native_group::read<BF, true, 16>(data_ + base_offset_, BF::tile_bytes, block * 16));
#else
      sp::native::store16(values, sp::native::read16<BF, true>(data_ + base_offset_, BF::tile_bytes, block * 16));
#endif
      for (unsigned i = 0; i < 16; ++i) decoded[i] = values[i];
    } else for (unsigned i = 0; i < 16; ++i) decoded[i] = base(block * 16 + i);
  }
  unsigned count_dead(unsigned block) const {
    unsigned n = 0;
    if constexpr (P == Placement::groups) {
      for (unsigned i = 0; i < 16; ++i) n += std::popcount(load<std::uint16_t>(dead_address(block * 256 + i * 16)));
    } else if constexpr (P == Placement::tuple) {
      for (unsigned i = 0; i < 256; ++i) n += dead(block * 256 + i);
    } else for (unsigned i = 0; i < 4; ++i) n += std::popcount(load<std::uint64_t>(dead_address(block * 256) + i * 8));
    return n;
  }
  std::uint16_t dead_mask16(unsigned first) const {
    if constexpr (P == Placement::tuple) {
      alignas(64) std::array<std::uint8_t, 64> packet;
      tp::native::store_packet(packet.data(), tuple_dead_native_->get_unchecked(first));
#if defined(__aarch64__)
      const auto bits = vld1q_u8(packet.data()); const int8x8_t shifts{0,1,2,3,4,5,6,7};
      return unsigned(vaddv_u8(vshl_u8(vget_low_u8(bits), shifts))) |
          (unsigned(vaddv_u8(vshl_u8(vget_high_u8(bits), shifts))) << 8);
#elif defined(__AVX2__)
      return unsigned(_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(packet.data())), _mm_set1_epi8(1))));
#else
      unsigned mask = 0; for (unsigned i = 0; i < 16; ++i) mask |= unsigned(packet[i]) << i; return mask;
#endif
    } else return load<std::uint16_t>(dead_address(first));
  }
  unsigned reserved(unsigned row) const { if constexpr (Widths) return geometry(row); else return geometry(row) - start(row); }
  Entry get(unsigned row) const {
    const auto [n, dead] = state(row); const auto s = start(row);
    return {s, Widths ? geometry(row) : geometry(row) - s, n, dead};
  }
  // Independent construction writes all owned codes, leaving stride gaps alone.
  void encode(unsigned row, unsigned start, unsigned reservation, unsigned used, bool dead) {
    const auto g = Widths ? reservation : start + reservation;
    if constexpr (P == Placement::series) {
      sp::author::store_unchecked(*geometry_, row, g); sp::author::store_unchecked(*used_, row, used);
    } else if constexpr (P == Placement::tuple) {
      const std::uint64_t bits = std::uint64_t(g) | (std::uint64_t(used) << geometry_bits) | (std::uint64_t(dead) << (geometry_bits + length_bits));
      for (unsigned i = 0; i < tuple_bytes; ++i) data_[row * tuple_bytes + i] = bits >> (i * 8);
    } else {
      if constexpr (Widths) store<W>(geometry_address(row), reservation); else store<G>(geometry_address(row), g);
      store<U>(used_address(row), used);
    }
    if constexpr (P != Placement::tuple) {
      auto* d = dead_address(row); const auto bit = std::uint8_t(1u << (row % 8)); *d = (*d & ~bit) | (dead ? bit : 0);
    }
    if (row % group == 0) {
      if constexpr (P == Placement::block_planes && Widths) store<std::uint32_t>(data_ + row / capacity * block_stride + capacity * (geometry_bytes + sizeof(U)) + capacity / 8 + row % capacity / group * 4, start);
      else if constexpr (P == Placement::groups) store<std::uint32_t>(unit(row), start);
      else if constexpr (Widths && P == Placement::series) sp::author::store_unchecked(*bases_, row / group, start);
      else if constexpr (Widths) store<std::uint32_t>(data_ + base_offset_ + row / group * 4, start);
    }
  }
  // Point metadata replacement only, not a complete payload mutation benchmark.
  // The named adapter is stable through synchronous journal consumption.
  template<class Journal> void update(unsigned row, unsigned used, bool dead, Journal& effects) {
    if constexpr (P == Placement::tuple) {
      std::uint64_t packet = 0;
      for (unsigned i = 0; i < used_fields_; ++i)
        packet |= std::uint64_t(std::uint8_t(used >> field_shift_[geom_fields_ + i])) << (i * 8);
      // Mask each byte-contained code, rather than passing upper input bits.
      const auto bits = std::uint64_t(used) << geometry_bits;
      for (unsigned i = 0; i < used_fields_; ++i) {
        const auto first = geometry_bits + field_shift_[geom_fields_ + i];
        const auto take = std::min(length_bits - field_shift_[geom_fields_ + i], 8 - first % 8);
        const auto mask = (1u << take) - 1;
        packet &= ~(std::uint64_t(255) << (i * 8)); packet |= ((bits >> first) & mask) << (i * 8);
      }
      packet |= std::uint64_t(dead) << (used_fields_ * 8);
      auto mask = tuple_write_->write_bytes();
      for (unsigned i = 0; i < tuple_bytes;) {
        if (!(mask & (1ull << i))) { ++i; continue; }
        const auto first = i; while (i < tuple_bytes && (mask & (1ull << i))) ++i;
        before(effects, data_ + row * tuple_bytes + first, i - first);
      }
      tp::native::write_body(tuple_write_->controls(), data_ + row * tuple_bytes, packet);
    } else {
      if constexpr (P == Placement::series) {
        auto* tile = data_ + used_offset_ + row / 8 * UF::tile_bytes;
        before(effects, tile + row % 8 * UF::body, UF::body);
        before(effects, tile + 8 * UF::body, UF::tail);
        sp::author::store_unchecked(*used_, row, used);
      } else { before(effects, used_address(row), sizeof(U)); store<U>(used_address(row), used); }
      auto* d = dead_address(row); before(effects, d, 1);
      const auto bit = std::uint8_t(1u << (row % 8)); *d = (*d & ~bit) | (dead ? bit : 0);
    }
  }
  std::optional<std::pair<unsigned, bool>> reverse(unsigned block, unsigned count, unsigned extent, unsigned offset) const {
    if (offset >= extent || !count) return std::nullopt;
    const auto first = block * capacity;
    if constexpr (!Widths) {
      unsigned lo = 0, hi = count;
      while (lo < hi) { const auto mid = (lo + hi) / 2; if (geometry(first + mid) <= offset) lo = mid + 1; else hi = mid; }
      return std::pair{lo, dead(first + lo)};
    } else {
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
  }
};
} // namespace streampack_layout
