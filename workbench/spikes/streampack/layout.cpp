#include "layout.h"
#include "layout_tuple_group.h"
#include "layout_hybrid.h"
#if STREAMPACK_EXPLICIT
#include "layout_native.h"
#endif
#include <orbital/allocation.h>
#include <chrono>
#include <string>
#include <numeric>
#include <thread>

namespace sl = streampack_layout;
namespace oa = orbital::allocation;
using sl::require;
oa::Pool* experiment_pool;
template<class R> auto take(R r) { require(bool(r), "Orbital operation"); return std::move(*r); }
unsigned permute(unsigned x, unsigned mask) {
  x = (x * 0x85ebca6bu) & mask; x ^= x >> 13;
  x = (x * 0xc2b2ae35u) & mask; x ^= x >> 7; return x;
}
constexpr unsigned inverse_odd(unsigned x) { unsigned y = x; for (unsigned i = 0; i < 5; ++i) y *= 2 - x * y; return y; }
unsigned unpermute(unsigned x, unsigned mask) {
  unsigned y = x; for (unsigned i = 7; i < 32; i += 7) y ^= x >> i;
  x = y * inverse_odd(0xc2b2ae35u) & mask; y = x;
  for (unsigned i = 13; i < 32; i += 13) y ^= x >> i;
  return y * inverse_odd(0x85ebca6bu) & mask;
}
unsigned reservation(unsigned row, unsigned maximum) {
  return row % 257 == 0 ? maximum : 8 + ((row * 0x9e3779b9u) >> 25);
}
unsigned used(unsigned row, unsigned r) { return 4 + ((row * 0x85ebca6bu) % (r - 3)); }
struct Block { std::uint64_t offset; unsigned extent, count; };
void escape(const void* p) { asm volatile("" : : "r"(p) : "memory"); }

#if STREAMPACK_EXPLICIT
namespace en = sl::explicit_native;
template<class Meta, class Expected>
void check_native_fields(const std::uint8_t* bytes, unsigned rows, Expected expected) {
  const en::View<Meta> view(bytes, rows);
  auto field = [&]<unsigned F>() {
    alignas(64) unsigned request[en::lanes], actual[en::lanes];
    for (unsigned first = 0; first < rows; first += en::lanes) {
      en::store32(actual, view.template ordered<F>(first));
      for (unsigned j = 0; j < en::lanes; ++j) require(actual[j] == expected(F, first + j), "explicit ordered exact lane oracle");
      for (unsigned pattern = 0; pattern < 3; ++pattern) {
        for (unsigned j = 0; j < en::lanes; ++j)
          request[j] = pattern == 0 ? permute(first + j, rows - 1) : pattern == 1 ? rows - 1 - ((first + j) % 17) : first;
        auto verify = [&]<en::Loads Method>() {
          en::store32(actual, view.template random<F, Method>(en::load32(request)));
          for (unsigned j = 0; j < en::lanes; ++j) require(actual[j] == expected(F, request[j]), "explicit indexed exact lane oracle");
        };
        verify.template operator()<en::Loads::lanes>();
#if defined(__AVX2__)
        verify.template operator()<en::Loads::gather>();
#endif
      }
      en::store32(actual, en::permute(en::ranks(first), rows - 1));
      for (unsigned j = 0; j < en::lanes; ++j) require(actual[j] == permute(first + j, rows - 1), "explicit rank packet matches scalar permutation");
    }
  };
  field.template operator()<0>(); field.template operator()<1>(); field.template operator()<2>();
  field.template operator()<3>(); field.template operator()<4>(); field.template operator()<5>();
}

template<class Meta, unsigned Maximum> void check_native_multiblock() {
  Meta meta(1024);
  auto backing = take(oa::allocate(*experiment_pool, {.bytes = meta.bytes(), .alignment = 64}));
  auto* bytes = reinterpret_cast<std::uint8_t*>(backing.address);
  std::memset(bytes, 0xa5, meta.bytes()); meta.attach(bytes);
  std::array<unsigned, 1024> reserved{}, used{}, starts{}; std::array<bool, 1024> dead{};
  unsigned position = 0;
  for (unsigned row = 0; row < 1024; ++row) {
    if (row % 256 == 0) position = 0;
    reserved[row] = row % 19 == 0 ? 0 : (row * 0x9e3779b9u) % (Maximum + 1);
    used[row] = (row * 0x85ebca6bu) % (reserved[row] + 1); dead[row] = row % 7 == 0;
    starts[row] = position; position += reserved[row];
    meta.encode(row, starts[row], reserved[row], used[row], dead[row]);
  }
  check_native_fields<Meta>(bytes, 1024, [&](unsigned field, unsigned row) -> unsigned {
    if (field == 0) return reserved[row];
    if (field == 1) return used[row];
    if (field == 2) return dead[row];
    if (field == 3) return starts[row / 16 * 16];
    if (field == 4) return dead[row] ? 0 : used[row];
    return reserved[row] + used[row];
  });
  require(meta.gaps_unchanged(), "native multiblock fixture preserves padding");
  oa::release(backing);
}
#endif

template<class W> void check_starts_native() {
  alignas(64) W widths[16]; alignas(64) unsigned actual[16];
  for (unsigned pattern = 0; pattern < 4; ++pattern) for (unsigned nonzero = 0; nonzero <= 16; ++nonzero) {
    for (unsigned i = 0; i < 16; ++i) widths[i] = i >= nonzero ? 0 : pattern == 0 ? 0 : pattern == 1 ? W(-1) : pattern == 2 ? W(i * 0x9e3779b9u) : W(i == 7 || i == 8 || i == 15 ? W(-1) : 0);
    unsigned expected = 1234567;
    const auto end = streampack_next::starts16_native(widths, actual, expected);
    for (unsigned i = 0; i < 16; ++i) { require(actual[i] == expected, "explicit widened prefix exact lane including sublane carry"); expected += widths[i]; }
    require(end == expected, "explicit prefix outgoing carry");
  }
}

