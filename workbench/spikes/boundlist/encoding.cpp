// Geometry experiment only. Value usage and payload movement are measured by
// reads/mutation/packing, not attributed to this codec comparison.
#include "scan-count.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ikea/boundlist/search.h>
#include <limits>
#include <string>
#include <vector>

namespace bl = ikea::boundlist;
enum class codec {
  direct,
  widths,
  relative,
  direct_checkpoints,
  fused_scan,
  full_prefix_sizes
};
template <class T> T load(const std::byte *p) {
  T v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}
template <class T> void store(std::byte *p, T v) {
  std::memcpy(p, &v, sizeof(v));
}
unsigned width_for(unsigned node, unsigned rank, unsigned maximum) {
  return rank % 11 != 10
             ? ((rank * 193u + node * 311u + 7u) ^ (rank * 17u)) % (maximum + 1)
             : 0;
}
// Schema-bounded, branch-free local sizes control. Widen when G16 can
// overflow uint16; no decoded temporary array is materialized.
[[gnu::always_inline]] unsigned
full_prefix_owner(const std::uint16_t *p, unsigned point, unsigned sum_bound) {
  if (sum_bound <= 65535) {
#if defined(__AVX2__)
    auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
    x = _mm256_add_epi16(x, _mm256_slli_si256(x, 2));
    x = _mm256_add_epi16(x, _mm256_slli_si256(x, 4));
    x = _mm256_add_epi16(x, _mm256_slli_si256(x, 8));
    const auto carry =
        _mm_set1_epi16(_mm_extract_epi16(_mm256_castsi256_si128(x), 7));
    x = _mm256_add_epi16(
        x, _mm256_inserti128_si256(_mm256_setzero_si256(), carry, 1));
    const auto sign = _mm256_set1_epi16(INT16_MIN);
    const auto gt =
        _mm256_cmpgt_epi16(_mm256_xor_si256(x, sign),
                           _mm256_xor_si256(_mm256_set1_epi16(point), sign));
    return std::countr_zero(unsigned(_mm256_movemask_epi8(gt))) / 2;
#elif defined(__aarch64__)
    const auto zero = vdupq_n_u16(0);
    auto lo = vld1q_u16(p), hi = vld1q_u16(p + 8);
    lo = vaddq_u16(lo, vextq_u16(zero, lo, 7));
    hi = vaddq_u16(hi, vextq_u16(zero, hi, 7));
    lo = vaddq_u16(lo, vextq_u16(zero, lo, 6));
    hi = vaddq_u16(hi, vextq_u16(zero, hi, 6));
    lo = vaddq_u16(lo, vextq_u16(zero, lo, 4));
    hi = vaddq_u16(hi, vextq_u16(zero, hi, 4));
    hi = vaddq_u16(hi, vdupq_n_u16(vgetq_lane_u16(lo, 7)));
    const auto q = vdupq_n_u16(point);
    return vaddvq_u16(vaddq_u16(vshrq_n_u16(vcleq_u16(lo, q), 15),
                                vshrq_n_u16(vcleq_u16(hi, q), 15)));
#endif
  }
#if defined(__aarch64__) || defined(__AVX2__)
  unsigned carry = 0, mask = 0;
  for (unsigned i = 0; i < 16; i += bl::native::lanes) {
    const auto sums =
        bl::native::add(bl::native::prefix(bl::native::load<std::uint16_t>(
                            reinterpret_cast<const std::byte *>(p + i))),
                        bl::native::splat(carry));
    mask |= bl::native::greater(sums, point) << i;
    carry = bl::native::last(sums);
  }
  return std::countr_zero(mask | 0x10000u);
#else
  return bl::detail::owner16_native(p, point);
#endif
}
// Every store preserves inactive neighbours. Complete initialized G16 backing
// admits these vector windows; there is no scalar-loop vectorizer dependency.
template <class T>
void add_suffix(std::byte *bytes, unsigned first, unsigned limit,
                unsigned padded, int delta) {
  auto *p = reinterpret_cast<T *>(bytes);
  unsigned i;
#if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
  constexpr unsigned wide = 64 / sizeof(T);
  i = first / wide * wide;
  for (; i < limit && i + wide <= padded; i += wide) {
    const auto x = _mm512_loadu_si512(p + i);
    if constexpr (sizeof(T) == 2) {
      const auto pos = _mm512_add_epi16(
          _mm512_set1_epi16(i),
          _mm512_set_epi16(31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,
                           18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4,
                           3, 2, 1, 0));
      const auto mask = _mm512_cmpge_epi16_mask(pos, _mm512_set1_epi16(first)) &
                        _mm512_cmplt_epi16_mask(pos, _mm512_set1_epi16(limit));
      _mm512_storeu_si512(
          p + i, _mm512_mask_add_epi16(x, mask, x, _mm512_set1_epi16(delta)));
    } else {
      const auto pos = _mm512_add_epi32(
          _mm512_set1_epi32(i), _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                  10, 11, 12, 13, 14, 15));
      const auto mask = _mm512_cmpge_epi32_mask(pos, _mm512_set1_epi32(first)) &
                        _mm512_cmplt_epi32_mask(pos, _mm512_set1_epi32(limit));
      _mm512_storeu_si512(
          p + i, _mm512_mask_add_epi32(x, mask, x, _mm512_set1_epi32(delta)));
    }
  }
