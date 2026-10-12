// Measures candidate tables through the frozen experimental FiniteGroup. The small
// controller uses newest-first prepared/missing passes; it omits Loom, async
// preparation and the production four-entry ready cache. Samples are batch means.
#include "backend/finite.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#if defined(__linux__)
#include <sched.h>
#endif
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
namespace finite = orbital::allocation::palette_experiment;
using Clock = std::chrono::steady_clock;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template<class T> T take(orbital::Result<T> result) {
  require(result.has_value(), "FiniteGroup operation failed");
  return std::move(*result);
}

void take(orbital::Result<void> result) {
  require(result.has_value(), "FiniteGroup operation failed");
}

std::size_t number(std::string_view text) {
  std::size_t value{};
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  require(!text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(),
          "invalid integer option");
  return value;
}

struct Palette { std::string name; std::vector<std::size_t> capacities; };

struct Options {
  std::string label{"candidate"}, workload{"mixed"}, stage{"all"};
  std::vector<Palette> palettes;
  std::size_t page{}, class_bits{4}, population{1024}, rounds{8}, batch{32};
  std::size_t alignment{16}, repetition{}, seed{20261012};
  std::size_t request_max{3072}, growth_end{3072};
  int cpu{-1};
  bool pack_small{}, strict{}, baseline_route{}, class_lookup{}, class_once{true};
};

Options options(int argc, char** argv) {
  Options result;
  for (int i = 1; i < argc; ++i) {
    const std::string_view key{argv[i]};
    require(i + 1 < argc, "each option needs a value");
    const std::string_view value{argv[++i]};
    if (key == "--palette") {
      const auto equals = value.find('=');
      require(equals && equals != std::string_view::npos, "palette must be name=capacities");
      Palette palette{std::string(value.substr(0, equals)), {}};
      auto tail = value.substr(equals + 1);
      for (;;) {
        const auto comma = tail.find(',');
        palette.capacities.push_back(number(tail.substr(0, comma)));
        if (comma == std::string_view::npos) break;
        tail.remove_prefix(comma + 1);
      }
      require(std::is_sorted(palette.capacities.begin(), palette.capacities.end()) &&
              std::adjacent_find(palette.capacities.begin(), palette.capacities.end()) ==
                  palette.capacities.end() && palette.capacities.front() >= 16 &&
              palette.capacities.back() <= 65535, "classes must increase within 16..65535");
      result.palettes.push_back(std::move(palette));
    } else if (key == "--label") result.label = value;
    else if (key == "--workload") result.workload = value;
    else if (key == "--stage") result.stage = value;
    else if (key == "--selection") {
      require(value == "strict" || value == "unrestricted", "unknown selection");
      result.strict = value == "strict";
    } else if (key == "--routing") {
      require(value == "baseline" || value == "best", "unknown routing");
      result.baseline_route = value == "baseline";
    } else if (key == "--pack-small") {
      require(value == "0" || value == "1", "pack-small must be 0 or 1");
      result.pack_small = value == "1";
    } else if (key == "--class-lookup") {
      require(value == "0" || value == "1", "class-lookup must be 0 or 1");
      result.class_lookup = value == "1";
    } else if (key == "--class-once") {
      require(value == "0" || value == "1", "class-once must be 0 or 1");
      result.class_once = value == "1";
    } else {
      const auto parsed = number(value);
      if (key == "--page") result.page = parsed;
      else if (key == "--class-bits") result.class_bits = parsed;
      else if (key == "--population") result.population = parsed;
      else if (key == "--rounds") result.rounds = parsed;
      else if (key == "--batch") result.batch = parsed;
      else if (key == "--alignment") result.alignment = parsed;
      else if (key == "--repetition") result.repetition = parsed;
      else if (key == "--seed") result.seed = parsed;
      else if (key == "--request-max") result.request_max = parsed;
      else if (key == "--growth-end") result.growth_end = parsed;
      else if (key == "--cpu") result.cpu = static_cast<int>(parsed);
      else require(false, "unknown option");
    }
  }
  require(!result.palettes.empty() && result.population && result.rounds && result.batch &&
          (result.class_bits == 4 || result.class_bits == 5) &&
          std::has_single_bit(result.alignment), "missing/invalid benchmark configuration");
  for (const auto& p : result.palettes) {
    require(p.name.find_first_of(",\n\r") == std::string::npos, "invalid palette name");
    require(p.capacities.size() <= (result.class_bits == 4 ? 16u : 32u),
            "too many classes for binding partition");
  }
  const auto host = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  const auto page = result.page ? result.page : host;
  std::size_t largest = 0;
  for (const auto& p : result.palettes) largest = std::max(largest, p.capacities.back());
  require(result.request_max >= 16 && result.request_max <= largest && result.request_max <= page &&
          result.growth_end >= 256 && result.growth_end % 16 == 0 &&
          result.growth_end <= largest && result.growth_end <= page,
          "request/growth limits exceed table or page, or growth end is not a 16-byte step");
  require(!result.baseline_route || result.palettes.size() >= 2, "baseline routing needs A/B");
  require(result.label.find_first_of(",\n\r") == std::string::npos, "invalid candidate label");
  constexpr std::array<std::string_view, 9> stages{"all", "direct", "fresh", "resolve", "churn", "churn-trim", "trim", "growth-exact", "growth-geometric"};
  require(std::find(stages.begin(), stages.end(), result.stage) != stages.end(), "unknown stage");
#if defined(__linux__)
  cpu_set_t allowed;
  require(!::sched_getaffinity(0, sizeof(allowed), &allowed), "read CPU affinity");
  if (result.cpu < 0)
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
      if (CPU_ISSET(cpu, &allowed)) { result.cpu = cpu; break; }
  require(result.cpu >= 0 && result.cpu < CPU_SETSIZE && CPU_ISSET(result.cpu, &allowed),
          "CPU not in allowed affinity");
  CPU_ZERO(&allowed); CPU_SET(result.cpu, &allowed);
  require(!::sched_setaffinity(0, sizeof(allowed), &allowed), "pin CPU");
#else
  require(result.cpu < 0, "explicit CPU pinning is only supported on Linux");
#endif
  return result;
}

