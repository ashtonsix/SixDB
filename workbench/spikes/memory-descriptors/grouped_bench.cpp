#include "grouped.h"

#include <algorithm>
#include <array>
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
#include <unistd.h>
#include <vector>

namespace {
using namespace descriptors;
using clock_type = std::chrono::steady_clock;
constexpr std::array all_layouts{
    grouped_layout::grid_major, grouped_layout::class_major,
    grouped_layout::class_major_reordered, grouped_layout::class_aligned};
struct options {
  double ms{20};
  unsigned repetitions{3};
  bool reverse{};
  std::vector<std::size_t> widths{16, 64, 256, 1024};
  std::vector<std::size_t> granules;
  std::vector<std::size_t> cohorts{64, 4096};
  std::vector<grouped_layout> layouts{all_layouts.begin(), all_layouts.end()};
  std::string source_context{"unfrozen"};
};

options parse_options(int argc, char **argv) {
  options result;
  const auto native_page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  result.granules = {native_page};
  if (native_page == 4096)
    result.granules.push_back(16384);
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--quick") {
      result.ms = 1;
      result.repetitions = 1;
    } else if (arg == "--reverseorder")
      result.reverse = true;
    else if (arg == "--ms" && i + 1 < argc)
      result.ms = std::stod(argv[++i]);
    else if (arg == "--repetitions" && i + 1 < argc) {
      const auto count = std::stoul(argv[++i]);
      if (count > std::numeric_limits<unsigned>::max())
        throw std::invalid_argument("repetition count overflow");
      result.repetitions = static_cast<unsigned>(count);
    } else if (arg == "--width" && i + 1 < argc)
      result.widths = {std::stoul(argv[++i])};
    else if (arg == "--granule" && i + 1 < argc)
      result.granules = {std::stoul(argv[++i])};
    else if (arg == "--allocations" && i + 1 < argc)
      result.cohorts = {std::stoul(argv[++i])};
    else if (arg == "--source-context" && i + 1 < argc)
      result.source_context = argv[++i];
    else if (arg == "--layout" && i + 1 < argc) {
      const std::string_view selected(argv[++i]);
      const auto found = std::find_if(
          all_layouts.begin(), all_layouts.end(),
          [&](auto layout) { return selected == grouped_layout_name(layout); });
      if (found == all_layouts.end())
        throw std::invalid_argument("unknown layout");
      result.layouts = {*found};
    } else if (arg == "--help") {
      std::cout
          << "grouped_bench [--quick] [--ms N] [--repetitions N] "
             "[--reverseorder] [--width N] [--granule N] "
             "[--allocations N] [--layout NAME] [--source-context TOKEN]\n";
      std::exit(0);
    } else
      throw std::invalid_argument("unknown or incomplete option");
  }
  if (!std::isfinite(result.ms) || result.ms <= 0 || !result.repetitions)
    throw std::invalid_argument("duration and repetitions must be positive");
  for (const auto count : result.cohorts)
    if (!count || count % 16)
      throw std::invalid_argument("cohort must be multiple of16");
  if (result.source_context.empty() ||
      result.source_context.find_first_of(",\r\n\"") != std::string::npos)
    throw std::invalid_argument("source context must be a CSV-safe token");
  if (result.reverse) {
    std::reverse(result.widths.begin(), result.widths.end());
    std::reverse(result.granules.begin(), result.granules.end());
    std::reverse(result.cohorts.begin(), result.cohorts.end());
    std::reverse(result.layouts.begin(), result.layouts.end());
  }
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
std::uint64_t load(const std::byte *address, std::size_t offset = 0) {
  std::uint64_t result;
  std::memcpy(&result, address + offset, 8);
  return result;
}
void store(std::byte *address, unsigned position, std::uint64_t value) {
  std::memcpy(address + position * 8, &value, 8);
}
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
struct dataset {
  grouped_arena arena;
  std::vector<Head> heads;
  std::vector<Head> direct;
  std::vector<std::size_t> order;
  std::uint64_t expected_scan{}, expected_point{};
  dataset(grouped_layout layout, std::size_t width, std::size_t granule,
          std::size_t count)
      : arena(layout, width, (count + width - 1) / width, granule),
        order(permutation(count)) {
    std::vector<std::size_t> next(count);
    for (std::size_t i = 0; i < count; ++i)
      next[order[i]] = order[(i + 1) % count];
    heads.reserve(count);
    direct.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto head = arena.allocate_in_grid(index / 16, 64);
      if (!head)
        throw std::runtime_error("access cohort identity unavailable");
      heads.push_back(head);
      auto *address = grouped_resolve(head, arena.layout());
      direct.push_back(reinterpret_cast<Head>(address) | (Head{9} << 57));
      store(address, 0, next[index]);
      expected_scan += next[index];
      for (unsigned position = 1; position < 8; ++position) {
        store(address, position, word(index, position));
        expected_scan += word(index, position);
      }
      expected_point += word(index, 1);
    }
  }
  ~dataset() {
    for (const auto head : heads)
      arena.release(head);
  }
};
enum class access { scan, point, chase };
struct access_case {
  bool direct;
  access workload;
};
const char *access_name(access workload) {
  switch (workload) {
  case access::scan:
    return "scan64";
  case access::point:
    return "random_point8";
  case access::chase:
    return "dependent_chase16";
  }
  std::terminate();
}
template <bool direct> std::byte *address(dataset &data, std::size_t index) {
  if constexpr (direct)
    return reinterpret_cast<std::byte *>(native(data.direct[index]));
  else
    return grouped_resolve(data.heads[index], data.arena.layout());
}
template <bool direct, access workload>
std::uint64_t read_kernel(dataset &data) {
  std::uint64_t checksum = 0;
  const auto count = data.heads.size();
  if constexpr (workload == access::scan) {
    for (std::size_t index = 0; index < count; ++index) {
      const auto *payload = address<direct>(data, index);
      for (unsigned position = 0; position < 8; ++position)
        checksum += load(payload, position * 8);
    }
  } else if constexpr (workload == access::point) {
    for (const auto index : data.order)
      checksum += load(address<direct>(data, index), 8);
  } else {
    auto index = data.order.front();
    for (std::size_t step = 0; step < count; ++step) {
      const auto *payload = address<direct>(data, index);
      checksum += load(payload, 8);
      index = load(payload);
    }
    checksum += index;
  }
  return checksum;
}
struct sample {
  double ns{};
  std::uint64_t iterations{}, checksum{};
};
template <class kernel>
sample measure(kernel work, std::size_t operations, std::uint64_t expected,
               const options &config) {
  if (work() != expected)
    throw std::runtime_error("independent checksum failed before timing");
  const auto repeats = std::max<std::size_t>(1, 4096 / operations);
  std::uint64_t checksum = 0, batches = 0;
  const auto start = clock_type::now();
  auto end = start;
  const auto duration = std::chrono::duration<double, std::milli>(config.ms);
  do {
    for (std::size_t repetition = 0; repetition < repeats; ++repetition) {
      asm volatile("" ::: "memory");
      checksum += work();
      asm volatile("" : "+r"(checksum)::"memory");
      ++batches;
    }
    end = clock_type::now();
  } while (end - start < duration);
  if (checksum != expected * batches)
    throw std::runtime_error("checksum failed after timing");
  const auto count = batches * operations;
  return {std::chrono::duration<double, std::nano>(end - start).count() /
              static_cast<double>(count),
          count, checksum};
}
template <bool direct, access workload>
sample read_sample(dataset &data, const options &config) {
  auto expected =
      workload == access::scan ? data.expected_scan : data.expected_point;
  if constexpr (workload == access::chase)
    expected += data.order.front();
  return measure([&] { return read_kernel<direct, workload>(data); },
                 data.heads.size(), expected, config);
}
sample read_sample(dataset &data, access_case selected, const options &config) {
  if (selected.direct) {
    switch (selected.workload) {
    case access::scan:
      return read_sample<true, access::scan>(data, config);
    case access::point:
      return read_sample<true, access::point>(data, config);
    case access::chase:
      return read_sample<true, access::chase>(data, config);
    }
  } else {
    switch (selected.workload) {
    case access::scan:
      return read_sample<false, access::scan>(data, config);
    case access::point:
      return read_sample<false, access::point>(data, config);
    case access::chase:
      return read_sample<false, access::chase>(data, config);
    }
  }
  std::terminate();
}

