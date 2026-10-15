#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace unified_quotient {

#if defined(__GNUC__) || defined(__clang__)
#define UQ_ALWAYS_INLINE __attribute__((always_inline)) inline
#define UQ_NOINLINE __attribute__((noinline))
#else
#define UQ_ALWAYS_INLINE inline
#define UQ_NOINLINE
#endif

inline uint32_t next_power_of_two(uint32_t value) {
  if (value <= 1) return 1;
  --value;
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  return value + 1;
}

inline unsigned log2_power_of_two(uint32_t value) {
  assert(value != 0 && (value & (value - 1)) == 0);
  return 31u - static_cast<unsigned>(__builtin_clz(value));
}

inline uint16_t permute(uint16_t key, uint16_t seed) {
  return static_cast<uint16_t>(
      static_cast<uint32_t>(static_cast<uint16_t>(key ^ seed)) * 40503u +
      13709u);
}

inline uint16_t inverse_permute(uint16_t transformed, uint16_t seed) {
  // Newton iteration for the inverse of an odd number modulo 2^16.
  uint32_t inverse = 40503u;
  inverse *= 2u - 40503u * inverse;
  inverse *= 2u - 40503u * inverse;
  inverse *= 2u - 40503u * inverse;
  inverse *= 2u - 40503u * inverse;
  const uint16_t shifted = static_cast<uint16_t>(transformed - 13709u);
  return static_cast<uint16_t>(
      static_cast<uint16_t>(static_cast<uint32_t>(shifted) * inverse) ^ seed);
}

template <class T>
inline T load_unaligned(const std::byte* address) {
  T value;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

template <class T>
inline void store_unaligned(std::byte* address, T value) {
  std::memcpy(address, &value, sizeof(value));
}

constexpr unsigned bits_for_count(unsigned slots) {
  unsigned bits = 0;
  while ((1u << bits) <= slots) ++bits;
  return bits;
}

template <class R>
inline unsigned match_lanes(const R* tags, unsigned slots, unsigned count,
                            R needle, bool sentinel_empties = false) {
  static_assert(std::is_same_v<R, uint8_t> || std::is_same_v<R, uint16_t>);
  const unsigned limit =
      sentinel_empties && needle != std::numeric_limits<R>::max() ? slots
                                                                  : count;
#if defined(__aarch64__) || defined(__ARM_NEON)
  if constexpr (std::is_same_v<R, uint8_t>) {
    static constexpr uint8_t lanes_data[16] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8x16_t lanes = vld1q_u8(lanes_data);
    for (unsigned base = 0; base < slots; base += 16) {
      const unsigned active = std::min(16u, limit > base ? limit - base : 0u);
      if (active == 0) break;
      uint8x16_t equal = vceqq_u8(vld1q_u8(tags + base), vdupq_n_u8(needle));
      equal = vandq_u8(equal, vcleq_u8(lanes, vdupq_n_u8(active)));
      const unsigned lane_number = vmaxvq_u8(vandq_u8(equal, lanes));
      if (lane_number != 0) return base + lane_number - 1;
    }
    return slots;
  } else {
    static constexpr uint16_t lanes_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint16x8_t lanes = vld1q_u16(lanes_data);
    for (unsigned base = 0; base < slots; base += 8) {
      const unsigned active = std::min(8u, limit > base ? limit - base : 0u);
      if (active == 0) break;
      uint16x8_t equal =
          vceqq_u16(vld1q_u16(tags + base), vdupq_n_u16(needle));
      equal = vandq_u16(equal, vcleq_u16(lanes, vdupq_n_u16(active)));
      const unsigned lane_number = vmaxvq_u16(vandq_u16(equal, lanes));
      if (lane_number != 0) return base + lane_number - 1;
    }
    return slots;
  }
#elif defined(__SSE2__)
  if constexpr (std::is_same_v<R, uint8_t>) {
    const __m128i wanted = _mm_set1_epi8(static_cast<char>(needle));
    for (unsigned base = 0; base < slots; base += 16) {
      const unsigned active = std::min(16u, limit > base ? limit - base : 0u);
      if (active == 0) break;
      const __m128i found = _mm_cmpeq_epi8(
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(tags + base)),
          wanted);
      unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(found));
      mask &= active == 16 ? 0xffffu : ((1u << active) - 1u);
      if (mask != 0) return base + static_cast<unsigned>(__builtin_ctz(mask));
    }
    return slots;
  } else {
    const __m128i wanted = _mm_set1_epi16(static_cast<short>(needle));
    for (unsigned base = 0; base < slots; base += 8) {
      const unsigned active = std::min(8u, limit > base ? limit - base : 0u);
      if (active == 0) break;
      const __m128i found = _mm_cmpeq_epi16(
          _mm_loadu_si128(reinterpret_cast<const __m128i*>(tags + base)),
          wanted);
      unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(found));
      mask &= active == 8 ? 0xffffu : ((1u << (active * 2)) - 1u);
      if (mask != 0)
        return base + static_cast<unsigned>(__builtin_ctz(mask)) / 2u;
    }
    return slots;
  }
#else
  for (unsigned lane = 0; lane < limit; ++lane)
    if (tags[lane] == needle) return lane;
  return slots;
#endif
}