std::unique_ptr<finite::FiniteGroup> geometry(const Options& config, std::size_t palette) {
  finite::FiniteConfig shape;
  shape.capacities = config.palettes[palette].capacities;
  shape.class_bits = static_cast<std::uint8_t>(config.class_bits);
  shape.page_granule = config.page;
  constexpr std::array<std::size_t, 5> legacy_prefix{16, 32, 48, 64, 96};
  const auto& capacities = config.palettes[palette].capacities;
  shape.pack_small = config.pack_small && capacities.size() >= legacy_prefix.size() &&
      std::equal(legacy_prefix.begin(), legacy_prefix.end(), capacities.begin());
  shape.class_lookup = config.class_lookup;
  auto result = take(finite::FiniteGroup::create(shape));
  take(result->prepare());
  return result;
}

std::vector<std::size_t> requests(const Options& config) {
  std::mt19937_64 generator(config.seed);
  std::vector<std::size_t> result(config.population);
  constexpr std::array<std::size_t, 8> tiny{24, 40, 72, 88, 112, 144, 176, 208};
  constexpr std::array<std::size_t, 9> boundary{193, 257, 513, 769, 1025, 1537, 1793, 2049, 2561};
  for (std::size_t i = 0; i < result.size(); ++i) {
    if (config.workload == "mixed") result[i] = 16 + generator() % (config.request_max - 15);
    else if (config.workload == "tiny") result[i] = tiny[generator() % tiny.size()];
    else if (config.workload == "cluster") result[i] = 2017 + generator() % 96;
    else if (config.workload == "boundary") result[i] = boundary[generator() % boundary.size()];
    else if (config.workload == "max") result[i] = std::max<std::size_t>(16, config.request_max * 125 / 128);
    else if (config.workload == "homogeneous") result[i] = std::max<std::size_t>(16, config.request_max / 12);
    else if (config.workload == "log") {
      const auto power = generator() % 8;
      result[i] = std::min<std::size_t>(3072, (std::size_t{16} << power) + generator() % (16 << power));
    } else require(false, "unknown workload");
  }
  return result;
}