template<class G> void check_endpoint_starts_native() {
  alignas(64) G ends[16]; alignas(64) unsigned actual[16];
  for (unsigned pattern = 0; pattern < 3; ++pattern) {
    for (unsigned i = 0; i < 16; ++i) ends[i] = pattern == 0 ? 0 : pattern == 1 ? G(-1) : G(i * 0x9e3779b9u);
    const auto end = streampack_next::endpoint_starts16_native(ends, actual, 12345);
    require(actual[0] == 12345 && end == ends[15], "Direct native incoming/outgoing group endpoint");
    for (unsigned i = 1; i < 16; ++i) require(actual[i] == ends[i - 1], "Direct native exact endpoint lane including sublane boundaries");
  }
}

template<class Out, unsigned Bits, unsigned FirstBits> void check_code_assembly_native() {
  constexpr unsigned count = 1 + (Bits > FirstBits ? (Bits - FirstBits + 7) / 8 : 0);
  alignas(64) std::uint8_t codes[count * 16]; alignas(64) Out expected[16], actual[16];
  for (unsigned pattern = 0; pattern < 3; ++pattern) {
    for (unsigned row = 0; row < 16; ++row) {
      expected[row] = (pattern == 0 ? 0 : pattern == 1 ? ~std::uint64_t(0) : row * 0x9e3779b9u) & ((std::uint64_t(1) << Bits) - 1);
      for (unsigned c = 0; c < count; ++c) {
        const auto shift = c ? FirstBits + (c - 1) * 8 : 0;
        const auto width = c ? std::min(8u, Bits - shift) : std::min(Bits, FirstBits);
        codes[c * 16 + row] = (expected[row] >> shift) & ((1u << width) - 1);
      }
    }
    streampack_next::assemble16_native<Out, Bits, FirstBits>(codes, actual);
    require(std::equal(std::begin(actual), std::end(actual), std::begin(expected)), "independent byte-code lane assembly including split low bits and exact tail");
  }
}

template<class Meta, unsigned Maximum> void check() {
  if constexpr (!Meta::valid) return;
  auto& pool = *experiment_pool;
  Meta meta(256);
  auto backing = take(oa::allocate(pool, {.bytes = meta.bytes(), .alignment = 64}));
  auto* bytes = reinterpret_cast<std::uint8_t*>(backing.address); meta.attach(bytes);
  for (unsigned pattern = 0; pattern < 3; ++pattern)
  for (unsigned count : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 33u, 239u, 255u, 256u}) {
    std::memset(bytes, 0xa5, meta.bytes());
    std::array<unsigned, 257> ends{};
    std::array<unsigned, 256> lengths{};
    std::array<bool, 256> dead{};
    for (unsigned row = 0; row < 256; ++row) {
      // Repeated endpoints, an entirely zero internal group, maximum final
      // reservations, empty live values, and tombstones with retained bytes.
      const auto r = row >= count || (!pattern && ((row >= 16 && row < 32) || row % 5 == 0)) ? 0 : pattern == 2 ? (row * 0x9e3779b9u) % (Maximum + 1) : Maximum;
      lengths[row] = r ? (pattern == 2 ? (row * 0x85ebca6bu) % (r + 1) : (row % 3 ? r : 0)) : 0; dead[row] = row < count && row % 7 == 0;
      ends[row + 1] = ends[row] + r;
      meta.encode(row, ends[row], r, lengths[row], dead[row]);
    }
    for (unsigned row = 0; row < count; ++row) {
      const auto actual = meta.get(row);
      require(meta.dead(row) == dead[row], "dead-only projection");
      require(meta.used_length(row) == lengths[row], "used-only projection");
      if (actual != sl::Entry{ends[row], ends[row + 1] - ends[row], lengths[row], dead[row]}) std::fprintf(stderr, "count=%u row=%u actual %u/%u/%u/%d expected %u/%u/%u/%d\n", count,row,actual.start,actual.reserved,actual.used,actual.dead,ends[row],ends[row+1]-ends[row],lengths[row],dead[row]);
      require(actual == sl::Entry{ends[row], ends[row + 1] - ends[row], lengths[row], dead[row]}, "independent forward owner/state");
      if (ends[row] != ends[row + 1]) for (auto x : {ends[row], (ends[row] + ends[row + 1] - 1) / 2, ends[row + 1] - 1}) {
        auto owner = meta.reverse(0, count, ends[count], x);
        require(owner && owner->first == row && owner->second == dead[row], "duplicate checkpoints and physical owner");
      }
    }
    require(!meta.reverse(0, count, ends[count], ends[count]), "extent is excluded independently of capacity");
#if STREAMPACK_FIELDS
    alignas(64) typename Meta::W reserved_group[16], used_group[16];
    alignas(64) std::uint32_t checkpoints[16];
    if constexpr (Meta::widths) meta.decode_bases(0, checkpoints);
    for (unsigned g = 0; g < 16; ++g) {
      if constexpr (Meta::widths) {
        meta.decode_widths(g * 16, reserved_group);
        require(checkpoints[g] == ends[g * 16], "full checkpoint decode");
      } else {
        alignas(64) typename Meta::G decoded[16]; alignas(64) unsigned starts[16];
        meta.decode_endpoints(g * 16, decoded);
        const auto end = streampack_next::endpoint_starts16_native(decoded, starts, ends[g * 16]);
        require(end == ends[g * 16 + 16], "Direct decoded outgoing endpoint");
        for (unsigned i = 0; i < 16; ++i) require(decoded[i] == ends[g * 16 + i + 1] && starts[i] == ends[g * 16 + i], "Direct native grouped endpoints and starts");
      }
      meta.decode_used(g * 16, used_group);
      unsigned expected_dead = 0; for (unsigned i = 0; i < 16; ++i) expected_dead |= unsigned(dead[g * 16 + i]) << i;
      require(meta.dead_mask16(g * 16) == expected_dead, "exact grouped liveness lanes");
      for (unsigned i = 0; i < 16; ++i) {
        const auto r = g * 16 + i;
        if constexpr (Meta::widths) require(reserved_group[i] == ends[r + 1] - ends[r], "full reservation decode");
        require(used_group[i] == lengths[r], "full used length decode");
      }
    }
    require(meta.count_dead(0) == std::count(dead.begin(), dead.end(), true), "packed tombstone consumption");
#endif
#if STREAMPACK_EXPLICIT
    check_native_fields<Meta>(bytes, 256, [&](unsigned field, unsigned row) -> unsigned {
      if (field == 0) return ends[row + 1] - ends[row];
      if (field == 1) return lengths[row];
      if (field == 2) return dead[row];
      if (field == 3) return ends[row / 16 * 16];
      if (field == 4) return dead[row] ? 0 : lengths[row];
      return ends[row + 1] - ends[row] + lengths[row];
    });
#endif

    for (unsigned row = 0; row < count; ++row) {
      const auto before = std::vector<std::uint8_t>(bytes, bytes + meta.bytes());
      std::array<ikea::owner_write, 16> records{}; ikea::source_write_journal effects{records};
      const auto length = (ends[row + 1] - ends[row]) / 2;
      meta.update(row, length, !dead[row], effects);
      auto replay = before; bool omission_observed = false;
      for (const auto& write : effects.entries()) {
        require(write.source == &meta && write.bytes.plane == 0 && write.bytes.offset + write.bytes.size <= meta.bytes(), "qualified actual metadata write");
        std::memcpy(replay.data() + write.bytes.offset, bytes + write.bytes.offset, write.bytes.size);
        for (std::size_t i = 0; i < write.bytes.size; ++i)
          omission_observed |= before[write.bytes.offset + i] != bytes[write.bytes.offset + i];
      }
      require(omission_observed && std::equal(replay.begin(), replay.end(), bytes), "owned afterimages reconstruct sibling-preserving mutation");
      lengths[row] = length; dead[row] = !dead[row];
      require(meta.dead(row) == dead[row], "updated dead-only projection");
      for (unsigned r = 0; r < count; ++r)
        require(meta.get(r) == sl::Entry{ends[r], ends[r + 1] - ends[r], lengths[r], dead[r]}, "updates preserve all siblings and geometry");
    }
    require(meta.gaps_unchanged(), "interleaved stride gaps preserve sibling bytes");
  }
