#pragma once
#define SZ_DYNAMIC_DISPATCH 0
#include <stringzilla/find.h>
#include "direct.h"

namespace streampack_next {
inline const char* stringzilla_backend() {
#if defined(__AVX512BW__) && defined(__AVX512VL__)
  return "StringZilla-skylake";
#elif defined(__AVX2__)
  return "StringZilla-haswell";
#elif defined(__aarch64__)
  return "StringZilla-neon";
#else
  return "StringZilla-serial";
#endif
}
inline const std::byte* stringzilla_find(Bytes h, Bytes q) {
  const auto* data = reinterpret_cast<const char*>(h.data());
  const auto* needle = reinterpret_cast<const char*>(q.data());
#if defined(__AVX512BW__) && defined(__AVX512VL__)
  return reinterpret_cast<const std::byte*>(sz_find_skylake(data, h.size(), needle, q.size()));
#elif defined(__AVX2__)
  return reinterpret_cast<const std::byte*>(sz_find_haswell(data, h.size(), needle, q.size()));
#elif defined(__aarch64__)
  return reinterpret_cast<const std::byte*>(sz_find_neon(data, h.size(), needle, q.size()));
#else
  return reinterpret_cast<const std::byte*>(sz_find_serial(data, h.size(), needle, q.size()));
#endif
}
template<class Find, class Emit> bool overlapping_find(Bytes h, Bytes q, Find&& find, Emit&& emit) {
  if (q.empty()) {
    for (std::size_t p = 0; p <= h.size(); ++p) if (!emit(p)) return false;
    return true;
  }
  // StringZilla's include-overlaps iterator uses this same restart policy.
  // It is a practical competitor; prepared KMP controls dense periodic work.
  std::size_t from = 0;
  while (from <= h.size() && q.size() <= h.size() - from) {
    const auto* hit = find(h.subspan(from), q);
    if (!hit) break;
    const auto p = std::size_t(hit - h.data());
    if (!emit(p)) return false;
    from = p + 1;
  }
  return true;
}
} // namespace streampack_next
