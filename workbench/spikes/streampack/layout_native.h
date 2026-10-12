#pragma once
#include "layout_hybrid.h"
#include <utility>

// Explicit execution controls for the byte-width layout experiment. A request
// packet is always sixteen ranks. Both compiler vectorizers are disabled for
// this target. These are research bodies, not a new public sidecar interface.
namespace streampack_layout::explicit_native {
enum class Layout { planes, split, grouped, padded, block_planes, block_grouped };
enum class Loads { lanes, gather };

#if defined(__AVX512F__) && defined(__AVX512BW__)
using V = __m512i;
inline constexpr unsigned lanes = 16;
inline V splat(unsigned x) { return _mm512_set1_epi32(x); }
inline V load32(const unsigned* p) { return _mm512_loadu_si512(p); }
inline void store32(unsigned* p, V v) { _mm512_storeu_si512(p, v); }
inline V add(V a, V b) { return _mm512_add_epi32(a, b); }
inline V mul(V a, unsigned b) { return _mm512_mullo_epi32(a, splat(b)); }
inline V band(V a, unsigned b) { return _mm512_and_si512(a, splat(b)); }
inline V bxor(V a, V b) { return _mm512_xor_si512(a, b); }
template<unsigned B> inline V shr(V a) { return _mm512_srli_epi32(a, B); }
inline V shift(V a, V b) { return _mm512_srlv_epi32(a, b); }
inline V live(V values, V dead) { return _mm512_maskz_mov_epi32(_mm512_cmpeq_epi32_mask(dead, splat(0)), values); }
template<class W> inline V contiguous(const std::uint8_t* p) {
  if constexpr (sizeof(W) == 1) return _mm512_cvtepu8_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
  else return _mm512_cvtepu16_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)));
}
inline V gather32(const std::uint8_t* p, V offsets) { return _mm512_i32gather_epi32(offsets, p, 1); }
struct Sum {
  V lo = _mm512_setzero_si512(), hi = _mm512_setzero_si512();
  void consume(V x) {
    lo = _mm512_add_epi64(lo, _mm512_cvtepu32_epi64(_mm512_castsi512_si256(x)));
    hi = _mm512_add_epi64(hi, _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(x, 1)));
  }
  std::uint64_t value() const { return _mm512_reduce_add_epi64(_mm512_add_epi64(lo, hi)); }
};
#elif defined(__AVX2__)
using V = __m256i;
inline constexpr unsigned lanes = 8;
inline V splat(unsigned x) { return _mm256_set1_epi32(x); }
inline V load32(const unsigned* p) { return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)); }
inline void store32(unsigned* p, V v) { _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), v); }
inline V add(V a, V b) { return _mm256_add_epi32(a, b); }
inline V mul(V a, unsigned b) { return _mm256_mullo_epi32(a, splat(b)); }
inline V band(V a, unsigned b) { return _mm256_and_si256(a, splat(b)); }
inline V bxor(V a, V b) { return _mm256_xor_si256(a, b); }
template<unsigned B> inline V shr(V a) { return _mm256_srli_epi32(a, B); }
inline V shift(V a, V b) { return _mm256_srlv_epi32(a, b); }
inline V live(V values, V dead) { return _mm256_and_si256(_mm256_cmpeq_epi32(dead, splat(0)), values); }
template<class W> inline V contiguous(const std::uint8_t* p) {
  if constexpr (sizeof(W) == 1) return _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
  else return _mm256_cvtepu16_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
}
inline V gather32(const std::uint8_t* p, V offsets) { return _mm256_i32gather_epi32(reinterpret_cast<const int*>(p), offsets, 1); }
struct Sum {
  V lo = _mm256_setzero_si256(), hi = _mm256_setzero_si256();
  void consume(V x) {
    lo = _mm256_add_epi64(lo, _mm256_cvtepu32_epi64(_mm256_castsi256_si128(x)));
    hi = _mm256_add_epi64(hi, _mm256_cvtepu32_epi64(_mm256_extracti128_si256(x, 1)));
  }
  std::uint64_t value() const {
    const auto s = _mm256_add_epi64(lo, hi);
    const auto t = _mm_add_epi64(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
    return std::uint64_t(_mm_cvtsi128_si64(t)) + std::uint64_t(_mm_extract_epi64(t, 1));
  }
};
#elif defined(__aarch64__)
using V = uint32x4_t;
inline constexpr unsigned lanes = 4;
inline V splat(unsigned x) { return vdupq_n_u32(x); }
inline V load32(const unsigned* p) { return vld1q_u32(p); }
inline void store32(unsigned* p, V v) { vst1q_u32(p, v); }
inline V add(V a, V b) { return vaddq_u32(a, b); }
inline V mul(V a, unsigned b) { return vmulq_n_u32(a, b); }
inline V band(V a, unsigned b) { return vandq_u32(a, splat(b)); }
inline V bxor(V a, V b) { return veorq_u32(a, b); }
template<unsigned B> inline V shr(V a) { return vshrq_n_u32(a, B); }
inline V shift(V a, V b) { return vshlq_u32(a, vnegq_s32(vreinterpretq_s32_u32(b))); }
inline V live(V values, V dead) { return vandq_u32(vceqq_u32(dead, splat(0)), values); }
template<class W> inline V contiguous(const std::uint8_t* p) {
  if constexpr (sizeof(W) == 1) {
    // Keep this four-byte load in SIMD. The scalar-to-vcreate spelling can
    // lower to ZIP1 using the running sum as irrelevant high-byte filler,
    // creating a false dependency through widening before the mask clears it.
    const auto bytes = vreinterpret_u8_u32(vld1_dup_u32(reinterpret_cast<const std::uint32_t*>(p)));
    return vmovl_u16(vget_low_u16(vmovl_u8(bytes)));
  } else return vmovl_u16(vld1_u16(reinterpret_cast<const std::uint16_t*>(p)));
}
struct Sum {
  uint64x2_t total = vdupq_n_u64(0);
  void consume(V x) { total = vaddq_u64(total, vpaddlq_u32(x)); }
  std::uint64_t value() const { return vaddvq_u64(total); }
};
#else
#error "Explicit layout controls require NEON, AVX2 or AVX-512"
#endif

inline V ranks(unsigned first) {
  alignas(64) static constexpr unsigned indices[16]{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
  return add(splat(first), load32(indices));
}
inline V permute(V x, unsigned mask) {
  x = band(mul(x, 0x85ebca6bu), mask); x = bxor(x, shr<13>(x));
  x = band(mul(x, 0xc2b2ae35u), mask); return bxor(x, shr<7>(x));
}
template<class W, std::size_t... I> inline V lane_loads(const std::uint8_t* bytes, V offsets, std::index_sequence<I...>) {
  alignas(64) unsigned addresses[lanes]; store32(addresses, offsets);
  // An explicit scalar-load control, with one narrow load per requested lane.
  // NEON has no arbitrary-address gather instruction. x86 also measures its
  // hardware gather separately; the compiler does not choose between them.
  alignas(64) const unsigned values[lanes]{load<W>(bytes + addresses[I])...};
  return load32(values);
}
template<class W, Loads Method> inline V indexed(const std::uint8_t* bytes, V offsets) {
  if constexpr (Method == Loads::lanes) return lane_loads<W>(bytes, offsets, std::make_index_sequence<lanes>{});
#if defined(__AVX2__)
  else if constexpr (sizeof(W) == 4) return gather32(bytes, offsets);
  else {
    // Each admitted R/U region and bitmap consists of whole aligned dwords.
    // The gather stays within that field region, including its final lane.
    const auto words = gather32(bytes, band(offsets, ~3u));
    return band(shift(words, mul(band(offsets, 3), 8)), sizeof(W) == 1 ? 255u : 65535u);
  }
#else
  else { static_assert(Method == Loads::lanes, "NEON has no native gather"); }
#endif
}

template<class Meta> struct Traits;
template<unsigned M, Placement P> struct Traits<Metadata<true, M, P>> {
  static_assert(P == Placement::planes || P == Placement::block_planes);
  static constexpr unsigned maximum = M;
  static constexpr Layout layout = P == Placement::planes ? Layout::planes : Layout::block_planes;
  using C = std::uint32_t;
};
template<unsigned M, bool Split, bool Wide, unsigned Padding, bool Block>
struct Traits<HybridWidth<M, Split, Wide, Padding, Block>> {
  static constexpr unsigned maximum = M;
  static constexpr Layout layout = Block ? Layout::block_grouped : Split ? Layout::split : Padding ? Layout::padded : Layout::grouped;
  static_assert(!Block || !Split);
  static_assert(Padding == 0 || Padding == 128);
  static_assert(!(Block || Split) || Padding == 0);
  using C = typename HybridWidth<M, Split, Wide, Padding, Block>::C;
};

template<class Meta> class View {
  static constexpr auto placement = Traits<Meta>::layout;
  static constexpr unsigned width = sizeof(typename Meta::W);
  using C = typename Traits<Meta>::C;
  static constexpr unsigned control_size = 32 + 16 * sizeof(C), length_origin = align64(control_size);
  static constexpr unsigned block_stride = length_origin + 256 * 2 * width;
  static constexpr bool blocked = placement == Layout::block_planes || placement == Layout::block_grouped;
  const std::uint8_t* bytes_;
  unsigned rows_;

  template<unsigned Field> [[gnu::always_inline]] V offsets(V row) const {
    static_assert(Field < 4);
    if constexpr (placement == Layout::planes) {
      if constexpr (Field < 2) return add(splat(Field * rows_ * width), mul(row, width));
      if constexpr (Field == 2) return add(splat(rows_ * 2 * width), shr<3>(row));
      if constexpr (Field == 3) return add(splat(align64(rows_ * 2 * width + rows_ / 8)), mul(shr<4>(row), 4));
    } else if constexpr (placement == Layout::block_planes) {
      const auto block = mul(shr<8>(row), block_stride), local = band(row, 255);
      if constexpr (Field < 2) return add(add(block, splat(Field * 256 * width)), mul(local, width));
      if constexpr (Field == 2) return add(add(block, splat(512 * width)), shr<3>(local));
      if constexpr (Field == 3) return add(add(block, splat(512 * width + 32)), mul(shr<4>(local), 4));
    } else {
      if constexpr (Field < 2) {
        if constexpr (placement == Layout::split) return add(splat(Field * rows_ * width), mul(row, width));
        const auto local = blocked ? band(row, 255) : row;
        const auto within = add(mul(band(local, ~15u), 2 * width), mul(band(local, 15), width));
        const auto origin = blocked ? add(mul(shr<8>(row), block_stride), splat(length_origin)) : splat(0);
        return add(add(origin, within), splat(Field * 16 * width));
      } else {
        constexpr unsigned stride = blocked ? block_stride : placement == Layout::padded ? 128 : control_size;
        const auto origin = add(splat(blocked ? 0 : rows_ * 2 * width), mul(shr<8>(row), stride));
        if constexpr (Field == 2) return add(origin, shr<3>(band(row, 255)));
        if constexpr (Field == 3) return add(add(origin, splat(32)), mul(band(shr<4>(row), 15), sizeof(C)));
      }
    }
  }
  template<unsigned Field> unsigned scalar_offset(unsigned row) const {
    // Only the ordered path needs one scalar address per native subpacket.
    // This definition is independently checked against the actual adapters.
    if constexpr (placement == Layout::planes) {
      if constexpr (Field < 2) return Field * rows_ * width + row * width;
      if constexpr (Field == 2) return rows_ * 2 * width + row / 8;
      if constexpr (Field == 3) return align64(rows_ * 2 * width + rows_ / 8) + row / 16 * 4;
    } else if constexpr (placement == Layout::block_planes) {
      const auto origin = row / 256 * block_stride, local = row % 256;
      if constexpr (Field < 2) return origin + Field * 256 * width + local * width;
      if constexpr (Field == 2) return origin + 512 * width + local / 8;
      if constexpr (Field == 3) return origin + 512 * width + 32 + local / 16 * 4;
    } else {
      if constexpr (Field < 2) {
        if constexpr (placement == Layout::split) return Field * rows_ * width + row * width;
        const auto local = blocked ? row % 256 : row;
        return (blocked ? row / 256 * block_stride + length_origin : 0) + (local / 16 * 32 + local % 16 + Field * 16) * width;
      } else {
        constexpr unsigned stride = blocked ? block_stride : placement == Layout::padded ? 128 : control_size;
        const auto origin = (blocked ? 0 : rows_ * 2 * width) + row / 256 * stride;
        if constexpr (Field == 2) return origin + row % 256 / 8;
        if constexpr (Field == 3) return origin + 32 + row % 256 / 16 * sizeof(C);
      }
    }
  }
public:
  View(const std::uint8_t* bytes, unsigned rows) : bytes_(bytes), rows_(rows) {}
  template<unsigned Field, Loads Method> [[gnu::always_inline]] V random(V row) const {
    if constexpr (Field < 2) return indexed<typename Meta::W, Method>(bytes_, offsets<Field>(row));
    if constexpr (Field == 2) return band(shift(indexed<std::uint8_t, Method>(bytes_, offsets<2>(row)), band(row, 7)), 1);
    if constexpr (Field == 3) return indexed<C, Method>(bytes_, offsets<3>(row));
    if constexpr (Field == 4) return live(random<1, Method>(row), random<2, Method>(row));
    if constexpr (Field == 5) return add(random<0, Method>(row), random<1, Method>(row));
  }
  template<unsigned Field> [[gnu::always_inline]] V ordered(unsigned first) const {
    // first is aligned to the native lane count, which divides16. Thus neither
    // the narrow contiguous read nor the tombstone read crosses a group.
    if constexpr (Field < 2) return contiguous<typename Meta::W>(bytes_ + scalar_offset<Field>(first));
    if constexpr (Field == 2) {
      const auto address = scalar_offset<2>(first & ~15u);
      const auto bits = load<std::uint16_t>(bytes_ + address);
      return band(shift(splat(bits), add(ranks(0), splat(first % 16))), 1);
    }
    if constexpr (Field == 3) return splat(load<C>(bytes_ + scalar_offset<3>(first)));
    if constexpr (Field == 4) return live(ordered<1>(first), ordered<2>(first));
    if constexpr (Field == 5) return add(ordered<0>(first), ordered<1>(first));
  }
};

template<unsigned Field, bool Shuffled, Loads Method, class Meta>
[[gnu::always_inline]] inline void consume(Sum& sum, const View<Meta>& view, unsigned first, unsigned mask) {
  if constexpr (Shuffled) sum.consume(view.template random<Field, Method>(permute(ranks(first), mask)));
  else sum.consume(view.template ordered<Field>(first));
}

template<unsigned Field, bool Shuffled, Loads Method, class Meta>
[[gnu::noinline]] std::uint64_t scan(const View<Meta>& view, unsigned rows) {
  Sum sum;
  for (unsigned first = 0; first < rows; first += 16) {
    consume<Field, Shuffled, Method>(sum, view, first, rows - 1);
    if constexpr (lanes < 16) consume<Field, Shuffled, Method>(sum, view, first + lanes, rows - 1);
    if constexpr (lanes == 4) {
      consume<Field, Shuffled, Method>(sum, view, first + 8, rows - 1);
      consume<Field, Shuffled, Method>(sum, view, first + 12, rows - 1);
    }
  }
  return sum.value();
}
} // namespace streampack_layout::explicit_native