#if STREAMPACK_EXPLICIT
  check_native_multiblock<Meta, Maximum>();
#endif
}

template<class Meta, unsigned Maximum> struct Population {
  unsigned rows;
  Meta metadata;
  oa::Allocation meta_owner, body_owner, block_owner;
  std::uint8_t* bytes;
  std::byte* body;
  Block* blocks;
  std::size_t body_bytes{};
  Population(oa::Pool& pool, unsigned n) : rows(n), metadata(n) {
    for (unsigned row = 0; row < n; ++row) body_bytes += reservation(row, Maximum);
    meta_owner = take(oa::allocate(pool, {.bytes = metadata.bytes(), .alignment = 64}));
    body_owner = take(oa::allocate(pool, {.bytes = body_bytes, .alignment = 64}));
    block_owner = take(oa::allocate(pool, {.bytes = n / 256 * sizeof(Block), .alignment = 64}));
    bytes = reinterpret_cast<std::uint8_t*>(meta_owner.address); body = body_owner.address;
    blocks = reinterpret_cast<Block*>(block_owner.address); std::memset(bytes, 0xa5, metadata.bytes()); metadata.attach(bytes);
    std::size_t global = 0; unsigned local = 0;
    for (unsigned row = 0; row < n; ++row) {
      if (row % 256 == 0) { local = 0; std::construct_at(blocks + row / 256, Block{global, 0, 256}); }
      const auto r = reservation(row, Maximum), length = used(row, r);
      metadata.encode(row, local, r, length, false);
      for (unsigned i = 0; i < r; ++i) body[global + i] = std::byte((row + i) & 255);
      require(metadata.get(row) == sl::Entry{local, r, length, false}, "all-row independent constructor geometry");
      local += r; global += r;
      if (row % 256 == 255) blocks[row / 256].extent = local;
    }
    require(global == body_bytes, "independent body extent");
    // Install a full-domain link cycle at the last four live bytes. There is no
    // resident request array or second population during timing.
    for (unsigned i = 0; i < rows; ++i) {
      const auto row = permute(i, rows - 1), next = permute((i + 1) & (rows - 1), rows - 1);
      const auto e = metadata.get(row); std::memcpy(body + blocks[row / 256].offset + e.start + e.used - 4, &next, 4);
    }
  }
  template<unsigned Chains> [[gnu::always_inline]] void chase_step(unsigned c,
      std::array<unsigned, Chains>& ranks, std::array<std::uint64_t, Chains>& sum) const {
      const auto row = ranks[c], begin = metadata.start(row);
      const auto [length, dead] = metadata.state(row);
#if STREAMPACK_FIELDS
      require(!dead, "live chain row");
#endif
      sum[c] += length;
      std::memcpy(&ranks[c], body + blocks[row / 256].offset + begin + length - 4, 4);
  }
  template<unsigned Chains, std::size_t... I> [[gnu::always_inline]] void chase_packet(
      std::array<unsigned, Chains>& ranks, std::array<std::uint64_t, Chains>& sum, std::index_sequence<I...>) const {
    (chase_step<Chains>(I, ranks, sum), ...);
  }
  template<unsigned Chains> __attribute__((noinline)) std::uint64_t chase() const {
    std::array<unsigned, Chains> ranks{}; std::array<std::uint64_t, Chains> sum{};
    for (unsigned c = 0; c < Chains; ++c) ranks[c] = permute(c * (rows / Chains), rows - 1);
    for (unsigned i = 0; i < rows / Chains; ++i) {
#if STREAMPACK_EXPLICIT || STREAMPACK_EXPLICIT_BULK
      chase_packet<Chains>(ranks, sum, std::make_index_sequence<Chains>{});
#else
      for (unsigned c = 0; c < Chains; ++c) chase_step<Chains>(c, ranks, sum);
#endif
    }
    for (unsigned c = 0; c < Chains; ++c) require(ranks[c] == permute((c + 1) * (rows / Chains) & (rows - 1), rows - 1), "dependent terminal rank");
    return std::accumulate(sum.begin(), sum.end(), std::uint64_t(0));
  }
  template<bool StateOnly> __attribute__((noinline)) std::uint64_t random() const {
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < rows; ++i) {
      const auto row = permute(i, rows - 1);
      if constexpr (StateOnly) { const auto [n, dead] = metadata.state(row); if (!dead) sum += n; }
      else { const auto e = metadata.get(row); if (!e.dead) sum += e.start + e.reserved + e.used; }
    }
    return sum;
  }
  template<bool Shuffled> unsigned project_block(unsigned b, std::span<std::byte> output) const {
      unsigned out = 0, position = 0;
      const auto* source = body + blocks[b].offset;
      for (unsigned j = 0; j < 256; ++j) {
        const auto r = Shuffled ? permute(j, 255) : j, row = b * 256 + r;
        const auto [length, dead] = metadata.state(row);
        const auto start = Shuffled ? metadata.start(row) : position;
        if (!dead) { std::memcpy(output.data() + out, source + start, length); out += length; }
        if constexpr (!Shuffled) {
          if constexpr (Meta::widths) position += metadata.geometry(row);
          else position = metadata.geometry(row);
        }
      }
      return out;
  }
  template<bool Shuffled> __attribute__((noinline)) std::uint64_t project(std::span<std::byte> output) const {
    std::uint64_t total = 0;
    for (unsigned i = 0; i < rows / 256; ++i) {
      const auto b = Shuffled ? permute(i, rows / 256 - 1) : i;
      total += project_block<Shuffled>(b, output); escape(output.data());
    }
    return total;
  }
