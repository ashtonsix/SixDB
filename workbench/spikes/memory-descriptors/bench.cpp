#include "arena.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace {
using descriptors::Arena;
using descriptors::Directory;
using descriptors::Head;
using descriptors::Packing;
using descriptors::Palette;
using Clock = std::chrono::steady_clock;

struct Options {
  double milliseconds{20};
  unsigned repetitions{3};
  bool reverse{};
  int metadata_writer_cpu{-1};
};

Options options(int argc, char** argv) {
  Options result;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--quick") {
      result.milliseconds = 2;
      result.repetitions = 1;
    } else if (arg == "--reverseorder") {
      result.reverse = true;
    } else if (arg == "--ms" && i + 1 < argc) {
      result.milliseconds = std::stod(argv[++i]);
    } else if (arg == "--repetitions" && i + 1 < argc) {
      const auto count = std::stoul(argv[++i]);
      if (count > std::numeric_limits<unsigned>::max())
        throw std::invalid_argument("repetition count is too large");
      result.repetitions = static_cast<unsigned>(count);
    } else if (arg == "--metadata-writer" && i + 1 < argc) {
      const auto cpu = std::stol(argv[++i]);
      if (cpu < 0 || cpu > std::numeric_limits<int>::max())
        throw std::invalid_argument("metadata writer CPU must be nonnegative");
      result.metadata_writer_cpu = static_cast<int>(cpu);
    } else if (arg == "--help") {
      std::cout << "memory_descriptors_bench [--quick] [--ms N] "
                   "[--repetitions N] [--reverseorder] [--metadata-writer CPU]\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown or incomplete option: " +
                                  std::string(arg));
    }
  }
  if (!std::isfinite(result.milliseconds) || result.milliseconds <= 0 ||
      !result.repetitions)
    throw std::invalid_argument("duration and repetitions must be positive");
  return result;
}

std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

std::uint64_t word(std::size_t index, unsigned position) {
  return mix(index * 8 + position);
}

std::uint64_t load(const std::byte* pointer, std::size_t offset = 0) {
  std::uint64_t result;
  std::memcpy(&result, pointer + offset, sizeof(result));
  return result;
}

void store(std::byte* pointer, unsigned position, std::uint64_t value) {
  std::memcpy(pointer + position * sizeof(value), &value, sizeof(value));
}

/// Fixed shuffle algorithm, independent of standard-library distribution code.
std::vector<std::size_t> permutation(std::size_t count) {
  std::vector<std::size_t> order(count);
  std::iota(order.begin(), order.end(), 0);
  std::uint64_t entropy = 0x243f6a8885a308d3ULL;
  for (std::size_t remaining = count; remaining > 1; --remaining) {
    entropy = mix(entropy);
    std::swap(order[remaining - 1], order[entropy % remaining]);
  }
  return order;
}

std::byte* hint_address(Head head, const descriptors::Geometry& geometry,
                        Directory directory) {
  constexpr std::size_t grids_per_control_page = 64;
  const auto address = descriptors::native(head);
  const auto page_base = address & ~(geometry.page - 1);
  const auto row = (address & (geometry.page - 1)) / geometry.pitch;
  const auto offset = directory == Directory::planes
      ? 16 * grids_per_control_page + row * 48
      : row * 64 + 16;
  return reinterpret_cast<std::byte*>(page_base + offset);
}

struct Dataset {
  // Member order ensures every scope is released before arena destruction.
  Arena arena;
  Directory directory;
  std::vector<Head> heads;
  std::vector<Head> direct;
  std::vector<std::size_t> order;
  std::vector<descriptors::Scope> scopes;
  std::uint64_t scan_expected{};
  std::uint64_t point_expected{};
  std::uint64_t joint_expected{};

