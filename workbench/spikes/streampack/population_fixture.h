#pragma once
#include "direct.h"
#include "prefix.h"
#include <duckdb/common/types/string_type.hpp>
#include <orbital/allocation.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace streampack_population {
namespace sn = streampack_next;
namespace oa = orbital::allocation;
void require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::abort(); } }
template<class R> auto take(R r) { require(bool(r), "Orbital operation"); return std::move(*r); }
inline void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }

// A bijection of the power-of-two rank domain, not a small repeating request
// sample. Construction needs no resident oracle or request array.
unsigned permute(unsigned x, unsigned mask) {
  x = (x * 0x85ebca6bu) & mask; x ^= x >> 13;
  x = (x * 0xc2b2ae35u) & mask; x ^= x >> 7; return x;
}
unsigned length(unsigned row) { return 4 + ((row * 0x9e3779b9u) >> 27); }
struct Location { std::uint32_t offset; std::uint16_t length, pad; };
struct Flat {
  const std::uint32_t* ends; const std::byte* body;
  unsigned size(unsigned r) const { return ends[r + 1] - ends[r]; }
  sn::Bytes value(unsigned r) const { return {body + ends[r], size(r)}; }
};
struct Narrow {
  const std::uint16_t* ends; const std::uint32_t* bases; const std::byte* body;
  unsigned shift;
  unsigned size(unsigned r) const { const auto i = r + (r >> shift); return ends[i + 1] - ends[i]; }
  sn::Bytes value(unsigned r) const {
    const auto group = r >> shift, i = r + group;
    return {body + bases[group] + ends[i], std::size_t(ends[i + 1] - ends[i])};
  }
};
template<bool Native> struct Widths {
  const std::uint8_t* widths; const std::uint32_t* bases; const std::byte* body;
  unsigned size(unsigned r) const { return widths[r]; }
  sn::Bytes value(unsigned r) const {
    const auto local = r % 16, first = r - local;
    const auto offset = Native ? sn::prefix16_native(widths + first, local) : sn::prefix16_scalar(widths + first, local);
    return {body + bases[r / 16] + offset, widths[r]};
  }
};
struct Descriptors {
  const Location* locations; const std::byte* body;
  unsigned size(unsigned r) const { return locations[r].length; }
  sn::Bytes value(unsigned r) const { const auto d = locations[r]; return {body + d.offset, d.length}; }
};
struct Duck {
  const duckdb::string_t* strings;
  unsigned size(unsigned r) const { return strings[r].GetSize(); }
  sn::Bytes value(unsigned r) const { return {reinterpret_cast<const std::byte*>(strings[r].GetData()), size(r)}; }
};
enum class Method { flat32, direct16, widths8, widths8_native, location8, duckdb16 };