template <class R, unsigned Slots>
inline unsigned match_fixed(const R* tags, unsigned count, R needle) {
  static_assert(std::is_same_v<R, uint8_t> || std::is_same_v<R, uint16_t>);
  static_assert(Slots <= 20);
#if defined(__aarch64__) || defined(__ARM_NEON)
  if constexpr (std::is_same_v<R, uint8_t>) {
    alignas(16) static constexpr uint8_t numbered_data[16] = {
        Slots >= 1 ? 1 : 0,   Slots >= 2 ? 2 : 0,
        Slots >= 3 ? 3 : 0,   Slots >= 4 ? 4 : 0,
        Slots >= 5 ? 5 : 0,   Slots >= 6 ? 6 : 0,
        Slots >= 7 ? 7 : 0,   Slots >= 8 ? 8 : 0,
        Slots >= 9 ? 9 : 0,   Slots >= 10 ? 10 : 0,
        Slots >= 11 ? 11 : 0, Slots >= 12 ? 12 : 0,
        Slots >= 13 ? 13 : 0, Slots >= 14 ? 14 : 0,
        Slots >= 15 ? 15 : 0, Slots >= 16 ? 16 : 0};
    uint8x16_t equal = vceqq_u8(vld1q_u8(tags), vdupq_n_u8(needle));
    const uint8x16_t numbered = vld1q_u8(numbered_data);
    if (needle == std::numeric_limits<R>::max())
      equal = vandq_u8(equal, vcleq_u8(numbered, vdupq_n_u8(count)));
    const unsigned lane_number = vmaxvq_u8(vandq_u8(equal, numbered));
    if (lane_number != 0) return lane_number - 1;
    if constexpr (Slots > 16) {
      for (unsigned lane = 16; lane < count; ++lane)
        if (tags[lane] == needle) return lane;
    }
    return Slots;
  } else {
    alignas(16) static constexpr uint16_t numbered_data[8] = {
        Slots >= 1 ? 1 : 0, Slots >= 2 ? 2 : 0, Slots >= 3 ? 3 : 0,
        Slots >= 4 ? 4 : 0, Slots >= 5 ? 5 : 0, Slots >= 6 ? 6 : 0,
        Slots >= 7 ? 7 : 0, Slots >= 8 ? 8 : 0};
    uint16x8_t equal = vceqq_u16(vld1q_u16(tags), vdupq_n_u16(needle));
    const uint16x8_t numbered = vld1q_u16(numbered_data);
    if (needle == std::numeric_limits<R>::max())
      equal = vandq_u16(equal, vcleq_u16(numbered, vdupq_n_u16(count)));
    const unsigned lane_number = vmaxvq_u16(vandq_u16(equal, numbered));
    if (lane_number != 0) return lane_number - 1;
    if constexpr (Slots > 8) {
      for (unsigned lane = 8; lane < count; ++lane)
        if (tags[lane] == needle) return lane;
    }
    return Slots;
  }
#elif defined(__SSE2__)
  if constexpr (std::is_same_v<R, uint8_t>) {
    const __m128i found = _mm_cmpeq_epi8(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(tags)),
        _mm_set1_epi8(static_cast<char>(needle)));
    unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(found));
    constexpr unsigned slot_mask = Slots >= 16 ? 0xffffu : (1u << Slots) - 1u;
    mask &= slot_mask;
    if (needle == std::numeric_limits<R>::max())
      mask &= count >= 16 ? 0xffffu : ((1u << count) - 1u);
    if (mask != 0) return static_cast<unsigned>(__builtin_ctz(mask));
    if constexpr (Slots > 16) {
      for (unsigned lane = 16; lane < count; ++lane)
        if (tags[lane] == needle) return lane;
    }
    return Slots;
  } else {
    const __m128i found = _mm_cmpeq_epi16(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(tags)),
        _mm_set1_epi16(static_cast<short>(needle)));
    unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(found));
    constexpr unsigned first_slots = Slots >= 8 ? 8 : Slots;
    constexpr unsigned slot_mask =
        first_slots == 8 ? 0xffffu : (1u << (first_slots * 2)) - 1u;
    mask &= slot_mask;
    if (needle == std::numeric_limits<R>::max()) {
      const unsigned first_count = std::min(count, 8u);
      mask &= first_count == 8 ? 0xffffu : ((1u << (first_count * 2)) - 1u);
    }
    if (mask != 0) return static_cast<unsigned>(__builtin_ctz(mask)) / 2u;
    if constexpr (Slots > 8) {
      for (unsigned lane = 8; lane < count; ++lane)
        if (tags[lane] == needle) return lane;
    }
    return Slots;
  }
#else
  for (unsigned lane = 0; lane < count; ++lane)
    if (tags[lane] == needle) return lane;
  return Slots;
#endif
}

struct alignas(64) Bucket64 {
  std::byte bytes[64];
};
static_assert(sizeof(Bucket64) == 64);

struct alignas(64) Overflow128 {
  std::byte bytes[128];
};
static_assert(sizeof(Overflow128) == 128);

template <class V, class R, unsigned Slots>
struct BucketOps {
  static_assert(std::is_same_v<R, uint8_t> || std::is_same_v<R, uint16_t>);
  static_assert(sizeof(V) == 2 || sizeof(V) == 4 || sizeof(V) == 8);
  static constexpr bool byte_remainder = std::is_same_v<R, uint8_t>;
  static constexpr bool lean64 =
      byte_remainder && sizeof(V) == 8 && Slots == 7;
  static constexpr bool rich64 =
      byte_remainder && sizeof(V) == 8 && Slots == 6;
  static constexpr unsigned count_bits = bits_for_count(Slots);
  static constexpr unsigned bloom_bits = rich64 ? 32 : 32 - count_bits;
  // Keep the queried Bloom width power-of-two.  The spare generic metadata
  // bits remain reserved; avoiding reduction modulo 27/28 matters on misses.
  static constexpr unsigned filter_bits = rich64 ? 32 : (lean64 ? 4 : 16);

