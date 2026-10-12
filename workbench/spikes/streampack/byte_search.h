#pragma once
#include "direct.h"
#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace streampack_next {
/// Native bit carriers stay inside the consuming loop. NEON uses four bits per
/// byte; x86 one. Construction and sinks contain no diagnostic counters.
struct ByteMask {
  std::uint64_t bits;
  unsigned step;
  unsigned first() const { return std::countr_zero(bits) / step; }
  unsigned population() const { return std::popcount(bits) / step; }
  void drop_first() {
    auto bit = std::countr_zero(bits);
    bits &= ~(((std::uint64_t{1} << step) - 1) << bit);
  }
  ByteMask interval(unsigned first, unsigned count) const {
    if (!count) return {0, step};
    auto lo = first * step, width = count * step;
    auto mask = width == 64 ? ~std::uint64_t{} : ((std::uint64_t{1} << width) - 1) << lo;
    return {bits & mask, step};
  }
};

/// Exact one-byte comparisons; emit(base, mask, lanes) may stop with false.
/// No load reaches outside bytes. Tail uses scalar byte masks, including for
/// a zero-length span whose data pointer may be null.
template<class Emit> bool byte_masks(Bytes bytes, std::byte needle, Emit&& emit) {
  std::size_t i = 0;
#if defined(__AVX512BW__)
  const auto q = _mm512_set1_epi8(std::to_integer<char>(needle));
  for (; bytes.size() - i >= 64; i += 64) {
    auto mask = _mm512_cmpeq_epi8_mask(_mm512_loadu_si512(bytes.data() + i), q);
    if (mask && !emit(i, ByteMask{mask, 1}, 64)) return false;
  }
#elif defined(__AVX2__)
  const auto q = _mm256_set1_epi8(std::to_integer<char>(needle));
  for (; bytes.size() - i >= 32; i += 32) {
    auto x = _mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(bytes.data() + i)), q);
    auto mask = std::uint32_t(_mm256_movemask_epi8(x));
    if (mask && !emit(i, ByteMask{mask, 1}, 32)) return false;
  }
#elif defined(__aarch64__)
  const auto q = vdupq_n_u8(std::to_integer<std::uint8_t>(needle));
  for (; bytes.size() - i >= 16; i += 16) {
    auto x = vceqq_u8(vld1q_u8(reinterpret_cast<const std::uint8_t*>(bytes.data() + i)), q);
    if (!vmaxvq_u8(x)) continue;
    auto mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(x), 4)), 0);
    if (!emit(i, ByteMask{mask, 4}, 16)) return false;
  }
#endif
  for (; i < bytes.size(); ++i)
    if (bytes[i] == needle && !emit(i, ByteMask{1, 1}, 1)) return false;
  return true;
}

/// Sparse physical scan: ordered hits advance through the reservation ends.
/// Emit sees physical rank + position in value, never implicit logical fanout.
template<class View, class Emit>
bool byte_sparse(const View& view, std::byte needle, Emit&& emit) {
  if constexpr (!requires { view.ends; }) {
    std::size_t rank = 0, start = 0, end = view.size() ? view.reserved(0) : 0;
    return byte_masks(view.body.first(view.extent()), needle,
        [&](std::size_t base, ByteMask mask, unsigned) {
          while (mask.bits) {
            const auto offset = base + mask.first(); mask.drop_first();
            // Running geometry amortizes width decoding over the physical scan.
            // Zero reservations advance ranks, but never own a matching byte.
            while (end <= offset) { start = end; end += view.reserved(++rank); }
            if (view.live(rank) && offset - start < view.length(rank) && !emit(rank, offset - start)) return false;
          }
          return true;
        });
  } else {
  std::size_t rank = 0;
  return byte_masks(view.body.first(view.extent()), needle,
      [&](std::size_t base, ByteMask mask, unsigned) {
        while (mask.bits) {
          const auto offset = base + mask.first();
          mask.drop_first();
          while (view.ends[rank + 1] <= offset) ++rank;
          const auto position = offset - view.start(rank);
          if (view.live(rank) && position < view.length(rank) && !emit(rank, position)) return false;
        }
        return true;
      });
  }
}

/// Dense physical scan: resolve geometry once per intersected slot/chunk,
/// retaining masks for count/membership instead of reverse lookup per byte.
/// Emit receives (rank, chunk offset within body, slot start, valid byte mask).
template<class View, class Emit>
bool byte_segmented(const View& view, std::byte needle, Emit&& emit) {
  if constexpr (!requires { view.ends; }) {
    std::size_t rank = 0, start = 0, end = view.size() ? view.reserved(0) : 0;
    return byte_masks(view.body.first(view.extent()), needle,
        [&](std::size_t base, ByteMask mask, unsigned lanes) {
          while (end <= base) { start = end; end += view.reserved(++rank); }
          auto r = rank, begin_slot = start, end_slot = end;
          const auto limit = base + lanes;
          for (; r < view.size() && begin_slot < limit;) {
            if (view.live(r)) {
              const auto begin = std::max(base, begin_slot);
              const auto stop = std::min(limit, begin_slot + view.length(r));
              if (begin < stop) {
                const auto valid = mask.interval(unsigned(begin - base), unsigned(stop - begin));
                if (valid.bits && !emit(r, base, begin_slot, valid)) return false;
              }
            }
            begin_slot = end_slot;
            if (++r < view.size()) end_slot += view.reserved(r);
          }
          return true;
        });
  } else {
  std::size_t rank = 0;
  return byte_masks(view.body.first(view.extent()), needle,
      [&](std::size_t base, ByteMask mask, unsigned lanes) {
        while (view.ends[rank + 1] <= base) ++rank;
        auto r = rank;
        const auto limit = base + lanes;
        for (; r < view.size() && view.start(r) < limit; ++r) {
          if (!view.live(r)) continue;
          const auto begin = std::max<std::size_t>(base, view.start(r));
          const auto end = std::min<std::size_t>(limit, std::size_t(view.start(r)) + view.length(r));
          if (begin >= end) continue;
          const auto valid = mask.interval(unsigned(begin - base), unsigned(end - begin));
          if (valid.bits && !emit(r, base, view.start(r), valid)) return false;
        }
        return true;
      });
  }
}

template<class View, class Emit>
bool byte_per_value(const View& view, std::byte needle, Emit&& emit) {
  std::size_t cursor = 0;
  for (std::size_t r = 0; r < view.size(); ++r) {
    const auto start = [&] {
      if constexpr (requires { view.ends; }) return std::size_t(view.ends[r]);
      else { const auto begin = cursor; cursor += view.reserved(r); return begin; }
    }();
    if (!view.live(r)) continue;
    const auto value = view.body.subspan(start, view.length(r));
    std::size_t from = 0;
    while (from < value.size()) {
      const auto* hit = static_cast<const std::byte*>(std::memchr(value.data() + from,
          std::to_integer<unsigned char>(needle), value.size() - from));
      if (!hit) break;
      const auto position = std::size_t(hit - value.data());
      if (!emit(r, position)) return false;
      from = position + 1;
    }
  }
  return true;
}
} // namespace streampack_next