#if STREAMPACK_FIELDS
  template<bool Shuffled> unsigned project_bulk_block(unsigned b, std::span<std::byte> output) const {
    alignas(64) typename Meta::W widths[16], lengths[Shuffled ? 256 : 16];
    alignas(64) unsigned starts[Shuffled ? 256 : 16];
    std::uint16_t dead[Shuffled ? 16 : 1];
    unsigned position = 0, out = 0;
    const auto* source = body + blocks[b].offset;
    for (unsigned g = 0; g < 16; ++g) {
      const auto first = b * 256 + g * 16;
      if constexpr (Meta::widths) metadata.decode_widths(first, widths);
      metadata.decode_used(first, lengths + (Shuffled ? g * 16 : 0));
      dead[Shuffled ? g : 0] = metadata.dead_mask16(first);
#if STREAMPACK_EXPLICIT_BULK
      if constexpr (Meta::widths) position = streampack_next::starts16_native(widths, starts + (Shuffled ? g * 16 : 0), position);
      else {
        alignas(64) typename Meta::G ends[16]; metadata.decode_endpoints(first, ends);
        position = streampack_next::endpoint_starts16_native(ends, starts + (Shuffled ? g * 16 : 0), position);
      }
      if constexpr (!Shuffled) for (unsigned i = 0; i < 16; ++i) if (!(dead[0] & (1u << i))) {
        std::memcpy(output.data() + out, source + starts[i], lengths[i]); out += lengths[i];
      }
#else
      for (unsigned i = 0; i < 16; ++i) {
        if constexpr (Shuffled) starts[g * 16 + i] = position;
        else if (!(dead[0] & (1u << i))) {
          std::memcpy(output.data() + out, source + position, lengths[i]); out += lengths[i];
        }
        position += widths[i];
      }
#endif
    }
    if constexpr (Shuffled) for (unsigned i = 0; i < 256; ++i) {
      const auto row = permute(i,255); const auto n = lengths[row];
      if (!(dead[row / 16] & (1u << (row % 16)))) {
        std::memcpy(output.data() + out, source + starts[row], n); out += n;
      }
    }
    return out;
  }
  template<bool Shuffled> __attribute__((noinline)) std::uint64_t project_bulk(std::span<std::byte> output) const {
    std::uint64_t total = 0;
    for (unsigned i = 0; i < rows / 256; ++i) {
      const auto b = Shuffled ? permute(i, rows / 256 - 1) : i;
      total += project_bulk_block<Shuffled>(b, output); escape(output.data());
    }
    return total;
  }
