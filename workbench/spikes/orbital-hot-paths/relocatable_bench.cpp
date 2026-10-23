// Readers on other cores resolve relocatable handles while the owner replaces
// payloads. Every read validates its payload's header and fill against the
// handle and generation it names, so a premature free shows up as an error.
// ns/access is a whole-loop mean per reader; ns/move is the owner's mean.
#include "relocatable.h"
#include <orbital/worker.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace a = orbital::allocation;
namespace w = orbital::worker;
using Clock = std::chrono::steady_clock;

void require(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "failed: %s\n", why); std::abort(); } }
void pin(int cpu) {
  if (cpu < 0) return;
  cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
  require(::sched_setaffinity(0, sizeof set, &set) == 0, "affinity");
}

constexpr std::uint32_t handles = 4096;
// Payload: 8-byte header (handle << 32 | generation), then the generation's low byte.
void fill(std::byte* bytes, std::size_t size, std::uint32_t handle, std::uint32_t generation) {
  const std::uint64_t header = (std::uint64_t{handle} << 32) | generation;
  std::memcpy(bytes, &header, 8);
  std::memset(bytes + 8, static_cast<int>(generation & 0xff), size - 8);
}
// Every byte a reader may see belongs to one generation of its own handle.
// A published extent may be the smaller of a replacement's old and new sizes.
bool valid(relocatable::View view, std::uint32_t handle) {
  if (!view.address || view.size < 64) return false;
  std::uint64_t header; std::memcpy(&header, view.address, 8);
  const auto generation = static_cast<std::uint32_t>(header);
  const auto expected = static_cast<std::byte>(generation & 0xff);
  return (header >> 32) == handle && view.address[8] == expected && view.address[view.size - 1] == expected;
}

struct Run {
  int readers; bool moving; std::size_t batch; bool direct;
};

void run(const Run& config, const std::vector<int>& cpus, double seconds) {
  auto arena = *a::open_arena(4ull << 30);
  w::Lane lane; require(bool(w::adopt(lane)), "adopt");
  auto* pool = *a::add_pool(*arena, lane);
  std::vector<relocatable::Reader> readers(3);
  auto table = std::make_unique<relocatable::Table>(*pool, handles, std::span(readers).first(config.readers));
  std::vector<std::uint32_t> generation(handles, 0);
  std::vector<const std::byte*> direct(handles);
  for (std::uint32_t h = 0; h < handles; ++h) {
    auto handle = *table->create(64);
    require(handle == h, "dense handles");
    fill(table->writable(h), 64, h, 0);
    direct[h] = table->writable(h);
  }
  std::atomic<bool> start{false}, stop{false};
  struct alignas(64) Result { std::uint64_t accesses{}, errors{}; double ns{}; };
  std::vector<Result> results(config.readers);
  std::vector<std::thread> threads;
  for (int r = 0; r < config.readers; ++r) threads.emplace_back([&, r] {
    pin(cpus[1 + r]);
    std::uint64_t state = 0x9e3779b97f4a7c15ull * (r + 1), accesses = 0, errors = 0;
    while (!start.load(std::memory_order_acquire)) {}
    const auto begin = Clock::now();
    while (!stop.load(std::memory_order_relaxed)) {
      if (config.direct) {
        for (std::size_t i = 0; i < config.batch; ++i) {
          state ^= state << 13; state ^= state >> 7; state ^= state << 17;
          const auto h = static_cast<std::uint32_t>(state % handles);
          errors += !valid({direct[h], 64}, h); // Same validation, no indirection.
        }
      } else {
        relocatable::Table::Section section(*table, readers[r]);
        for (std::size_t i = 0; i < config.batch; ++i) {
          state ^= state << 13; state ^= state >> 7; state ^= state << 17;
          const auto h = static_cast<std::uint32_t>(state % handles);
          errors += !valid(table->resolve(h), h);
        }
      }
      accesses += config.batch;
    }
    results[r] = {accesses, errors, std::chrono::duration<double, std::nano>(Clock::now() - begin).count()};
  });
  pin(cpus[0]);
  std::uint64_t moves = 0, collected = 0, peak_retired = 0;
  double move_ns = 0;
  start.store(true, std::memory_order_release);
  const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
  std::uint64_t state = 42;
  while (Clock::now() < deadline) {
    if (!config.moving) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
    const auto begin = Clock::now();
    for (int i = 0; i < 256; ++i) {
      state ^= state << 13; state ^= state >> 7; state ^= state << 17;
      const auto h = static_cast<std::uint32_t>(state % handles);
      const auto g = ++generation[h];
      const std::size_t bytes = (g & 1) ? 200 : 64; // Alternate classes: every replacement moves.
      require(bool(table->replace(h, bytes, [&](std::byte* p, std::size_t n) { fill(p, n, h, g); })), "replace");
      ++moves;
    }
    peak_retired = std::max<std::uint64_t>(peak_retired, table->retired());
    collected += table->collect();
    move_ns += std::chrono::duration<double, std::nano>(Clock::now() - begin).count();
    w::poll(lane, 64);
  }
  stop.store(true);
  for (auto& t : threads) t.join();
  collected += table->collect();
  require(table->retired() == 0, "retirement backlog after readers left");
  std::uint64_t errors = 0; double reader_ns = 0;
  for (const auto& r : results) { errors += r.errors; reader_ns += r.ns / std::max<std::uint64_t>(1, r.accesses); }
  std::printf("%s,%d,%d,%zu,%.3f,%llu,%.2f,%llu,%llu\n", config.direct ? "direct" : "handle", config.readers,
      int(config.moving), config.batch, config.readers ? reader_ns / config.readers : 0.0,
      static_cast<unsigned long long>(errors), moves ? move_ns / moves : 0.0,
      static_cast<unsigned long long>(moves), static_cast<unsigned long long>(peak_retired));
  std::fflush(stdout);
  for (std::uint32_t h = 0; h < handles; ++h) table->destroy(h);
  table->collect();
  table.reset();
  for (;;) {
    auto progress = a::close(*pool);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    require(progress.kind() != orbital::Progress::Kind::failed, "close");
    w::poll(lane, 4096); std::this_thread::yield();
  }
  (void)a::remove_pool(*pool); w::relinquish(lane);
}
} // namespace

int main(int argc, char** argv) {
  std::vector<int> cpus{0, 1, 2, 3};
  double seconds = 1.0;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--cpus" && i + 1 < argc) {
      cpus.clear();
      for (char* cursor = argv[++i]; *cursor;) { cpus.push_back(int(std::strtol(cursor, &cursor, 10))); if (*cursor == ',') ++cursor; }
    } else if (arg == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
  }
  require(cpus.size() >= 4, "need four CPUs");
  std::printf("reader,readers,moving,batch,reader_ns_per_access,errors,owner_ns_per_move,moves,peak_retired\n");
  for (int readers : {1, 3}) {
    run({readers, false, 64, true}, cpus, seconds);
    for (std::size_t batch : {1, 64}) {
      run({readers, false, batch, false}, cpus, seconds);
      run({readers, true, batch, false}, cpus, seconds);
    }
  }
}
