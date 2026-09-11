#include "../../probe_controls.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <span>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
namespace sm = seriespack_measurement;
using U = std::uint64_t;
struct guarded {
  std::byte *mapping;
  std::byte *bytes;
  std::size_t extent, mapped;
  explicit guarded(std::size_t n) : extent(n) {
    const auto page = std::size_t(sysconf(_SC_PAGESIZE));
    mapped = ((n + page - 1) / page + 2) * page;
    mapping = static_cast<std::byte *>(
        mmap(nullptr, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (mapping == MAP_FAILED)
      std::abort();
    if (mprotect(mapping + page, mapped - 2 * page, PROT_READ | PROT_WRITE))
      std::abort();
    bytes = mapping + mapped - page - n;
    std::memset(bytes, 0xcd, n);
  }
  ~guarded() { munmap(mapping, mapped); }
  std::uint8_t *u8() { return reinterpret_cast<std::uint8_t *>(bytes); }
  U *u64() { return reinterpret_cast<U *>(bytes); }
};
U mix(U x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
unsigned residual(unsigned k, unsigned group, unsigned bit) {
  constexpr unsigned three[8][3] = {{0, 1, 2},    {3, 4, 5},    {6, 7, 14},
                                    {8, 9, 10},   {11, 12, 13}, {22, 23, 15},
                                    {16, 17, 18}, {19, 20, 21}};
  constexpr unsigned six[4][6] = {{0, 1, 2, 3, 4, 5},
                                  {8, 9, 10, 11, 6, 7},
                                  {12, 13, 14, 15, 22, 23},
                                  {16, 17, 18, 19, 20, 21}};
  if (k == 3)
    return three[group][bit];
  if (k == 6)
    return six[group][bit];
  const auto start = group * k, room = 8 - start % 8;
  if (k <= room)
    return start + bit;
  const auto low = k - room;
  return bit < low ? (start / 8 + 1) * 8 + bit : start + bit - low;
}
void oracle(std::span<const std::uint8_t> values, unsigned k, bool striped,
            std::uint8_t *out) {
  std::fill_n(out, values.size() * k / 8, 0);
  for (std::size_t i = 0; i < values.size(); ++i)
    for (unsigned b = 0; b < k; ++b) {
      std::size_t byte;
      unsigned bit;
      if (!striped) {
        byte = i / 8 * k + b;
        bit = i % 8;
      } else {
        const auto T = 32 * (8 / std::gcd(k, 8u)), B = T * k / 8;
        const auto p = residual(k, (i % T) / 32, b);
        byte = i / T * B + p / 8 * 32 + i % 32;
        bit = p % 8;
      }
      out[byte] |= ((values[i] >> b) & 1u) << bit;
    }
}
std::size_t points = 0, groups = 0;
template <class Codec>
void read_all(Codec c, const std::uint8_t *bytes, std::span<const U> expected,
              std::size_t first = 0) {
  guarded group(sizeof(U) * 16 + 32);
  auto *out = group.u64() + 4;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (c.get(bytes, first + i) != expected[i])
      std::abort();
    ++points;
  }
  for (std::size_t i = 0; i < expected.size(); i += 16) {
    std::memset(group.bytes, 0xcd, group.extent);
    c.get16(bytes, first + i, out);
    for (unsigned j = 0; j < 16; ++j)
      if (out[j] != expected[i + j])
        std::abort();
    for (unsigned j = 0; j < 32; ++j)
      if (group.u8()[j] != 0xcd)
        std::abort();
    ++groups;
  }
}
template <class Codec>
void sparse(Codec c, std::span<const std::uint8_t> cell,
            std::span<const U> expected) {
  constexpr std::size_t first = std::size_t{1} << 32;
  const auto page = std::size_t(sysconf(_SC_PAGESIZE)),
             offset = (first / 256) * cell.size(), mapped = offset + 3 * page;
  auto *p = static_cast<std::uint8_t *>(
      mmap(nullptr, mapped, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (p == MAP_FAILED)
    std::abort();
  auto *last = p + offset + 2 * page - cell.size();
  if (mprotect(p + offset + page, page, PROT_READ | PROT_WRITE))
    std::abort();
  std::memcpy(last, cell.data(), cell.size());
  read_all(c, last - offset, expected, first);
  munmap(p, mapped);
}
int main() {
  constexpr std::size_t n = 768;
  for (unsigned k = 1; k <= 7; ++k)
    for (bool stripe : {false, true}) {
      auto c = sm::predecessor(k, stripe);
      if (!c.encode || !c.decode || !c.get || !c.get16)
        std::abort();
      guarded input(n), packed(n * k / 8), decoded(n);
      std::vector<U> values(n);
      for (std::size_t i = 0; i < n; ++i)
        input.u8()[i] = std::uint8_t(values[i] = mix(i + 73) & ((1u << k) - 1));
      std::vector<std::uint8_t> expected(n * k / 8);
      oracle({input.u8(), n}, k, stripe, expected.data());
      c.encode(input.u8(), packed.u8(), n);
      if (!std::equal(expected.begin(), expected.end(), packed.u8()))
        std::abort();
      c.decode(packed.u8(), decoded.u8(), n);
      if (std::memcmp(input.bytes, decoded.bytes, n))
        std::abort();
      read_all(c, packed.u8(), values);
      sparse(c, {expected.data() + expected.size() - 32 * k, 32 * k},
             {values.data() + n - 256, 256});
    }
  for (bool region : {false, true}) {
    auto c = sm::predecessor56(region);
    if (!c.encode) {
      if (region)
        continue;
      std::abort();
    }
    guarded input(n * 8), packed(n * 7), decoded(n * 8);
    std::vector<U> values(n);
    std::vector<std::uint8_t> expected(n * 7);
    for (std::size_t i = 0; i < n; ++i) {
      input.u64()[i] = values[i] = mix(i + 79) & 0x00ffffffffffffffULL;
      for (unsigned b = 0; b < 7; ++b)
        expected[i * 7 + b] = std::uint8_t(values[i] >> (8 * b));
    }
    c.encode(input.u64(), packed.u8(), n);
    if (!std::equal(expected.begin(), expected.end(), packed.u8()))
      std::abort();
    c.decode(packed.u8(), decoded.u64(), n);
    if (std::memcmp(input.bytes, decoded.bytes, n * 8))
      std::abort();
    read_all(c, packed.u8(), values);
    sparse(c, {expected.data() + expected.size() - 1792, 1792},
           {values.data() + n - 256, 256});
  }
  auto bad = sm::predecessor(8, false);
  if (bad.encode || bad.decode || bad.get || bad.get16)
    std::abort();
  std::printf("Predecessor access(%s): %zu point and %zu get16 checks; "
              "independent wire/value oracle, three cells and indices>=2^32, "
              "exact guarded input/output; encode/decode preserved\n",
              sm::predecessor_target(), points, groups);
}