  Dataset(std::size_t count, Directory chosen)
      : arena(Palette::a, chosen, Packing::private_large, count / 16, 7),
        directory(chosen), order(permutation(count)) {
    heads.reserve(count);
    direct.reserve(count);
    scopes.reserve(count / 16);
    for (std::size_t grid = 0; grid < count / 16; ++grid) {
      for (unsigned owner = 0; owner < 16; ++owner) {
        const auto head = arena.create(grid, 64, 0);
        if (!head) throw std::runtime_error("failed to create benchmark owner");
        heads.push_back(head);
      }
      scopes.push_back(arena.protect(grid));
    }
    std::vector<std::size_t> next(count);
    for (std::size_t i = 0; i < count; ++i)
      next[order[i]] = order[(i + 1) % count];
    for (std::size_t index = 0; index < count; ++index) {
      auto* payload = descriptors::resolve(heads[index], arena.layout());
      direct.push_back(reinterpret_cast<Head>(payload) | (Head{7} << 57));
      store(payload, 0, next[index]);
      scan_expected += next[index];
      for (unsigned position = 1; position < 8; ++position) {
        const auto value = word(index, position);
        store(payload, position, value);
        scan_expected += value;
      }
      point_expected += word(index, 1);
      auto* hint = hint_address(heads[index], arena.layout(), directory);
      if (hint != arena.hint(heads[index]) || *hint != std::byte{64})
        throw std::runtime_error("hint arithmetic disagrees with arena");
    }
    for (std::size_t i = 0; i < joint_count(); ++i)
      joint_expected += word(order[i], 1) + 64;
    if (arena.counters().payload_pages != count / 16)
      throw std::runtime_error("64-byte cohort did not occupy one page per grid");
  }

  std::size_t count() const { return heads.size(); }
  std::size_t joint_count() const { return heads.size() / 4; }
};

/// Independent telemetry writes leave the hint, trampolines and payload intact.
class MetadataWriter {
 public:
  MetadataWriter(Dataset& data, int cpu) {
    if (cpu < 0) return;
#if defined(__linux__)
    if (cpu >= CPU_SETSIZE)
      throw std::invalid_argument("metadata writer CPU exceeds affinity mask");
    addresses_.reserve(data.arena.grids());
    for (std::size_t grid = 0; grid < data.arena.grids(); ++grid) {
      auto* bytes = data.arena.hint(data.heads[grid * 16]) + 8;
      if (reinterpret_cast<std::uintptr_t>(bytes) % alignof(std::uint64_t))
        throw std::runtime_error("metadata telemetry address is misaligned");
      addresses_.push_back(reinterpret_cast<std::uint64_t*>(bytes));
    }
    thread_ = std::thread([this, cpu] {
      cpu_set_t affinity;
      CPU_ZERO(&affinity);
      CPU_SET(cpu, &affinity);
      const int error = pthread_setaffinity_np(
          pthread_self(), sizeof(affinity), &affinity);
      if (error) {
        status_.store(-error, std::memory_order_release);
        return;
      }
      std::uint64_t count = 0;
      do {
        for (auto* address : addresses_)
          __atomic_store_n(address, ++count, __ATOMIC_RELAXED);
        updates_.store(count, std::memory_order_relaxed);
        if (status_.load(std::memory_order_relaxed) == 0)
          status_.store(1, std::memory_order_release);
      } while (!stop_.load(std::memory_order_relaxed));
    });
    int status;
    do {
      status = status_.load(std::memory_order_acquire);
      if (!status) std::this_thread::yield();
    } while (!status);
    if (status < 0) {
      thread_.join();
      throw std::runtime_error("metadata writer CPU affinity: " +
                               std::string(std::strerror(-status)));
    }
#else
    throw std::invalid_argument("metadata writer CPU affinity requires Linux");
#endif
  }

  ~MetadataWriter() {
    stop_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
  }
  MetadataWriter(const MetadataWriter&) = delete;
  MetadataWriter& operator=(const MetadataWriter&) = delete;
  std::uint64_t updates() const {
    return updates_.load(std::memory_order_relaxed);
  }

 private:
  std::vector<std::uint64_t*> addresses_;
  std::thread thread_;
  alignas(64) std::atomic<bool> stop_{false};
  alignas(64) std::atomic<int> status_{0};
  alignas(64) std::atomic<std::uint64_t> updates_{0};
};

enum class Path { direct, resolve, borrow };
enum class Workload { scan, point, chase, joint };

struct Case {
  Path path;
  Workload workload;
};

