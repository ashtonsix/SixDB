// A distinct allocation lifecycle cohort, not a database transaction timing.
// Real packs grow/rebind, foreign readers retire, and VM work physically drains.
#include <ikea/seriespack.h>
#include <ikea/tuplepack.h>
#include <loom/worker.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sched.h>
#include <string>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
namespace sp = ikea::seriespack;
namespace tp = ikea::tuplepack;
namespace alloc = orbital::allocation;
using Clock = std::chrono::steady_clock;
using Format = sp::format<20>;
// Both columns remain within Orbital's finite ceiling in the finite treatment.
constexpr std::size_t prefix_rows = 7, full_rows = 128, stride = 20;
constexpr auto value_mask = (1u << 20) - 1;

[[noreturn]] void fail(const char* reason) {
  std::fprintf(stderr, "Orbital pressure: %s\n", reason);
  std::abort();
}
void require(bool value, const char* reason) { if (!value) fail(reason); }
template<class T, class E> T take(std::expected<T, E> value, const char* reason) {
  if (!value) fail(reason);
  return std::move(*value);
}
template<class E> void okay(std::expected<void, E> value, const char* reason) {
  require(bool(value), reason);
}
std::uint64_t ns(Clock::duration elapsed) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
}
void pin(pid_t tid, int cpu) {
  cpu_set_t mask;
  CPU_ZERO(&mask); CPU_SET(cpu, &mask);
  require(!::sched_setaffinity(tid, sizeof(mask), &mask), "set thread affinity");
}
std::uint64_t mix(std::uint64_t h, std::uint64_t value) {
  return (h ^ value) * 1099511628211ull;
}
std::uint64_t key(std::size_t round, std::size_t slot, std::size_t row) {
  return (std::uint64_t(round) << 32) | (std::uint64_t(slot) << 16) | row;
}
std::uint32_t value(std::uint64_t k) {
  return std::uint32_t((k * 271u + (k >> 32) * 193u + 17u) & value_mask);
}
std::uint32_t tag(std::uint64_t k) { return std::uint32_t(k ^ (k >> 32) ^ 0xa5b6c7d8u); }
std::uint64_t row_hash(std::uint64_t h, std::uint64_t k, std::uint64_t next,
                       std::uint32_t v, std::uint32_t t) {
  return mix(mix(mix(mix(h, k), next), v), t);
}
std::uint64_t expected(std::size_t round, std::size_t slot, std::size_t rows) {
  std::uint64_t h = 1469598103934665603ull;
  for (std::size_t i = 0; i < rows; ++i) {
    const auto k = key(round, slot, i);
    h = row_hash(h, k, key(round, slot, i + 1), value(k), tag(k));
  }
  return h;
}