std::byte pattern(std::uint64_t seed) { return std::byte{static_cast<std::uint8_t>(1 + seed * 37 % 251)}; }

struct Counters {
  std::uint64_t claims{}, examined_groups{}, new_groups{}, page_events{}, protections{}, trims{};
  std::uint64_t moves{}, local_moves{}, cross_moves{}, copied_bytes{};
};

struct Group { std::unique_ptr<finite::FiniteGroup> finite; std::size_t palette{}; };
struct Record { Group* group{}; finite::Identity identity; finite::Home home; std::size_t size{}; std::uint64_t seed{}; };

class Pool {
public:
  explicit Pool(const Options& config) : config_(config) {}

  Record allocate(std::size_t size, std::uint64_t seed, std::size_t reserve = 0,
                  std::size_t initialized_prefix = 0) {
    if (!reserve) reserve = size;
    require(reserve >= size && initialized_prefix <= size, "invalid reservation or initialization prefix");
    const auto wanted = preferred(reserve);
    for (const bool ready : {true, false}) {
      for (auto group = groups_.rbegin(); group != groups_.rend(); ++group) {
        if (config_.strict && (*group)->palette != wanted) continue;
        const auto& table = config_.palettes[(*group)->palette].capacities;
        if (reserve > table.back()) continue;
        ++counts.examined_groups;
        if (auto result = candidate(**group, reserve, size, seed, ready, initialized_prefix)) return *result;
      }
    }
    auto group = std::make_unique<Group>(Group{geometry(config_, wanted), wanted});
    auto* pointer = group.get();
    groups_.push_back(std::move(group));
    ++counts.new_groups;
    auto result = candidate(*pointer, reserve, size, seed, false, initialized_prefix);
    require(result.has_value(), "fresh Group cannot supply a Home");
    return *result;
  }

  void release(Record& value) {
    take(value.group->finite->release_identity(value.identity));
    take(value.group->finite->release_home(value.home));
    value.group = nullptr;
  }

  void grow(Record& value, std::size_t size, bool geometric) {
    require(size >= value.size, "growth must not shrink");
    if (size <= value.home.capacity) {
      std::memset(value.home.address + value.size, std::to_integer<int>(pattern(value.seed)), size - value.size);
      value.size = size;
      return;
    }
    const auto reserve = geometric ? std::max(size, std::min(config_.growth_end, value.home.capacity * 2)) : size;
    auto home = value.group->finite->claim_home(value.identity, reserve, config_.alignment);
    ++counts.claims;
    if (home) {
      prepare(*value.group, *home);
      std::memcpy(home->address, value.home.address, value.size);
      std::memset(home->address + value.size, std::to_integer<int>(pattern(value.seed)), size - value.size);
      take(value.group->finite->bind(value.identity, *home));
      take(value.group->finite->release_home(value.home));
      counts.copied_bytes += value.size;
      value.home = *home; value.size = size;
      ++counts.local_moves;
    } else {
      require(home.error() == orbital::Error::capacity || home.error() == orbital::Error::invalid_argument,
              "unexpected private growth failure");
      auto replacement = allocate(size, value.seed, reserve, value.size);
      std::memcpy(replacement.home.address, value.home.address, value.size);
      counts.copied_bytes += value.size;
      release(value);
      value = replacement;
      ++counts.cross_moves;
    }
    ++counts.moves;
  }

