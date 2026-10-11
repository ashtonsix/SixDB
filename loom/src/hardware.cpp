#include <loom/hardware.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <sched.h>
#include <sstream>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
#endif

namespace loom {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Word = std::uintptr_t;
constexpr Word cookie = 0xd1b54a32d192ed03ULL;
#include "hardware_table.inc"

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::optional<std::string> read(const fs::path& path, std::size_t limit = 65536) {
  std::ifstream input(path);
  if (!input) return {};
  std::string result;
  char buffer[4096];
  while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
    const auto count = std::size_t(input.gcount());
    if (count > limit - result.size()) return {};
    result.append(buffer, count);
  }
  if (!input.eof()) return {};
  return trim(std::move(result));
}
std::string json_quote(std::string_view input) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result{"\""};
  for (unsigned char c : input) {
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 32 || c >= 128) {
      result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
    } else result += char(c);
  }
  return result + '"';
}
std::string nullable(const std::optional<std::string>& value) {
  return value ? json_quote(*value) : "null";
}
using Object = std::map<std::string, std::string>;
std::string object(const Object& fields) {
  std::string result{"{"};
  for (const auto& [key, value] : fields) {
    if (result.size() != 1) result += ',';
    result += json_quote(key) + ':' + value;
  }
  return result + '}';
}
std::string array(std::span<const std::string> values) {
  std::string result{"["};
  for (const auto& value : values) {
    if (result.size() != 1) result += ',';
    result += value;
  }
  return result + ']';
}
unsigned number(std::string_view value) {
  unsigned result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? result : 0;
}
unsigned cpu_count(const std::optional<std::string>& text) {
  if (!text || text->empty()) return 0;
  unsigned count{};
  std::stringstream parts(*text);
  std::string part;
  while (std::getline(parts, part, ',')) {
    const auto dash = part.find('-');
    const auto first = number(part.substr(0, dash));
    const auto last = dash == std::string::npos ? first : number(part.substr(dash + 1));
    if (last < first || last - first >= CPU_SETSIZE || count > CPU_SETSIZE - (last - first + 1)) return 0;
    count += last - first + 1;
  }
  return count;
}
std::vector<fs::path> children(const fs::path& path, std::string_view prefix) {
  std::vector<fs::path> result;
  std::error_code error;
  for (fs::directory_iterator it(path, error), end; !error && it != end; it.increment(error))
    if (it->path().filename().string().starts_with(prefix)) result.push_back(it->path());
  std::sort(result.begin(), result.end());
  return result;
}
bool allowed_cpu(int cpu) {
  if (cpu < 0 || cpu >= CPU_SETSIZE) return false;
  cpu_set_t set;
  CPU_ZERO(&set);
  return sched_getaffinity(0, sizeof(set), &set) == 0 && CPU_ISSET(cpu, &set);
}
bool valid_limits(HardwareCalibrationLimits limits, HintGeometry geometry) {
  if (limits.wall_time <= std::chrono::milliseconds::zero() || limits.wall_time > std::chrono::seconds(30) ||
      limits.mapping_bytes < 1024 * 1024 || limits.mapping_bytes > 512 * 1024 * 1024 ||
      limits.mapping_bytes % geometry.page_bytes || limits.mapping_bytes % geometry.line_bytes ||
      limits.mapping_bytes / geometry.line_bytes < detail::hardware_widths.back()) return false;
  const auto order_bytes = limits.mapping_bytes / geometry.line_bytes * sizeof(Word);
  return limits.mapping_bytes <= limits.memory_bytes && order_bytes <= limits.memory_bytes - limits.mapping_bytes;
}
bool stopped(std::stop_token stop, Clock::time_point deadline) {
  return stop.stop_requested() || Clock::now() >= deadline;
}
HardwareState stop_state(std::stop_token stop) {
  return stop.stop_requested() ? HardwareState::cancelled : HardwareState::timed_out;
}
inline Word load(Word address) noexcept { return *reinterpret_cast<volatile Word*>(address); }
inline void fence() noexcept {
#if defined(__x86_64__)
  _mm_lfence();
#elif defined(__aarch64__)
  asm volatile("dsb ish\n\tisb" ::: "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}
template<unsigned N> [[gnu::noinline]] Word chase(std::array<Word, N>& pointers, unsigned rounds) {
  for (unsigned r = 0; r < rounds; ++r) {
#pragma clang loop unroll(full)
    for (unsigned i = 0; i < N; ++i) pointers[i] = load(pointers[i]) ^ cookie;
  }
  Word result{};
  for (auto value : pointers) result ^= value;
  return result;
}
void consume(Word value) noexcept { asm volatile("" :: "r"(value) : "memory"); }

template<unsigned N> bool width_measure(std::span<const Word> order,
    std::stop_token stop, Clock::time_point deadline, double& result) {
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (!(i & 4095) && stopped(stop, deadline)) return false;
    *reinterpret_cast<Word*>(order[i]) = order[i + N < order.size() ? i + N : i % N] ^ cookie;
  }
  std::array<Word, N> pointers;
  std::copy_n(order.begin(), N, pointers.begin());
  // Validate complete disjoint cycles outside the timed section.
  for (unsigned lane = 0; lane < N; ++lane) {
    auto pointer = pointers[lane];
    std::size_t checked{};
    for (std::size_t i = lane; i < order.size(); i += N) {
      if (!(checked++ & 4095) && stopped(stop, deadline)) return false;
      if (pointer != order[i]) return false;
      pointer = load(pointer) ^ cookie;
    }
    if (pointer != pointers[lane]) return false;
  }
  std::array<double, 6> samples{};
  // Bounded chunks check cancellation between at most 256 dependent loads per
  // chain. The clock/check cost is part of these local candidate observations.
  constexpr unsigned chunk = 256;
  for (auto& sample : samples) {
    const auto begin = Clock::now();
    unsigned chunks{};
    do {
      consume(chase<N>(pointers, chunk));
      ++chunks;
      if (stopped(stop, deadline)) return false;
    } while (Clock::now() - begin < std::chrono::milliseconds(1));
    const auto elapsed = std::chrono::duration<double, std::nano>(Clock::now() - begin).count();
    sample = elapsed / (std::uint64_t(chunks) * chunk * N);
  }
  std::sort(samples.begin(), samples.end());
  result = (samples[2] + samples[3]) * .5;
  return true;
}

bool flush_supported() noexcept {
#if defined(__x86_64__)
  unsigned a{}, b{}, c{}, d{};
  return __get_cpuid(1, &a, &b, &c, &d) && (d & (1u << 19));
#else
  // EL0 cache maintenance can trap on ARM. A multithreaded production startup
  // never tests it by forking or installing a process-wide signal handler.
  return false;
#endif
}
void flush(Word address) noexcept {
#if defined(__x86_64__)
  _mm_clflush(reinterpret_cast<const void*>(address));
#else
  (void)address;
#endif
}
void flush_done() noexcept {
#if defined(__x86_64__)
  _mm_mfence();
#endif
}
[[gnu::noinline]] void train_touch(Word address) noexcept { consume(load(address)); fence(); }
struct Mapping {
  void* data{MAP_FAILED};
  std::size_t bytes{};
  explicit Mapping(std::size_t size) : bytes(size) {
    data = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data != MAP_FAILED && madvise(data, bytes, MADV_NOHUGEPAGE)) {
      munmap(data, bytes); data = MAP_FAILED;
    }
  }
  ~Mapping() { if (data != MAP_FAILED) munmap(data, bytes); }
};