  static constexpr unsigned tag_offset = [] {
    if constexpr (rich64) return 48u;
    if constexpr (lean64) return 56u;
    return 0u;
  }();
  static constexpr unsigned meta_offset = []() -> unsigned {
    if constexpr (byte_remainder && sizeof(V) == 2) return 3u * Slots;
    if constexpr (byte_remainder && sizeof(V) == 4) return 12u;
    if constexpr (rich64) return 54u;
    if constexpr (lean64) return 63u;
    return static_cast<unsigned>(Slots * sizeof(R));
  }();
  static constexpr unsigned value_offset = []() -> unsigned {
    if constexpr (byte_remainder && sizeof(V) == 2) return Slots;
    if constexpr (byte_remainder && sizeof(V) == 4) return 16u;
    if constexpr (rich64) return 0u;
    if constexpr (lean64) return 0u;
    constexpr unsigned after_meta = Slots * sizeof(R) + 4u;
    return static_cast<unsigned>((after_meta + alignof(V) - 1u) &
                                 ~(alignof(V) - 1u));
  }();
  static_assert(tag_offset + Slots * sizeof(R) <= 64);
  static_assert(value_offset + Slots * sizeof(V) <= 64);
  static_assert(rich64    ? meta_offset + 10 <= 64
                : lean64 ? meta_offset + 1 <= 64
                         : meta_offset + 4 <= 64);

  static constexpr uint32_t bloom_bits_mask() {
    if constexpr (lean64)
      return 0x1fu;
    else if constexpr (bloom_bits == 32)
      return UINT32_MAX;
    else
      return (uint32_t{1} << bloom_bits) - 1u;
  }

  static void initialize(Bucket64& bucket) {
    std::memset(&bucket, 0, sizeof(bucket));
    for (unsigned lane = 0; lane < Slots; ++lane)
      set_tag(bucket, lane, std::numeric_limits<R>::max());
  }

  static R tag(const Bucket64& bucket, unsigned lane) {
    return load_unaligned<R>(bucket.bytes + tag_offset + lane * sizeof(R));
  }
  static void set_tag(Bucket64& bucket, unsigned lane, R value) {
    store_unaligned<R>(bucket.bytes + tag_offset + lane * sizeof(R), value);
  }
  static V value(const Bucket64& bucket, unsigned lane) {
    return load_unaligned<V>(bucket.bytes + value_offset + lane * sizeof(V));
  }
  static void set_value(Bucket64& bucket, unsigned lane, V value) {
    store_unaligned<V>(bucket.bytes + value_offset + lane * sizeof(V), value);
  }
  static unsigned count(const Bucket64& bucket) {
    if constexpr (rich64) {
      return static_cast<unsigned>(bucket.bytes[meta_offset]);
    } else if constexpr (lean64) {
      return static_cast<unsigned>(bucket.bytes[meta_offset]) & 7u;
    } else {
      const uint32_t meta = load_unaligned<uint32_t>(bucket.bytes + meta_offset);
      return meta >> bloom_bits;
    }
  }
  static void set_count(Bucket64& bucket, unsigned count_value) {
    if constexpr (rich64) {
      bucket.bytes[meta_offset] = static_cast<std::byte>(count_value);
    } else if constexpr (lean64) {
      const unsigned meta = static_cast<unsigned>(bucket.bytes[meta_offset]);
      bucket.bytes[meta_offset] =
          static_cast<std::byte>((meta & 0xf8u) | count_value);
    } else {
      uint32_t meta = load_unaligned<uint32_t>(bucket.bytes + meta_offset);
      meta = (meta & bloom_bits_mask()) | (count_value << bloom_bits);
      store_unaligned<uint32_t>(bucket.bytes + meta_offset, meta);
    }
  }
  static uint32_t bloom(const Bucket64& bucket) {
    if constexpr (rich64)
      return load_unaligned<uint32_t>(bucket.bytes + meta_offset + 2);
    if constexpr (lean64)
      return static_cast<unsigned>(bucket.bytes[meta_offset]) >> 3;
    return load_unaligned<uint32_t>(bucket.bytes + meta_offset) &
           bloom_bits_mask();
  }
  static void set_bloom(Bucket64& bucket, uint32_t value) {
    value &= bloom_bits_mask();
    if constexpr (rich64) {
      store_unaligned<uint32_t>(bucket.bytes + meta_offset + 2, value);
    } else if constexpr (lean64) {
      const unsigned meta = static_cast<unsigned>(bucket.bytes[meta_offset]);
      bucket.bytes[meta_offset] =
          static_cast<std::byte>((value << 3) | (meta & 7u));
    } else {
      uint32_t meta = load_unaligned<uint32_t>(bucket.bytes + meta_offset);
      meta = (meta & ~bloom_bits_mask()) | value;
      store_unaligned<uint32_t>(bucket.bytes + meta_offset, meta);
    }
  }
  static void set_overflow_count(Bucket64& bucket, unsigned count_value) {
    if constexpr (rich64)
      bucket.bytes[meta_offset + 1] =
          static_cast<std::byte>(std::min(count_value, 255u));
  }
  static void set_ref(Bucket64& bucket, uint32_t reference) {
    if constexpr (rich64)
      store_unaligned<uint32_t>(bucket.bytes + meta_offset + 6, reference);
  }
  static uint32_t ref(const Bucket64& bucket) {
    if constexpr (rich64)
      return load_unaligned<uint32_t>(bucket.bytes + meta_offset + 6);
    return 0;
  }
  UQ_ALWAYS_INLINE static unsigned find(const Bucket64& bucket, R needle) {
    if constexpr (lean64) {
#if defined(__aarch64__) || defined(__ARM_NEON)
      alignas(8) static constexpr uint8_t numbered_data[8] = {1, 2, 3, 4,
                                                              5, 6, 7, 0};
      uint8x8_t equal = vceq_u8(
          vld1_u8(reinterpret_cast<const uint8_t*>(bucket.bytes + tag_offset)),
          vdup_n_u8(needle));
      if (needle == std::numeric_limits<R>::max()) {
        const uint8x8_t numbered = vld1_u8(numbered_data);
        equal = vand_u8(equal,
                        vcle_u8(numbered, vdup_n_u8(count(bucket))));
      }
      const unsigned lane = vmaxv_u8(
          vand_u8(equal, vld1_u8(numbered_data)));
      return lane == 0 ? Slots : lane - 1;
#else
      for (unsigned lane = 0; lane < count(bucket); ++lane)
        if (tag(bucket, lane) == needle) return lane;
      return Slots;
#endif
    }
    const unsigned live = needle == std::numeric_limits<R>::max()
                              ? count(bucket)
                              : Slots;
    const auto* tags = reinterpret_cast<const R*>(bucket.bytes + tag_offset);
    return match_fixed<R, Slots>(tags, live, needle);
  }
  static void append(Bucket64& bucket, R remainder, V value_data) {
    const unsigned lane = count(bucket);
    assert(lane < Slots);
    set_tag(bucket, lane, remainder);
    set_value(bucket, lane, value_data);
    set_count(bucket, lane + 1);
  }
  static void erase_lane(Bucket64& bucket, unsigned lane) {
    const unsigned live = count(bucket);
    assert(lane < live);
    const unsigned last = live - 1;
    if (lane != last) {
      set_tag(bucket, lane, tag(bucket, last));
      set_value(bucket, lane, value(bucket, last));
    }
    set_tag(bucket, last, std::numeric_limits<R>::max());
    set_value(bucket, last, V{});
    set_count(bucket, last);
  }
};