  void trim() {
    for (auto& group : groups_) {
      auto& g = *group->finite;
      std::vector<std::uint64_t> live(g.live_words());
      take(g.live_pages(live));
      for (std::size_t page = 0; page < g.payload_pages();) {
        if (!g.mapped(page) || (live[page / 64] & (std::uint64_t{1} << (page % 64)))) { ++page; continue; }
        const auto first = page;
        while (page < g.payload_pages() && g.mapped(page) && !(live[page / 64] & (std::uint64_t{1} << (page % 64)))) ++page;
        const finite::PageSpan span{first, page - first};
        take(g.begin_trim(span));
        auto* address = g.page_address(first);
        const auto bytes = span.count * g.native_page();
        require(!::madvise(address, bytes, MADV_DONTNEED) && !::mprotect(address, bytes, PROT_NONE), "discard idle pages");
        take(g.finish_trim(span, true));
        ++counts.trims;
      }
    }
  }

  void verify(const Record& value) const {
    require(value.group && value.group->finite->resolve(value.identity) == value.home.address &&
            value.group->finite->bound_capacity(value.identity) == value.home.capacity &&
            reinterpret_cast<std::uintptr_t>(value.home.address) % config_.alignment == 0,
            "binding, capacity or alignment differs");
    const auto byte = pattern(value.seed);
    for (std::size_t i = 0; i < value.size; ++i)
      require(value.home.address[i] == byte, "live payload damaged");
  }

  void verify_population(std::span<const Record> values) const {
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> intervals;
    std::vector<std::pair<std::uintptr_t, std::size_t>> identities;
    for (const auto& value : values) if (value.group) {
      verify(value);
      const auto first = reinterpret_cast<std::uintptr_t>(value.home.address);
      intervals.emplace_back(first, first + value.home.capacity);
      identities.emplace_back(reinterpret_cast<std::uintptr_t>(value.group), value.identity.index);
    }
    std::sort(intervals.begin(), intervals.end());
    std::sort(identities.begin(), identities.end());
    for (std::size_t i = 1; i < intervals.size(); ++i)
      require(intervals[i - 1].second <= intervals[i].first, "live reservations overlap");
    require(std::adjacent_find(identities.begin(), identities.end()) == identities.end(),
            "live records share an identity");
  }

  std::size_t backed() const {
    std::size_t bytes = 0;
    for (const auto& group : groups_) bytes += group->finite->mapped_pages() * group->finite->native_page();
    return bytes;
  }

  std::size_t reserved() const {
    std::size_t bytes = 0;
    for (const auto& group : groups_) bytes += group->finite->reserved_bytes();
    return bytes;
  }

  std::size_t metadata() const {
    std::size_t bytes = 0;
    for (const auto& group : groups_) bytes += group->finite->control_bytes() + sizeof(finite::FiniteGroup);
    return bytes;
  }

  std::size_t groups() const { return groups_.size(); }
  std::size_t lookup_bytes() const {
    std::size_t bytes = 0;
    for (const auto& group : groups_) bytes += group->finite->class_lookup_bytes();
    return bytes;
  }
  std::size_t lookup_groups() const {
    std::size_t count = 0;
    for (const auto& group : groups_) count += group->finite->class_lookup_entries() != 0;
    return count;
  }
  Counters counts;

private:
  std::size_t preferred(std::size_t bytes) const {
    if (config_.baseline_route) return bytes > 192 || config_.alignment >= 64 ? 1 : 0;
    auto best = std::numeric_limits<std::size_t>::max(), wanted = best;
    for (std::size_t p = 0; p < config_.palettes.size(); ++p) {
      const auto& table = config_.palettes[p].capacities;
      const auto found = std::lower_bound(table.begin(), table.end(), bytes);
      if (found != table.end() && *found < best) { best = *found; wanted = p; }
    }
    require(wanted != std::numeric_limits<std::size_t>::max(), "request outside all palettes");
    return wanted;
  }