bool training_measure(const HardwareContext& context, const Mapping& memory,
    std::mt19937_64& random, unsigned training, std::stop_token stop,
    Clock::time_point deadline, detail::HardwareControl& result) {
  std::array<std::array<double, 20>, 3> samples{};
  std::array<unsigned, 60> arms;
  for (unsigned i = 0; i < arms.size(); ++i) arms[i] = i % 3;
  std::shuffle(arms.begin(), arms.end(), random);
  std::array<unsigned, 3> counts{};
  const auto geometry = context.geometry;
  const auto pages = memory.bytes / geometry.page_bytes;
  for (auto arm : arms) {
    if (stopped(stop, deadline)) return false;
    const auto page = random() % pages;
    const Word base = reinterpret_cast<Word>(memory.data) + page * geometry.page_bytes + 4 * geometry.line_bytes;
    const Word target = base + training * geometry.line_bytes;
    flush(target);
    for (unsigned i = 0; i < training; ++i) flush(base + i * geometry.line_bytes);
    flush_done();
    if (arm == 1) for (unsigned i = 0; i < training; ++i) train_touch(base + i * geometry.line_bytes);
    if (arm == 0) { flush(target); flush_done(); }
    if (arm == 2) train_touch(target);
    Word delay = target;
    for (unsigned i = 0; i < 128; ++i) asm volatile("" : "+r"(delay));
    fence();
    const auto begin = Clock::now();
    const auto value = load(target);
    fence();
    const auto elapsed = std::chrono::duration<double, std::nano>(Clock::now() - begin).count();
    consume(value);
    samples[arm][counts[arm]++] = elapsed;
  }
  for (auto& values : samples) std::sort(values.begin(), values.end());
  result = {samples[0][1], (samples[0][9] + samples[0][10]) * .5,
            (samples[2][9] + samples[2][10]) * .5, samples[2][17],
            (samples[1][9] + samples[1][10]) * .5};
  return true;
}

