#include <loom/worker.h>
#include <array>
#include <barrier>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/syscall.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {
namespace alloc = orbital::allocation;
constexpr std::size_t nodes = 128, node_bytes = 64, input_bytes = nodes * node_bytes;
constexpr std::uint64_t arena_bytes = 32 << 20, iteration_limit = 20000000;

void require(bool condition, const char* explanation) {
  if (!condition) throw std::runtime_error(explanation);
}

std::uint64_t number(std::string_view value) {
  std::uint64_t result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(), "invalid integer option");
  return result;
}

struct Options {
  std::string arena{"shared"}, mode{"hold"};
  std::uint64_t workers{1}, iterations{1000000}, repeat{};
  std::vector<int> cpus;

  Options(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
      const std::string_view key(argv[i]);
      require(++i < argc, "missing option value");
      const std::string_view value(argv[i]);
      if (key == "--arena") arena = value;
      else if (key == "--mode") mode = value;
      else if (key == "--workers") workers = number(value);
      else if (key == "--iterations") iterations = number(value);
      else if (key == "--repeat") repeat = number(value);
      else if (key == "--cpus") {
        require(cpus.empty() && !value.empty(), "empty or repeated CPU option");
        std::size_t start = 0;
        for (;;) {
          const auto end = value.find(',', start);
          const auto cpu = number(value.substr(start, end == value.npos ? end : end - start));
          require(cpu < CPU_SETSIZE, "CPU outside supported affinity set");
          cpus.push_back(static_cast<int>(cpu));
          if (end == value.npos) break;
          start = end + 1;
        }
      } else require(false, "unknown option");
    }
    require(workers == 1 || workers == 2 || workers == 4, "workers must be 1, 2 or 4");
    require(arena == "shared" || arena == "separate", "arena must be shared or separate");
    require(mode == "hold" || mode == "owner", "mode must be hold or owner");
    require(iterations && iterations <= iteration_limit, "iterations must be 1..20000000 per worker");
    if (cpus.empty()) for (std::uint64_t i = 0; i < workers; ++i) cpus.push_back(static_cast<int>(i));
    require(cpus.size() == workers, "one distinct CPU is required per worker");
    cpu_set_t allowed;
    require(::sched_getaffinity(0, sizeof(allowed), &allowed) == 0, "read allowed CPUs failed");
    for (std::size_t i = 0; i < cpus.size(); ++i) {
      require(CPU_ISSET(cpus[i], &allowed), "requested CPU is outside caller affinity");
      for (std::size_t j = 0; j < i; ++j) require(cpus[i] != cpus[j], "duplicate CPU");
    }
  }
};

std::uint64_t clock_ns(clockid_t clock) noexcept {
  timespec value;
  if (::clock_gettime(clock, &value)) std::abort();
  return std::uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}

std::string address(const void* value) {
  std::ostringstream stream; stream << value; return stream.str();
}

struct alignas(64) Node {
  std::uint64_t next{}, value{};
  std::array<std::uint64_t, 6> initialized_padding{};
};
static_assert(sizeof(Node) == node_bytes);

constexpr std::uint64_t next(std::uint64_t cursor) { return (cursor * 5 + 3) % nodes; }

constexpr std::uint64_t value(std::uint64_t cursor) { return cursor * 73 + 19; }

struct Kernel { std::uint64_t cursor{}, checksum{}, uses{}; bool complete{true}; };

[[gnu::always_inline]] inline void useful(const Node& node, Kernel& result) noexcept {
  result.checksum += node.value;
  result.cursor = node.next;
}