struct Schema {
  std::array<tp::code, stride> codes;
  tp::layout layout;
  tp::constructor constructor;
  tp::reader<8> keys, links, tags;
  static std::array<tp::code, stride> make_codes() {
    std::array<tp::code, stride> result;
    for (std::size_t i = 0; i < stride; ++i) result[i] = {tp::byte(i), 0, 8};
    return result;
  }
  Schema() : codes(make_codes()), layout(take(tp::layout::make(stride, codes), "Tuple layout")),
      constructor(take(tp::constructor::make(layout), "Tuple constructor")),
      keys(take(tp::reader<8>::make(layout, std::array<tp::byte, 8>{0,1,2,3,4,5,6,7}), "key reader")),
      links(take(tp::reader<8>::make(layout, std::array<tp::byte, 8>{8,9,10,11,12,13,14,15}), "link reader")),
      tags(take(tp::reader<8>::make(layout, std::array<tp::byte, 4>{16,17,18,19}), "tag reader")) {}
  Schema(const Schema&) = delete;
  Schema& operator=(const Schema&) = delete;
};
struct Batch {
  alloc::Allocation series, tuples;
};
struct Effects {
  std::uint64_t bytes{}, spans{};
  void consume(const ikea::source_write_journal& journal, const void* source, std::size_t extent) {
    for (const auto& issued : journal.entries()) {
      require(issued.source == source && issued.bytes.plane == 0 &&
          issued.bytes.offset <= extent && issued.bytes.size <= extent - issued.bytes.offset,
          "issued effect outside qualified pack");
      bytes += issued.bytes.size; ++spans;
    }
  }
};
void write(Batch& batch, const Schema& schema, std::size_t round, std::size_t slot,
           std::size_t rows, std::size_t first, Effects& effects) {
  // These named stack bindings and their erased endpoints retire before grow.
  auto series = take(sp::view<Format, std::uint8_t>::attach(rows,
      {{{{reinterpret_cast<std::uint8_t*>(batch.series.address), batch.series.size}, Format::tile_bytes}, {}, {}}}),
      "Series view");
  auto mutation = take(sp::bind_mutation(series), "Series mutation");
  std::array<std::uint32_t, full_rows> values{};
  for (std::size_t i = first; i < rows; ++i) values[i - first] = value(key(round, slot, i));
  std::array<ikea::owner_write, full_rows * 4> writes;
  ikea::source_write_journal journal{writes};
  if (!first) {
    okay(mutation.initialize(std::span<const std::uint32_t>(values.data(), rows), journal), "Series initialize");
  } else {
    sp::sum_change delta;
    okay(mutation.replace(first, std::span<const std::uint32_t>(values.data(), rows - first),
                          sp::row_selection::all(), delta, journal), "Series append");
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < rows - first; ++i) sum += values[i];
    require(delta.finish() == sum, "Series append delta");
  }
  effects.consume(journal, &series, batch.series.size);
  journal.used = 0;
  auto tuples = take(tp::view::bind(schema.layout,
      {reinterpret_cast<tp::byte*>(batch.tuples.address), batch.tuples.size}, rows, stride), "Tuple view");
  auto construction = take(tp::bind_constructor(schema.constructor, tuples), "Tuple binding");
  std::array<tp::construction_input, full_rows> inputs{};
  for (std::size_t i = first; i < rows; ++i) {
    const auto k = key(round, slot, i), next = key(round, slot, i + 1);
    const auto t = tag(k);
    for (unsigned j = 0; j < 8; ++j) { inputs[i-first][j] = k >> (8*j); inputs[i-first][8+j] = next >> (8*j); }
    for (unsigned j = 0; j < 4; ++j) inputs[i-first][16+j] = t >> (8*j);
  }
  okay(construction.initialize(first,
      std::span<const tp::construction_input>(inputs.data(), rows - first), journal), "Tuple construction");
  effects.consume(journal, &tuples, batch.tuples.size);
}
std::uint64_t read(orbital::MutableBytes series_bytes, orbital::MutableBytes tuple_bytes,
                   const Schema& schema, std::size_t rows) {
  auto series = take(sp::view<Format, std::uint8_t>::attach(rows,
      {{{{reinterpret_cast<std::uint8_t*>(series_bytes.data()), series_bytes.size()}, Format::tile_bytes}, {}, {}}}), "read Series view");
  auto values = sp::bind_decoder<std::uint32_t>(series);
  auto tuples = take(tp::view::bind(schema.layout,
      {reinterpret_cast<tp::byte*>(tuple_bytes.data()), tuple_bytes.size()}, rows, stride), "read Tuple view");
  auto keys = take(tp::bind_reader(schema.keys, tuples), "bound key reader");
  auto links = take(tp::bind_reader(schema.links, tuples), "bound link reader");
  auto tags = take(tp::bind_reader(schema.tags, tuples), "bound tag reader");
  std::uint64_t h = 1469598103934665603ull;
  for (std::size_t i = 0; i < rows; ++i)
    h = row_hash(h, take(keys.get(i), "key"), take(links.get(i), "link"),
        take(values.get(i), "value"), std::uint32_t(take(tags.get(i), "tag")));
  return h;
}