template <class V, size_t N>
struct BatchResult {
  std::array<V, N> values{};
  uint32_t present = 0;
};

#if defined(__aarch64__) || defined(__ARM_NEON)
// A homogeneous vector aggregate is returned in NEON registers by AAPCS64.
// Keeping presence as lanes rather than a scalar bit mask avoids an sret
// buffer and lets callers select an application-specific missing value.
struct NativeBatch8U16 {
  uint16x8_t values;
  uint16x8_t present;
};
#endif

template <class V, unsigned Capacity>
class SmallList {
 public:
  static_assert(Capacity == 16 || Capacity == 32);
  using value_type = V;

  bool insert_or_assign(uint16_t key, V value) {
    const unsigned lane = find_lane(key);
    if (lane != Capacity) {
      values_[lane] = value;
      return false;
    }
    assert(count_ < Capacity);
    keys_[count_] = key;
    values_[count_] = value;
    ++count_;
    return true;
  }
  UQ_ALWAYS_INLINE V* find(uint16_t key) {
    const unsigned lane = find_lane(key);
    return lane == Capacity ? nullptr : &values_[lane];
  }
  UQ_ALWAYS_INLINE const V* find(uint16_t key) const {
    return const_cast<SmallList*>(this)->find(key);
  }
  bool erase(uint16_t key) {
    const unsigned lane = find_lane(key);
    if (lane == Capacity) return false;
    const unsigned last = --count_;
    if (lane != last) {
      keys_[lane] = keys_[last];
      values_[lane] = values_[last];
    }
    keys_[last] = 0;
    values_[last] = V{};
    return true;
  }
  uint32_t size() const { return count_; }
  uint64_t modeled_bytes() const {
    return 4u + Capacity * (sizeof(uint16_t) + sizeof(V));
  }
  uint64_t storage_digest() const {
    uint64_t hash = UINT64_C(1469598103934665603);
    consume(hash, &count_, sizeof(count_));
    consume(hash, keys_.data(), sizeof(keys_));
    consume(hash, values_.data(), sizeof(values_));
    return hash;
  }
  template <size_t N>
  BatchResult<V, N> gather(const uint16_t* keys) const {
    static_assert(N <= 32);
    BatchResult<V, N> result;
    for (size_t lane = 0; lane < N; ++lane) {
      const V* found = find(keys[lane]);
      if (found != nullptr) {
        result.values[lane] = *found;
        result.present |= uint32_t{1} << lane;
      }
    }
    return result;
  }
  template <size_t N>
  uint32_t scatter(const uint16_t* keys, const V* values) {
    static_assert(N <= 32);
    uint32_t inserted = 0;
    for (size_t lane = 0; lane < N; ++lane)
      inserted |= static_cast<uint32_t>(insert_or_assign(keys[lane], values[lane]))
                  << lane;
    return inserted;
  }

 private:
  static void consume(uint64_t& hash, const void* data, size_t bytes) {
    const auto* cursor = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
      hash ^= cursor[i];
      hash *= UINT64_C(1099511628211);
    }
  }
  unsigned find_lane(uint16_t key) const {
    return match_lanes(keys_.data(), Capacity, count_, key);
  }
  alignas(32) std::array<uint16_t, Capacity> keys_{};
  alignas(32) std::array<V, Capacity> values_{};
  unsigned count_ = 0;
};

struct MapStatistics {
  uint32_t buckets = 0;
  uint32_t primary_entries = 0;
  uint32_t overflow_entries = 0;
  uint32_t routed_buckets = 0;
  uint32_t two_page_buckets = 0;
  uint32_t promoted_buckets = 0;
  uint32_t maximum_overflow = 0;
  uint32_t allocated_overflow_blocks = 0;
  uint32_t free_overflow_blocks = 0;
  uint64_t modeled_bytes = 0;
  uint64_t resident_modeled_bytes = 0;
  double bloom_pass_rate_on_misses = 0;
};

template <class V, class R, unsigned Slots>
class QuotientMap {
 public:
  using value_type = V;
  using remainder_type = R;
  using Ops = BucketOps<V, R, Slots>;
  static constexpr unsigned slots = Slots;

  explicit QuotientMap(uint32_t bucket_count, uint16_t seed = 713)
      : primary_(bucket_count),
        directory_(Ops::rich64 ? 0u : bucket_count, 0),
        seed_(seed) {
    assert(bucket_count != 0 && (bucket_count & (bucket_count - 1)) == 0);
    remainder_bits_ = 16u - log2_power_of_two(bucket_count);
    assert(remainder_bits_ <= sizeof(R) * 8u);
    remainder_mask_ = remainder_bits_ == 16
                          ? UINT16_MAX
                          : static_cast<uint16_t>((1u << remainder_bits_) - 1u);
    for (Bucket64& bucket : primary_) Ops::initialize(bucket);
  }

