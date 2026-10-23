#pragma once
#include "direct.h"
#include "prefix.h"

namespace streampack_next {
// Deliberately one group size for the first substitution experiment. Arrays
// remain caller-owned; group-relative ends and widths are distinct formats.
template<class Width, bool Relative, bool Used = false, bool Dead = false>
struct Grouped {
  static constexpr unsigned group = 16;
  static constexpr bool relative = Relative;
  std::span<const Width> values; // padded through the final 16-group, initialized
  std::span<const std::uint32_t> bases; // leading zero and final extent
  Bytes body;
  std::span<const std::uint16_t> used;
  std::span<const std::uint64_t> tombs;
  std::uint16_t count{};
  std::size_t size() const { return count; }
  std::uint32_t extent() const { return bases.back(); }
  /// r names an assigned rank (r < count); extent() supplies the terminal boundary.
  std::uint32_t start(unsigned r) const {
    const auto local = r % group, first = r - local;
    if constexpr (Relative) return bases[r / group] + (local ? values[r - 1] : 0);
    else {
      return bases[r / group] + prefix16_native(values.data() + first, local);
    }
  }
  std::uint32_t reserved(unsigned r) const {
    if constexpr (Relative) return values[r] - (r % group ? values[r - 1] : 0);
    else return values[r];
  }
  std::uint32_t length(unsigned r) const { if constexpr (Used) return used[r]; else return reserved(r); }
  bool live(unsigned r) const { if constexpr (Dead) return !(tombs[r / 64] & (1ull << (r % 64))); else return true; }
  Bytes value(unsigned r) const { return body.subspan(start(r), length(r)); }
  using Owner = typename Direct<std::uint32_t>::Owner;
  std::optional<Owner> reverse_scalar(std::uint32_t x) const {
    if (x >= extent()) return std::nullopt;
    const auto g = unsigned(std::upper_bound(bases.begin() + 1, bases.end(), x) - bases.begin() - 1);
    const auto first = g * group, last = std::min<unsigned>(count, first + group);
    unsigned r = first;
    if constexpr (Relative) {
      r += std::upper_bound(values.begin() + first, values.begin() + last, x - bases[g]) - (values.begin() + first);
    } else {
      auto end = bases[g];
      for (; r < last; ++r) { end += values[r]; if (x < end) break; }
    }
    return Owner{std::uint16_t(r), !live(r)};
  }
  // Same stored bytes and owner contract. The bounded byte-width SIMD path is
  // selected by the paired ARM/AMD/Intel controls; scalar remains inspectable.
  std::optional<Owner> reverse_native(std::uint32_t x) const {
    if constexpr (Relative || !std::is_same_v<Width, std::uint8_t>) return reverse_scalar(x);
    else {
      if (x >= extent()) return std::nullopt;
      const auto g = unsigned(std::upper_bound(bases.begin() + 1, bases.end(), x) - bases.begin() - 1);
      const auto r = g * group + owner16_native(values.data() + g * group, x - bases[g]);
      return Owner{std::uint16_t(r), !live(r)};
    }
  }
  std::optional<Owner> reverse(std::uint32_t x) const { return reverse_native(x); }
  Error admit(std::uint16_t maximum = 65535) const {
    const auto groups = (count + group - 1) / group;
    if (count > 512 || values.size() != groups * group || bases.size() != groups + 1 || bases[0] != 0 ||
        !std::has_single_bit(std::uint32_t(maximum) + 1)) return Error::shape;
    if constexpr (Used) if (used.size() < count) return Error::shape;
    if constexpr (Dead) if (tombs.size() < (count + 63u) / 64) return Error::shape;
    std::uint32_t end = 0;
    for (unsigned r = 0; r < count; ++r) {
      if (r % group == 0 && bases[r / group] != end) return Error::shape;
      if constexpr (Relative) if (r % group && values[r] < values[r - 1]) return Error::length;
      const auto width = reserved(r);
      if (width > maximum) return Error::length;
      if constexpr (Used) if (used[r] > width) return Error::length;
      end += width;
    }
    if (end != extent()) return Error::shape;
    return end <= body.size() ? Error::none : Error::capacity;
  }
};

template<class Width, bool Relative> struct GroupStorage {
  std::span<Width> values;
  std::span<std::uint32_t> bases;
  std::uint16_t& count;
  // Allocate-once geometry construction/transcode, not a hidden decoded cache.
  // Source geometry and all destination/effect planes must be disjoint. No
  // payload stores. All shape/width/effect refusals precede any output stores.
  template<class View, class Effects> Error encode(const View& source, Effects& effects) {
    if (source.admit() != Error::none || source.size() > 512) return Error::shape;
    const auto n = unsigned(source.size()), groups = (n + 15) / 16;
    if (values.size() < groups * 16 || bases.size() < groups + 1) return Error::capacity;
    std::uint32_t within = 0;
    for (unsigned r = 0; r < n; ++r) {
      if (r % 16 == 0) within = 0;
      within += source.reserved(r);
      if ((Relative ? within : source.reserved(r)) > std::numeric_limits<Width>::max()) return Error::length;
    }
    if (!MutableDirect<std::uint32_t>::room(effects, (groups != 0) + 2)) return Error::effects;
    if (groups) effects.before(*this, 0, 0, groups * 16 * sizeof(Width));
    effects.before(*this, 1, 0, (groups + 1) * sizeof(std::uint32_t));
    effects.before(*this, 2, 0, sizeof(count));
    std::fill_n(values.begin(), groups * 16, 0);
    std::uint32_t offset = 0; within = 0;
    for (unsigned r = 0; r < n; ++r) {
      if (r % 16 == 0) { bases[r / 16] = offset; within = 0; }
      const auto width = source.reserved(r); within += width; offset += width;
      values[r] = Width(Relative ? within : width);
    }
    bases[groups] = offset; count = n;
    return Error::none;
  }
};

/// Native checkpoint-width mutation, with the same exclusive/alias/effect
/// preconditions as MutableDirect. Widths stay fixed on ordinary edits. Planes
/// are body=0, widths=1, used=2, tombs=3, count=4, checkpoints=5.
template<class Width, bool Used = true, bool Dead = true> struct MutableWidths {
  std::span<Width> widths;
  std::span<std::uint32_t> bases;
  std::span<std::byte> body;
  std::span<std::uint16_t> used;
  std::span<std::uint64_t> tombs;
  std::uint16_t& count;
  std::uint16_t maximum{std::numeric_limits<Width>::max()};
  using Common = MutableDirect<std::uint32_t, Used, Dead>;
  Grouped<Width, false, Used, Dead> read() const {
    const auto groups = (count + 15u) / 16;
    return {widths.first(groups * 16), bases.first(groups + 1), body, used, tombs, count};
  }
  Error shape() const {
    if ((widths.size() != 128 && widths.size() != 256 && widths.size() != 512) || count > widths.size() ||
        bases.size() != widths.size() / 16 + 1 || maximum > std::numeric_limits<Width>::max() ||
        !std::has_single_bit(std::uint32_t(maximum) + 1)) return Error::shape;
    if constexpr (Used) if (used.size() < widths.size()) return Error::shape;
    if constexpr (Dead) if (tombs.size() < widths.size() / 64) return Error::shape;
    return Error::none;
  }
  STREAMPACK_SMALL_INLINE bool metadata_alias(Bytes input) const {
    return overlaps(input.data(), input.size(), widths.data(), widths.size_bytes()) ||
        overlaps(input.data(), input.size(), bases.data(), bases.size_bytes()) ||
        (Used && overlaps(input.data(), input.size(), used.data(), used.size_bytes())) ||
        (Dead && overlaps(input.data(), input.size(), tombs.data(), tombs.size_bytes())) ||
        overlaps(input.data(), input.size(), &count, sizeof(count));
  }
  template<class Effects> STREAMPACK_SMALL_INLINE void before(Effects& effects, unsigned plane, std::size_t offset, std::size_t size) const {
    if (size) { STREAMPACK_EFFECT_CALL effects.before(*this, plane, offset, size); }
  }
  template<class Effects> Error initialize(Effects& effects) {
    if (auto error = shape(); error != Error::none) return error;
    if (!Common::room(effects, 2 + Dead)) return Error::effects;
    before(effects, 5, 0, 4); before(effects, 4, 0, sizeof(count));
    if constexpr (Dead) before(effects, 3, 0, tombs.size_bytes());
    bases[0] = 0; count = 0;
    if constexpr (Dead) std::fill(tombs.begin(), tombs.end(), 0);
    return Error::none;
  }
  template<class Effects> Error append(Bytes input, std::uint16_t reservation, Effects& effects) {
    if (input.size() > reservation || reservation > maximum) return Error::length;
    if constexpr (!Used) if (input.size() != reservation) return Error::length;
    if (count == widths.size()) return Error::capacity;
    const std::size_t begin = bases[(count + 15u) / 16];
    if (begin > body.size() || reservation > body.size() - begin) return Error::capacity;
    if (metadata_alias(input) || Common::effect_alias(input, effects)) return Error::alias;
    if (!Common::room(effects, (reservation != 0) + 3 + Used + Dead)) return Error::effects;
    const auto initialized = count % 16 ? 1 : 16;
    before(effects, 0, begin, reservation);
    before(effects, 1, count * sizeof(Width), initialized * sizeof(Width));
    before(effects, 5, (count / 16 + 1) * 4, 4);
    if constexpr (Used) before(effects, 2, count * 2, 2);
    if constexpr (Dead) before(effects, 3, count / 64 * 8, 8);
    before(effects, 4, 0, sizeof(count));
    if (!input.empty()) std::memmove(body.data() + begin, input.data(), input.size());
    if (reservation > input.size()) std::memset(body.data() + begin + input.size(), 0, reservation - input.size());
    if (initialized == 16) std::fill_n(widths.begin() + count, 16, 0);
    widths[count] = Width(reservation); bases[count / 16 + 1] = begin + reservation;
    if constexpr (Used) used[count] = input.size();
    if constexpr (Dead) tombs[count / 64] &= ~(1ull << (count % 64));
    ++count; return Error::none;
  }
  template<bool ClearSlack = false, class Effects> Error replace(std::uint16_t rank, Bytes input, Effects& effects) {
    if (rank >= count) return Error::rank;
    const auto view = read();
    if (!view.live(rank)) return Error::dead;
    if (input.size() > widths[rank]) return Error::length;
    if constexpr (!Used) if (input.size() != widths[rank]) return Error::length;
    if (metadata_alias(input) || Common::effect_alias(input, effects)) return Error::alias;
    const auto start = view.start(rank), old = view.length(rank);
    const auto touched = ClearSlack ? std::max<std::size_t>(old, input.size()) : input.size();
    if (!Common::room(effects, (touched != 0) + Used)) return Error::effects;
    before(effects, 0, start, touched);
    if constexpr (Used) before(effects, 2, rank * 2, 2);
    if (!input.empty()) std::memmove(body.data() + start, input.data(), input.size());
    if constexpr (ClearSlack) if (old > input.size()) std::memset(body.data() + start + input.size(), 0, old - input.size());
    if constexpr (Used) used[rank] = input.size();
    return Error::none;
  }
  template<class Effects> Error retire(std::uint16_t rank, Effects& effects) requires Dead {
    if (rank >= count) return Error::rank;
    if (!Common::room(effects, 1)) return Error::effects;
    before(effects, 3, rank / 64 * 8, 8); tombs[rank / 64] |= 1ull << (rank % 64); return Error::none;
  }
};

template<class Width, bool Used, bool Dead, class Effects>
Error soft_pack(MutableWidths<Width, Used, Dead>& dst, bool shrink, Effects& effects) {
  const auto groups = (dst.count + 15u) / 16;
  using Common = typename MutableWidths<Width, Used, Dead>::Common;
  if (!Common::room(effects, std::size_t(dst.count) * (2 + Used) + groups + 1)) return Error::effects;
  unsigned source = 0, output = 0;
  for (unsigned r = 0; r < dst.count; ++r) {
    const auto reserved = dst.widths[r];
    const auto used = Used ? dst.used[r] : reserved;
    bool live = true; if constexpr (Dead) live = !(dst.tombs[r / 64] & (1ull << (r % 64)));
    const auto width = live ? (shrink ? used : reserved) : 0;
    if (r % 16 == 0) { dst.before(effects, 5, r / 16 * 4, 4); dst.bases[r / 16] = output; }
    if (width && source != output) { dst.before(effects, 0, output, width); std::memmove(dst.body.data() + output, dst.body.data() + source, width); }
    dst.before(effects, 1, r * sizeof(Width), sizeof(Width)); dst.widths[r] = Width(width);
    if constexpr (Used) if (!live) { dst.before(effects, 2, r * 2, 2); dst.used[r] = 0; }
    source += reserved; output += width;
  }
  dst.before(effects, 5, groups * 4, 4); dst.bases[groups] = output;
  return Error::none;
}

// Independent geometry specialization. Presence means positive reservation,
// not liveness. Soft packing fixed-stride data switches to packed presence;
// arbitrary live shrink must instead materialize explicit geometry.
template<bool Packed, bool Used = false, bool Dead = false> struct Implicit {
  std::uint16_t count{}, stride{};
  std::span<const std::uint64_t> present, tombs;
  std::span<const std::uint16_t> used;
  Bytes body;
  std::size_t size() const { return count; }
  bool occupies(unsigned r) const { if constexpr (Packed) return present[r / 64] & (1ull << (r % 64)); else return true; }
  std::uint32_t ordinal(unsigned r) const {
    if constexpr (!Packed) return r;
    else {
      unsigned sum = 0;
      for (unsigned word = 0; word < r / 64; ++word) sum += std::popcount(present[word]);
      if (r % 64) sum += std::popcount(present[r / 64] & ((1ull << (r % 64)) - 1));
      return sum;
    }
  }
  std::uint32_t start(unsigned r) const { return ordinal(r) * stride; }
  std::uint32_t reserved(unsigned r) const { return occupies(r) ? stride : 0; }
  std::uint32_t length(unsigned r) const { if constexpr (Used) return used[r]; else return reserved(r); }
  bool live(unsigned r) const { if constexpr (Dead) return !(tombs[r / 64] & (1ull << (r % 64))); else return true; }
  Bytes value(unsigned r) const { return body.subspan(start(r), length(r)); }
  std::uint32_t extent() const { return start(count); }
  using Owner = typename Direct<std::uint32_t>::Owner;
  std::optional<Owner> reverse(std::uint32_t x) const {
    if (x >= extent()) return std::nullopt;
    auto r = x / stride;
    if constexpr (Packed) {
      unsigned word = 0;
      for (; word < present.size(); ++word) {
        const auto n = unsigned(std::popcount(present[word]));
        if (r < n) break; r -= n;
      }
      auto bits = present[word]; while (r) { --r; bits &= bits - 1; }
      r = word * 64 + std::countr_zero(bits);
    }
    return Owner{std::uint16_t(r), !live(r)};
  }
  Error admit(std::uint16_t maximum = 65535) const {
    if (count > 512 || stride > maximum || !std::has_single_bit(std::uint32_t(maximum) + 1)) return Error::shape;
    if constexpr (Packed) {
      if (present.size() != (count + 63u) / 64) return Error::shape;
      if (count % 64 && (present.back() >> (count % 64))) return Error::shape;
    }
    if constexpr (Dead) if (tombs.size() < (count + 63u) / 64) return Error::shape;
    if constexpr (Used) {
      if (used.size() < count) return Error::shape;
      for (unsigned r = 0; r < count; ++r) if (used[r] > reserved(r)) return Error::length;
    }
    return extent() <= body.size() ? Error::none : Error::capacity;
  }
};
} // namespace streampack_next