const char* name(Path path) {
  switch (path) {
    case Path::direct: return "direct";
    case Path::resolve: return "protected_resolve";
    case Path::borrow: return "point_borrow";
  }
  std::terminate();
}

const char* name(Workload workload) {
  switch (workload) {
    case Workload::scan: return "scan64";
    case Workload::point: return "random_point8";
    case Workload::chase: return "dependent_chase16";
    case Workload::joint: return "joint_point8_hint1";
  }
  std::terminate();
}

template<Path path>
inline std::byte* payload(Dataset& data, std::size_t index) {
  if constexpr (path == Path::direct)
    return reinterpret_cast<std::byte*>(descriptors::native(data.direct[index]));
  else
    return descriptors::resolve(data.heads[index], data.arena.layout());
}

template<Path path, Workload workload>
std::uint64_t kernel(Dataset& data) {
  std::uint64_t checksum = 0;
  if constexpr (workload == Workload::scan) {
    for (std::size_t index = 0; index < data.count(); ++index) {
      const auto* bytes = payload<path>(data, index);
      for (unsigned position = 0; position < 8; ++position)
        checksum += load(bytes, position * 8);
    }
  } else if constexpr (workload == Workload::chase) {
    std::size_t index = data.order.front();
    for (std::size_t step = 0; step < data.count(); ++step) {
      const auto* bytes = payload<path>(data, index);
      checksum += load(bytes, 8);
      index = load(bytes);
    }
    // The checksum also verifies that the cycle closes at its known start.
    checksum += index;
  } else {
    const auto count = workload == Workload::joint
        ? data.joint_count() : data.count();
    for (std::size_t step = 0; step < count; ++step) {
      const auto index = data.order[step];
      if constexpr (path == Path::borrow) {
        auto grant = data.arena.borrow(data.heads[index]);
        checksum += load(grant.data(), 8);
      } else {
        checksum += load(payload<path>(data, index), 8);
      }
      if constexpr (workload == Workload::joint)
        checksum += std::to_integer<unsigned>(
            *hint_address(data.heads[index], data.arena.layout(), data.directory));
    }
  }
  return checksum;
}

struct Sample {
  double nanoseconds{};
  std::uint64_t iterations{};
  std::uint64_t checksum{};
  std::uint64_t metadata_updates{};
};

template<Path path, Workload workload>
Sample measure(Dataset& data, const Options& opts, MetadataWriter& writer) {
  const auto count = workload == Workload::joint
      ? data.joint_count() : data.count();
  std::uint64_t expected = data.point_expected;
  if constexpr (workload == Workload::scan) expected = data.scan_expected;
  if constexpr (workload == Workload::joint) expected = data.joint_expected;
  if constexpr (workload == Workload::chase) expected += data.order.front();
  if (kernel<path, workload>(data) != expected)
    throw std::runtime_error("benchmark checksum failed before timing");

  const auto batches_per_clock = std::max<std::size_t>(1, 4096 / count);
  std::uint64_t batches = 0;
  std::uint64_t checksum = 0;
  const auto updates_before = writer.updates();
  const auto start = Clock::now();
  auto end = start;
  const auto duration = std::chrono::duration<double, std::milli>(opts.milliseconds);
  do {
    for (std::size_t batch = 0; batch < batches_per_clock; ++batch) {
      // Prevent hoisting a completed read batch across later timing iterations.
      asm volatile("" ::: "memory");
      checksum += kernel<path, workload>(data);
      asm volatile("" : "+r"(checksum) :: "memory");
      ++batches;
    }
    end = Clock::now();
  } while (end - start < duration);
  const auto updates_after = writer.updates();
  if (checksum != expected * batches)
    throw std::runtime_error("benchmark checksum failed after timing");
  const auto iterations = batches * count;
  return {std::chrono::duration<double, std::nano>(end - start).count() /
              static_cast<double>(iterations), iterations, checksum,
              updates_after - updates_before};
}