detail::HardwareCalibrationResult calibrate(const HardwareContext& context,
    HardwareCalibrationLimits limits, std::stop_token stop, const HardwareSnapshot& known) noexcept {
  detail::HardwareCalibrationResult result;
  const auto deadline = Clock::now() + limits.wall_time;
  try {
    if (stopped(stop, deadline)) { result.state = stop_state(stop); return result; }
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(context.cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set)) return result;
    auto before = discover_hardware(context.cpu);
    if (!before || !compatible_hardware_context(*before, context)) return result;
    const bool measure_width = !known.width_qualified;
    const bool measure_training = !known.training_qualified && flush_supported() &&
        (detail::hardware_training.back() + 8) * context.geometry.line_bytes < context.geometry.page_bytes;
    // A partial hit must not repeat its qualified sweep. Unsupported missing
    // fields stay unknown without allocating scratch or disturbing memory.
    if (!measure_width && !measure_training) {
      result.state = stopped(stop, deadline) ? stop_state(stop) : HardwareState::calibrated_local;
      return result;
    }
    Mapping memory(limits.mapping_bytes);
    if (memory.data == MAP_FAILED) return result;
    const auto line = context.geometry.line_bytes;
    const auto order_size = limits.mapping_bytes / line;
    auto order_storage = measure_width ? std::make_unique_for_overwrite<Word[]>(order_size) : nullptr;
    const std::span<Word> order(order_storage.get(), measure_width ? order_size : 0);
    for (std::size_t i = 0; i < order_size; ++i) {
      if (!(i & 4095) && stopped(stop, deadline)) { result.state = stop_state(stop); return result; }
      auto* address = reinterpret_cast<Word*>(static_cast<char*>(memory.data) + i * line);
      if (measure_width) order[i] = reinterpret_cast<Word>(address);
      std::construct_at(address, Word(i));
    }
    if (measure_width) for (unsigned seed = 0; seed < 2; ++seed) {
      std::mt19937_64 random(357712151618ULL + seed);
      // Fisher-Yates with cancellation, instead of an uninterruptible shuffle
      // over the whole mapping. The two seeds generate independent permutations.
      for (std::size_t i = order.size(); i > 1; --i) {
        if (!(i & 4095) && stopped(stop, deadline)) { result.state = stop_state(stop); return result; }
        std::swap(order[i - 1], order[random() % i]);
      }
      unsigned index{};
#define LOOM_WIDTH(N) if (!width_measure<N>(order, stop, deadline, result.evidence.ns_per_load[seed][index++])) { result.state = stop_state(stop); return result; }
      LOOM_WIDTH(1); LOOM_WIDTH(2); LOOM_WIDTH(4); LOOM_WIDTH(8); LOOM_WIDTH(12);
      LOOM_WIDTH(16); LOOM_WIDTH(24); LOOM_WIDTH(32); LOOM_WIDTH(48); LOOM_WIDTH(64);
#undef LOOM_WIDTH
    }
    result.evidence.width_complete = measure_width;
    if (measure_training) {
      for (unsigned seed = 0; seed < 2; ++seed) {
        std::mt19937_64 random(357712151618ULL + seed);
        for (unsigned rep = 0; rep < 2; ++rep) for (unsigned i = 0; i < detail::hardware_training.size(); ++i)
          if (!training_measure(context, memory, random, detail::hardware_training[i], stop, deadline,
                                result.evidence.training[seed * 2 + rep][i])) {
            result.state = stop_state(stop); return result;
          }
      }
      result.evidence.training_complete = true;
    }
    auto after = discover_hardware(context.cpu);
    if (!after || !compatible_hardware_context(*after, context)) return result;
    result.state = stopped(stop, deadline) ? stop_state(stop) : HardwareState::calibrated_local;
  } catch (...) { result.state = HardwareState::failed; }
  return result;
}
} // namespace