#endif
  __attribute__((noinline)) std::uint64_t reverse() const {
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < rows; ++i) {
      const auto row = permute(i, rows - 1), b = row / 256;
      const auto offset = (row * 0x9e3779b9u) % blocks[b].extent;
      const auto owner = metadata.reverse(b, 256, blocks[b].extent, offset);
      require(bool(owner) && !owner->second, "live physical reverse owner"); sum += owner->first;
    }
    return sum;
  }
  std::uint64_t expected_reverse() const {
    std::uint64_t sum = 0; std::array<unsigned, 257> ends{};
    for (unsigned b = 0; b < rows / 256; ++b) {
      for (unsigned r = 0; r < 256; ++r) ends[r + 1] = ends[r] + reservation(b * 256 + r, Maximum);
      for (unsigned r = 0; r < 256; ++r) {
        const auto offset = ((b * 256 + r) * 0x9e3779b9u) % ends[256];
        sum += std::upper_bound(ends.begin(), ends.end(), offset) - ends.begin() - 1;
      }
    }
    return sum;
  }
  void verify_projection(std::span<std::byte> output, bool mixed = false) const {
    // Exact byte/order check on up to 128 blocks spread over the full population;
    // no oracle buffer or sample list survives this pre-measurement check.
    std::vector<std::byte> expected(256 * Maximum);
    const auto samples = std::min(128u, rows / 256);
    for (unsigned i = 0; i < samples; ++i) {
      const auto b = i * (rows / 256 / samples);
      for (const bool shuffled : {false, true}) {
        unsigned n = 0;
        for (unsigned j = 0; j < 256; ++j) {
          const auto row = b * 256 + (shuffled ? permute(j, 255) : j);
          if (mixed && row % 7 == 0) continue;
          const auto len = mixed && row % 5 == 0 ? 0 : used(row, reservation(row, Maximum));
          if (!len) continue;
          require(permute(unpermute(row, rows - 1), rows - 1) == row, "independent cycle inversion");
          const auto next = permute((unpermute(row, rows - 1) + 1) & (rows - 1), rows - 1);
          for (unsigned k = 0; k < len - 4; ++k) expected[n + k] = std::byte((row + k) & 255);
          std::memcpy(expected.data() + n + len - 4, &next, 4); n += len;
        }
        const auto actual = shuffled ? project_block<true>(b, output) : project_block<false>(b, output);
        require(actual == n && std::equal(expected.begin(), expected.begin() + n, output.begin()), "independent complete projection bytes/order");
#if STREAMPACK_FIELDS
        const auto bulk = shuffled ? project_bulk_block<true>(b, output) : project_bulk_block<false>(b, output);
        require(bulk == n && std::equal(expected.begin(), expected.begin() + n, output.begin()), "independent grouped projection bytes/order");
#endif
      }
    }
  }
#if STREAMPACK_FIELDS
  template<unsigned Field, bool Shuffled> __attribute__((noinline)) std::uint64_t field() const {
    std::uint64_t sum = 0;
    for (unsigned i = 0; i < rows; ++i) {
      const auto row = Shuffled ? permute(i, rows - 1) : i;
      if constexpr (Field == 0) sum += metadata.geometry(row);
      if constexpr (Field == 1) sum += metadata.used_length(row);
      if constexpr (Field == 2) sum += metadata.dead(row);
      if constexpr (Field == 3) sum += metadata.base(row / 16);
      if constexpr (Field == 4) sum += metadata.start(row);
    }
    return sum;
  }
  template<unsigned Field> __attribute__((noinline)) std::uint64_t decode() const {
    std::uint64_t sum = 0;
    if constexpr (Field < 2) {
      for (unsigned row = 0; row < rows; row += 16) {
        alignas(64) typename Meta::W values[16];
        if constexpr (Field == 0) metadata.decode_widths(row, values); else metadata.decode_used(row, values);
        for (auto v : values) sum += v;
      }
    } else if constexpr (Field == 2) {
      for (unsigned b = 0; b < rows / 256; ++b) sum += metadata.count_dead(b);
    } else {
      for (unsigned b = 0; b < rows / 256; ++b) {
        alignas(64) std::uint32_t values[16]; metadata.decode_bases(b, values);
        for (auto v : values) sum += v;
      }
    }
    return sum;
  }
  void check_bulk_state(std::span<std::byte> output) {
    std::array<ikea::owner_write, 16> records{};
    for (unsigned phase = 0; phase < 2; ++phase) {
      for (unsigned row = 0; row < rows; ++row) {
        const auto n = used(row, reservation(row, Maximum));
        ikea::source_write_journal effects{records};
        metadata.update(row, !phase && row % 5 == 0 ? 0 : n, !phase && row % 7 == 0, effects);
      }
      verify_projection(output, !phase);
    }
  }
  void seed_field_state() {
    std::array<ikea::owner_write, 16> records{};
    for (unsigned row = 0; row < rows; ++row) {
      ikea::source_write_journal effects{records}; metadata.update(row, used(row, reservation(row, Maximum)), row % 7 == 0, effects);
      escape(records.data());
    }
  }
#endif
  __attribute__((noinline)) std::uint64_t update() {
    std::uint64_t damage = 0;
    std::array<ikea::owner_write, 16> records{};
    for (unsigned i = 0; i < rows; ++i) {
      const auto row = permute(i, rows - 1);
      const auto was_dead = metadata.dead(row); const auto length = used(row, reservation(row, Maximum));
      ikea::source_write_journal effects{records}; metadata.update(row, was_dead ? length : length / 2, !was_dead, effects);
      for (const auto& e : effects.entries()) damage += e.bytes.size;
      escape(records.data());
    }
    return damage;
  }
};

