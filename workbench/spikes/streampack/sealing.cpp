#include "direct.h"
#include <orbital/allocation.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <unistd.h>
#include <vector>

namespace sn = streampack_next;
namespace oa = orbital::allocation;
void require(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::abort(); } }
template<class R> auto take(R r) { require(bool(r), "Orbital operation"); return std::move(*r); }
unsigned mix(unsigned x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
unsigned length(unsigned row, unsigned distribution) {
  if (distribution == 0) return mix(row) & 63;
  if (distribution == 1) return row % 8 ? mix(row) & 255 : 4096 + mix(row) % 61440;
  return row % 2 ? 0 : 65535;
}
struct Block {
  oa::Allocation body, geometry;
  std::uint16_t count{};
  unsigned first{}, extent{};
};
template<class End> auto writer(Block& b, unsigned cap) {
  return sn::MutableDirect<End, false, false>{{reinterpret_cast<End*>(b.geometry.address), cap + 1},
      {b.body.address, b.body.size}, {}, {}, b.count};
}

// A stream builder owns actual Orbital allocations. No per-slot allocator,
// reservation manager or MVCC adapter is introduced. Each block rolls over on
// either independent rank or byte pressure. Sidecars are charged at capacity.
// The exact builder pays its count/layout pass; the grow builder gets no future
// lengths. Both use the same input stream and append implementation.
template<class End> void run(oa::Pool& pool, unsigned cap, unsigned budget,
    unsigned distribution, unsigned rows, bool exact, unsigned rep) {
  std::array<std::byte, 65535> input;
  for (unsigned j = 0; j < input.size(); ++j) input[j] = std::byte((j * 31 + j / 251) & 255);
  std::vector<Block> blocks;
  std::uint64_t logical_bytes = 0, growths = 0, relocations = 0, preserved_on_relocation = 0;
  ikea::no_effects effects;
  const auto start = std::chrono::steady_clock::now();
  for (unsigned row = 0; row < rows; ++row) {
    const auto n = length(row, distribution);
    if (blocks.empty() || blocks.back().count == cap || n > budget - blocks.back().extent) {
      Block b; b.first = row;
      b.geometry = take(oa::allocate(pool, {.bytes = (cap + 1) * sizeof(End), .alignment = alignof(End)}));
      if (exact) {
        unsigned extent = 0;
        for (unsigned next = row; next < rows && next - row < cap; ++next) {
          const auto size = length(next, distribution);
          if (size > budget - extent) break;
          extent += size;
        }
        if (extent) b.body = take(oa::allocate(pool, {.bytes = extent, .alignment = 64}));
      }
      blocks.push_back(std::move(b));
      auto w = writer<End>(blocks.back(), cap);
      require(w.initialize(effects) == sn::Error::none, "empty block");
    }
    auto& b = blocks.back();
    if (b.extent + n > b.body.size) {
      require(!exact, "exact layout covers append");
      const auto size = std::min<std::size_t>(budget, std::max<std::size_t>(b.extent + n, std::max<std::size_t>(256, b.body.size * 2)));
      if (!b.body) b.body = take(oa::allocate(pool, {.bytes = size, .alignment = 64}));
      else {
        const auto* old_address = b.body.address; const auto old_size = b.body.size;
        require(bool(oa::grow(b.body, {.bytes = size, .alignment = 64})), "direct grow");
        ++growths;
        if (old_address != b.body.address) { ++relocations; preserved_on_relocation += old_size; }
      }
    }
    // Rebind only after allocation/grow; the external input cannot dangle.
    auto w = writer<End>(b, cap);
    require(w.append(std::span(input).first(n), n, effects) == sn::Error::none, "stream append");
    b.extent += n; logical_bytes += n;
  }
  const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
  std::uint64_t body_size = 0, body_capacity = 0, geometry_size = 0, geometry_capacity = 0, verified_bytes = 0;
  unsigned row = 0;
  for (auto& b : blocks) {
    const auto v = writer<End>(b, cap).read();
    require(b.first == row && v.admit() == sn::Error::none, "continuous admitted blocks");
    unsigned offset = 0;
    for (unsigned r = 0; r < b.count; ++r, ++row) {
      const auto n = length(row, distribution); const auto bytes = v.value(r);
      require(v.start(r) == offset && v.reserved(r) == n && bytes.size() == n, "independent stream geometry");
      for (unsigned j = 0; j < n; ++j) require(bytes[j] == std::byte((j * 31 + j / 251) & 255), "exact stream payload");
      if (n) {
        const auto first = v.reverse(offset), last = v.reverse(offset + n - 1);
        require(first && last && first->rank == r && last->rank == r && !first->tombstone && !last->tombstone, "reverse stream ownership");
      }
      offset += n;
    }
    require(offset == b.extent && !v.reverse(offset), "stream extent");
    require(b.count <= cap && b.extent <= budget, "independent rank and byte limits");
    if (row < rows) require(b.count == cap || length(row, distribution) > budget - b.extent,
        "greedy rollover required by next input");
    verified_bytes += offset; body_size += b.body.size; body_capacity += oa::capacity(b.body);
    geometry_size += b.geometry.size; geometry_capacity += oa::capacity(b.geometry);
  }
  require(row == rows && verified_bytes == logical_bytes, "complete logical population");
  const auto stats = oa::statistics(pool);
  std::ifstream statm("/proc/self/statm"); std::size_t pages = 0, resident = 0;
  require(bool(statm >> pages >> resident), "RSS receipt");
  std::printf("%zu,%u,%u,%u,%u,%s,%u,%zu,%llu,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%llu,%zu\n",
    sizeof(End), cap, budget, distribution, rows, exact ? "exact" : "grow", rep, blocks.size(),
    (unsigned long long)logical_bytes, ns, (unsigned long long)body_size, (unsigned long long)body_capacity,
    (unsigned long long)geometry_size, (unsigned long long)geometry_capacity, (unsigned long long)growths,
    (unsigned long long)relocations, (unsigned long long)preserved_on_relocation,
    (unsigned long long)pool.arena->charged_bytes.load(), (unsigned long long)stats.mapped_bytes,
    (unsigned long long)stats.reserved_bytes, blocks.capacity() * sizeof(Block),
    (unsigned long long)stats.idle_bytes, resident * std::size_t(sysconf(_SC_PAGESIZE)));
}
int main(int argc, char** argv) {
  require(argc == 8, "end-bytes capacity byte-budget distribution rows exact repeats");
  const unsigned end = std::strtoul(argv[1], nullptr, 10), cap = std::strtoul(argv[2], nullptr, 10), budget = std::strtoul(argv[3], nullptr, 10),
      distribution = std::strtoul(argv[4], nullptr, 10), rows = std::strtoul(argv[5], nullptr, 10), exact = std::strtoul(argv[6], nullptr, 10), reps = std::strtoul(argv[7], nullptr, 10);
  require((end == 2 || end == 4) && (cap == 128 || cap == 256 || cap == 512) && budget >= 65535 && budget <= cap * 65535 &&
      (end == 4 || budget == 65535) && distribution <= 2 && rows && rows <= 8192 && exact <= 1 && reps, "bounds");
  orbital::worker::Lane lane; require(bool(orbital::worker::adopt(lane)), "Lane");
  std::puts("end_bytes,capacity,byte_budget,distribution,rows,builder,repetition,blocks,logical_bytes,build_ns,body_size,body_capacity,geometry_size,geometry_capacity,growths,relocations,relocated_old_size,arena_charge,mapped,reserved,control_capacity_bytes,idle,rss");
  for (unsigned rep = 0; rep < reps; ++rep) {
    // A fresh Arena separates retained data from prior repetition allocator cache.
    auto arena = take(oa::open_arena(1536ull << 20)); auto* pool = take(oa::add_pool(*arena, lane));
    if (end == 2) run<std::uint16_t>(*pool, cap, budget, distribution, rows, exact, rep);
    else run<std::uint32_t>(*pool, cap, budget, distribution, rows, exact, rep);
    for (;;) { const auto p = oa::close(*pool); if (p.kind() == orbital::Progress::Kind::done) break;
      require(p.kind() != orbital::Progress::Kind::failed, "close"); orbital::worker::poll(lane, 128); std::this_thread::yield(); }
    require(arena->charged_bytes == 0, "settlement"); require(bool(oa::remove_pool(*pool)), "remove Pool");
  }
  orbital::worker::relinquish(lane);
}
