#include "workload.h"
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace b = orbital_bench;
namespace a = orbital::allocation;
namespace {
bool refuse_pool{};
unsigned refusals{};
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void healthy_store() {
  const auto rows = b::initial_rows(3, 123);
  auto store = b::packed_store(64u << 20);
  store->load(rows);
  std::array<b::Row, 3> output;
  store->begin(false);
  const auto count = store->scan(0, output);
  store->end();
  require(count == rows.size() && std::equal(output.begin(), output.end(), rows.begin()),
          "healthy store after startup refusal returned different rows");
  store->drain(); store->audit();
}
}

// A dedicated Linux linker interposition refuses exactly the public Pool setup
// boundary. No production fault hook or assertions are disabled.
orbital::Result<a::Pool*> real_add_pool(a::Arena&, orbital::worker::Lane&)
    asm("__real__ZN7orbital10allocation8add_poolERNS0_5ArenaERNS_6worker4LaneE");
orbital::Result<a::Pool*> wrap_add_pool(a::Arena&, orbital::worker::Lane&)
    asm("__wrap__ZN7orbital10allocation8add_poolERNS0_5ArenaERNS_6worker4LaneE");
orbital::Result<a::Pool*> wrap_add_pool(a::Arena& arena, orbital::worker::Lane& lane) {
  if (refuse_pool) {
    require(orbital::worker::on_lane(lane), "Pool refusal did not reach the adopted Lane");
    ++refusals;
    return std::unexpected(orbital::Error::capacity);
  }
  return real_add_pool(arena, lane);
}

int main() try {
  bool rejected = false;
  try { (void)b::packed_store(0); }
  catch (const std::runtime_error&) { rejected = true; }
  require(rejected, "zero budget was accepted");
  healthy_store();
  refuse_pool = true;
  rejected = false;
  try { (void)b::packed_store(64u << 20); }
  catch (const std::runtime_error&) { rejected = true; }
  refuse_pool = false;
  require(rejected && refusals == 1, "Pool setup refusal was not exercised exactly once");
  healthy_store();
  std::puts("startup: zero budget and Pool setup refusal unwind; subsequent stores work");
} catch (const std::exception& error) {
  std::fprintf(stderr, "startup: %s\n", error.what());
  return 1;
}