// Both paths perform the identical dependent useful body. Only the physical
// acquisition/local-retirement path differs; there are no loop clocks/counters
// visible to other threads. Allocation backing remains initialized and live.
template<bool Hold>
[[gnu::noinline]] Kernel native(const alloc::Allocation& allocation,
    const Node* owner_base, std::uint64_t iterations, std::uint64_t cursor) noexcept {
  Kernel result{cursor};
  while (result.uses != iterations) {
    const auto offset = result.cursor * node_bytes;
    if constexpr (Hold) {
      auto held = loom::hold(allocation, {offset, node_bytes});
      if (!held) { result.complete = false; return result; }
      useful(*reinterpret_cast<const Node*>(loom::bytes(*held).data()), result);
      // The actual Hold destructor retires this exact captured node locally.
    } else {
      useful(owner_base[result.cursor], result);
    }
    ++result.uses;
  }
  return result;
}

Kernel expected(std::uint64_t iterations, std::uint64_t cursor) {
  // The prescribed LCG visits all 128 nodes exactly once per cycle. Form the
  // oracle arithmetically, independently of the allocated image/native loop.
  std::uint64_t full = 0;
  for (std::uint64_t i = 0; i < nodes; ++i) full += value(i);
  Kernel result{cursor, iterations / nodes * full, iterations};
  for (std::uint64_t i = 0; i < iterations % nodes; ++i) {
    result.checksum += value(result.cursor); result.cursor = next(result.cursor);
  }
  return result;
}

struct Session {
  loom::Worker worker;
  alloc::Allocation input;
  bool adopted{};

  ~Session() {
    loom::release(input);
    if (!adopted) return;
    const auto deadline = clock_ns(CLOCK_MONOTONIC) + 10000000000ull;
    for (;;) {
      const auto progress = loom::shutdown(worker);
      if (progress.kind() == orbital::Progress::Kind::done) break;
      if (progress.kind() == orbital::Progress::Kind::failed || clock_ns(CLOCK_MONOTONIC) >= deadline)
        std::abort();
      if (!loom::poll(worker, {16, 0})) std::this_thread::yield();
    }
    loom::relinquish(worker);
  }
};

struct alignas(64) Result {
  Kernel native;
  alloc::Statistics pool;
  std::uint64_t begin_ns{}, end_ns{}, thread_cpu_ns{}, tid{};
  int cpu_begin{-1}, cpu_end{-1};
  std::string lane, arena, failure;
};

struct Cohort {
  const Options& options;
  const std::vector<orbital::Ref<alloc::Arena>>& arenas;
  std::array<std::uint64_t, 4> charged_begin{}, charged_end{};
  std::uint64_t begin_ns{}, end_ns{}, process_begin_ns{}, process_end_ns{};
  unsigned phase{};

  void transition() noexcept {
    if (!phase++) {
      for (std::size_t i = 0; i < arenas.size(); ++i)
        charged_begin[i] = arenas[i]->charged_bytes.load(std::memory_order_acquire);
      process_begin_ns = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
      begin_ns = clock_ns(CLOCK_MONOTONIC);
    } else {
      end_ns = clock_ns(CLOCK_MONOTONIC);
      process_end_ns = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
      for (std::size_t i = 0; i < arenas.size(); ++i)
        charged_end[i] = arenas[i]->charged_bytes.load(std::memory_order_acquire);
    }
  }
};

struct Completion { Cohort* cohort; void operator()() noexcept { cohort->transition(); } };
using Gate = std::barrier<Completion>;