  QuotientMap(const QuotientMap&) = delete;
  QuotientMap& operator=(const QuotientMap&) = delete;

  bool insert_or_assign(uint16_t key, V value) {
    const Parts parts = split(key);
    Bucket64& bucket = primary_[parts.bucket];
    const unsigned lane = Ops::find(bucket, static_cast<R>(parts.remainder));
    if (lane != Slots) {
      Ops::set_value(bucket, lane, value);
      return false;
    }
    if (overflow_assign(parts.bucket, parts.remainder, value)) return false;
    if (Ops::count(bucket) < Slots) {
      Ops::append(bucket, static_cast<R>(parts.remainder), value);
    } else {
      overflow_insert(parts.bucket, parts.remainder, value);
    }
    ++size_;
    return true;
  }

  V* find(uint16_t key) {
    const Parts parts = split(key);
    Bucket64& bucket = primary_[parts.bucket];
    const unsigned lane = Ops::find(bucket, static_cast<R>(parts.remainder));
    if (lane != Slots) return value_pointer(bucket, lane);
    const uint32_t stored_bloom = Ops::bloom(bucket);
    if (stored_bloom == 0) return nullptr;
    const uint32_t wanted = bloom_mask(parts.remainder);
    if ((stored_bloom & wanted) != wanted) return nullptr;
    return overflow_find(parts.bucket, parts.remainder);
  }
  const V* find(uint16_t key) const {
    return const_cast<QuotientMap*>(this)->find(key);
  }

  bool erase(uint16_t key) {
    const Parts parts = split(key);
    Bucket64& bucket = primary_[parts.bucket];
    const unsigned lane = Ops::find(bucket, static_cast<R>(parts.remainder));
    if (lane != Slots) {
      Ops::erase_lane(bucket, lane);
      R moved_remainder{};
      V moved_value{};
      if (overflow_take_one(parts.bucket, moved_remainder, moved_value))
        Ops::append(bucket, moved_remainder, moved_value);
      recompute_bloom(parts.bucket);
      --size_;
      return true;
    }
    if (!overflow_erase(parts.bucket, parts.remainder)) return false;
    recompute_bloom(parts.bucket);
    --size_;
    return true;
  }

  uint32_t size() const { return size_; }
  uint32_t bucket_count() const {
    return static_cast<uint32_t>(primary_.size());
  }
  unsigned remainder_bits() const { return remainder_bits_; }
  uint64_t modeled_bytes() const { return statistics().modeled_bytes; }

  void prefetch_l1(uint16_t key) const {
    const Parts parts = split(key);
    __builtin_prefetch(&primary_[parts.bucket], 0, 3);
  }
  void prefetch_outer(uint16_t key) const {
    const Parts parts = split(key);
    __builtin_prefetch(&primary_[parts.bucket], 0, 1);
  }
  void prefetch_nta(uint16_t key) const {
    const Parts parts = split(key);
    __builtin_prefetch(&primary_[parts.bucket], 0, 0);
  }

  template <size_t N>
  BatchResult<V, N> gather(const uint16_t* keys) const {
    static_assert(N <= 32);
    BatchResult<V, N> result;
    for (size_t lane = 0; lane < N; ++lane) {
      const V* found = find(keys[lane]);
      if (found != nullptr) {
        result.values[lane] = *found;
        result.present |= uint32_t{1} << lane;
      }
    }
    return result;
  }
  template <size_t N>
  uint32_t scatter(const uint16_t* keys, const V* values) {
    static_assert(N <= 32);
    uint32_t inserted = 0;
    for (size_t lane = 0; lane < N; ++lane)
      inserted |= static_cast<uint32_t>(insert_or_assign(keys[lane], values[lane]))
                  << lane;
    return inserted;
  }

#if defined(__aarch64__) || defined(__ARM_NEON)
  NativeBatch8U16 gather8_native(const uint16_t* keys) const {
    static_assert(std::is_same_v<V, uint16_t>);
    return gather8_native_impl(keys, std::make_index_sequence<8>{});
  }
#endif

  uint64_t storage_digest() const {
    uint64_t hash = UINT64_C(1469598103934665603);
    consume(hash, primary_.data(), primary_.size() * sizeof(Bucket64));
    consume(hash, directory_.data(),
            directory_.size() * sizeof(directory_[0]));
    for (uint32_t bucket = 0; bucket < bucket_count(); ++bucket) {
      const uint32_t reference = reference_for(bucket);
      if (reference == 0) continue;
      const size_t index = reference - 1;
      consume(hash, &blocks_[index], sizeof(Overflow128));
      const Micro* micro = micros_[index].get();
      const uint8_t kind = micro == nullptr ? 0 : (micro->dense ? 1 : 2);
      consume(hash, &kind, sizeof(kind));
      if (micro == nullptr) continue;
      if (micro->dense) {
        consume(hash, micro->bitmap.data(),
                micro->bitmap.size() * sizeof(uint64_t));
        consume(hash, micro->dense_values.data(),
                micro->dense_values.size() * sizeof(V));
      } else {
        // std::pair may contain padding whose bytes are not logical state.
        // Hash fields independently so the digest is deterministic too.
        for (const auto& [remainder, value] : micro->sorted) {
          consume(hash, &remainder, sizeof(remainder));
          consume(hash, &value, sizeof(value));
        }
      }
    }
    consume(hash, &seed_, sizeof(seed_));
    consume(hash, &remainder_bits_, sizeof(remainder_bits_));
    consume(hash, &size_, sizeof(size_));
    return hash;
  }