// Exactly one candidate per process. No duplicate value vectors or other
// candidates remain resident. Pool statistics and process mappings are captured.
template<Method M> struct Fixture {
  unsigned count, capacity;
  oa::Allocation metadata, bases, body;
  std::size_t logical_bytes{}, payload_bytes{}, metadata_bytes{}, base_bytes{};
  Fixture(oa::Pool& pool, unsigned n, unsigned cap) : count(n), capacity(cap) {
    for (unsigned r = 0; r < n; ++r) {
      const auto len = length(r); logical_bytes += len;
      if (M != Method::duckdb16 || len > 12) payload_bytes += len;
    }
    if constexpr (M == Method::flat32) metadata_bytes = (std::size_t(n) + 1) * 4;
    if constexpr (M == Method::direct16) { metadata_bytes = (std::size_t(n) + n / cap) * 2; base_bytes = n / cap * 4; }
    if constexpr (M == Method::widths8 || M == Method::widths8_native) { metadata_bytes = n; base_bytes = n / 16 * 4; }
    if constexpr (M == Method::location8) metadata_bytes = std::size_t(n) * sizeof(Location);
    if constexpr (M == Method::duckdb16) metadata_bytes = std::size_t(n) * sizeof(duckdb::string_t);
    metadata = take(oa::allocate(pool, {.bytes = metadata_bytes, .alignment = 64}));
    if (base_bytes) bases = take(oa::allocate(pool, {.bytes = base_bytes, .alignment = 64}));
    if (payload_bytes) body = take(oa::allocate(pool, {.bytes = payload_bytes, .alignment = 64}));
    unsigned offset = 0, block_start = 0;
    for (unsigned r = 0; r < n; ++r) {
      const auto len = length(r);
      if constexpr (M == Method::flat32) reinterpret_cast<std::uint32_t*>(metadata.address)[r] = offset;
      if constexpr (M == Method::direct16) {
        if (r % cap == 0) { reinterpret_cast<std::uint32_t*>(bases.address)[r / cap] = offset; block_start = offset; }
        auto* ends = reinterpret_cast<std::uint16_t*>(metadata.address);
        ends[r + r / cap] = offset - block_start; ends[r + r / cap + 1] = offset + len - block_start;
      }
      if constexpr (M == Method::widths8 || M == Method::widths8_native) {
        reinterpret_cast<std::uint8_t*>(metadata.address)[r] = len;
        if (r % 16 == 0) reinterpret_cast<std::uint32_t*>(bases.address)[r / 16] = offset;
      }
      if constexpr (M == Method::location8) std::construct_at(reinterpret_cast<Location*>(metadata.address) + r, Location{offset, std::uint16_t(len), 0});
      if constexpr (M == Method::duckdb16) {
        // The actual DuckDB descriptor owns <=12 bytes inline. Outlined values
        // alone consume body space; no full duplicate StreamPack body exists.
        std::array<char, 36> small{};
        auto* p = len > 12 ? reinterpret_cast<char*>(body.address + offset) : small.data();
        for (unsigned j = 0; j < len; ++j) p[j] = char((r + j) & 255);
        std::construct_at(reinterpret_cast<duckdb::string_t*>(metadata.address) + r, p, len);
        if (len > 12) offset += len;
      } else {
        for (unsigned j = 0; j < len; ++j) body.address[offset + j] = std::byte((r + j) & 255);
        offset += len;
      }
    }
    if constexpr (M == Method::flat32) reinterpret_cast<std::uint32_t*>(metadata.address)[n] = offset;
    require(offset == payload_bytes, "exact fixture extent");
    auto view = reader();
    // Independent physical placement oracle, before writing any links through
    // the tested reader. This also distinguishes swapped four-byte rows, whose
    // entire payload will subsequently be occupied by the link.
    offset = 0;
    for (unsigned r = 0; r < n; ++r) {
      const auto len = length(r);
      const std::byte* expected = body.address + offset;
      if constexpr (M == Method::duckdb16)
        if (len <= 12) expected = reinterpret_cast<const std::byte*>(reinterpret_cast<const duckdb::string_t*>(metadata.address)[r].GetPrefix());
      const auto value = view.value(r);
      require(value.data() == expected && value.size() == len && view.size(r) == len, "independent pointer and length oracle");
      for (unsigned j = 0; j < len; ++j) require(value[j] == std::byte((r + j) & 255), "construction bytes before links");
      if (M != Method::duckdb16 || len > 12) offset += len;
    }
    // Form one verified full-domain cycle. Each span ends with its next rank;
    // 1- and 8-chain consumers touch the same values, with different dependence.
    for (unsigned i = 0; i < n; ++i) {
      const auto r = permute(i, n - 1), next = permute((i + 1) & (n - 1), n - 1);
      auto value = view.value(r); std::memcpy(const_cast<std::byte*>(value.data()) + value.size() - sizeof(next), &next, sizeof(next));
      if constexpr (M == Method::duckdb16) reinterpret_cast<duckdb::string_t*>(metadata.address)[r].Finalize();
    }
    for (unsigned i = 0; i < n; ++i) {
      const auto r = permute(i, n - 1), next = permute((i + 1) & (n - 1), n - 1);
      const auto value = view.value(r); unsigned got;
      require(view.size(r) == length(r) && value.size() == length(r), "all-row size oracle");
      std::memcpy(&got, value.data() + value.size() - sizeof(got), sizeof(got)); require(got == next, "all-row cycle identity");
      for (unsigned j = 0; j < value.size() - 4; ++j) require(value[j] == std::byte((r + j) & 255), "all-row payload oracle");
    }
  }
  auto reader() const {
    if constexpr (M == Method::flat32) return Flat{reinterpret_cast<const std::uint32_t*>(metadata.address), body.address};
    if constexpr (M == Method::direct16) return Narrow{reinterpret_cast<const std::uint16_t*>(metadata.address), reinterpret_cast<const std::uint32_t*>(bases.address), body.address, unsigned(std::countr_zero(capacity))};
    if constexpr (M == Method::widths8 || M == Method::widths8_native) return Widths<M == Method::widths8_native>{reinterpret_cast<const std::uint8_t*>(metadata.address), reinterpret_cast<const std::uint32_t*>(bases.address), body.address};
    if constexpr (M == Method::location8) return Descriptors{reinterpret_cast<const Location*>(metadata.address), body.address};
    if constexpr (M == Method::duckdb16) return Duck{reinterpret_cast<const duckdb::string_t*>(metadata.address)};
  }
};

template<unsigned Chains> struct Result { std::uint64_t lengths{}; std::array<unsigned, Chains> rows{}; bool operator==(const Result&) const = default; };
template<unsigned Chains, class Reader> __attribute__((noinline)) Result<Chains> chase(Reader reader, unsigned count) {
  Result<Chains> result;
  for (unsigned j = 0; j < Chains; ++j) result.rows[j] = permute(j * (count / Chains), count - 1);
  for (unsigned i = 0; i < count / Chains; ++i) {
    // The compiler can overlap independent chains. No forced SIMD or hint
    // assumes what the target's memory-level parallelism should be.
    for (auto& row : result.rows) { const auto value = reader.value(row); result.lengths += value.size(); std::memcpy(&row, value.data() + value.size() - sizeof(row), sizeof(row)); }
  }
  return result;
}
template<class Reader> __attribute__((noinline)) std::uint64_t lengths(Reader reader, unsigned count) {
  std::uint64_t sum = 0;
  for (unsigned i = 0; i < count; ++i) sum += reader.size(permute(i, count - 1));
  return sum;
}

} // namespace streampack_population