void run(const Options& options, const orbital::Ref<alloc::Arena>& arena,
    std::size_t index, Gate& gate, Result& result) noexcept {
  Session session;
  bool ready = false;
  const Node* owner_base = nullptr;
  try {
    require(loom::adopt(session.worker, options.cpus[index]).has_value(), "Worker CPU adoption failed");
    session.adopted = true;
    require(loom::configure(session.worker, arena, 0).has_value(), "Worker configuration failed");
    auto input = loom::allocate(session.worker, {.bytes = input_bytes, .alignment = node_bytes});
    require(input.has_value(), "native input allocation failed");
    session.input = std::move(*input);
    auto* data = reinterpret_cast<Node*>(loom::bytes(session.input).data());
    owner_base = data;
    for (std::uint64_t i = 0; i < nodes; ++i)
      std::construct_at(data + i, Node{next(i), value(i), {i, i, i, i, i, i}});
    // First touch and an untimed full dependent cycle warm the private input.
    const auto warm = native<false>(session.input, owner_base, nodes, index * 13 % nodes);
    require(warm.checksum == expected(nodes, index * 13 % nodes).checksum, "warm input oracle failed");
    result.pool = alloc::statistics(*session.worker.memory);
    result.arena = address(arena.get()); result.lane = address(&session.worker.lane);
    result.tid = static_cast<std::uint64_t>(::syscall(SYS_gettid));
    result.cpu_begin = ::sched_getcpu();
    require(result.cpu_begin == options.cpus[index], "Worker did not execute on requested CPU");
    ready = true;
  } catch (const std::exception& error) { result.failure = error.what(); }

  // A failed setup still participates in both gates; successful peers retire
  // their physical resources normally. No exception can strand a gate partner.
  gate.arrive_and_wait();
  if (ready) {
    const auto cpu_start = clock_ns(CLOCK_THREAD_CPUTIME_ID);
    result.begin_ns = clock_ns(CLOCK_MONOTONIC);
    result.native = options.mode == "hold"
        ? native<true>(session.input, owner_base, options.iterations, index * 13 % nodes)
        : native<false>(session.input, owner_base, options.iterations, index * 13 % nodes);
    result.end_ns = clock_ns(CLOCK_MONOTONIC);
    result.thread_cpu_ns = clock_ns(CLOCK_THREAD_CPUTIME_ID) - cpu_start;
  }
  gate.arrive_and_wait();
  try {
    if (ready) {
      result.cpu_end = ::sched_getcpu();
      const auto wanted = expected(options.iterations, index * 13 % nodes);
      require(result.native.complete && result.native.uses == wanted.uses &&
          result.native.checksum == wanted.checksum && result.native.cursor == wanted.cursor, "timed useful oracle failed");
      require(result.cpu_end == options.cpus[index], "Worker left requested CPU");
      const auto* data = reinterpret_cast<const Node*>(loom::bytes(session.input).data());
      for (std::uint64_t i = 0; i < nodes; ++i) {
        require(data[i].next == next(i) && data[i].value == value(i), "native input mutated");
        for (auto padding : data[i].initialized_padding) require(padding == i, "initialized node footprint mutated");
      }
    }
  } catch (const std::exception& error) { result.failure = error.what(); }
}
} // namespace