constexpr std::size_t pairs_per_batch = 4096;
constexpr std::uint64_t pair_value = 0x0123456789abcdefULL;
std::uint64_t allocation_kernel(grouped_arena &arena, std::size_t bytes) {
  std::uint64_t checksum = 0;
  for (std::size_t pair = 0; pair < pairs_per_batch; ++pair) {
    const auto head = arena.allocate_in_grid(0, bytes);
    if (!head)
      throw std::runtime_error("prepared allocation identity unavailable");
    auto *payload = reinterpret_cast<volatile std::uint64_t *>(
        grouped_resolve(head, arena.layout()));
    *payload = pair_value + pair;
    checksum += *payload;
    arena.release(head);
  }
  return checksum;
}

const char *architecture() {
#if defined(__x86_64__)
  return "x86_64";
#elif defined(__aarch64__)
  return "aarch64";
#else
  return "other";
#endif
}
void report(const options &config, grouped_arena &arena,
            std::size_t allocations, std::size_t bytes, const char *path,
            const char *workload, const char *unit, unsigned repetition,
            sample result, bool paired) {
  const auto &geometry = arena.layout();
  const auto counters = arena.counters();
  const auto mapped = (arena.control_granules() + counters.payload_granules) *
                      geometry.mapping_granule;
  std::cout << config.source_context << ',' << architecture() << ",clang-"
            << __clang_major__ << '.' << __clang_minor__ << '.'
            << __clang_patchlevel__ << ',' << geometry.native_page << ','
            << geometry.mapping_granule << ','
            << (geometry.native_page == geometry.mapping_granule
                    ? "native"
                    : "modeled_mapping_granule")
            << ',' << geometry.trampolines_per_group << ','
            << grouped_layout_name(geometry.kind) << ',' << allocations << ','
            << bytes << ',' << path << ',' << workload << ',' << unit << ','
            << repetition << ',' << arena.reserved_bytes() << ','
            << arena.control_granules() << ',' << counters.payload_granules
            << ',' << mapped << ',' << mapped / geometry.native_page << ','
            << arena.owner_record_bytes() << ','
            << (paired ? 0 : allocations * sizeof(Head)) << ','
            << (paired ? 16 * sizeof(Head) : 2 * allocations * sizeof(Head))
            << ',' << (paired ? 0 : allocations * sizeof(std::size_t)) << ','
            << counters.live_allocations << ','
            << counters.cached_empty_granules << ',' << counters.map_calls
            << ',' << counters.released_granules << ',' << std::fixed
            << std::setprecision(4) << result.ns << ',';
  if (paired)
    std::cout << result.ns;
  std::cout << ',' << result.iterations << ',' << result.checksum << '\n';
}