template<class Meta, unsigned Maximum> void run(std::string name, unsigned rows, unsigned repetitions) {
  if constexpr (!Meta::valid) return;
  if (!rows) {
    std::fprintf(stderr, "check %s width=%d max=%u\n", name.c_str(), Meta::widths, Maximum); check<Meta, Maximum>();
#if STREAMPACK_DIRECT
    Population<Meta, Maximum> sample(*experiment_pool, 1024);
    std::vector<std::byte> output(256 * Maximum); sample.check_bulk_state(output);
    std::uint64_t expected = 0;
    for (unsigned row = 0; row < 1024; ++row) expected += used(row, reservation(row, Maximum));
    require(sample.template chase<1>() == expected && sample.template chase<8>() == expected, "Direct complete consumer chain oracles");
    require(sample.reverse() == sample.expected_reverse(), "Direct complete reverse oracle");
#endif
    return;
  }
  auto& pool = *experiment_pool; Population<Meta, Maximum> p(pool, rows);
  std::vector<std::byte> output(256 * Maximum);
  p.verify_projection(output);
  std::uint64_t expected_used = 0;
#if !STREAMPACK_EXPLICIT && !STREAMPACK_DIRECT
  std::uint64_t expected_geometry = 0; unsigned offset = 0;
#endif
  for (unsigned row = 0; row < rows; ++row) {
#if !STREAMPACK_EXPLICIT && !STREAMPACK_DIRECT
    if (row % 256 == 0) offset = 0;
    const auto r = reservation(row, Maximum), u = used(row, r); expected_used += u; expected_geometry += offset + r + u; offset += r;
#else
    expected_used += used(row, reservation(row, Maximum));
#endif
  }
  auto measure = [&](const char* operation, auto body, std::optional<std::uint64_t> expected, unsigned items = 0) {
    if (!items) items = rows;
    auto execute = [&] { const auto value = body(); escape(&value); if (expected) require(value == *expected, "complete operation oracle"); return value; };
    execute();
    for (unsigned rep = 0; rep < repetitions; ++rep) {
      const auto start = std::chrono::steady_clock::now(); unsigned loops = 0; std::uint64_t result = 0; double elapsed;
      do { result = execute(); ++loops; elapsed = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count(); } while (elapsed < 5e6);
      std::printf("%s,%u,%u,%s,%u,%u,%.5f,%zu,%zu,%zu,%zu,%zu,%llu,%u,%.5f\n", name.c_str(), Maximum, rows, operation, rep, loops, elapsed / rows / loops, p.metadata.bytes(), p.metadata.owned_bytes(), oa::capacity(p.meta_owner), p.body_bytes, rows / 256 * sizeof(Block), (unsigned long long)result, items, elapsed / items / loops);
    }
  };
#if STREAMPACK_DIRECT
  p.check_bulk_state(output);
  measure("explicit-value-c1", [&] { return p.template chase<1>(); }, expected_used);
  measure("explicit-value-c8", [&] { return p.template chase<8>(); }, expected_used);
  measure("project-sequential", [&] { return p.template project<false>(output); }, expected_used);
  measure("project-shuffled", [&] { return p.template project<true>(output); }, expected_used);
  measure("explicit-group-project-sequential", [&] { return p.template project_bulk<false>(output); }, expected_used);
  measure("explicit-group-project-shuffled", [&] { return p.template project_bulk<true>(output); }, expected_used);
  measure("reverse-scattered", [&] { return p.reverse(); }, p.expected_reverse());
  measure("metadata-update", [&] { return p.update(); }, std::nullopt);
#elif STREAMPACK_EXPLICIT
  // Complete lookup: one dependent chain measures serial service; eight chains
  // expose overlap. Both visit the same full-domain cycle once, include state,
  // group-prefix geometry and payload-tail load, and check terminal ranks.
  const bool ordered_only = std::getenv("STREAMPACK_LAYOUT_ORDERED_ONLY");
  if (!ordered_only) {
    measure("explicit-value-c1", [&] { return p.template chase<1>(); }, expected_used);
    measure("explicit-value-c8", [&] { return p.template chase<8>(); }, expected_used);
  }
  p.seed_field_state();
  require(p.metadata.bytes() < (std::size_t(1) << 31), "signed gather offsets fit admitted metadata");
  const en::View<Meta> native(p.bytes, rows);
  std::array<std::uint64_t, 6> sums{};
  unsigned running = 0, base = 0;
  for (unsigned row = 0; row < rows; ++row) {
    if (row % 256 == 0) running = 0;
    if (row % 16 == 0) base = running;
    const auto r = reservation(row, Maximum), u = used(row, r); const bool dead = row % 7 == 0;
    sums[0] += r; sums[1] += u; sums[2] += dead; sums[3] += base; sums[4] += dead ? 0 : u; sums[5] += r + u;
    running += r;
  }
  // Exact lane admission is checked for independent mixed/max/empty histories
  // in the check command. These full-population sums additionally check every
  // timed request, including the explicit vector permutation and 64-bit sums.
  auto field = [&]<unsigned F>(const char* name) {
    const auto ordered = std::string("explicit16-") + name + "-ordered";
    const auto staged = std::string("explicit16-") + name + "-shuffled-lanes";
    measure(ordered.c_str(), [&] { return en::scan<F, false, en::Loads::lanes>(native, rows); }, sums[F]);
    if (ordered_only) return;
    measure(staged.c_str(), [&] { return en::scan<F, true, en::Loads::lanes>(native, rows); }, sums[F]);
#if defined(__AVX2__)
    const auto gathered = std::string("explicit16-") + name + "-shuffled-gather";
    measure(gathered.c_str(), [&] { return en::scan<F, true, en::Loads::gather>(native, rows); }, sums[F]);
#endif
  };
  field.template operator()<0>("reserved"); field.template operator()<1>("used");
  field.template operator()<2>("dead"); field.template operator()<3>("checkpoint-per-rank");
  field.template operator()<4>("live-used"); field.template operator()<5>("reserved-plus-used");
#else
#if STREAMPACK_FIELDS
  if (std::getenv("STREAMPACK_LAYOUT_BULK_ONLY")) {
    p.check_bulk_state(output);
#if STREAMPACK_EXPLICIT_BULK
    measure("explicit-value-c1", [&] { return p.template chase<1>(); }, expected_used);
    measure("explicit-value-c8", [&] { return p.template chase<8>(); }, expected_used);
    measure("explicit-prefix-project-sequential", [&] { return p.template project_bulk<false>(output); }, expected_used);
    measure("explicit-prefix-project-shuffled", [&] { return p.template project_bulk<true>(output); }, expected_used);
#else
    measure("state-random", [&] { return p.template random<true>(); }, expected_used);
    measure("eight-chains-live-check", [&] { return p.template chase<8>(); }, expected_used);
    measure("reverse-scattered", [&] { return p.reverse(); }, p.expected_reverse());
    measure("project-sequential", [&] { return p.template project<false>(output); }, expected_used);
    measure("project-shuffled", [&] { return p.template project<true>(output); }, expected_used);
    measure("project-group-sequential", [&] { return p.template project_bulk<false>(output); }, expected_used);
    measure("project-group-shuffled", [&] { return p.template project_bulk<true>(output); }, expected_used);
    measure("metadata-update", [&] { return p.update(); }, std::nullopt);
#endif
    return;
  }
#endif
  measure("state-random", [&] { return p.template random<true>(); }, expected_used);
  measure("geometry-random", [&] { return p.template random<false>(); }, expected_geometry);
#if !STREAMPACK_FIELDS
  measure("dependent", [&] { return p.template chase<1>(); }, expected_used);
  measure("eight-chains", [&] { return p.template chase<8>(); }, expected_used);
#else
  measure("eight-chains-live-check", [&] { return p.template chase<8>(); }, expected_used);
#endif
  measure("project-sequential", [&] { return p.template project<false>(output); }, expected_used);
  measure("project-shuffled", [&] { return p.template project<true>(output); }, expected_used);
  if (std::getenv("STREAMPACK_LAYOUT_REVERSE")) measure("reverse-scattered", [&] { return p.reverse(); }, p.expected_reverse());
  measure("metadata-update", [&] { return p.update(); }, std::nullopt);
#if STREAMPACK_FIELDS
  p.seed_field_state();
  std::array<std::uint64_t, 5> expected{}; offset = 0; unsigned checkpoint = 0;
  for (unsigned row = 0; row < rows; ++row) {
    if (row % 256 == 0) offset = 0;
    if (row % 16 == 0) checkpoint = offset;
    const auto r = reservation(row, Maximum);
    expected[0] += r; expected[1] += used(row, r); expected[2] += row % 7 == 0;
    expected[3] += checkpoint; expected[4] += offset; offset += r;
  }
  auto point = [&]<unsigned F>(const char* seq, const char* shuffled) {
    measure(seq, [&] { return p.template field<F, false>(); }, expected[F]);
    measure(shuffled, [&] { return p.template field<F, true>(); }, expected[F]);
  };
  point.template operator()<0>("reservation-sequential", "reservation-shuffled");
  point.template operator()<1>("used-sequential", "used-shuffled");
  point.template operator()<2>("dead-sequential", "dead-shuffled");
  point.template operator()<3>("checkpoint-per-rank-sequential", "checkpoint-per-rank-shuffled");
  point.template operator()<4>("start-sequential", "start-shuffled");
  measure("decode-reservation", [&] { return p.template decode<0>(); }, expected[0]);
  measure("decode-used", [&] { return p.template decode<1>(); }, expected[1]);
  measure("consume-dead-bits", [&] { return p.template decode<2>(); }, expected[2]);
  measure("decode-checkpoints", [&] { return p.template decode<3>(); }, expected[3] / 16, rows / 16);
#endif
#endif
}