#elif defined(__AVX2__)
  i = first / (32 / sizeof(T)) * (32 / sizeof(T));
#else
  i = first / (16 / sizeof(T)) * (16 / sizeof(T));
#endif
#if defined(__AVX2__)
  constexpr unsigned lanes = 32 / sizeof(T);
  for (; i < limit && i + lanes <= padded; i += lanes) {
    const auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p + i));
    if constexpr (sizeof(T) == 2) {
      const auto pos = _mm256_add_epi16(
          _mm256_set1_epi16(i), _mm256_setr_epi16(0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                  10, 11, 12, 13, 14, 15));
      const auto mask = _mm256_and_si256(
          _mm256_cmpgt_epi16(pos, _mm256_set1_epi16(int(first) - 1)),
          _mm256_cmpgt_epi16(_mm256_set1_epi16(limit), pos));
      _mm256_storeu_si256(
          reinterpret_cast<__m256i *>(p + i),
          _mm256_add_epi16(x,
                           _mm256_and_si256(mask, _mm256_set1_epi16(delta))));
    } else {
      const auto pos = _mm256_add_epi32(
          _mm256_set1_epi32(i), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
      const auto mask = _mm256_and_si256(
          _mm256_cmpgt_epi32(pos, _mm256_set1_epi32(int(first) - 1)),
          _mm256_cmpgt_epi32(_mm256_set1_epi32(limit), pos));
      _mm256_storeu_si256(
          reinterpret_cast<__m256i *>(p + i),
          _mm256_add_epi32(x,
                           _mm256_and_si256(mask, _mm256_set1_epi32(delta))));
    }
  }
#elif defined(__aarch64__)
  constexpr unsigned lanes = 16 / sizeof(T);
  for (; i < limit && i + lanes <= padded; i += lanes) {
    if constexpr (sizeof(T) == 2) {
      const uint16x8_t indices{0, 1, 2, 3, 4, 5, 6, 7};
      const auto pos = vaddq_u16(vdupq_n_u16(i), indices);
      const auto mask = vandq_u16(vcgeq_u16(pos, vdupq_n_u16(first)),
                                  vcltq_u16(pos, vdupq_n_u16(limit)));
      vst1q_u16(p + i, vaddq_u16(vld1q_u16(p + i),
                                 vandq_u16(mask, vdupq_n_u16(delta))));
    } else {
      const uint32x4_t indices{0, 1, 2, 3};
      const auto pos = vaddq_u32(vdupq_n_u32(i), indices);
      const auto mask = vandq_u32(vcgeq_u32(pos, vdupq_n_u32(first)),
                                  vcltq_u32(pos, vdupq_n_u32(limit)));
      vst1q_u32(p + i, vaddq_u32(vld1q_u32(p + i),
                                 vandq_u32(mask, vdupq_n_u32(delta))));
    }
  }
#else
  for (i = first; i < limit; ++i)
    store(bytes + i * sizeof(T), T(load<T>(bytes + i * sizeof(T)) + delta));