void access_runs(const options &config, grouped_layout layout,
                 std::size_t width, std::size_t granule) {
  std::vector<access_case> cases{{true, access::scan},  {false, access::scan},
                                 {true, access::point}, {false, access::point},
                                 {true, access::chase}, {false, access::chase}};
  if (config.reverse)
    std::reverse(cases.begin(), cases.end());
  for (const auto count : config.cohorts) {
    dataset data(layout, width, granule, count);
    for (const auto selected : cases)
      for (unsigned repetition = 0; repetition < config.repetitions;
           ++repetition)
        report(config, data.arena, count, 64,
               selected.direct ? "direct" : "grouped_resolve",
               access_name(selected.workload), "allocation_access", repetition,
               read_sample(data, selected, config), false);
  }
}
void allocation_runs(const options &config, grouped_layout layout,
                     std::size_t width, std::size_t granule) {
  std::array<std::size_t, 3> sizes{16, 64, 96};
  if (config.reverse)
    std::reverse(sizes.begin(), sizes.end());
  for (const auto bytes : sizes) {
    grouped_arena arena(layout, width, 1, granule);
    std::vector<Head> preparation;
    for (unsigned identity = 0; identity < 16; ++identity) {
      const auto head = arena.allocate_in_grid(0, bytes);
      if (!head)
        throw std::runtime_error("allocation preparation failed");
      // Fault and initialize every legal home in this class/grid outside
      // timing.
      std::memset(grouped_resolve(head, arena.layout()), 0,
                  arena.capacity(head));
      preparation.push_back(head);
    }
    for (const auto head : preparation)
      arena.release(head);
    const auto maps = arena.counters().map_calls;
    const auto expected = pair_value * pairs_per_batch +
                          pairs_per_batch * (pairs_per_batch - 1) / 2;
    for (unsigned repetition = 0; repetition < config.repetitions;
         ++repetition) {
      const auto before = arena.counters();
      const auto result =
          measure([&] { return allocation_kernel(arena, bytes); },
                  pairs_per_batch, expected, config);
      const auto after = arena.counters();
      if (after.map_calls != maps || after.live_allocations ||
          after.allocations - before.allocations !=
              result.iterations + pairs_per_batch ||
          after.releases - before.releases !=
              result.iterations + pairs_per_batch)
        throw std::runtime_error(
            "allocation/free count or prepared-mapping invariant failed");
      report(config, arena, 1, bytes, "prepared_grid",
             "allocate_touch8_release", "allocation_free_pair", repetition,
             result, true);
    }
    arena.trim();
    if (arena.counters().payload_granules)
      throw std::runtime_error("empty prepared backing was not reclaimed");
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    const auto config = parse_options(argc, argv);
    std::cout << "source_context,architecture,compiler,native_page_bytes,"
                 "mapping_granule_bytes,"
                 "mapping_granule_mode,trampolines_per_group,layout,"
                 "allocations,payload_bytes,"
                 "path,workload,unit,repetition,reserved_va_bytes,control_"
                 "granules,payload_granules,"
                 "mapped_page_bytes,mapped_native_pages,owner_record_bytes,"
                 "active_head_array_bytes,total_head_array_bytes,order_array_"
                 "bytes,live_allocations,"
                 "cached_empty_granules,map_calls,released_granules,ns_per_op,"
                 "ns_per_pair,iterations,checksum\n";
    for (const auto granule : config.granules)
      for (const auto width : config.widths)
        for (const auto layout : config.layouts) {
          if (config.reverse)
            allocation_runs(config, layout, width, granule);
          access_runs(config, layout, width, granule);
          if (!config.reverse)
            allocation_runs(config, layout, width, granule);
        }
  } catch (const std::exception &failure) {
    std::cerr << "grouped benchmark: " << failure.what() << '\n';
    return 1;
  }
}
