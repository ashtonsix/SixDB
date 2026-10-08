#include "workload.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace b = orbital_bench;
void require(bool ok, const char* what) {
  if (!ok) { std::fprintf(stderr, "scalar bounds: %s\n", what); std::abort(); }
}
int main() {
  const auto rows = b::initial_rows(256, 42421);
  for (auto access : {b::Access::owner_ready, b::Access::fresh_hold, b::Access::retained_hold}) {
    auto store = b::packed_store(64u << 20, access, true);
    store->load(rows);
    std::span<std::uint16_t> ranks;
    for (const auto& r : store->memory_regions())
      if (r.kind == "rank_index") ranks = {reinterpret_cast<std::uint16_t*>(r.address), r.bytes / 2};
    require(ranks.size() == 256, "fixture must expose one complete rank map");
    const std::vector<std::uint16_t> original(ranks.begin(), ranks.end());
    const auto before = store->counters(false);
    std::fill(ranks.begin(), ranks.end(), std::numeric_limits<std::uint16_t>::max());
    const b::Row sentinel{123, {456, 789, 101}};
    std::array<b::Row, 8> output; output.fill(sentinel);
    auto reject = [&](auto operation) {
      bool rejected = false;
      store->begin(true);
      try { operation(); } catch (const std::runtime_error&) { rejected = true; }
      store->end();
      require(rejected, "invalid physical slot must reject before payload access");
    };
    reject([&] { (void)store->get(rows[0].key); });
    reject([&] { (void)store->scan(rows[0].key, output); });
    reject([&] { (void)store->update(rows[0].key, rows[1].record); });
    reject([&] { (void)store->erase(rows[0].key); });
    reject([&] { (void)store->insert(rows[0].key + 1, rows[1].record); });
    require(std::all_of(output.begin(), output.end(), [&](auto row) { return row == sentinel; }),
            "rejected scan changed output");
    store->begin(false);
    require(store->scan(rows[0].key, {}) == 0, "empty output must not inspect rank map");
    require(!store->get(rows.back().key + 1), "fence miss must not inspect rank map");
    store->end();
    const auto after = store->counters(false);
    require(after.issued_write_bytes == before.issued_write_bytes &&
            after.index_write_bytes == before.index_write_bytes &&
            after.logical_live_rows == before.logical_live_rows,
            "rejected scalar request changed maintenance");
    std::copy(original.begin(), original.end(), ranks.begin());
    store->audit();
    std::vector<b::Row> actual(rows.size());
    store->begin(false);
    require(store->scan(0, actual) == rows.size() && actual == rows,
            "restored rank map does not recover original independent rows");
    store->end();
  }
  std::puts("scalar bounds: all three access modes reject malformed physical slots without writes; empty/fence misses bypass payload");
}