int main(int argc, char** argv) try {
  const Options options(argc, argv);
  std::vector<orbital::Ref<alloc::Arena>> arenas;
  arenas.reserve(options.workers);
  for (std::size_t i = 0; i < options.workers; ++i) {
    if (i && options.arena == "shared") arenas.push_back(arenas.front());
    else {
      auto arena = alloc::open_arena(arena_bytes);
      require(arena.has_value(), "open Arena failed");
      arenas.push_back(std::move(*arena));
    }
  }
  Cohort cohort{options, arenas};
  Gate gate(static_cast<std::ptrdiff_t>(options.workers + 1), Completion{&cohort});
  std::vector<Result> results(options.workers);
  std::vector<std::thread> threads;
  threads.reserve(options.workers);
  try {
    for (std::size_t i = 0; i < options.workers; ++i)
      threads.emplace_back([&, i] { run(options, arenas[i], i, gate, results[i]); });
  } catch (...) {
    // Remove all unstarted participants, then release and join actual owners.
    for (std::size_t i = threads.size(); i < options.workers; ++i) gate.arrive_and_drop();
    gate.arrive_and_wait(); gate.arrive_and_wait();
    for (auto& thread : threads) thread.join();
    throw;
  }
  gate.arrive_and_wait(); gate.arrive_and_wait();
  for (auto& thread : threads) thread.join();
  std::uint64_t checksum = 0, uses = 0, thread_cpu = 0;
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    require(result.failure.empty(), result.failure.c_str());
    require(arenas[i]->charged_bytes.load(std::memory_order_acquire) == 0, "Arena charge survived owner shutdown");
    require(cohort.charged_begin[i] == cohort.charged_end[i], "timed loop changed prepared Arena charge");
    for (std::size_t j = 0; j < i; ++j) {
      require(result.tid != results[j].tid && result.lane != results[j].lane, "workers shared a thread or Lane identity");
      require((result.arena == results[j].arena) == (options.arena == "shared"), "Arena control identity mismatch");
    }
    checksum += result.native.checksum; uses += result.native.uses; thread_cpu += result.thread_cpu_ns;
  }
  std::cout << "{\"family\":\"loom-hold\",\"variant\":\"" << options.arena << '-' << options.mode
      << "\",\"unit\":\"dependent node use\",\"repeat\":" << options.repeat
      << ",\"verified\":true,\"items\":" << uses << ",\"elapsed_ns\":" << cohort.end_ns - cohort.begin_ns
      << ",\"parameters\":{\"workers\":\"" << options.workers << "\",\"arena\":\"" << options.arena
      << "\",\"mode\":\"" << options.mode << "\",\"iterations_per_worker\":\"" << options.iterations
      << "\",\"input\":\"private warm 8KiB,128 dependent64B nodes per worker\","
      << "\"timed_scope\":\"barrier cohort includes gate dispatch/arrival; worker region is native loop; setup/oracle/shutdown excluded\","
      << "\"diagnostic\":\"acquisition and local retirement contention; excludes foreign handoff and Objects/Views\"},"
      << "\"counters\":{\"uses\":" << uses << ",\"hold_acquires\":" << (options.mode == "hold" ? uses : 0)
      << ",\"hold_local_retires\":" << (options.mode == "hold" ? uses : 0)
      << ",\"thread_cpu_ns_sum\":" << thread_cpu
      << ",\"process_cpu_ns\":" << cohort.process_end_ns - cohort.process_begin_ns
      << ",\"checksum\":" << checksum << ",\"charged_after_shutdown\":0},\"workers\":[";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i]; const auto& pool = result.pool;
    if (i) std::cout << ',';
    std::cout << "{\"index\":" << i << ",\"requested_cpu\":" << options.cpus[i]
        << ",\"cpu_begin\":" << result.cpu_begin << ",\"cpu_end\":" << result.cpu_end
        << ",\"tid\":" << result.tid << ",\"lane_identity\":\"" << result.lane
        << "\",\"arena_identity\":\"" << result.arena << "\",\"elapsed_ns\":" << result.end_ns - result.begin_ns
        << ",\"start_after_cohort_ns\":" << result.begin_ns - cohort.begin_ns
        << ",\"end_after_cohort_ns\":" << result.end_ns - cohort.begin_ns
        << ",\"thread_cpu_ns\":" << result.thread_cpu_ns << ",\"uses\":" << result.native.uses
        << ",\"checksum\":" << result.native.checksum << ",\"final_cursor\":" << result.native.cursor
        << ",\"pool_mapped_bytes\":" << pool.mapped_bytes << ",\"pool_reserved_bytes\":" << pool.reserved_bytes
        << ",\"pool_metadata_bytes\":" << pool.metadata_bytes << ",\"pool_idle_bytes\":" << pool.idle_bytes
        << ",\"pool_allocations\":" << pool.allocations << ",\"pool_reuses\":" << pool.reuses
        << ",\"pool_growths\":" << pool.growths << ",\"pool_relocations\":" << pool.relocations
        << ",\"pool_foreign_returns\":" << pool.foreign_returns << ",\"pool_trim_pages\":" << pool.trim_pages
        << ",\"arena_charged_before\":" << cohort.charged_begin[i]
        << ",\"arena_charged_after_native\":" << cohort.charged_end[i] << '}';
  }
  std::cout << "]}\n";
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "loom_hold_probe: %s\n", error.what());
  return 1;
}
