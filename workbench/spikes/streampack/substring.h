#pragma once
#include "byte_search.h"
#include <array>
#include <vector>

namespace streampack_next {
// Prepared linear-work overlapping control. Preparation is separately charged;
// scan_parts preserves state across a caller-owned prefix/suffix seam.
struct Kmp {
  std::vector<std::byte> needle;
  std::vector<std::size_t> fallback;
  explicit Kmp(Bytes q) : needle(q.begin(), q.end()), fallback(q.size()) {
    for (std::size_t i = 1, j = 0; i < q.size(); ++i) {
      while (j && q[i] != q[j]) j = fallback[j - 1];
      if (q[i] == q[j]) ++j;
      fallback[i] = j;
    }
  }
  template<class Emit> bool scan_parts(Bytes prefix, Bytes suffix, Emit&& emit) const {
    if (needle.size() > prefix.size() + suffix.size()) return true;
    if (needle.empty()) {
      for (std::size_t i = 0; i <= prefix.size() + suffix.size(); ++i) if (!emit(i)) return false;
      return true;
    }
    std::size_t offset = 0, j = 0;
    for (auto part : {prefix, suffix}) for (auto b : part) {
      while (j && b != needle[j]) j = fallback[j - 1];
      if (b == needle[j]) ++j;
      ++offset;
      if (j == needle.size()) {
        if (!emit(offset - j)) return false;
        j = fallback[j - 1];
      }
    }
    return true;
  }
};

template<class View, class Emit>
bool substring_body(const View& view, Bytes q, Emit&& emit) {
  if (q.empty()) {
    for (unsigned r = 0; r < view.size(); ++r) if (view.live(r))
      for (unsigned p = 0; p <= view.length(r); ++p) if (!emit(r, p)) return false;
    return true;
  }
  // A bounded anchor candidate is not yet a substring. Reject matches crossing
  // any used-span boundary, then verify. KMP is the periodic-input contender.
  return byte_sparse(view, q[0], [&](auto r, auto position) {
    if (q.size() > view.length(r) - position) return true;
    const auto* candidate = view.body.data() + view.start(r) + position;
    if (q.size() == 1 || (candidate[q.size() - 1] == q.back() &&
        std::memcmp(candidate, q.data(), q.size()) == 0)) return emit(r, position);
    return true;
  });
}

struct Prefix { std::array<std::byte, 4> bytes{}; std::uint8_t size{}; };
struct Fanout { std::span<const std::uint32_t> ends, rows; };
// Cold caller admission. Scratch is caller-owned and reusable; no allocation or
// ownership state is introduced into the packed payload.
template<class View> Error admit_schema(const View& view, std::span<const Prefix> prefixes,
    std::span<const std::uint16_t> holders, Fanout inverse, std::span<std::uint8_t> seen) {
  if (view.admit() != Error::none || prefixes.size() != holders.size() || seen.size() < holders.size() ||
      inverse.ends.size() != view.size() + 1 || inverse.ends[0] != 0 || inverse.ends.back() != inverse.rows.size())
    return Error::shape;
  std::fill(seen.begin(), seen.end(), 0);
  for (unsigned r = 0; r < view.size(); ++r) {
    if (inverse.ends[r] > inverse.ends[r + 1] || inverse.ends[r + 1] > inverse.rows.size()) return Error::shape;
    for (auto i = inverse.ends[r]; i < inverse.ends[r + 1]; ++i) {
      const auto row = inverse.rows[i];
      if (row >= holders.size() || holders[row] != r || !view.live(r) || seen[row]++) return Error::shape;
    }
  }
  for (unsigned row = 0; row < holders.size(); ++row)
    if (prefixes[row].size > 4 || seen[row] != (holders[row] != 65535)) return Error::shape;
  return Error::none;
}
inline bool seam_equal(Bytes prefix, Bytes suffix, std::size_t at, Bytes q) {
  if (at > prefix.size() + suffix.size() || q.size() > prefix.size() + suffix.size() - at) return false;
  const auto in_prefix = at < prefix.size() ? std::min(q.size(), prefix.size() - at) : 0;
  if (in_prefix && std::memcmp(prefix.data() + at, q.data(), in_prefix)) return false;
  const auto remaining = q.size() - in_prefix;
  return !remaining || std::memcmp(suffix.data() + (at + in_prefix - prefix.size()), q.data() + in_prefix, remaining) == 0;
}

// Caller composition: prefixes and logical mapping stay outside the primitive.
// This emits physical discovery order. A caller asking for logical order pays
// for restoration; an any/count/membership sink need not materialize matches.
// Caller admission requires one prefix per holder, prefix sizes <= 4, live
// in-range non-absent holders, and an exact inverse mapping (each non-absent row
// once under its current rank). Retargeting invalidates that inverse. Prepare
// it at a completed frontier; per-hit checks cannot repair a stale mapping.
template<class View, class Emit>
bool schema_body_search(const View& view, std::span<const Prefix> prefixes,
    std::span<const std::uint16_t> holders, Fanout inverse, Bytes q, Emit&& emit) {
  for (unsigned row = 0; row < holders.size(); ++row) {
    const auto r = holders[row]; if (r == 65535) continue;
    const Bytes prefix(prefixes[row].bytes.data(), prefixes[row].size);
    const auto suffix = view.value(r);
    for (unsigned p = 0; p < prefix.size(); ++p)
      if (seam_equal(prefix, suffix, p, q) && !emit(row, p)) return false;
  }
  return substring_body(view, q, [&](auto r, auto position) {
    for (auto i = inverse.ends[r]; i < inverse.ends[r + 1]; ++i) {
      const auto row = inverse.rows[i];
      if (!emit(row, position + prefixes[row].size)) return false;
    }
    return true;
  });
}
} // namespace streampack_next