// One independent foreign reader, with an operation-owned completion. Pool
// capacity_changed alone need not pulse when one Hold retires but owner remains.
struct Foreign {
  const Schema& schema;
  std::atomic<unsigned> phase{}; // 0 idle, 1 read, 2 read done, 3 release, 4 done, 5 stop
  alloc::Hold series, tuples;
  std::uint64_t digest{};
  std::size_t rows{};
  pid_t tid{};
  std::thread thread;
  Foreign(const Schema& s, int cpu) : schema(s), thread([this,cpu] {
    tid = pid_t(::syscall(SYS_gettid)); pin(0, cpu);
    for (;;) {
      auto state = phase.load(std::memory_order_acquire);
      if (state == 0 || state == 4) { phase.wait(state, std::memory_order_acquire); continue; }
      if (state == 5) return;
      require(state == 1, "foreign handshake");
      digest = read(alloc::bytes(series), alloc::bytes(tuples), schema, rows);
      phase.store(2, std::memory_order_release); phase.notify_one();
      phase.wait(2, std::memory_order_acquire);
      require(phase.load(std::memory_order_acquire) == 3, "foreign release handshake");
      alloc::release(series, {.temperature = alloc::Temperature::cold});
      alloc::release(tuples, {.temperature = alloc::Temperature::cold});
      phase.store(4, std::memory_order_release); phase.notify_one();
    }
  }) {}
  void start(Batch& batch) {
    require(phase.load(std::memory_order_acquire) == 0 || phase.load(std::memory_order_acquire) == 4, "foreign busy");
    series = take(loom::hold(batch.series, {0, batch.series.size}), "foreign Series Hold");
    tuples = take(loom::hold(batch.tuples, {0, batch.tuples.size}), "foreign Tuple Hold");
    rows = full_rows;
    phase.store(1, std::memory_order_release); phase.notify_one();
  }
  void stop() {
    require(phase.load(std::memory_order_acquire) == 0 || phase.load(std::memory_order_acquire) == 4, "foreign stop while active");
    phase.store(5, std::memory_order_release); phase.notify_one(); thread.join();
  }
  ~Foreign() { require(!thread.joinable(), "foreign thread not drained"); }
};
struct Observed {
  std::uint64_t charged{}, mapped{}, metadata{}, reserved{};
  void sample(const loom::Worker& worker, const alloc::Arena& arena) {
    const auto s = alloc::statistics(*worker.memory);
    charged = std::max(charged, arena.charged_bytes.load());
    mapped = std::max(mapped, s.mapped_bytes); metadata = std::max(metadata, s.metadata_bytes);
    reserved = std::max(reserved, s.reserved_bytes);
    require(arena.charged_bytes <= arena.byte_limit, "Arena exceeded limit");
  }
};
std::uint64_t wait_phase(Foreign& foreign, unsigned phase, loom::Worker& worker) {
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  std::uint64_t polls = 0;
  while (foreign.phase.load(std::memory_order_acquire) != phase) {
    require(Clock::now() < deadline, "foreign read/release timeout");
    loom::poll(worker, {64,0}); ++polls; std::this_thread::yield();
  }
  return polls;
}
std::uint64_t trim(loom::Worker& worker) {
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  std::uint64_t polls = 0;
  for (;;) {
    auto status = alloc::trim_cold(*worker.memory, 0);
    if (status.kind() == orbital::Progress::Kind::done) return polls;
    require(status.kind() != orbital::Progress::Kind::failed, "trim failed");
    require(Clock::now() < deadline, "trim physical drain timeout");
    loom::poll(worker, {64,0}); ++polls; std::this_thread::yield();
  }
}
struct Config {
  std::filesystem::path output;
  std::size_t rounds{64}, slots{16}, trim_every{4}, foreign_every{4};
  std::uint64_t limit{8u << 20};
  int cpu{-1}, helper{-1};
  std::string runtime{"mixed"};
};
Config arguments(int argc, char** argv) {
  Config c;
  for (int i = 1; i < argc; i += 2) {
    require(i + 1 < argc, "arguments need values");
    const std::string name = argv[i], arg = argv[i+1];
    if (name == "--output") c.output = arg;
    else if (name == "--runtime") c.runtime = arg;
    else if (name == "--rounds") c.rounds = std::stoull(arg);
    else if (name == "--slots") c.slots = std::stoull(arg);
    else if (name == "--arena-bytes") c.limit = std::stoull(arg);
    else if (name == "--trim-every") c.trim_every = std::stoull(arg);
    else if (name == "--foreign-every") c.foreign_every = std::stoull(arg);
    else if (name == "--cpu") c.cpu = std::stoi(arg);
    else if (name == "--helper-cpu") c.helper = std::stoi(arg);
    else fail("unknown argument");
  }
  require(!c.output.empty() && c.rounds && c.rounds <= 100000 && c.slots && c.slots <= 256 &&
      c.limit >= (1u << 20) && c.trim_every && c.foreign_every, "invalid configuration");
  require(c.runtime == "finite" || c.runtime == "window" || c.runtime == "mixed", "runtime must be finite/window/mixed");
  cpu_set_t allowed; require(!::sched_getaffinity(0, sizeof(allowed), &allowed), "get caller affinity");
  for (int i = 0; i < CPU_SETSIZE; ++i) if (CPU_ISSET(i, &allowed)) { if (c.cpu < 0) c.cpu = i; break; }
  if (c.helper < 0) {
    c.helper = c.cpu;
    for (int i = 0; i < CPU_SETSIZE; ++i) if (i != c.cpu && CPU_ISSET(i, &allowed)) { c.helper = i; break; }
  }
  require(c.cpu >= 0 && c.cpu < CPU_SETSIZE && c.helper >= 0 && c.helper < CPU_SETSIZE &&
      CPU_ISSET(c.cpu, &allowed) && CPU_ISSET(c.helper, &allowed), "CPU outside initial allowed set");
  require(!std::filesystem::exists(c.output) || std::filesystem::is_empty(c.output), "output must be new/empty");
  std::filesystem::create_directories(c.output);
  return c;
}
} // namespace