Sample run(Case selected, Dataset& data, const Options& opts, MetadataWriter& writer) {
  if (selected.path == Path::direct) {
    switch (selected.workload) {
      case Workload::scan: return measure<Path::direct, Workload::scan>(data, opts, writer);
      case Workload::point: return measure<Path::direct, Workload::point>(data, opts, writer);
      case Workload::chase: return measure<Path::direct, Workload::chase>(data, opts, writer);
      case Workload::joint: break;
    }
  } else if (selected.path == Path::resolve) {
    switch (selected.workload) {
      case Workload::scan: return measure<Path::resolve, Workload::scan>(data, opts, writer);
      case Workload::point: return measure<Path::resolve, Workload::point>(data, opts, writer);
      case Workload::chase: return measure<Path::resolve, Workload::chase>(data, opts, writer);
      case Workload::joint: return measure<Path::resolve, Workload::joint>(data, opts, writer);
    }
  } else if (selected.path == Path::borrow && selected.workload == Workload::point) {
    return measure<Path::borrow, Workload::point>(data, opts, writer);
  }
  throw std::logic_error("unsupported benchmark case");
}

void report(Dataset& data, Case selected, unsigned repetition, Sample sample,
            int metadata_writer_cpu) {
  const auto controls = data.arena.control_pages();
  const auto pages = data.arena.counters().payload_pages;
  const auto page = data.arena.layout().page;
  std::cout << data.count() << ",64,"
            << (data.directory == Directory::planes ? "planes" : "records")
            << ',' << name(selected.path) << ',' << name(selected.workload)
            << ',' << repetition << ',' << data.arena.reserved_bytes()
            << ',' << controls << ',' << pages << ',' << (controls + pages) * page
            << ',' << data.count() * sizeof(Head)
            << ',' << (data.heads.size() + data.direct.size()) * sizeof(Head)
            << ',' << data.order.size() * sizeof(std::size_t)
            << ',' << data.arena.grids() << ',' << data.arena.grids() * 256
            << ',' << data.arena.owner_record_bytes()
            << ',' << metadata_writer_cpu << ',' << sample.metadata_updates
            << ',' << std::fixed << std::setprecision(0)
            << static_cast<double>(sample.metadata_updates) * 1e9 /
                   (sample.nanoseconds * static_cast<double>(sample.iterations))
            << ',' << std::fixed << std::setprecision(4) << sample.nanoseconds
            << ',' << sample.iterations << ',' << sample.checksum << '\n';
}
} // namespace

int main(int argc, char** argv) {
  try {
    const auto opts = options(argc, argv);
    std::vector<std::size_t> sizes{256, 4096, 65536};
    std::array directories{Directory::planes, Directory::records};
    std::vector<Case> cases{
        {Path::direct, Workload::scan}, {Path::resolve, Workload::scan},
        {Path::direct, Workload::point}, {Path::resolve, Workload::point},
        {Path::borrow, Workload::point},
        {Path::direct, Workload::chase}, {Path::resolve, Workload::chase},
        {Path::resolve, Workload::joint}};
    if (opts.reverse) {
      std::reverse(sizes.begin(), sizes.end());
      std::reverse(directories.begin(), directories.end());
      std::reverse(cases.begin(), cases.end());
    }
    std::cout << "allocations,payload_bytes,layout,path,workload,repetition,"
                 "reserved_va_bytes,control_pages,payload_pages,mapped_page_bytes,"
                 "active_head_array_bytes,total_head_array_bytes,order_array_bytes,"
                 "safety_registry_rows,safety_registry_homes,owner_record_bytes,"
                 "metadata_writer_cpu,metadata_updates,metadata_updates_per_second,"
                 "ns_per_op,iterations,checksum\n";
    for (const auto size : sizes) {
      for (const auto directory : directories) {
        Dataset data(size, directory);
        // Reverse local destruction joins the writer before releasing the arena.
        MetadataWriter writer(data, opts.metadata_writer_cpu);
        for (const auto selected : cases)
          for (unsigned repetition = 0; repetition < opts.repetitions; ++repetition)
            report(data, selected, repetition, run(selected, data, opts, writer),
                   opts.metadata_writer_cpu);
      }
    }
  } catch (const std::exception& failure) {
    std::cerr << "memory descriptor benchmark: " << failure.what() << '\n';
    return 1;
  }
}