  std::optional<Record> candidate(Group& group, std::size_t reserve, std::size_t size,
                                  std::uint64_t seed, bool ready, std::size_t initialized_prefix) {
    auto& g = *group.finite;
    if (!g.has_identity()) return {};
    const auto cls = config_.class_once ? take(g.class_for(reserve)) : std::uint8_t{};
    std::uint64_t excluded = 0;
    for (;;) {
      auto identity = g.claim_identity(excluded);
      if (!identity) { require(identity.error() == orbital::Error::capacity, "claim identity failed"); return {}; }
      auto home = config_.class_once ? g.claim_home_class(*identity, cls, config_.alignment)
                                     : g.claim_home(*identity, reserve, config_.alignment);
      ++counts.claims;
      if (!home) {
        require(home.error() == orbital::Error::capacity, "claim Home failed");
        excluded |= std::uint64_t{1} << g.grid_of(*identity);
        take(g.release_identity(*identity));
        continue;
      }
      bool mapped = true;
      for (auto p = home->pages.first; p < home->pages.first + home->pages.count; ++p) mapped &= g.mapped(p);
      if (ready && !mapped) {
        excluded |= std::uint64_t{1} << g.grid_of(*identity);
        take(g.release_home(*home));
        take(g.release_identity(*identity));
        continue;
      }
      if (!mapped) prepare(group, *home);
      take(g.bind(*identity, *home));
      std::memset(home->address + initialized_prefix, std::to_integer<int>(pattern(seed)), size - initialized_prefix);
      return Record{&group, *identity, *home, size, seed};
    }
  }

  void prepare(Group& group, const finite::Home& home) {
    auto& g = *group.finite;
    bool missing = false;
    for (auto p = home.pages.first; p < home.pages.first + home.pages.count; ++p) missing |= !g.mapped(p);
    if (!missing) return;
    std::array<finite::PageSpan, 2> spans;
    const auto count = take(g.begin_prepare(home, spans));
    for (auto span : std::span(spans.data(), count)) {
      auto* address = g.page_address(span.first);
      const auto bytes = span.count * g.native_page();
      require(!::mprotect(address, bytes, PROT_READ | PROT_WRITE), "prepare payload mapping");
      // Force actual host-page backing, including the unused part of a larger
      // policy granule. Logical protection alone would overstate resident work.
      for (std::size_t offset = 0; offset < bytes; offset += g.host_page())
        *static_cast<volatile std::byte*>(address + offset) = std::byte{0};
      counts.page_events += span.count; ++counts.protections;
    }
    take(g.finish_prepare(home, std::span(spans.data(), count), true));
  }

  const Options& config_;
  std::vector<std::unique_ptr<Group>> groups_;
};

struct Samples {
  std::vector<double> batch_ns;
  std::uint64_t operations{}, elapsed_ns{}, maintenance_ns{};
  template<class Fn> void batch(std::size_t count, Fn&& fn) {
    const auto start = Clock::now();
    fn();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    operations += count; elapsed_ns += static_cast<std::uint64_t>(elapsed);
    batch_ns.push_back(static_cast<double>(elapsed) / static_cast<double>(count));
  }
  double percentile(unsigned percent) const {
    if (batch_ns.empty()) return 0;
    auto copy = batch_ns;
    std::sort(copy.begin(), copy.end());
    return copy[(copy.size() - 1) * percent / 100];
  }
};

template<class Fn> void batches(Samples& samples, std::size_t count, std::size_t width, Fn&& fn) {
  for (std::size_t first = 0; first < count; first += width) {
    const auto end = std::min(count, first + width);
    samples.batch(end - first, [&] { for (auto i = first; i < end; ++i) fn(i); });
  }
}