  MapStatistics statistics(const std::vector<uint16_t>& absent = {}) const {
    MapStatistics result;
    result.buckets = bucket_count();
    result.allocated_overflow_blocks = static_cast<uint32_t>(blocks_.size());
    result.free_overflow_blocks = static_cast<uint32_t>(free_blocks_.size());
    result.modeled_bytes = primary_.size() * 64u;
    if constexpr (!Ops::rich64) result.modeled_bytes += directory_.size() * 2u;
    for (uint32_t bucket = 0; bucket < bucket_count(); ++bucket) {
      result.primary_entries += Ops::count(primary_[bucket]);
      const uint32_t reference = reference_for(bucket);
      if (reference == 0) continue;
      ++result.routed_buckets;
      result.modeled_bytes += 128;
      const size_t index = reference - 1;
      const unsigned block_entries = block_count(blocks_[index]);
      const Micro* micro = micros_[index].get();
      unsigned overflow = block_entries;
      if (block_entries > Slots) ++result.two_page_buckets;
      if (micro != nullptr) {
        ++result.promoted_buckets;
        overflow += micro_count(*micro);
        if (micro->dense)
          result.modeled_bytes += micro->bitmap.size() * sizeof(uint64_t) +
                                  micro->dense_values.size() * sizeof(V);
        else
          result.modeled_bytes +=
              micro->sorted.capacity() * sizeof(micro->sorted[0]);
      }
      result.overflow_entries += overflow;
      result.maximum_overflow = std::max(result.maximum_overflow, overflow);
    }
    if (!absent.empty()) {
      uint64_t eligible = 0;
      uint64_t passed = 0;
      for (uint16_t key : absent) {
        const Parts parts = split(key);
        const Bucket64& bucket = primary_[parts.bucket];
        if (Ops::find(bucket, static_cast<R>(parts.remainder)) != Slots)
          continue;
        ++eligible;
        const uint32_t wanted = bloom_mask(parts.remainder);
        passed += (Ops::bloom(bucket) & wanted) == wanted;
      }
      result.bloom_pass_rate_on_misses =
          eligible == 0 ? 0.0
                        : static_cast<double>(passed) /
                              static_cast<double>(eligible);
    }
    result.resident_modeled_bytes =
        result.modeled_bytes + result.free_overflow_blocks * 128u;
    return result;
  }

 private:
#if defined(__aarch64__) || defined(__ARM_NEON)
  template <size_t Lane>
  UQ_ALWAYS_INLINE void gather8_native_lane(const uint16_t* keys,
                                            uint16x8_t& values,
                                            uint16x8_t& present) const {
    const uint16_t* found = find(keys[Lane]);
    values = vsetq_lane_u16(found == nullptr ? 0 : *found, values, Lane);
    present =
        vsetq_lane_u16(found == nullptr ? 0 : UINT16_MAX, present, Lane);
  }

  template <size_t... I>
  UQ_ALWAYS_INLINE NativeBatch8U16 gather8_native_impl(
      const uint16_t* keys, std::index_sequence<I...>) const {
    uint16x8_t values = vdupq_n_u16(0);
    uint16x8_t present = vdupq_n_u16(0);
    (gather8_native_lane<I>(keys, values, present), ...);
    return {values, present};
  }
#endif

  struct Parts {
    uint32_t bucket;
    uint16_t remainder;
  };
  struct Micro {
    bool dense = false;
    std::vector<uint64_t> bitmap;
    std::vector<V> dense_values;
    std::vector<std::pair<uint16_t, V>> sorted;
  };

  static constexpr unsigned block_slots = Ops::lean64 ? 13u : 2u * Slots;
  static constexpr unsigned block_tag_offset = 0;
  static constexpr unsigned block_value_offset =
      ((block_slots * sizeof(R)) + alignof(V) - 1u) & ~(alignof(V) - 1u);
  static constexpr unsigned block_count_offset =
      block_value_offset + block_slots * sizeof(V);
  static_assert(block_count_offset < 128);