template<bool Widths, unsigned Maximum> void dispatch(const std::string& name, unsigned rows, unsigned reps) {
  using sl::Placement;
#if STREAMPACK_DIRECT
  static_assert(!Widths);
  if (name == "planes") run<sl::Metadata<false, Maximum, Placement::planes>, Maximum>(name, rows, reps);
  else if (name == "block-planes") run<sl::Metadata<false, Maximum, Placement::block_planes>, Maximum>(name, rows, reps);
  else if (name == "group-tight") run<sl::Metadata<false, Maximum, Placement::groups>, Maximum>(name, rows, reps);
  else if (name == "group64") run<sl::Metadata<false, Maximum, Placement::groups, 64>, Maximum>(name, rows, reps);
  else if (name == "group96") run<sl::Metadata<false, Maximum, Placement::groups, 96>, Maximum>(name, rows, reps);
  else if (name == "group128") run<sl::Metadata<false, Maximum, Placement::groups, 128>, Maximum>(name, rows, reps);
  else if (name == "series") run<sl::Metadata<false, Maximum, Placement::series>, Maximum>(name, rows, reps);
  else if (name == "tuple") run<sl::Metadata<false, Maximum, Placement::tuple>, Maximum>(name, rows, reps);
  else require(false, "known Direct placement");
#elif STREAMPACK_FIELDS
  static_assert(Widths);
  if (name == "block-planes") run<sl::Metadata<true, Maximum, Placement::block_planes>, Maximum>(name, rows, reps);
  else if (name == "block-hybrid") run<sl::HybridWidth<Maximum, false, true, 0, true>, Maximum>(name, rows, reps);
  else if (name == "hybrid-split32") run<sl::HybridWidth<Maximum, true, true>, Maximum>(name, rows, reps);
  else if (name == "hybrid32") run<sl::HybridWidth<Maximum, false, true>, Maximum>(name, rows, reps);
  else if (name == "hybrid") run<sl::HybridWidth<Maximum>, Maximum>(name, rows, reps);
  else if (name == "hybrid128") run<sl::HybridWidth<Maximum, false, true, 128>, Maximum>(name, rows, reps);
#if !STREAMPACK_EXPLICIT
  else if (name == "tuple64") run<sl::TupleGroup<Maximum, 64>, Maximum>(name, rows, reps);
#endif
  else if (name == "planes") run<sl::Metadata<true, Maximum, Placement::planes>, Maximum>(name, rows, reps);
#if !STREAMPACK_EXPLICIT
  else if (name == "group-tight") run<sl::Metadata<true, Maximum, Placement::groups>, Maximum>(name, rows, reps);
  else if (name == "series") run<sl::Metadata<true, Maximum, Placement::series>, Maximum>(name, rows, reps);
#endif
  else require(false, "known field placement");
#else
  if constexpr (Widths) {
    if (name == "tuple-tight") { run<sl::TupleGroup<Maximum>, Maximum>(name, rows, reps); return; }
    if (name == "tuple64") { run<sl::TupleGroup<Maximum, 64>, Maximum>(name, rows, reps); return; }
  }
  if (name == "planes") run<sl::Metadata<Widths, Maximum, Placement::planes>, Maximum>(name, rows, reps);
  else if (name == "planes-used16") run<sl::Metadata<Widths, Maximum, Placement::planes, 0, true>, Maximum>(name, rows, reps);
  else if (name == "group-tight") run<sl::Metadata<Widths, Maximum, Placement::groups>, Maximum>(name, rows, reps);
  else if (name == "group64") run<sl::Metadata<Widths, Maximum, Placement::groups, 64>, Maximum>(name, rows, reps);
  else if (name == "group96") run<sl::Metadata<Widths, Maximum, Placement::groups, 96>, Maximum>(name, rows, reps);
  else if (name == "group128") run<sl::Metadata<Widths, Maximum, Placement::groups, 128>, Maximum>(name, rows, reps);
  else if (name == "series") run<sl::Metadata<Widths, Maximum, Placement::series>, Maximum>(name, rows, reps);
  else if (name == "tuple") run<sl::Metadata<Widths, Maximum, Placement::tuple>, Maximum>(name, rows, reps);
  else require(false, "known placement");
#endif
}
int perform(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "check") {
    check_starts_native<std::uint8_t>(); check_starts_native<std::uint16_t>();
    check_endpoint_starts_native<std::uint16_t>(); check_endpoint_starts_native<std::uint32_t>();
    check_code_assembly_native<std::uint8_t, 7, 1>();
    check_code_assembly_native<std::uint16_t, 12, 4>(); check_code_assembly_native<std::uint16_t, 13, 3>();
    check_code_assembly_native<std::uint32_t, 20, 8>(); check_code_assembly_native<std::uint32_t, 32, 8>();
#if STREAMPACK_DIRECT
    for (auto name : {"planes", "block-planes", "group-tight", "group64", "group96", "group128", "series", "tuple"}) {
      dispatch<false, 255>(name, 0, 0); dispatch<false, 4095>(name, 0, 0);
      dispatch<false, 4096>(name, 0, 0); dispatch<false, 65535>(name, 0, 0);
    }
#elif STREAMPACK_FIELDS
#if STREAMPACK_EXPLICIT
    for (auto name : {"planes", "hybrid-split32", "hybrid32", "hybrid", "hybrid128", "block-planes", "block-hybrid"}) {
#else
    for (auto name : {"planes", "group-tight", "series", "tuple64", "hybrid-split32", "hybrid32", "hybrid", "hybrid128", "block-planes", "block-hybrid"}) {
#endif
      dispatch<true, 255>(name, 0, 0); dispatch<true, 4095>(name, 0, 0); dispatch<true, 4096>(name, 0, 0);
    }
#else
    for (auto name : {"tuple-tight", "tuple64"}) {
      dispatch<true, 255>(name, 0, 0); dispatch<true, 4095>(name, 0, 0); dispatch<true, 4096>(name, 0, 0);
    }
    for (auto name : {"planes", "planes-used16", "group-tight", "group64", "group96", "group128", "series", "tuple"}) {
      dispatch<false, 255>(name, 0, 0); dispatch<true, 255>(name, 0, 0);
      dispatch<false, 4095>(name, 0, 0); dispatch<true, 4095>(name, 0, 0);
      dispatch<false, 4096>(name, 0, 0); dispatch<true, 4096>(name, 0, 0);
    }
#endif
    std::puts("layout checks passed"); return 0;
  }
  require(argc == 6, "geometry placement maximum rows repetitions");
  const bool widths = std::string(argv[1]) == "width"; require(widths || std::string(argv[1]) == "direct", "geometry");
  const std::string name = argv[2]; const auto maximum = unsigned(std::stoul(argv[3])), rows = unsigned(std::stoul(argv[4])), reps = unsigned(std::stoul(argv[5]));
  require(rows >= 256 && std::has_single_bit(rows) && reps, "benchmark shape");
  std::puts("placement,maximum,rows,operation,repetition,iterations,ns_per_row,metadata_bytes,metadata_owned_bytes,metadata_capacity_bytes,body_bytes,control_bytes,result,items_per_iteration,ns_per_item");
#if STREAMPACK_DIRECT
  require(!widths, "Direct study uses endpoint geometry");
  if (maximum == 255) dispatch<false, 255>(name, rows, reps);
  else if (maximum == 4095) dispatch<false, 4095>(name, rows, reps);
  else if (maximum == 4096) dispatch<false, 4096>(name, rows, reps);
  else if (maximum == 65535) dispatch<false, 65535>(name, rows, reps);
#elif STREAMPACK_FIELDS
  require(widths, "field study uses Width geometry");
  if (maximum == 255) dispatch<true, 255>(name, rows, reps);
  else if (maximum == 4095) dispatch<true, 4095>(name, rows, reps);
  else if (maximum == 4096) dispatch<true, 4096>(name, rows, reps);
#else
  if (maximum == 255) { if (widths) dispatch<true, 255>(name, rows, reps); else dispatch<false, 255>(name, rows, reps); }
  else if (maximum == 4095) { if (widths) dispatch<true, 4095>(name, rows, reps); else dispatch<false, 4095>(name, rows, reps); }
  else if (maximum == 4096) { if (widths) dispatch<true, 4096>(name, rows, reps); else dispatch<false, 4096>(name, rows, reps); }
#endif
  else require(false, "supported maximum");
  return 0;
}

int main(int argc, char** argv) {
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "adopt Lane");
  auto arena = take(oa::open_arena(8ull << 30)); experiment_pool = take(oa::add_pool(*arena, lane));
  const auto result = perform(argc, argv);
  for (;;) { auto progress = oa::close(*experiment_pool); if (progress.kind() == orbital::Progress::Kind::done) break;
    require(progress.kind() != orbital::Progress::Kind::failed, "close Pool"); orbital::worker::poll(lane, 128); std::this_thread::yield(); }
  require(arena->charged_bytes == 0, "layout backing refunded"); require(bool(oa::remove_pool(*experiment_pool)), "remove Pool");
  arena.reset(); orbital::worker::relinquish(lane); return result;
}