std::span<const HardwareTableEntry> hardware_table() noexcept { return retained_table; }

bool compatible_hardware_context(const HardwareContext& a, const HardwareContext& b) noexcept {
  return a.geometry == b.geometry && a.qualified == b.qualified && a.signature == b.signature &&
      (a.qualified || a.cpu == b.cpu);
}

orbital::Result<HintGeometry> reported_hint_geometry(int cpu) {
  if (cpu == -1) cpu = sched_getcpu();
  if (!allowed_cpu(cpu)) return std::unexpected(orbital::Error::invalid_argument);
  try {
    HintGeometry result;
    const auto page = sysconf(_SC_PAGESIZE);
    if (page > 0 && std::has_single_bit(std::size_t(page))) result.page_bytes = std::size_t(page);
    std::size_t minimum_line = std::numeric_limits<std::size_t>::max();
    const fs::path root = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
    for (const auto& directory : children(root / "cache", "index")) {
      const auto type = read(directory / "type");
      const auto line = read(directory / "coherency_line_size");
      if (type && line && (*type == "Data" || *type == "Unified")) {
        const auto size = number(*line);
        if (size && std::has_single_bit(size)) minimum_line = std::min(minimum_line, std::size_t(size));
      }
    }
    if (minimum_line != std::numeric_limits<std::size_t>::max() && minimum_line <= result.page_bytes)
      result.line_bytes = minimum_line;
    return valid_hint_geometry(result) ? result : HintGeometry{};
  } catch (...) { return std::unexpected(orbital::Error::unavailable); }
}

orbital::Result<HardwareContext> discover_hardware(int cpu) {
  if (cpu == -1) cpu = sched_getcpu();
  if (!allowed_cpu(cpu)) return std::unexpected(orbital::Error::invalid_argument);
  try {
    HardwareContext result;
    result.cpu = cpu;
    const auto page = sysconf(_SC_PAGESIZE);
    if (page > 0 && std::has_single_bit(std::size_t(page))) result.geometry.page_bytes = std::size_t(page);
    utsname system{};
    const bool have_system = uname(&system) == 0;
    Object info;
    bool selected{};
    const auto raw_info = read("/proc/cpuinfo", 2 * 1024 * 1024);
    if (raw_info) {
      std::stringstream lines(*raw_info);
      std::string text;
      Object block;
      while (std::getline(lines, text)) {
        if (text.empty()) {
          if (block["processor"] == std::to_string(cpu)) { info = block; selected = true; break; }
          block.clear(); continue;
        }
        const auto colon = text.find(':');
        if (colon != std::string::npos) block[trim(text.substr(0, colon))] = trim(text.substr(colon + 1));
      }
      if (!selected && block["processor"] == std::to_string(cpu)) { info = block; selected = true; }
    }
    const fs::path root = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
    Object identity;
    for (const char* field : {"vendor_id", "cpu family", "model", "stepping", "model name", "microcode",
                             "CPU implementer", "CPU architecture", "CPU variant", "CPU part", "CPU revision"}) {
      const auto found = info.find(field);
      identity[field] = found == info.end() ? "null" : json_quote(found->second);
    }
    identity["isa"] = have_system ? json_quote(system.machine) : "null";
    const auto flags = ' ' + (info.contains("flags") ? info["flags"] : info["Features"]) + ' ';
    identity["hypervisor_flag"] = flags.find(" hypervisor ") != std::string::npos ? "true" : "false";
    identity["midr"] = nullable(read(root / "regs/identification/midr_el1"));
    std::vector<std::string> caches;
    std::size_t minimum_line = std::numeric_limits<std::size_t>::max();
    bool complete_caches = true;
    for (const auto& directory : children(root / "cache", "index")) {
      Object cache;
      for (const char* field : {"level", "type", "size", "coherency_line_size", "ways_of_associativity"}) {
        const auto value = read(directory / field);
        cache[field] = nullable(value);
        if (!value) complete_caches = false;
      }
      const auto sharing = cpu_count(read(directory / "shared_cpu_list"));
      cache["shared_cpus"] = std::to_string(sharing);
      if (!sharing) complete_caches = false;
      const auto type = read(directory / "type");
      const auto line = read(directory / "coherency_line_size");
      if (type && line && (*type == "Data" || *type == "Unified")) {
        const auto size = number(*line);
        if (size && std::has_single_bit(size)) minimum_line = std::min(minimum_line, std::size_t(size));
      }
      caches.push_back(object(cache));
    }
    if (minimum_line != std::numeric_limits<std::size_t>::max() && minimum_line <= result.geometry.page_bytes)
      result.geometry.line_bytes = minimum_line;
    const fs::path thp = "/sys/kernel/mm/transparent_hugepage";
    Object thp_sizes;
    for (const auto& directory : children(thp, "hugepages-"))
      thp_sizes[directory.filename().string()] = nullable(read(directory / "enabled"));
    const auto siblings = cpu_count(read(root / "topology/thread_siblings_list"));
    Object context{{"identity", object(identity)}, {"kernel", have_system ? json_quote(system.release) : "null"},
                   {"base_page", std::to_string(result.geometry.page_bytes)}, {"caches", array(caches)},
                   {"smt_width", std::to_string(siblings)}, {"governor", nullable(read(root / "cpufreq/scaling_governor"))},
                   {"thp_policy", array(std::array{nullable(read(thp / "enabled")), nullable(read(thp / "defrag")), object(thp_sizes)})}};
    result.signature = object(context);
    result.qualified = selected && have_system && page > 0 && !caches.empty() && complete_caches && siblings &&
        minimum_line != std::numeric_limits<std::size_t>::max() && valid_hint_geometry(result.geometry);
    if (!valid_hint_geometry(result.geometry)) result.geometry = {};
    return result;
  } catch (...) { return std::unexpected(orbital::Error::unavailable); }
}