  static void consume(uint64_t& hash, const void* data, size_t bytes) {
    const auto* cursor = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
      hash ^= cursor[i];
      hash *= UINT64_C(1099511628211);
    }
  }
  UQ_ALWAYS_INLINE Parts split(uint16_t key) const {
    const uint16_t transformed = permute(key, seed_);
    return {static_cast<uint32_t>(transformed >> remainder_bits_),
            static_cast<uint16_t>(transformed & remainder_mask_)};
  }
  static V* value_pointer(Bucket64& bucket, unsigned lane) {
    return reinterpret_cast<V*>(bucket.bytes + Ops::value_offset +
                                lane * sizeof(V));
  }
  uint32_t bloom_mask(uint16_t remainder) const {
    uint32_t mixed = static_cast<uint32_t>(remainder) * 40503u + 13709u;
    mixed ^= mixed >> 7;
    const unsigned first =
        Ops::lean64 ? mixed % 5u : mixed & (Ops::filter_bits - 1u);
    const unsigned second = Ops::lean64
                                ? (mixed >> 8) % 5u
                                : (mixed >> 8) & (Ops::filter_bits - 1u);
    return (uint32_t{1} << first) | (uint32_t{1} << second);
  }
  uint32_t reference_for(uint32_t bucket) const {
    if constexpr (Ops::rich64) return Ops::ref(primary_[bucket]);
    return directory_[bucket];
  }
  void set_reference(uint32_t bucket, uint32_t reference) {
    if constexpr (!Ops::rich64) {
      assert(reference <= UINT16_MAX);
      directory_[bucket] = static_cast<uint16_t>(reference);
    }
    Ops::set_ref(primary_[bucket], reference);
  }
  static unsigned block_count(const Overflow128& block) {
    return static_cast<unsigned>(block.bytes[block_count_offset]);
  }
  static void set_block_count(Overflow128& block, unsigned count) {
    block.bytes[block_count_offset] = static_cast<std::byte>(count);
  }
  static R block_tag(const Overflow128& block, unsigned lane) {
    return load_unaligned<R>(block.bytes + block_tag_offset + lane * sizeof(R));
  }
  static void set_block_tag(Overflow128& block, unsigned lane, R value) {
    store_unaligned<R>(block.bytes + block_tag_offset + lane * sizeof(R), value);
  }
  static V block_value(const Overflow128& block, unsigned lane) {
    return load_unaligned<V>(block.bytes + block_value_offset + lane * sizeof(V));
  }
  static void set_block_value(Overflow128& block, unsigned lane, V value) {
    store_unaligned<V>(block.bytes + block_value_offset + lane * sizeof(V),
                       value);
  }
  static void initialize_block(Overflow128& block) {
    std::memset(&block, 0, sizeof(block));
    for (unsigned lane = 0; lane < block_slots; ++lane)
      set_block_tag(block, lane, std::numeric_limits<R>::max());
  }
  static unsigned block_find(const Overflow128& block, uint16_t remainder) {
    const unsigned live = block_count(block);
    const auto* tags = reinterpret_cast<const R*>(block.bytes + block_tag_offset);
    return match_lanes(tags, block_slots, live, static_cast<R>(remainder), true);
  }
  static void block_append(Overflow128& block, uint16_t remainder, V value) {
    const unsigned lane = block_count(block);
    assert(lane < block_slots);
    set_block_tag(block, lane, static_cast<R>(remainder));
    set_block_value(block, lane, value);
    set_block_count(block, lane + 1);
  }
  static void block_erase_lane(Overflow128& block, unsigned lane) {
    const unsigned live = block_count(block);
    const unsigned last = live - 1;
    if (lane != last) {
      set_block_tag(block, lane, block_tag(block, last));
      set_block_value(block, lane, block_value(block, last));
    }
    set_block_tag(block, last, std::numeric_limits<R>::max());
    set_block_value(block, last, V{});
    set_block_count(block, last);
  }
  static unsigned micro_count(const Micro& micro) {
    if (!micro.dense) return static_cast<unsigned>(micro.sorted.size());
    unsigned count = 0;
    for (uint64_t word : micro.bitmap)
      count += static_cast<unsigned>(__builtin_popcountll(word));
    return count;
  }
  static V* micro_find(Micro& micro, uint16_t remainder) {
    if (micro.dense) {
      if ((micro.bitmap[remainder >> 6] &
           (UINT64_C(1) << (remainder & 63))) == 0)
        return nullptr;
      return &micro.dense_values[remainder];
    }
    auto found = std::lower_bound(
        micro.sorted.begin(), micro.sorted.end(), remainder,
        [](const auto& entry, uint16_t value) { return entry.first < value; });
    return found != micro.sorted.end() && found->first == remainder
               ? &found->second
               : nullptr;
  }
  static bool micro_insert(Micro& micro, uint16_t remainder, V value) {
    if (micro.dense) {
      const uint64_t mask = UINT64_C(1) << (remainder & 63);
      uint64_t& word = micro.bitmap[remainder >> 6];
      const bool inserted = (word & mask) == 0;
      word |= mask;
      micro.dense_values[remainder] = value;
      return inserted;
    }
    auto found = std::lower_bound(
        micro.sorted.begin(), micro.sorted.end(), remainder,
        [](const auto& entry, uint16_t value_data) {
          return entry.first < value_data;
        });
    if (found != micro.sorted.end() && found->first == remainder) {
      found->second = value;
      return false;
    }
    micro.sorted.insert(found, {remainder, value});
    return true;
  }
  static bool micro_erase(Micro& micro, uint16_t remainder) {
    if (micro.dense) {
      const uint64_t mask = UINT64_C(1) << (remainder & 63);
      uint64_t& word = micro.bitmap[remainder >> 6];
      if ((word & mask) == 0) return false;
      word &= ~mask;
      micro.dense_values[remainder] = V{};
      return true;
    }
    auto found = std::lower_bound(
        micro.sorted.begin(), micro.sorted.end(), remainder,
        [](const auto& entry, uint16_t value_data) {
          return entry.first < value_data;
        });
    if (found == micro.sorted.end() || found->first != remainder) return false;
    micro.sorted.erase(found);
    return true;
  }
  size_t allocate_block(uint32_t bucket) {
    size_t index;
    if (!free_blocks_.empty()) {
      auto smallest = std::min_element(free_blocks_.begin(), free_blocks_.end());
      index = *smallest;
      free_blocks_.erase(smallest);
    } else {
      index = blocks_.size();
      blocks_.emplace_back();
      micros_.push_back(nullptr);
    }
    initialize_block(blocks_[index]);
    micros_[index].reset();
    set_reference(bucket, static_cast<uint32_t>(index + 1));
    return index;
  }
  void release_block(uint32_t bucket, size_t index) {
    initialize_block(blocks_[index]);
    micros_[index].reset();
    set_reference(bucket, 0);
    free_blocks_.push_back(index);
  }
  void promote(size_t index) {
    auto micro = std::make_unique<Micro>();
    // The supported wide tier bottoms out at 32 buckets (11 remainder bits).
    // A bucket-local direct table therefore costs at most 2^11 values.  It is
    // a bounded, exact escape hatch for a deliberately targeted bucket and
    // avoids turning the adversarial path into a long chain or sorted vector.
    micro->dense = remainder_bits_ <= 11;
    if (micro->dense) {
      const size_t universe = size_t{1} << remainder_bits_;
      micro->bitmap.assign((universe + 63) / 64, 0);
      micro->dense_values.assign(universe, V{});
    }
    Overflow128& block = blocks_[index];
    const unsigned live = block_count(block);
    for (unsigned lane = 0; lane < live; ++lane)
      micro_insert(*micro, static_cast<uint16_t>(block_tag(block, lane)),
                   block_value(block, lane));
    initialize_block(block);
    micros_[index] = std::move(micro);
  }
  UQ_NOINLINE V* overflow_find(uint32_t bucket, uint16_t remainder) {
    const uint32_t reference = reference_for(bucket);
    if (reference == 0) return nullptr;
    const size_t index = reference - 1;
    Overflow128& block = blocks_[index];
    const unsigned lane = block_find(block, remainder);
    if (lane != block_slots)
      return reinterpret_cast<V*>(block.bytes + block_value_offset +
                                  lane * sizeof(V));
    Micro* micro = micros_[index].get();
    return micro == nullptr ? nullptr : micro_find(*micro, remainder);
  }
  bool overflow_assign(uint32_t bucket, uint16_t remainder, V value) {
    V* found = overflow_find(bucket, remainder);
    if (found == nullptr) return false;
    *found = value;
    return true;
  }
  void overflow_insert(uint32_t bucket, uint16_t remainder, V value) {
    uint32_t reference = reference_for(bucket);
    const size_t index =
        reference == 0 ? allocate_block(bucket) : reference - 1;
    Micro* micro = micros_[index].get();
    if (micro != nullptr) {
      const bool inserted = micro_insert(*micro, remainder, value);
      assert(inserted);
    } else if (block_count(blocks_[index]) < block_slots) {
      block_append(blocks_[index], remainder, value);
    } else {
      promote(index);
      const bool inserted = micro_insert(*micros_[index], remainder, value);
      assert(inserted);
    }
    Bucket64& primary = primary_[bucket];
    Ops::set_bloom(primary, Ops::bloom(primary) | bloom_mask(remainder));
    Ops::set_overflow_count(primary, overflow_size(bucket));
  }
  bool overflow_erase(uint32_t bucket, uint16_t remainder) {
    const uint32_t reference = reference_for(bucket);
    if (reference == 0) return false;
    const size_t index = reference - 1;
    Overflow128& block = blocks_[index];
    const unsigned lane = block_find(block, remainder);
    if (lane != block_slots) {
      block_erase_lane(block, lane);
      return true;
    }
    Micro* micro = micros_[index].get();
    return micro != nullptr && micro_erase(*micro, remainder);
  }
  bool overflow_take_one(uint32_t bucket, R& remainder, V& value) {
    const uint32_t reference = reference_for(bucket);
    if (reference == 0) return false;
    const size_t index = reference - 1;
    Overflow128& block = blocks_[index];
    const unsigned live = block_count(block);
    if (live != 0) {
      const unsigned lane = live - 1;
      remainder = block_tag(block, lane);
      value = block_value(block, lane);
      block_erase_lane(block, lane);
      return true;
    }
    Micro* micro = micros_[index].get();
    if (micro == nullptr || micro_count(*micro) == 0) return false;
    uint16_t selected = 0;
    if (micro->dense) {
      for (size_t word = 0; word < micro->bitmap.size(); ++word) {
        if (micro->bitmap[word] == 0) continue;
        selected = static_cast<uint16_t>(
            word * 64 + __builtin_ctzll(micro->bitmap[word]));
        break;
      }
    } else {
      selected = micro->sorted.front().first;
    }
    V* found = micro_find(*micro, selected);
    assert(found != nullptr);
    remainder = static_cast<R>(selected);
    value = *found;
    const bool erased = micro_erase(*micro, selected);
    assert(erased);
    return true;
  }
  unsigned overflow_size(uint32_t bucket) const {
    const uint32_t reference = reference_for(bucket);
    if (reference == 0) return 0;
    const size_t index = reference - 1;
    unsigned result = block_count(blocks_[index]);
    if (micros_[index] != nullptr) result += micro_count(*micros_[index]);
    return result;
  }
  void recompute_bloom(uint32_t bucket) {
    Bucket64& primary = primary_[bucket];
    const uint32_t reference = reference_for(bucket);
    if (reference == 0) {
      Ops::set_bloom(primary, 0);
      Ops::set_overflow_count(primary, 0);
      return;
    }
    const size_t index = reference - 1;
    Micro* micro = micros_[index].get();
    if (micro != nullptr) {
      const unsigned micro_entries = micro_count(*micro);
      if (micro_entries != 0) {
        // Promotion means this bucket is already pathological.  Saturating
        // its filter keeps erases O(1) and cannot create a false negative;
        // exact lookup still happens in the block/direct micro-table.  Normal
        // (unpromoted) buckets continue to get an exact rebuilt filter.
        Ops::set_bloom(primary, Ops::bloom_bits_mask());
        Ops::set_overflow_count(primary,
                                block_count(blocks_[index]) + micro_entries);
        return;
      }
      micros_[index].reset();
    }
    uint32_t bloom = 0;
    const Overflow128& block = blocks_[index];
    for (unsigned lane = 0; lane < block_count(block); ++lane)
      bloom |= bloom_mask(static_cast<uint16_t>(block_tag(block, lane)));
    const unsigned count = overflow_size(bucket);
    if (count == 0) {
      release_block(bucket, index);
      Ops::set_bloom(primary, 0);
      Ops::set_overflow_count(primary, 0);
    } else {
      Ops::set_bloom(primary, bloom);
      Ops::set_overflow_count(primary, count);
    }
  }

  std::vector<Bucket64> primary_;
  std::vector<uint16_t> directory_;
  std::vector<Overflow128> blocks_;
  std::vector<std::unique_ptr<Micro>> micros_;
  std::vector<size_t> free_blocks_;
  uint16_t seed_ = 0;
  unsigned remainder_bits_ = 0;
  uint16_t remainder_mask_ = 0;
  uint32_t size_ = 0;
};

using Wide16 = QuotientMap<uint16_t, uint16_t, 15>;
using Wide32 = QuotientMap<uint32_t, uint16_t, 10>;
using Wide64 = QuotientMap<uint64_t, uint16_t, 6>;
using Byte16 = QuotientMap<uint16_t, uint8_t, 20>;
using Byte16x16 = QuotientMap<uint16_t, uint8_t, 16>;
using Byte32 = QuotientMap<uint32_t, uint8_t, 12>;
using Byte64 = QuotientMap<uint64_t, uint8_t, 6>;
using Byte64x7 = QuotientMap<uint64_t, uint8_t, 7>;

}  // namespace unified_quotient