void emit(const Options& config, std::string_view stage, const Samples& samples,
          const Pool& pool, std::span<const Record> values, std::uint64_t checksum = 0,
          const finite::FiniteGroup* direct_group = nullptr) {
  std::uint64_t requested = 0, capacity = 0;
  for (const auto& value : values) if (value.group) { requested += value.size; capacity += value.home.capacity; }
  const auto host = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  const auto page = config.page ? config.page : host;
  const auto& c = pool.counts;
  std::cout << config.label << ',' << config.workload << ',' << stage << ',' << config.repetition << ','
            << host << ',' << page << ',' << (host == page ? "native" : "policy-emulation") << ','
            << config.cpu << ',' << (config.cpu >= 0 ? "pinned" : "unpinned") << ','
            << config.class_bits << ',' << config.alignment << ',' << config.population << ',' << config.batch << ','
            << config.request_max << ',' << config.growth_end << ','
            << (config.class_lookup ? 1 : 0) << ',' << (config.class_once ? 1 : 0) << ','
            << (direct_group ? (direct_group->class_lookup_entries() ? 1 : 0) : pool.lookup_groups()) << ','
            << (direct_group ? direct_group->class_lookup_bytes() : pool.lookup_bytes()) << ','
            << samples.operations << ',' << samples.batch_ns.size() << ','
            << (samples.operations ? double(samples.elapsed_ns) / double(samples.operations) : 0) << ','
            << samples.percentile(50) << ',' << samples.percentile(99) << ',' << samples.maintenance_ns << ','
            << (direct_group ? 1 : pool.groups()) << ',' << requested << ',' << capacity << ','
            << (direct_group ? direct_group->mapped_pages() * direct_group->native_page() : pool.backed()) << ','
            << (direct_group ? direct_group->reserved_bytes() : pool.reserved()) << ','
            << (direct_group ? direct_group->control_bytes() + sizeof(finite::FiniteGroup) : pool.metadata()) << ','
            << c.claims << ',' << c.examined_groups << ','
            << c.new_groups << ',' << c.page_events << ',' << c.protections << ',' << c.trims << ','
            << c.moves << ',' << c.local_moves << ',' << c.cross_moves << ',' << c.copied_bytes << ',' << checksum << '\n';
}

void direct(const Options& config, const std::vector<std::size_t>& offered) {
  // A fully prepared single Group isolates the actual lookup/binding operations.
  // It is a cache-hot lower-level control, not the public allocation fast path.
  Pool unused(config);
  for (std::size_t p = 0; p < config.palettes.size(); ++p) {
    std::vector<std::size_t> eligible;
    for (auto size : offered) if (size <= config.palettes[p].capacities.back()) eligible.push_back(size);
    // A narrower member can legitimately serve none of a wide-family trace.
    // Keep its direct control absent rather than treating that as allocation failure.
    if (eligible.empty()) continue;
    auto g = geometry(config, p);
    require(!::mprotect(g->page_address(0), g->payload_pages() * g->native_page(), PROT_READ | PROT_WRITE),
            "prepare direct control");
    for (std::size_t page = 0; page < g->payload_pages(); ++page)
      for (std::size_t offset = 0; offset < g->native_page(); offset += g->host_page())
        *static_cast<volatile std::byte*>(g->page_address(page) + offset) = std::byte{0};
    take(g->mark_mapped({0, g->payload_pages()}));
    const auto cycle = [&](std::size_t size) {
      auto identity = take(g->claim_identity());
      auto home = take(g->claim_home(identity, size, config.alignment));
      take(g->bind(identity, home));
      const auto address = reinterpret_cast<std::uintptr_t>(g->resolve(identity));
      take(g->release_identity(identity));
      take(g->release_home(home));
      return static_cast<std::uint64_t>(address);
    };
    std::uint64_t expected = 0;
    for (auto size : eligible) expected += cycle(size);
    Samples samples; std::uint64_t checksum = 0;
    for (std::size_t round = 0; round < config.rounds * 32; ++round)
      batches(samples, eligible.size(), config.batch, [&](auto i) { checksum += cycle(eligible[i]); });
    require(checksum == expected * config.rounds * 32 && g->has_identity(), "direct reuse result differs");
    emit(config, "direct-" + config.palettes[p].name, samples, unused, {}, checksum, g.get());
  }
}