#endif
  assert(i >= limit);
}
template <codec C, class W, class O, class L> struct image {
  static constexpr bool direct = C == codec::direct || C == codec::fused_scan ||
                                 C == codec::direct_checkpoints;
  static constexpr bool sizes =
      C == codec::widths || C == codec::full_prefix_sizes;
  static constexpr bool cp =
      sizes || C == codec::relative || C == codec::direct_checkpoints;
  using slot =
      std::conditional_t<direct, O,
                         std::conditional_t<C == codec::relative, L, W>>;
  unsigned n, nodes, maximum, data_offset, checkpoint_offset, control_offset,
      stride, encoded;
  std::vector<std::byte> bytes;
  image(unsigned count, unsigned population, unsigned max)
      : n(count), nodes(population), maximum(max) {
    checkpoint_offset = 32;
    data_offset = 32 + unsigned(cp) * 16 * sizeof(O);
    control_offset =
        data_offset + (direct ? 256 : ((n + 15) / 16) * 16) * sizeof(slot);
    encoded = control_offset + 2 + unsigned(!direct) * 4;
    stride = (encoded + 7) & ~7u;
    bytes.resize(std::size_t(stride) * nodes);
    for (unsigned node = 0; node < nodes; ++node)
      rebuild(node);
  }
  std::byte *base(unsigned node) {
    return bytes.data() + std::size_t(node) * stride;
  }
  const std::byte *base(unsigned node) const {
    return bytes.data() + std::size_t(node) * stride;
  }
  const O *checkpoints(unsigned node) const {
    return reinterpret_cast<const O *>(base(node) + checkpoint_offset);
  }
  const slot *values(unsigned node) const {
    return reinterpret_cast<const slot *>(base(node) + data_offset);
  }
  [[gnu::always_inline]] unsigned extent(unsigned node) const {
    if constexpr (direct)
      return load<O>(base(node) + data_offset + (n - 1) * sizeof(O));
    else
      return load<unsigned>(base(node) + control_offset + 2);
  }
  void rebuild(unsigned node) {
    auto *p = base(node);
    // The same full-capacity directory convention as the curated sidecar.
    std::memset(p, 0, data_offset + (direct ? 256 * sizeof(O) : 0));
    unsigned end = 0, origin = 0;
    for (unsigned r = 0; r < n; ++r) {
      if constexpr (cp)
        if (!(r % 16)) {
          origin = end;
          store(p + checkpoint_offset + (r / 16) * sizeof(O), O(end));
        }
      if constexpr (!direct)
        if (!(r % 16))
          std::memset(p + data_offset + r * sizeof(slot), 0, 16 * sizeof(slot));
      const auto width = width_for(node, r, maximum);
      end += width;
      const unsigned value = direct                 ? end
                             : C == codec::relative ? end - origin
                                                    : width;
      store(p + data_offset + r * sizeof(slot), slot(value));
      if (!(r % 7))
        p[r / 8] |= std::byte(1u << (r % 8));
    }
    store(p + control_offset, std::uint16_t(n));
    if constexpr (!direct)
      store(p + control_offset + 2, end);
  }
  [[gnu::always_inline]] unsigned start(unsigned node, unsigned rank) const {
    const auto *v = values(node);
    if constexpr (direct)
      return rank ? bl::detail::read_element(v, rank - 1) : 0;
    else {
      const auto origin =
          bl::detail::read_element(checkpoints(node), rank / 16);
      if constexpr (C == codec::relative)
        return origin + (rank % 16 ? bl::detail::read_element(v, rank - 1) : 0);
      else
        return origin +
               bl::detail::prefix16_native(v + (rank / 16) * 16, rank % 16);
    }
  }
  [[gnu::always_inline]] unsigned reserved(unsigned node, unsigned rank) const {
    const auto *v = values(node);
    if constexpr (sizes)
      return bl::detail::read_element(v, rank);
    else if constexpr (C == codec::relative)
      return bl::detail::read_element(v, rank) -
             (rank % 16 ? bl::detail::read_element(v, rank - 1) : 0);
    else
      return bl::detail::read_element(v, rank) - start(node, rank);
  }
  [[gnu::always_inline]] unsigned local_after(unsigned node,
                                              unsigned threshold) const {
    const auto next = bl::detail::checkpoint_boundary<bl::edge::after, 16>(
        checkpoints(node), (n + 15) / 16, O(threshold));
    const auto group = next - 1, first = group * 16;
    if constexpr (sizes) {
      const auto relative =
          threshold - bl::detail::read_element(checkpoints(node), group);
      if constexpr (C == codec::full_prefix_sizes && sizeof(W) == 2)
        return std::min(n, first +
                               full_prefix_owner(values(node) + first, relative,
                                                 std::min(16u, n) * maximum));
      else
        return std::min(n, first + bl::detail::owner16_native(
                                       values(node) + first, relative));
    } else {
      const unsigned local =
          C == codec::relative
              ? threshold - bl::detail::read_element(checkpoints(node), group)
              : threshold;
      const auto count = std::min(16u, n - first);
      const auto mask = bl::detail::native_mask<bl::edge::after>(
                            values(node) + first, 16, slot(local)) &
                        unsigned(bl::detail::low_bits(count));
      return mask ? first + std::countr_zero(mask) : n;
    }
  }
  template <bl::edge E>
  [[gnu::always_inline]] unsigned boundary(unsigned node,
                                           unsigned point) const {
    if constexpr (C == codec::fused_scan)
      return boundlist_count_control::scan<E == bl::edge::at_or_after>(
          values(node), n, O(point));
    else if constexpr (C == codec::direct)
      return bl::detail::hybrid_array<E, true>(values(node), n, O(point));
    else {
      // Origin checkpoints need a predecessor. Inclusive searches at zero
      // are a distinct domain case, including leading zero widths.
      if constexpr (E == bl::edge::at_or_after) {
        if (!point)
          return 0;
      }
      const unsigned threshold = point - unsigned(E == bl::edge::at_or_after);
      if (threshold >= extent(node))
        return n;
      return local_after(node, threshold);
    }
  }
  [[gnu::always_inline]] unsigned owner(unsigned node, unsigned point) const {
    if (point >= extent(node))
      return n;
    if constexpr (cp)
      return local_after(node, point);
    else
      return boundary<bl::edge::after>(node, point);
  }
  void change(unsigned node, unsigned rank) {
    auto *p = base(node);
    const auto old = reserved(node, rank);
    const auto replacement = old == maximum && !(maximum & 1) ? old : old ^ 1u;
    const int delta = int(replacement) - int(old);
    if constexpr (sizes)
      store(p + data_offset + rank * sizeof(W), W(replacement));
    else {
      const auto limit = direct ? n : std::min(n, (rank / 16 + 1) * 16);
      add_suffix<slot>(p + data_offset, rank, limit,
                       direct ? 256 : ((n + 15) / 16) * 16, delta);
    }
    if constexpr (cp)
      if (rank / 16 + 1 < (n + 15) / 16)
        add_suffix<O>(p + checkpoint_offset, rank / 16 + 1, (n + 15) / 16, 16,
                      delta);
    if constexpr (!direct) {
      auto *at = p + control_offset + 2;
      store(at, unsigned(load<unsigned>(at) + delta));
    }
  }
  void verify() const {
    for (unsigned node = 0; node < std::min(nodes, 8u); ++node) {
      std::vector<unsigned> ends;
      unsigned end = 0;
      for (unsigned r = 0; r < n; ++r) {
        assert(start(node, r) == end &&
               reserved(node, r) == width_for(node, r, maximum));
        end += width_for(node, r, maximum);
        ends.push_back(end);
      }
      assert(extent(node) == end);
      auto check = [&](unsigned point) {
        assert(boundary<bl::edge::after>(node, point) ==
               unsigned(std::upper_bound(ends.begin(), ends.end(), point) -
                        ends.begin()));
        assert(boundary<bl::edge::at_or_after>(node, point) ==
               unsigned(std::lower_bound(ends.begin(), ends.end(), point) -
                        ends.begin()));
      };
      check(0);
      check(end);
      if (end < std::numeric_limits<O>::max())
        check(end + 1);
      for (auto x : ends) {
        check(x);
        if (x)
          check(x - 1);
      }
    }
  }
  void verify_changes() {
    if constexpr (C == codec::fused_scan)
      return;
    for (unsigned node = 0; node < std::min(nodes, 2u); ++node) {
      const std::vector<std::byte> original(base(node), base(node) + stride);
      for (unsigned changed = 0; changed < n; ++changed) {
        change(node, changed);
        unsigned position = 0;
        for (unsigned r = 0; r < n; ++r) {
          auto width = width_for(node, r, maximum);
          if (r == changed && ((maximum & 1) || width != maximum))
            width ^= 1u;
          assert(start(node, r) == position && reserved(node, r) == width);
          if (width) {
            assert(owner(node, position) == r);
            assert(owner(node, position + width - 1) == r);
          }
          position += width;
        }
        assert(extent(node) == position && owner(node, position) == n);
        change(node, changed);
        assert(std::equal(original.begin(), original.end(), base(node)));
      }
    }
  }
  void verify_empty_groups() {
    auto *p = base(0);
    const std::vector<std::byte> original(p, p + stride);
    std::memset(p + checkpoint_offset, 0, control_offset - checkpoint_offset);
    store(p + data_offset + (n - 1) * sizeof(slot), slot(1));
    if constexpr (!direct)
      store(p + control_offset + 2, 1u);
    assert(boundary<bl::edge::after>(0, 0) == n - 1);
    assert(boundary<bl::edge::at_or_after>(0, 0) == 0);
    assert(boundary<bl::edge::at_or_after>(0, 1) == n - 1);
    assert(owner(0, 0) == n - 1 && owner(0, 1) == n);
    store(p + data_offset + (n - 1) * sizeof(slot), slot(0));
    if constexpr (!direct)
      store(p + control_offset + 2, 0u);
    assert(boundary<bl::edge::after>(0, 0) == n);
    assert(boundary<bl::edge::at_or_after>(0, 0) == 0);
    assert(owner(0, 0) == n);
    std::copy(original.begin(), original.end(), p);
  }
};
struct request {
  unsigned node, rank, point, high;
};
std::uint64_t random_word(std::uint64_t &x) {
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  return x;
}
volatile std::uint64_t observed;
template <unsigned Op, unsigned Chains, class Image>
[[gnu::noinline]] std::uint64_t
consume(Image &data, const std::vector<request> &requests, unsigned rounds) {
  std::array<std::uint64_t, Chains> state{};
  for (unsigned c = 0; c < Chains; ++c)
    state[c] = c * 7919 + 1;
  for (unsigned round = 0; round < rounds; ++round) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      auto one = [&](auto c) __attribute__((always_inline)) {
        const auto &q = requests[state[c] & (requests.size() - 1)];
        // Select the block from the live dependency, not a tiny fixed
        // request table's node IDs: large populations must be reachable.
        const unsigned node = unsigned(state[c] >> 32) & (data.nodes - 1);
        std::uint64_t answer;
        if constexpr (Op == 0)
          answer = (std::uint64_t(data.start(node, q.rank)) << 16) |
                   data.reserved(node, q.rank);
        else if constexpr (Op == 1)
          answer = data.template boundary<bl::edge::after>(node, q.point);
        else if constexpr (Op == 2)
          answer = (std::uint64_t(
                        data.template boundary<bl::edge::after>(node, q.point))
                    << 16) |
                   data.template boundary<bl::edge::at_or_after>(node, q.high);
        else if constexpr (Op == 3) {
          const auto r = data.owner(node, q.point);
          answer = r == data.n
                       ? r
                       : r | (std::uint64_t((std::to_integer<unsigned>(
                                                 data.base(node)[r / 8]) >>
                                             (r % 8)) &
                                            1)
                              << 16);
        } else if constexpr (Op == 6) {
          answer = q.point + q.rank + node;
        } else if constexpr (Op == 4) {
          data.rebuild(node);
          asm volatile("" ::: "memory");
          answer = data.extent(node);
        } else {
          data.change(node, q.rank);
          asm volatile("" ::: "memory");
          answer = data.extent(node);
        }
        state[c] = std::rotl(state[c], 7) + answer * 0x9e3779b1u + 1;
      };
      (one(std::integral_constant<unsigned, I>{}), ...);
    }(std::make_index_sequence<Chains>{});
  }
  std::uint64_t result = 0;
  for (auto s : state)
    result ^= s;
  return result;
}
template <codec C, class W, class O, class L>
void panel(unsigned n, unsigned nodes, unsigned maximum, unsigned operations,
           unsigned reps) {
  image<C, W, O, L> data(n, nodes, maximum);
  data.verify();
  data.verify_changes();
  data.verify_empty_groups();
  std::vector<request> requests(256);
  std::uint64_t seed = 713;
  for (auto &q : requests) {
    q.node = random_word(seed) % nodes;
    q.rank = random_word(seed) % n;
    const auto extent = data.extent(q.node);
    q.point = extent ? random_word(seed) % extent : 0;
    q.high = extent ? q.point + random_word(seed) % (extent - q.point + 1) : 0;
  }
  constexpr const char *names[] = {"direct",
                                   "sizes",
                                   "relative_ends",
                                   "direct_checkpoints",
                                   "fused_direct_scan",
                                   "sizes_full_prefix"};
  constexpr const char *ops[] = {
      "point_geometry",   "upper_bound",     "two_extrema",    "reverse",
      "rebuild_geometry", "change_geometry", "request_control"};
  auto run = [&]<unsigned Op, unsigned Chains>(unsigned rep) {
    // Mutating trials start from the same admitted state, outside timing.
    if constexpr (Op == 4 || Op == 5)
      for (unsigned node = 0; node < nodes; ++node)
        data.rebuild(node);
    else
      observed = consume<Op, Chains>(data, requests, 4096 / Chains);
    const unsigned rounds =
        Op == 4 ? std::max(8u, operations / (n * Chains)) : operations / Chains;
    const auto start = std::chrono::steady_clock::now();
    const auto result = consume<Op, Chains>(data, requests, rounds);
    const auto finish = std::chrono::steady_clock::now();
    observed = result;
    std::printf("%s,%u,%u,%u,%s,%u,%u,%.6f,%u,%zu,%llu\n", names[unsigned(C)],
                maximum, n, nodes, ops[Op], Chains, rep,
                double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           finish - start)
                           .count()) /
                    (rounds * Chains),
                data.encoded, data.bytes.size(), (unsigned long long)result);
  };
  for (unsigned rep = 0; rep < reps; ++rep) {
    if (nodes <= 4096)
      for (unsigned node = 0; node < nodes; ++node)
        data.rebuild(node);
    if constexpr (C == codec::direct) {
      run.template operator()<6, 1>(rep);
      run.template operator()<6, 8>(rep);
    }
    run.template operator()<0, 1>(rep);
    run.template operator()<0, 8>(rep);
    run.template operator()<1, 1>(rep);
    run.template operator()<1, 8>(rep);
    run.template operator()<2, 1>(rep);
    run.template operator()<2, 8>(rep);
    run.template operator()<3, 1>(rep);
    run.template operator()<3, 8>(rep);
    if constexpr (C != codec::fused_scan)
      if (nodes <= 4096) {
        run.template operator()<4, 1>(rep);
        run.template operator()<4, 8>(rep);
        run.template operator()<5, 1>(rep);
        run.template operator()<5, 8>(rep);
      }
  }
}
template <class W, class O, class L>
void formats(unsigned n, unsigned nodes, unsigned max, unsigned ops,
             unsigned reps, bool reverse) {
  auto one = [&]<codec C>() { panel<C, W, O, L>(n, nodes, max, ops, reps); };
  for (unsigned i = 0; i < 6; ++i)
    switch (reverse ? 5 - i : i) {
    case 0:
      one.template operator()<codec::direct>();
      break;
    case 1:
      one.template operator()<codec::widths>();
      break;
    case 2:
      one.template operator()<codec::relative>();
      break;
    case 3:
      one.template operator()<codec::direct_checkpoints>();
      break;
    case 4:
      one.template operator()<codec::fused_scan>();
      break;
    case 5:
      one.template operator()<codec::full_prefix_sizes>();
      break;
    }
}
int main(int argc, char **argv) {
  const unsigned n = argc > 1 ? std::stoul(argv[1]) : 256,
                 nodes = argc > 2 ? std::stoul(argv[2]) : 1;
  const unsigned ops = argc > 3 ? std::stoul(argv[3]) : 262144,
                 reps = argc > 4 ? std::stoul(argv[4]) : 5;
  const bool reverse = argc > 5 ? std::stoul(argv[5]) : false;
  if (!n || n > 256 || !std::has_single_bit(nodes) || !reps || ops < 2048)
    return 2;
  std::puts("format,maximum,count,nodes,operation,chains,rep,ns,encoded_bytes,"
            "geometry_backing_bytes,checksum");
  if (n * 255 <= 65535)
    formats<std::uint8_t, std::uint16_t, std::uint16_t>(n, nodes, 255, ops,
                                                        reps, reverse);
  if (n * 4095 <= 65535)
    formats<std::uint16_t, std::uint16_t, std::uint16_t>(n, nodes, 4095, ops,
                                                         reps, reverse);
  else
    formats<std::uint16_t, std::uint32_t, std::uint16_t>(n, nodes, 4095, ops,
                                                         reps, reverse);
  if (n * 4096 <= 65535)
    formats<std::uint16_t, std::uint16_t, std::uint16_t>(n, nodes, 4096, ops,
                                                         reps, reverse);
  else
    formats<std::uint16_t, std::uint32_t, std::uint32_t>(n, nodes, 4096, ops,
                                                         reps, reverse);
  if (n == 1)
    formats<std::uint16_t, std::uint16_t, std::uint16_t>(n, nodes, 65535, ops,
                                                         reps, reverse);
  else
    formats<std::uint16_t, std::uint32_t, std::uint32_t>(n, nodes, 65535, ops,
                                                         reps, reverse);
}