int main(int argc, char** argv) {
  const auto c = arguments(argc, argv);
  Schema schema;
  auto arena = take(alloc::open_arena(c.limit, c.helper), "open bounded Arena with helper affinity");
  loom::Worker worker;
  okay(loom::adopt(worker, c.cpu), "adopt caller");
  okay(loom::configure(worker, arena, 0), "configure Worker");
  Foreign foreign(schema, c.helper);
  std::vector<Batch> slots(c.slots);
  std::vector<std::uint64_t> samples;
  samples.reserve(c.rounds);
  struct Cycle {
    std::uint64_t elapsed, charged;
    alloc::Statistics inventory;
  };
  std::vector<Cycle> cycles;
  cycles.reserve(c.rounds);
  std::uint64_t checksum = 0, expected_checksum = 0, rows = 0, polls = 0, conflicts = 0;
  for (std::size_t r = 0; r < c.rounds; ++r) {
    for (std::size_t s = 0; s < c.slots; ++s) expected_checksum = mix(expected_checksum, expected(r,s,full_rows));
    if (r % c.foreign_every == 0) expected_checksum = mix(expected_checksum, expected(r,0,full_rows));
  }
  Effects effects;
  Observed peaks;
  alloc::Statistics last{};
  rusage usage_before{}, usage_after{};
  require(!::getrusage(RUSAGE_SELF, &usage_before), "initial faults");
  const auto started = Clock::now();
  for (std::size_t r = 0; r < c.rounds; ++r) {
    const auto cycle_started = Clock::now();
    for (std::size_t s = 0; s < c.slots; ++s) {
      auto& batch = slots[s];
      const bool window = c.runtime == "window" || (c.runtime == "mixed" && s % 2);
      const auto reserve = window ? std::size_t{16384} : 0;
      batch.series = take(loom::allocate(worker,
          {.bytes = Format::tile_bytes, .reserved_bytes = reserve, .alignment = 64,
           .initialization = alloc::Initialization::zero}), "allocate Series prefix");
      batch.tuples = take(loom::allocate(worker,
          {.bytes = prefix_rows * stride, .reserved_bytes = reserve, .alignment = 64,
           .initialization = alloc::Initialization::zero}), "allocate Tuple prefix");
      write(batch,schema,r,s,prefix_rows,0,effects);
      if (!s) {
        // A real physical grant refuses relocation/overwrite without corrupting prefix.
        auto held = take(loom::hold(batch.tuples,{0,batch.tuples.size}), "growth conflict Hold");
        auto rejected = loom::grow(batch.tuples,
            {.bytes = full_rows * stride, .reserved_bytes = reserve, .alignment = 64,
             .initialization = alloc::Initialization::zero});
        if (!window) {
          require(!rejected && rejected.error() == orbital::Error::conflict, "finite growth must conflict with Hold");
          ++conflicts;
        } else {
          // Window growth is disjoint from the held prefix, so it may succeed.
          okay(std::move(rejected), "disjoint window growth");
        }
        require(read(alloc::bytes(batch.series),alloc::bytes(held),schema,prefix_rows) == expected(r,s,prefix_rows), "held prefix changed");
      }
      okay(loom::grow(batch.series,
          {.bytes = full_rows * 20 / 8, .reserved_bytes = reserve, .alignment = 64,
           .initialization = alloc::Initialization::zero}), "Series grow/rebind");
      okay(loom::grow(batch.tuples,
          {.bytes = full_rows * stride, .reserved_bytes = reserve, .alignment = 64,
           .initialization = alloc::Initialization::zero}), "Tuple grow/rebind");
      write(batch,schema,r,s,full_rows,prefix_rows,effects);
      checksum = mix(checksum, read(alloc::bytes(batch.series),alloc::bytes(batch.tuples),schema,full_rows));
      rows += full_rows;
      peaks.sample(worker,*arena);
    }
    if (r % c.foreign_every == 0) {
      foreign.start(slots[0]);
      polls += wait_phase(foreign,2,worker);
      checksum = mix(checksum, foreign.digest);
      alloc::release(slots[0].series,{.temperature = alloc::Temperature::cold});
      alloc::release(slots[0].tuples,{.temperature = alloc::Temperature::cold});
      peaks.sample(worker,*arena); // Actual bytes remain charged while foreign Holds live.
      foreign.phase.store(3,std::memory_order_release); foreign.phase.notify_one();
      polls += wait_phase(foreign,4,worker);
    }
    for (auto& batch : slots) {
      alloc::release(batch.series,{.temperature = alloc::Temperature::cold});
      alloc::release(batch.tuples,{.temperature = alloc::Temperature::cold});
    }
    loom::poll(worker,{1024,0}); ++polls;
    if ((r + 1) % c.trim_every == 0) polls += trim(worker);
    peaks.sample(worker,*arena);
    samples.push_back(ns(Clock::now() - cycle_started));
    last = alloc::statistics(*worker.memory);
    cycles.push_back({samples.back(), arena->charged_bytes.load(), last});
  }
  const auto drain_started = Clock::now();
  polls += trim(worker);
  last = alloc::statistics(*worker.memory);
  const auto final_charged = arena->charged_bytes.load();
  foreign.stop(); // Join actual reader after completion; included in final drain.
  const auto deadline = Clock::now() + std::chrono::seconds(10);
  for (;;) {
    const auto status = loom::shutdown(worker);
    if (status.kind() == orbital::Progress::Kind::done) break;
    require(status.kind() != orbital::Progress::Kind::failed, "shutdown failed");
    require(Clock::now() < deadline, "shutdown physical drain timeout");
    loom::poll(worker,{1024,0}); ++polls; std::this_thread::yield();
  }
  const auto stopped = Clock::now();
  require(arena->charged_bytes == 0, "budget remains after actual shutdown");
  require(checksum == expected_checksum, "complete replay checksum differs from independent values");
  require(!::getrusage(RUSAGE_SELF, &usage_after), "final faults");
  loom::relinquish(worker);
  std::ofstream requests(c.output / "cycles.csv");
  requests << "cycle,elapsed_ns,rows,arena_charged_bytes,mapped_bytes,metadata_bytes,idle_bytes\n";
  for (std::size_t r = 0; r < cycles.size(); ++r) {
    const auto& row = cycles[r];
    requests << r << ',' << row.elapsed << ',' << c.slots * full_rows << ','
             << row.charged << ',' << row.inventory.mapped_bytes << ',' << row.inventory.metadata_bytes
             << ',' << row.inventory.idle_bytes << '\n';
  }
  std::sort(samples.begin(), samples.end());
  auto quantile = [&](double p) { return samples[std::size_t(p * (samples.size()-1))]; };
  std::ofstream out(c.output / "result.json");
  out << "{\n  \"format\": 1, \"cohort\": \"allocation-pack-lifecycle\",\n"
      << "  \"runtime\": \"" << c.runtime << "\", \"offered_cycles\": " << c.rounds
      << ", \"completed_cycles\": " << samples.size() << ", \"rows_constructed\": " << rows << ",\n"
      << "  \"caller_cpu\": " << c.cpu << ", \"foreign_cpu\": " << c.helper << ", \"vm_helper_cpu\": " << c.helper
      << ", \"vm_helper_placement\": \"Arena startup affinity\", \"foreign_tid\": " << foreign.tid << ",\n"
      << "  \"arena_limit_bytes\": " << c.limit << ", \"slots\": " << c.slots
      << ", \"trim_every_cycles\": " << c.trim_every << ", \"foreign_every_cycles\": " << c.foreign_every << ",\n"
      << "  \"elapsed_ns\": " << ns(stopped-started) << ", \"drain_ns\": " << ns(stopped-drain_started)
      << ", \"cycle_p50_ns\": " << quantile(.50) << ", \"cycle_p99_ns\": " << quantile(.99) << ",\n"
      << "  \"observed_peak_charged_bytes\": " << peaks.charged << ", \"observed_peak_mapped_bytes\": " << peaks.mapped
      << ", \"observed_peak_metadata_bytes\": " << peaks.metadata << ", \"observed_peak_reserved_bytes\": " << peaks.reserved << ",\n"
      << "  \"after_trim_charged_bytes\": " << final_charged << ", \"after_trim_mapped_bytes\": " << last.mapped_bytes
      << ", \"after_trim_metadata_bytes\": " << last.metadata_bytes << ", \"after_shutdown_charged_bytes\": 0,\n"
      << "  \"allocations\": " << last.allocations << ", \"reuses\": " << last.reuses << ", \"growths\": " << last.growths
      << ", \"relocations\": " << last.relocations << ", \"foreign_returns\": " << last.foreign_returns
      << ", \"trim_pages\": " << last.trim_pages << ", \"growth_conflicts\": " << conflicts << ", \"polls\": " << polls << ",\n"
      << "  \"issued_write_bytes\": " << effects.bytes << ", \"issued_write_spans\": " << effects.spans
      << ", \"minor_faults\": " << usage_after.ru_minflt - usage_before.ru_minflt
      << ", \"major_faults\": " << usage_after.ru_majflt - usage_before.ru_majflt
      << ", \"checksum\": " << checksum << ", \"verified\": true\n}\n";
  require(bool(requests) && bool(out), "write receipts");
  std::printf("%s: %zu lifecycle cycles, %llu rows, checksum verified, zero final charge\n",
      c.runtime.c_str(), c.rounds, static_cast<unsigned long long>(rows));
}
