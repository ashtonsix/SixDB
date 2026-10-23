// Logical footprint counters (not timings): mapped, reserved and metadata bytes
// for fixed live populations, before and after trim. Deterministic per build.
#include <orbital/allocation.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace a = orbital::allocation;
namespace w = orbital::worker;

void require(bool ok, const char* why) {
  if (!ok) { std::fprintf(stderr, "failed: %s\n", why); std::abort(); }
}

struct Owner {
  orbital::Ref<a::Arena> arena;
  w::Lane lane;
  a::Pool* pool{};
  Owner() : arena(*a::open_arena(4ull << 30)) {
    require(bool(w::adopt(lane)), "adopt"); pool = *a::add_pool(*arena, lane);
  }
  template<class F> void settle(F step) {
    for (;;) {
      auto progress = step();
      require(progress.kind() != orbital::Progress::Kind::failed, "settle");
      if (progress.kind() == orbital::Progress::Kind::done) return;
      w::poll(lane, 4096); std::this_thread::yield();
    }
  }
  ~Owner() { settle([&] { return a::close(*pool); }); (void)a::remove_pool(*pool); w::relinquish(lane); }
};

void report(const char* name, Owner& owner, const std::vector<a::Allocation>& live) {
  std::size_t requested = 0, capacity = 0;
  for (const auto& value : live) { requested += value.size; capacity += a::capacity(value); }
  const auto row = [&](const char* phase) {
    const auto s = a::statistics(*owner.pool);
    std::printf("%s,%s,%zu,%zu,%zu,%llu,%llu,%llu,%llu\n", name, phase, live.size(), requested, capacity,
        static_cast<unsigned long long>(s.mapped_bytes), static_cast<unsigned long long>(s.idle_bytes),
        static_cast<unsigned long long>(s.metadata_bytes), static_cast<unsigned long long>(s.reserved_bytes));
  };
  row("live");
  owner.settle([&] { return a::trim_cold(*owner.pool, 0); });
  row("trimmed");
}

void population(const char* name, const std::vector<std::size_t>& sizes, std::size_t alignment = 16) {
  Owner owner;
  std::vector<a::Allocation> live;
  for (auto bytes : sizes) {
    auto value = a::allocate(*owner.pool, {.bytes = bytes, .alignment = alignment});
    require(bool(value), "allocate");
    value->address[0] = std::byte{1};
    live.push_back(std::move(*value));
  }
  report(name, owner, live);
}
} // namespace

int main() {
  std::printf("population,phase,allocations,requested,capacity,mapped,idle,metadata,reserved\n");
  std::vector<std::size_t> each{16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072};
  population("one-per-class", each);
  std::vector<std::size_t> tiny(64, 64);
  population("64x64B", tiny);
  for (std::size_t count : {1000, 100000}) {
    std::mt19937_64 random(7);
    std::vector<std::size_t> mixed(count);
    for (auto& bytes : mixed) bytes = 16 + random() % 3057;
    population(("uniform16-3072x" + std::to_string(count)).c_str(), mixed);
    std::vector<std::size_t> skewed(count);
    for (auto& bytes : skewed) bytes = 16 + static_cast<std::size_t>(std::exp2(4 + (random() % 1000) / 1000.0 * 7.5));
    population(("log16-3072x" + std::to_string(count)).c_str(), skewed);
  }
  std::vector<std::size_t> lines(20000, 200);
  population("cacheline200x20000", lines, 64);
}