detail::HardwareInference detail::qualify_hardware(const HardwareEvidence& evidence) noexcept {
  HardwareInference result;
  if (evidence.width_complete) {
    std::array<double, 2> best;
    bool valid = true, censored = false;
    for (unsigned seed = 0; seed < 2; ++seed) {
      const auto& curve = evidence.ns_per_load[seed];
      for (auto value : curve) if (!std::isfinite(value) || value <= 0) valid = false;
      best[seed] = *std::min_element(curve.begin(), curve.end());
      unsigned knee{};
      while (knee < curve.size() && curve[knee] > 1.1 * best[seed]) ++knee;
      censored |= knee + 1 == curve.size();
    }
    if (valid && !censored && std::max(best[0], best[1]) <= 1.3 * std::min(best[0], best[1]))
      for (unsigned i = 0; i < hardware_widths.size(); ++i)
        if (evidence.ns_per_load[0][i] <= 1.15 * best[0] && evidence.ns_per_load[1][i] <= 1.15 * best[1]) {
          result.preferred_width = hardware_widths[i]; break;
        }
  }
  if (evidence.training_complete) {
    std::array<bool, 8> benefits;
    for (unsigned i = 0; i < hardware_training.size(); ++i) {
      benefits[i] = true;
      for (const auto& row : evidence.training) {
        const auto p = row[i];
        const auto gap = p.cold_median - p.hot_median;
        const bool finite = std::isfinite(p.cold_p10) && std::isfinite(p.cold_median) &&
            std::isfinite(p.hot_median) && std::isfinite(p.hot_p90) && std::isfinite(p.trained_median);
        benefits[i] &= finite && p.hot_median > 0 && p.trained_median > 0 &&
            p.cold_p10 > 1.2 * p.hot_p90 && p.cold_median >= p.cold_p10 &&
            p.hot_p90 >= p.hot_median && gap > 0 && (p.cold_median - p.trained_median) / gap >= .3;
      }
    }
    for (unsigned i = 1; i + 1 < hardware_training.size(); ++i)
      if (std::all_of(benefits.begin() + i, benefits.end(), [](bool benefit) { return benefit; })) {
        result.training_accesses = hardware_training[i]; break;
      }
  }
  return result;
}

HardwareSnapshot HardwareProfile::snapshot() const noexcept {
  std::lock_guard lock(mutex_);
  return snapshot_;
}

HardwareService::~HardwareService() { cancel(); if (thread_.joinable()) thread_.join(); }
void HardwareService::cancel() noexcept {
  std::stop_source source(std::nostopstate);
  {
    std::lock_guard lock(mutex_);
    closing_ = true;
    source = thread_.get_stop_source();
  }
  // stop callbacks run synchronously and can themselves query/cancel the
  // service. Never invoke a caller-supplied callback under the registry lock.
  source.request_stop();
  changed_.notify_all();
}

