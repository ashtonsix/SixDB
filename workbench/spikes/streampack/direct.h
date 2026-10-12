#pragma once
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <ikea/effects.h>

#if defined(STREAMPACK_INLINE_HELPERS) && STREAMPACK_INLINE_HELPERS
#define STREAMPACK_SMALL_INLINE [[gnu::always_inline]]
#else
#define STREAMPACK_SMALL_INLINE
#endif
#if defined(STREAMPACK_INLINE_EFFECTS) && STREAMPACK_INLINE_EFFECTS
#define STREAMPACK_EFFECT_CALL [[clang::always_inline]]
#else
#define STREAMPACK_EFFECT_CALL
#endif

namespace streampack_next {
using Bytes = std::span<const std::byte>;
enum class Error { none, shape, capacity, length, rank, dead, alias, effects };

/// All storage is borrowed. Endpoints include a leading zero. These controls
/// retain neither allocation ownership nor a logical version or write authority.
template<class End, bool Used = false, bool Dead = false> struct Direct {
  static_assert(std::is_same_v<End, std::uint16_t> ||
                std::is_same_v<End, std::uint32_t>);
  std::span<const End> ends;
  Bytes body;
  std::span<const std::uint16_t> used;
  std::span<const std::uint64_t> tombs;
  static constexpr bool has_used = Used, has_dead = Dead;
  std::size_t size() const { return ends.size() - 1; }
  std::uint32_t start(std::size_t r) const { return ends[r]; }
  std::uint32_t reserved(std::size_t r) const { return ends[r + 1] - ends[r]; }
  std::uint32_t length(std::size_t r) const {
    if constexpr (Used) return used[r];
    else return reserved(r);
  }
  bool live(std::size_t r) const {
    if constexpr (Dead) return !(tombs[r / 64] & (std::uint64_t{1} << (r % 64)));
    else return true;
  }
  Bytes value(std::size_t r) const { return body.subspan(start(r), length(r)); }
  std::uint32_t extent() const { return ends.back(); }

  /// Cold geometric admission, independent of caller interpretation identity.
  Error admit(std::uint16_t maximum = 65535) const {
    if (ends.empty() || ends.size() > 513 || ends[0] != 0 ||
        !std::has_single_bit(std::uint32_t(maximum) + 1)) return Error::shape;
    if constexpr (Used) if (used.size() < size()) return Error::shape;
    if constexpr (Dead) if (tombs.size() < (size() + 63) / 64) return Error::shape;
    for (std::size_t r = 0; r < size(); ++r) {
      if (ends[r + 1] < ends[r] || reserved(r) > maximum) return Error::length;
      if constexpr (Used) if (used[r] > reserved(r)) return Error::length;
    }
    return extent() <= body.size() ? Error::none : Error::capacity;
  }

  struct Owner { std::uint16_t rank; bool tombstone; };
  std::optional<Owner> reverse(std::uint32_t offset) const {
    if (offset >= extent()) return std::nullopt;
    // upper_bound, not lower_bound: zero reservations have no byte owner.
    auto first = ends.begin() + 1, last = ends.end();
    auto found = std::upper_bound(first, last, offset);
    auto rank = std::uint16_t(found - first);
    return Owner{rank, !live(rank)};
  }
};

inline bool overlaps(const void* a, std::size_t an, const void* b, std::size_t bn) {
  if (!an || !bn) return false;
  auto x = reinterpret_cast<std::uintptr_t>(a), y = reinterpret_cast<std::uintptr_t>(b);
  return x <= y ? y - x < an : x - y < bn;
}

/// Caller-provided writable capacity. Admit the current interpretation once and
/// keep planes/count/effect storage disjoint, and inputs disjoint from custom
/// effect-hook storage. Exclusive write authority is an
/// owner precondition. Body aliases in input are supported while this binding
/// stays valid; the owner must resolve aliases BEFORE any relocating grow.
template<class End, bool Used = true, bool Dead = true> struct MutableDirect {
  std::span<End> ends;
  std::span<std::byte> body;
  std::span<std::uint16_t> used;
  std::span<std::uint64_t> tombs;
  std::uint16_t& count;
  std::uint16_t maximum{65535};

  Direct<End, Used, Dead> read() const {
    return {ends.first(count + 1), body, used, tombs};
  }
  Error shape() const {
    if (ends.size() != 129 && ends.size() != 257 && ends.size() != 513) return Error::shape;
    if (count >= ends.size() || !std::has_single_bit(std::uint32_t(maximum) + 1)) return Error::shape;
    if constexpr (Used) if (used.size() < ends.size() - 1) return Error::shape;
    if constexpr (Dead) if (tombs.size() < (ends.size() - 1) / 64) return Error::shape;
    return Error::none;
  }
  STREAMPACK_SMALL_INLINE bool metadata_alias(Bytes bytes) const {
    return overlaps(bytes.data(), bytes.size(), ends.data(), ends.size_bytes()) ||
        (Used && overlaps(bytes.data(), bytes.size(), used.data(), used.size_bytes())) ||
        (Dead && overlaps(bytes.data(), bytes.size(), tombs.data(), tombs.size_bytes())) ||
        overlaps(bytes.data(), bytes.size(), &count, sizeof(count));
  }
  template<class Effects> static bool room(Effects& effects, std::size_t n) {
    if constexpr (requires { effects.remaining(); }) return effects.remaining() >= n;
    else return true;
  }
  template<class Effects> static bool effect_alias(Bytes input, const Effects& effects) {
    if constexpr (requires { effects.storage; }) {
      const auto storage = std::as_bytes(effects.storage);
      return overlaps(input.data(), input.size(), storage.data(), storage.size());
    } else return false;
  }
  template<class Effects> STREAMPACK_SMALL_INLINE void before(Effects& effects, unsigned plane,
                                       std::size_t offset, std::size_t bytes) const {
    if (bytes) { STREAMPACK_EFFECT_CALL effects.before(*this, ikea::byte_write{plane, offset, bytes}); }
  }
  /// Planes: body=0, endpoints=1, used=2, tombstones=3, assigned count=4.
  /// Hooks report issued stores and require preadmitted no-fail capacity.
  template<class Effects> Error initialize(Effects& effects) {
    if (auto e = shape(); e != Error::none) return e;
    if (!room(effects, 2 + Dead)) return Error::effects;
    before(effects, 1, 0, sizeof(End));
    before(effects, 4, 0, sizeof(count));
    if constexpr (Dead) before(effects, 3, 0, tombs.size_bytes());
    ends[0] = 0;
    count = 0;
    if constexpr (Dead) std::fill(tombs.begin(), tombs.end(), 0);
    return Error::none;
  }
  template<class Effects> Error append(Bytes input, std::uint16_t reservation, Effects& effects) {
    if (input.size() > reservation || reservation > maximum) return Error::length;
    if constexpr (!Used) if (input.size() != reservation) return Error::length;
    if (count + 1u >= ends.size()) return Error::capacity;
    const std::size_t begin = ends[count];
    if (begin > body.size() || reservation > body.size() - begin ||
        reservation > std::numeric_limits<End>::max() - begin) return Error::capacity;
    if (metadata_alias(input) || effect_alias(input, effects)) return Error::alias;
    if (!room(effects, (reservation != 0) + 2 + Used + Dead)) return Error::effects;
    before(effects, 0, begin, reservation);
    before(effects, 1, (count + 1) * sizeof(End), sizeof(End));
    if constexpr (Used) before(effects, 2, count * sizeof(std::uint16_t), sizeof(std::uint16_t));
    if constexpr (Dead) before(effects, 3, (count / 64) * sizeof(std::uint64_t), sizeof(std::uint64_t));
    before(effects, 4, 0, sizeof(count));
    if (!input.empty()) std::memmove(body.data() + begin, input.data(), input.size());
    if (reservation > input.size()) std::memset(body.data() + begin + input.size(), 0, reservation - input.size());
    ends[count + 1] = End(begin + reservation);
    if constexpr (Used) used[count] = std::uint16_t(input.size());
    if constexpr (Dead) tombs[count / 64] &= ~(std::uint64_t{1} << (count % 64));
    ++count;
    return Error::none;
  }
  /// Existing reservation bytes stay initialized. Clearing newly unused bytes
  /// is a separate write policy, not required for correct used-span search.
  template<bool ClearSlack = false, class Effects>
  Error replace(std::uint16_t rank, Bytes input, Effects& effects) {
    if (rank >= count) return Error::rank;
    auto view = read();
    if (!view.live(rank)) return Error::dead;
    if (input.size() > view.reserved(rank)) return Error::length;
    if constexpr (!Used) if (input.size() != view.reserved(rank)) return Error::length;
    if (metadata_alias(input) || effect_alias(input, effects)) return Error::alias;
    auto old_used = view.length(rank);
    auto touched = ClearSlack ? std::max<std::size_t>(old_used, input.size()) : input.size();
    if (!room(effects, (touched != 0) + Used)) return Error::effects;
    before(effects, 0, view.start(rank), touched);
    if constexpr (Used) before(effects, 2, rank * sizeof(std::uint16_t), sizeof(std::uint16_t));
    if (!input.empty()) std::memmove(body.data() + view.start(rank), input.data(), input.size());
    if constexpr (ClearSlack)
      if (old_used > input.size()) std::memset(body.data() + view.start(rank) + input.size(), 0, old_used - input.size());
    if constexpr (Used) used[rank] = std::uint16_t(input.size());
    return Error::none;
  }
  template<class Effects> Error retire(std::uint16_t rank, Effects& effects) requires Dead {
    if (rank >= count) return Error::rank;
    if (!room(effects, 1)) return Error::effects;
    before(effects, 3, (rank / 64) * sizeof(std::uint64_t), sizeof(std::uint64_t));
    tombs[rank / 64] |= std::uint64_t{1} << (rank % 64);
    return Error::none;
  }
};

/// Exclusive in-place stable packing. Keeps ranks/tombstones and optionally
/// shrinks live reservations. Requires admitted storage, no escaped old reader.
/// The destination lies no later than its source, so forward memmove is safe.
template<class End, bool Used, bool Dead, class Effects>
Error soft_pack(MutableDirect<End, Used, Dead>& dst, bool shrink, Effects& effects) {
  if (!dst.room(effects, std::size_t(dst.count) * (2 + Used))) return Error::effects;
  std::uint32_t source_begin = 0, output = 0;
  for (std::uint16_t r = 0; r < dst.count; ++r) {
    auto source_end = std::uint32_t(dst.ends[r + 1]);
    auto reserved = source_end - source_begin;
    auto used = Used ? std::uint32_t(dst.used[r]) : reserved;
    bool live = true;
    if constexpr (Dead) live = !(dst.tombs[r / 64] & (std::uint64_t{1} << (r % 64)));
    auto width = live ? (shrink ? used : reserved) : 0;
    if (width && output != source_begin) {
      dst.before(effects, 0, output, width);
      std::memmove(dst.body.data() + output, dst.body.data() + source_begin, width);
    }
    output += width;
    dst.before(effects, 1, (r + 1) * sizeof(End), sizeof(End));
    dst.ends[r + 1] = End(output);
    if constexpr (Used) if (!live) {
      dst.before(effects, 2, r * sizeof(std::uint16_t), sizeof(std::uint16_t));
      dst.used[r] = 0;
    }
    source_begin = source_end;
  }
  return Error::none;
}
} // namespace streampack_next