void consumer(const Options& config, const std::vector<std::size_t>& offered, std::string_view stage) {
  Pool pool(config); std::vector<Record> live(config.population);
  Samples samples;
  if (stage == "fresh") {
    batches(samples, live.size(), config.batch, [&](auto i) { live[i] = pool.allocate(offered[i], i + 1); });
  } else {
    for (std::size_t i = 0; i < live.size(); ++i) live[i] = pool.allocate(offered[i], i + 1);
  }
  pool.verify_population(live);
  if (stage == "resolve") {
    std::vector<std::size_t> order(live.size()); std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), std::mt19937_64(config.seed + 11));
    std::uint64_t sum = 0, expected = 0;
    for (const auto& value : live) expected += std::to_integer<unsigned>(pattern(value.seed)) * 2;
    for (std::size_t round = 0; round < config.rounds * 128; ++round)
      batches(samples, live.size(), config.batch, [&](auto i) {
        const auto& value = live[order[i]];
        const auto* address = static_cast<const volatile std::byte*>(value.group->finite->resolve(value.identity));
        sum += std::to_integer<unsigned>(address[0]) + std::to_integer<unsigned>(address[value.size - 1]);
      });
    require(sum == expected * config.rounds * 128, "resolved payload checksum differs");
    emit(config, stage, samples, pool, live, sum);
  } else if (stage == "churn" || stage == "churn-trim") {
    for (std::size_t round = 0; round < config.rounds; ++round) {
      batches(samples, live.size(), config.batch, [&](auto i) {
        auto successor = pool.allocate(offered[(i + (round + 1) * 127) % offered.size()], (round + 1) * live.size() + i + 1);
        pool.release(live[i]); live[i] = successor;
      });
      if (stage == "churn-trim") {
        const auto start = Clock::now();
        pool.trim();
        samples.maintenance_ns += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
      }
      pool.verify_population(live);
    }
    emit(config, stage, samples, pool, live);
  } else if (stage == "trim") {
    // Surviving allocations span the entire population rather than retaining
    // only the newest Group. Verify their bytes after real discard/protection.
    for (std::size_t i = 0; i < live.size(); ++i) if (i % 16) pool.release(live[i]);
    // A trim item is one complete Pool sweep, not one released allocation.
    samples.batch(1, [&] { pool.trim(); });
    pool.verify_population(live);
    emit(config, stage, samples, pool, live);
  } else emit(config, stage, samples, pool, live);
  for (auto& value : live) if (value.group) pool.release(value);
  pool.trim();
  require(pool.backed() == 0, "idle backing remains after final trim");
}

void growth(const Options& config, bool geometric) {
  Pool pool(config); std::vector<Record> live(config.population);
  for (std::size_t i = 0; i < live.size(); ++i) live[i] = pool.allocate(256, i + 1);
  Samples samples;
  for (std::size_t size = 272; size <= config.growth_end; size += 16)
    batches(samples, live.size(), config.batch, [&](auto i) { pool.grow(live[i], size, geometric); });
  pool.verify_population(live);
  emit(config, geometric ? "growth-geometric" : "growth-exact", samples, pool, live);
  for (auto& value : live) pool.release(value);
  pool.trim();
  require(pool.backed() == 0, "growth backing remains after release");
}
} // namespace

int main(int argc, char** argv) {
  try {
    const auto config = options(argc, argv);
    const auto offered = requests(config);
    std::cout << "candidate,workload,stage,repetition,host_page,policy_page,page_mode,cpu,affinity,class_bits,alignment,population,batch_size,request_max,growth_end,class_lookup,class_once,lookup_groups,lookup_heap_bytes,operations,batches,mean_ns,batch_p50_ns,batch_p99_ns,maintenance_ns,groups,requested_bytes,capacity_bytes,backed_bytes,reserved_bytes,metadata_bytes,home_claims,examined_groups,new_groups,page_events,mprotect_calls,trim_calls,moves,local_moves,cross_moves,copied_bytes,checksum\n";
    if (config.stage == "all" || config.stage == "direct") direct(config, offered);
    for (const auto stage : {"fresh", "resolve", "churn", "churn-trim", "trim"})
      if (config.stage == "all" || config.stage == stage) consumer(config, offered, stage);
    if (config.stage == "all" || config.stage == "growth-exact") growth(config, false);
    if (config.stage == "all" || config.stage == "growth-geometric") growth(config, true);
  } catch (const std::exception& error) {
    std::cerr << "allocation-palettes timing: " << error.what() << '\n';
    return 1;
  }
}