orbital::Result<std::shared_ptr<HardwareProfile>> HardwareService::request(HardwareContext context) {
  if (!valid_hint_geometry(context.geometry) || context.cpu < 0 || context.cpu >= CPU_SETSIZE ||
      context.signature.size() > 65536 || !options_.context_limit || options_.context_limit > 256 ||
      !valid_limits(options_.limits, context.geometry)) return std::unexpected(orbital::Error::invalid_argument);
  try {
    std::lock_guard lock(mutex_);
    if (closing_) return std::unexpected(orbital::Error::unavailable);
    for (const auto& profile : profiles_) if (compatible_hardware_context(profile->context_, context)) return profile;
    if (profiles_.size() >= options_.context_limit) return std::unexpected(orbital::Error::capacity);
    auto profile = std::make_shared<HardwareProfile>();
    profile->context_ = std::move(context);
    profile->snapshot_.profile.geometry = profile->context_.geometry;
    for (const auto& entry : options_.table) {
      if (!profile->context_.qualified || entry.format != 1 || !entry.validated ||
          entry.geometry != profile->context_.geometry || entry.signature != profile->context_.signature) continue;
      auto& snapshot = profile->snapshot_;
      if (entry.preferred_width && entry.preferred_width <= 64) {
        snapshot.profile.preferred_width = entry.preferred_width; snapshot.width_qualified = true;
      }
      if (entry.training_accesses >= 2 && entry.training_accesses <= 64) {
        snapshot.profile.training_accesses = entry.training_accesses; snapshot.training_qualified = true;
      }
      snapshot.profile.revision = snapshot.width_qualified || snapshot.training_qualified;
      snapshot.state = options_.calibrate && !(snapshot.width_qualified && snapshot.training_qualified) ?
          HardwareState::pending : HardwareState::lookup;
      break;
    }
    if (profile->snapshot_.state == HardwareState::pending && !options_.calibrate)
      profile->snapshot_.state = HardwareState::disabled;
    if (profile->snapshot_.state == HardwareState::pending && !thread_.joinable())
      thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
    profiles_.push_back(profile);
    changed_.notify_all();
    return profile;
  } catch (...) { return std::unexpected(orbital::Error::unavailable); }
}

void HardwareService::run(std::stop_token stop) noexcept {
  for (;;) {
    std::shared_ptr<HardwareProfile> selected;
    {
      std::unique_lock lock(mutex_);
      changed_.wait(lock, stop, [&] {
        if (closing_) return true;
        for (const auto& profile : profiles_) if (profile->snapshot().state == HardwareState::pending) return true;
        return false;
      });
      if (stop.stop_requested() || closing_) {
        for (const auto& profile : profiles_) {
          std::lock_guard snapshot_lock(profile->mutex_);
          if (profile->snapshot_.state == HardwareState::pending) profile->snapshot_.state = HardwareState::cancelled;
        }
        return;
      }
      for (const auto& profile : profiles_) if (profile->snapshot().state == HardwareState::pending) { selected = profile; break; }
    }
    if (!selected) continue;
    const auto begin = Clock::now();
    const auto known = selected->snapshot();
    const auto result = options_.calibrator ?
        options_.calibrator(selected->context_, options_.limits, stop, options_.calibrator_context) :
        calibrate(selected->context_, options_.limits, stop, known);
    std::lock_guard snapshot_lock(selected->mutex_);
    auto& snapshot = selected->snapshot_;
    if (stop.stop_requested()) snapshot.state = HardwareState::cancelled;
    else if (Clock::now() - begin >= options_.limits.wall_time) snapshot.state = HardwareState::timed_out;
    else if (result.state != HardwareState::calibrated_local) {
      snapshot.state = result.state == HardwareState::timed_out ? HardwareState::timed_out : HardwareState::failed;
    } else {
      const auto inference = detail::qualify_hardware(result.evidence);
      bool learned = false;
      if (!snapshot.width_qualified && inference.preferred_width) {
        snapshot.profile.preferred_width = inference.preferred_width; snapshot.width_qualified = true; learned = true;
      }
      if (!snapshot.training_qualified && inference.training_accesses) {
        snapshot.profile.training_accesses = inference.training_accesses; snapshot.training_qualified = true; learned = true;
      }
      if (learned) ++snapshot.profile.revision;
      snapshot.state = HardwareState::calibrated_local;
    }
  }
}
} // namespace loom
