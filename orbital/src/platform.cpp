#include <orbital/platform.h>
#include <algorithm>
#include <bit>
#include <cerrno>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sched.h>
#include <set>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>

namespace orbital {
namespace {
constexpr unsigned cpu_bound = 65536;
std::string_view trim(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
template<class T> std::optional<T> decimal(std::string_view text) {
  text = trim(text);
  if (text.empty()) return {};
  T value{};
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return {};
  return value;
}
template<class T> std::optional<T> integer(std::string_view text) {
  text = trim(text);
  int base = 10;
  if (text.starts_with("0x")) { text.remove_prefix(2); base = 16; }
  if (text.empty()) return {};
  T value{};
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return {};
  return value;
}
std::optional<std::string> read_file(std::string_view path, void*) {
  std::ifstream input{std::string(path)};
  if (!input) return {};
  const std::size_t limit = path == "/proc/cpuinfo" ? 8 * 1024 * 1024 : 65536;
  std::string result;
  char buffer[4096];
  while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
    const auto count = std::size_t(input.gcount());
    if (count > limit - result.size()) return {};
    result.append(buffer, count);
  }
  if (!input.eof()) return {};
  return std::string(trim(result));
}
std::optional<std::vector<std::string>> entries(std::string_view path, void*) {
  namespace fs = std::filesystem;
  std::error_code error;
  fs::directory_iterator iterator(std::string(path), error), end;
  if (error) return {};
  std::vector<std::string> result;
  for (; iterator != end; iterator.increment(error)) {
    if (error || result.size() == cpu_bound) return {};
    result.push_back(iterator->path().filename().string());
  }
  if (error) return {};
  std::sort(result.begin(), result.end());
  return result;
}
using Fields = std::map<std::string, std::string>;
std::map<int, Fields> cpu_info(const std::optional<std::string>& text) {
  std::map<int, Fields> result;
  if (!text) return result;
  std::set<int> duplicated;
  Fields block;
  auto finish = [&] {
    const auto found = block.find("processor");
    const auto cpu = found == block.end() ? std::optional<unsigned>{} : decimal<unsigned>(found->second);
    if (cpu && *cpu < cpu_bound && !duplicated.contains(int(*cpu))) {
      if (!result.emplace(int(*cpu), block).second) { result.erase(int(*cpu)); duplicated.insert(int(*cpu)); }
    }
    block.clear();
  };
  std::stringstream input(*text);
  std::string line;
  while (std::getline(input, line)) {
    if (trim(line).empty()) { finish(); continue; }
    const auto colon = line.find(':');
    if (colon != std::string::npos)
      block[std::string(trim(std::string_view(line).substr(0, colon)))] =
          std::string(trim(std::string_view(line).substr(colon + 1)));
  }
  finish();
  return result;
}
std::optional<std::string> field(const Fields& fields, std::string_view name) {
  const auto found = fields.find(std::string(name));
  if (found == fields.end() || found->second.empty()) return {};
  return found->second;
}
std::optional<unsigned> identity_number(const Fields& fields, std::string_view name) {
  const auto value = field(fields, name);
  return value ? integer<unsigned>(*value) : std::optional<unsigned>{};
}
std::optional<std::vector<std::string>> features(const Fields& info) {
  auto found = info.find("flags");
  if (found == info.end()) found = info.find("Features");
  if (found == info.end()) return {};
  std::stringstream stream(found->second);
  std::vector<std::string> result;
  std::string feature;
  while (stream >> feature) {
    if (!std::all_of(feature.begin(), feature.end(), [](unsigned char c) {
          return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        })) return {};
    result.push_back(feature);
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}
std::optional<std::string> selected_policy(const std::optional<std::string>& text) {
  if (!text) return {};
  const auto first = text->find('['), last = text->find(']');
  if (first == std::string::npos || last == std::string::npos || last <= first + 1 ||
      text->find('[', first + 1) != std::string::npos || text->find(']', last + 1) != std::string::npos ||
      (first && (*text)[first - 1] != ' ' && (*text)[first - 1] != '\t') ||
      (last + 1 < text->size() && (*text)[last + 1] != ' ' && (*text)[last + 1] != '\t')) return {};
  return text->substr(first + 1, last - first - 1);
}
std::optional<TransparentPagePolicy> transparent_policy(const std::optional<std::string>& text) {
  const auto value = selected_policy(text);
  if (value == "always") return TransparentPagePolicy::always;
  if (value == "madvise") return TransparentPagePolicy::madvise;
  if (value == "never") return TransparentPagePolicy::never;
  if (value == "inherit") return TransparentPagePolicy::inherit;
  return {};
}
std::optional<TransparentPageDefrag> defrag_policy(const std::optional<std::string>& text) {
  const auto value = selected_policy(text);
  if (value == "always") return TransparentPageDefrag::always;
  if (value == "defer") return TransparentPageDefrag::defer;
  if (value == "defer+madvise") return TransparentPageDefrag::defer_madvise;
  if (value == "madvise") return TransparentPageDefrag::madvise;
  if (value == "never") return TransparentPageDefrag::never;
  return {};
}
std::optional<std::uint64_t> page_size_name(std::string_view name) {
  if (!name.starts_with("hugepages-") || !name.ends_with("kB")) return {};
  name.remove_prefix(10); name.remove_suffix(2);
  const auto size = decimal<std::uint64_t>(name);
  if (!size || !*size || *size > std::numeric_limits<std::uint64_t>::max() / 1024) return {};
  return *size * 1024;
}
std::optional<unsigned> numbered_name(std::string_view name, std::string_view prefix) {
  if (!name.starts_with(prefix)) return {};
  name.remove_prefix(prefix.size());
  const auto value = decimal<unsigned>(name);
  if (!value || *value >= cpu_bound || name != std::to_string(*value)) return {};
  return value;
}

struct Reader {
  detail::LinuxPlatformSource source;
  std::optional<std::string> read(const std::string& path) const {
    auto text = source.read(path, source.context);
    if (text) *text = std::string(trim(*text));
    return text;
  }
  std::optional<std::vector<std::string>> list(const std::string& path) const {
    auto names = source.entries(path, source.context);
    if (names) {
      if (names->size() > cpu_bound || std::any_of(names->begin(), names->end(), [](const auto& name) {
            return name.empty() || name.find('/') != std::string::npos || name == "." || name == "..";
          })) return {};
      std::sort(names->begin(), names->end());
      if (std::adjacent_find(names->begin(), names->end()) != names->end()) return {};
    }
    return names;
  }
  template<class T> std::optional<T> number(const std::string& path, bool positive = false) const {
    const auto text = read(path);
    auto value = text ? decimal<T>(*text) : std::optional<T>{};
    if (value && positive && !*value) value.reset();
    return value;
  }
  std::optional<std::vector<int>> cpus(const std::string& path, int required = -1) const {
    const auto text = read(path);
    auto value = text ? detail::parse_platform_cpu_list(*text) : std::optional<std::vector<int>>{};
    if (value && required >= 0 && !std::binary_search(value->begin(), value->end(), required)) value.reset();
    return value;
  }
};

Result<std::vector<int>> allowed_cpus() {
  // Grow the syscall buffer until the kernel accepts it, rather than silently
  // truncating a host to the libc fixed cpu_set_t width.
  for (unsigned count = 128; count <= cpu_bound; count *= 2) {
    const auto bytes = CPU_ALLOC_SIZE(count);
    const auto free_mask = [](cpu_set_t* mask) { CPU_FREE(mask); };
    std::unique_ptr<cpu_set_t, decltype(free_mask)> mask(CPU_ALLOC(count), free_mask);
    if (!mask) return std::unexpected(Error::unavailable);
    CPU_ZERO_S(bytes, mask.get());
    const auto rc = sched_getaffinity(0, bytes, mask.get()), error = errno;
    std::vector<int> result;
    if (!rc) for (unsigned cpu = 0; cpu < count; ++cpu) if (CPU_ISSET_S(cpu, bytes, mask.get())) result.push_back(int(cpu));
    if (!rc) return result;
    if (error != EINVAL) return std::unexpected(Error::unavailable);
  }
  return std::unexpected(Error::capacity);
}
} // namespace

std::optional<std::vector<int>> detail::parse_platform_cpu_list(std::string_view text) {
  text = trim(text);
  std::vector<int> result;
  if (text.empty()) return result; // A memory-only NUMA node may have no CPUs.
  while (!text.empty()) {
    const auto comma = text.find(',');
    const auto part = text.substr(0, comma);
    const auto dash = part.find('-');
    const auto first = decimal<unsigned>(part.substr(0, dash));
    const auto last = dash == std::string_view::npos ? first : decimal<unsigned>(part.substr(dash + 1));
    // Whitespace inside a Linux list and leading signs are not valid grammar.
    if (part.find_first_of(" \t\r\n+") != std::string_view::npos || !first || !last ||
        *first > *last || *last >= cpu_bound || (!result.empty() && *first <= unsigned(result.back()))) return {};
    for (unsigned cpu = *first; cpu <= *last; ++cpu) result.push_back(int(cpu));
    if (comma == std::string_view::npos) break;
    text.remove_prefix(comma + 1);
    if (text.empty()) return {};
  }
  return result;
}

std::optional<std::uint64_t> detail::parse_platform_size(std::string_view text) {
  text = trim(text);
  if (text.empty() || text.find_first_of(" \t\r\n+") != std::string_view::npos) return {};
  unsigned shift{};
  switch (text.back()) {
  case 'K': shift = 10; break;
  case 'M': shift = 20; break;
  case 'G': shift = 30; break;
  case 'T': shift = 40; break;
  default: break;
  }
  if (shift) text.remove_suffix(1);
  const auto value = decimal<std::uint64_t>(text);
  if (!value || *value > (std::numeric_limits<std::uint64_t>::max() >> shift)) return {};
  return *value << shift;
}

const CpuReport* PlatformReport::cpu(int number) const noexcept {
  const auto found = std::lower_bound(cpus.begin(), cpus.end(), number,
      [](const CpuReport& value, int target) { return value.cpu < target; });
  return found != cpus.end() && found->cpu == number ? &*found : nullptr;
}

Result<PlatformReport> detail::describe_linux_platform(int cpu, std::span<const int> allowed,
    std::optional<std::uint64_t> base_page, std::optional<std::string> isa,
    std::optional<std::string> kernel, LinuxPlatformSource source) {
  if (!source.read || !source.entries || cpu < 0 || unsigned(cpu) >= cpu_bound || allowed.empty() ||
      allowed.size() > cpu_bound || !std::is_sorted(allowed.begin(), allowed.end()) ||
      std::adjacent_find(allowed.begin(), allowed.end()) != allowed.end() ||
      allowed.front() < 0 || unsigned(allowed.back()) >= cpu_bound ||
      !std::binary_search(allowed.begin(), allowed.end(), cpu)) return std::unexpected(Error::invalid_argument);
  try {
    Reader input{source};
    PlatformReport result;
    result.selected_cpu = cpu;
    result.allowed_cpus.assign(allowed.begin(), allowed.end());
    result.isa = std::move(isa); result.kernel = std::move(kernel);
    if (base_page && *base_page && std::has_single_bit(*base_page)) result.pages.base_bytes = base_page;
    const auto info = cpu_info(input.read("/proc/cpuinfo"));
    for (auto number : allowed) {
      CpuReport report;
      report.cpu = number;
      const auto root = "/sys/devices/system/cpu/cpu" + std::to_string(number);
      const auto found = info.find(number);
      if (found != info.end()) {
        const auto& fields = found->second;
        auto& identity = report.identity;
        identity.vendor = field(fields, "vendor_id"); identity.model_name = field(fields, "model name");
        identity.microcode = field(fields, "microcode");
        identity.family = identity_number(fields, "cpu family"); identity.model = identity_number(fields, "model");
        identity.stepping = identity_number(fields, "stepping");
        identity.implementer = identity_number(fields, "CPU implementer");
        identity.architecture = identity_number(fields, "CPU architecture");
        identity.variant = identity_number(fields, "CPU variant"); identity.part = identity_number(fields, "CPU part");
        identity.revision = identity_number(fields, "CPU revision");
        report.advertised_features = features(fields);
        if (report.advertised_features) identity.hypervisor_advertised = std::binary_search(
            report.advertised_features->begin(), report.advertised_features->end(), "hypervisor");
      }
      const auto midr = input.read(root + "/regs/identification/midr_el1");
      if (midr) report.identity.midr = integer<std::uint64_t>(*midr);
      report.core_id = input.number<unsigned>(root + "/topology/core_id");
      report.package_id = input.number<unsigned>(root + "/topology/physical_package_id");
      report.die_id = input.number<unsigned>(root + "/topology/die_id");
      report.thread_siblings = input.cpus(root + "/topology/thread_siblings_list", number);
      report.package_cpus = input.cpus(root + "/topology/core_siblings_list", number);
      const auto members = input.list(root);
      if (members) {
        report.numa_nodes.emplace();
        for (const auto& name : *members) if (const auto node = numbered_name(name, "node"))
          report.numa_nodes->push_back(int(*node));
        std::sort(report.numa_nodes->begin(), report.numa_nodes->end());
      }
      const auto caches = input.list(root + "/cache");
      report.cache_directory_reported = caches.has_value();
      if (caches) for (const auto& name : *caches) if (numbered_name(name, "index")) {
        const auto cache_root = root + "/cache/" + name;
        CacheReport cache;
        cache.id = input.number<unsigned>(cache_root + "/id");
        cache.level = input.number<unsigned>(cache_root + "/level", true);
        const auto type = input.read(cache_root + "/type");
        if (type == "Data") cache.type = CacheType::data;
        else if (type == "Instruction") cache.type = CacheType::instruction;
        else if (type == "Unified") cache.type = CacheType::unified;
        const auto capacity = input.read(cache_root + "/size");
        if (capacity) cache.capacity_bytes = parse_platform_size(*capacity);
        if (cache.capacity_bytes && !*cache.capacity_bytes) cache.capacity_bytes.reset();
        cache.line_bytes = input.number<std::uint64_t>(cache_root + "/coherency_line_size", true);
        if (cache.line_bytes && !std::has_single_bit(*cache.line_bytes)) cache.line_bytes.reset();
        cache.ways = input.number<std::uint64_t>(cache_root + "/ways_of_associativity", true);
        cache.sets = input.number<std::uint64_t>(cache_root + "/number_of_sets", true);
        cache.shared_cpus = input.cpus(cache_root + "/shared_cpu_list", number);
        report.caches.push_back(std::move(cache));
      }
      result.cpus.push_back(std::move(report));
    }
    const auto nodes = input.list("/sys/devices/system/node");
    result.numa_directory_reported = nodes.has_value();
    std::vector<int> node_ids;
    if (nodes) for (const auto& name : *nodes) if (const auto node = numbered_name(name, "node")) node_ids.push_back(int(*node));
    std::sort(node_ids.begin(), node_ids.end());
    // distance emits one column per online node, not per existing directory.
    // Never infer a destination from column number or an offline directory.
    auto distance_ids = input.cpus("/sys/devices/system/node/online");
    if (distance_ids && !std::all_of(distance_ids->begin(), distance_ids->end(), [&](int node) {
          return std::binary_search(node_ids.begin(), node_ids.end(), node);
        })) distance_ids.reset();
    for (auto node : node_ids) {
      const auto root = "/sys/devices/system/node/node" + std::to_string(node);
      NumaReport report;
      report.node = node; report.cpus = input.cpus(root + "/cpulist");
      const auto distances = input.read(root + "/distance");
      if (distances && distance_ids) {
        std::stringstream values(*distances);
        std::string word;
        std::vector<NodeDistance> parsed;
        bool valid = true;
        while (values >> word) {
          const auto distance = decimal<unsigned>(word);
          if (!distance || !*distance || parsed.size() >= distance_ids->size()) { valid = false; break; }
          parsed.push_back({(*distance_ids)[parsed.size()], *distance});
        }
        if (valid && parsed.size() == distance_ids->size()) report.distances = std::move(parsed);
      }
      result.numa.push_back(std::move(report));
    }
    const std::string thp = "/sys/kernel/mm/transparent_hugepage";
    result.pages.transparent_policy = transparent_policy(input.read(thp + "/enabled"));
    result.pages.transparent_defrag = defrag_policy(input.read(thp + "/defrag"));
    const auto transparent = input.list(thp);
    result.pages.transparent_directory_reported = transparent.has_value();
    if (transparent) for (const auto& name : *transparent) if (name.starts_with("hugepages-"))
      result.pages.transparent_sizes.push_back({page_size_name(name), transparent_policy(input.read(thp + '/' + name + "/enabled"))});
    const std::string huge = "/sys/kernel/mm/hugepages";
    const auto sizes = input.list(huge);
    result.pages.huge_directory_reported = sizes.has_value();
    if (sizes) for (const auto& name : *sizes) if (name.starts_with("hugepages-")) {
      const auto root = huge + '/' + name;
      result.pages.huge_sizes.push_back({page_size_name(name), input.number<std::uint64_t>(root + "/nr_hugepages"),
          input.number<std::uint64_t>(root + "/free_hugepages"), input.number<std::uint64_t>(root + "/resv_hugepages"),
          input.number<std::uint64_t>(root + "/surplus_hugepages")});
    }
    return result;
  } catch (...) { return std::unexpected(Error::unavailable); }
}

Result<PlatformReport> discover_platform(int cpu) {
  try {
    const auto allowed = allowed_cpus();
    if (!allowed) return std::unexpected(allowed.error());
    if (cpu == -1) cpu = sched_getcpu();
    const auto bytes = sysconf(_SC_PAGESIZE);
    utsname system{};
    const bool reported_system = uname(&system) == 0;
    return detail::describe_linux_platform(cpu, *allowed,
        bytes > 0 ? std::optional<std::uint64_t>(std::uint64_t(bytes)) : std::nullopt,
        reported_system ? std::optional<std::string>(system.machine) : std::nullopt,
        reported_system ? std::optional<std::string>(system.release) : std::nullopt,
        {read_file, entries, nullptr});
  } catch (...) { return std::unexpected(Error::unavailable); }
}
} // namespace orbital
