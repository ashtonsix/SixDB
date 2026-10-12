#include <orbital/platform.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sched.h>
#include <source_location>
#include <unistd.h>

namespace {
using namespace orbital;
void check(bool condition, const char* reason,
    std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), reason);
    std::abort();
  }
}
struct Fixture {
  std::map<std::string, std::string> files;
  std::map<std::string, std::vector<std::string>> directories;
  static std::optional<std::string> read(std::string_view path, void* context) {
    const auto& files = static_cast<Fixture*>(context)->files;
    const auto found = files.find(std::string(path));
    return found == files.end() ? std::nullopt : std::optional(found->second);
  }
  static std::optional<std::vector<std::string>> entries(std::string_view path, void* context) {
    const auto& directories = static_cast<Fixture*>(context)->directories;
    const auto found = directories.find(std::string(path));
    return found == directories.end() ? std::nullopt : std::optional(found->second);
  }
  detail::LinuxPlatformSource source() { return {read, entries, this}; }
  Result<PlatformReport> describe(int cpu = 1) {
    constexpr std::array allowed{1, 8};
    return detail::describe_linux_platform(cpu, allowed, 4096, "fixture-isa", "fixture-kernel", source());
  }
};

Fixture fixture() {
  Fixture result;
  result.files["/proc/cpuinfo"] =
      "processor : 0\nvendor_id : ignored-outside-mask\nmodel : 999\n\n"
      "processor : 1\nvendor_id : GenuineIntel\ncpu family : 6\nmodel : 173\nstepping : 0\n"
      "model name : fixture one\nmicrocode : 0x23\nflags : sse2 hypervisor avx sse2\n\n"
      "processor : 8\nvendor_id : AuthenticAMD\ncpu family : 26\nmodel : 2\nstepping : 1\n"
      "model name : fixture two\nflags : sse2 avx2\n";
  for (auto cpu : {1, 8}) {
    const auto root = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
    result.directories[root] = {"cache", cpu == 1 ? "node0" : "node2", "topology"};
    result.files[root + "/topology/core_id"] = cpu == 1 ? "0" : "4";
    result.files[root + "/topology/physical_package_id"] = cpu == 1 ? "0" : "1";
    result.files[root + "/topology/die_id"] = "0";
    result.files[root + "/topology/thread_siblings_list"] = cpu == 1 ? "0-1" : "8-9";
    result.files[root + "/topology/core_siblings_list"] = cpu == 1 ? "0-3" : "8-11";
    result.directories[root + "/cache"] = {"index0", "index3"};
    for (const auto* index : {"index0", "index3"}) {
      const auto cache = root + "/cache/" + index;
      const bool l1 = std::string_view(index) == "index0";
      result.files[cache + "/id"] = cpu == 1 ? "0" : "1";
      result.files[cache + "/level"] = l1 ? "1" : "3";
      result.files[cache + "/type"] = l1 ? "Data" : "Unified";
      result.files[cache + "/size"] = l1 ? "48K" : "32M";
      result.files[cache + "/coherency_line_size"] = "64";
      result.files[cache + "/ways_of_associativity"] = l1 ? "12" : "16";
      result.files[cache + "/number_of_sets"] = l1 ? "64" : "32768";
      result.files[cache + "/shared_cpu_list"] = l1 ? (cpu == 1 ? "0-1" : "8-9") : "0-3,8-11";
    }
  }
  result.directories["/sys/devices/system/node"] = {"node2", "node0", "online"};
  result.files["/sys/devices/system/node/online"] = "0,2";
  result.files["/sys/devices/system/node/node0/cpulist"] = "0-3";
  result.files["/sys/devices/system/node/node0/distance"] = "10 21";
  result.files["/sys/devices/system/node/node2/cpulist"] = "8-11";
  result.files["/sys/devices/system/node/node2/distance"] = "22 10";
  const std::string thp = "/sys/kernel/mm/transparent_hugepage";
  result.directories[thp] = {"enabled", "hugepages-64kB", "hugepages-2048kB"};
  result.files[thp + "/enabled"] = "always [madvise] never";
  result.files[thp + "/defrag"] = "always defer [defer+madvise] madvise never";
  result.files[thp + "/hugepages-64kB/enabled"] = "always [inherit] madvise never";
  result.files[thp + "/hugepages-2048kB/enabled"] = "always inherit madvise [never]";
  const std::string huge = "/sys/kernel/mm/hugepages";
  result.directories[huge] = {"hugepages-2048kB"};
  result.files[huge + "/hugepages-2048kB/nr_hugepages"] = "3";
  result.files[huge + "/hugepages-2048kB/free_hugepages"] = "0";
  result.files[huge + "/hugepages-2048kB/resv_hugepages"] = "1";
  result.files[huge + "/hugepages-2048kB/surplus_hugepages"] = "0";
  return result;
}

void parsers() {
  using detail::parse_platform_cpu_list;
  using detail::parse_platform_size;
  check(parse_platform_cpu_list("0-2,8,10-11") == std::optional(std::vector{0, 1, 2, 8, 10, 11}),
      "valid disjoint Linux ranges were not expanded exactly");
  check(parse_platform_cpu_list("\n") == std::optional(std::vector<int>{}), "memory-only node lost empty membership");
  check(parse_platform_cpu_list("65535") == std::optional(std::vector{65535}), "CPU bound lost its final valid ID");
  for (const auto* invalid : {"-1", "+1", "0x1", "1,,2", "1-2,2-3", "1,1", "2,1", "2-1", "1-", "0,",
                            "1---3", "1, 2", "0-65536", "65536", "999999999999999999999"})
    check(!parse_platform_cpu_list(invalid), "malformed/overlapping CPU list became topology");
  check(parse_platform_size("48K") == 49152 && parse_platform_size("2M") == 2097152 &&
      parse_platform_size("1G") == 1073741824 && parse_platform_size("4096") == 4096,
      "cache capacities did not retain exact binary units");
  for (const auto* invalid : {"", "-1", "+1", "1.5K", "1 K", "64kB", "64KB", "18446744073709551616", "18446744073709551615K"})
    check(!parse_platform_size(invalid), "malformed/overflowing size became reported capacity");
}

void reports() {
  auto input = fixture();
  const auto discovered = input.describe();
  check(discovered.has_value(), "controlled Linux report failed");
  const auto& report = *discovered;
  check(report.selected_cpu == 1 && report.allowed_cpus == std::vector{1, 8} && report.cpus.size() == 2,
      "restricted affinity mask changed reported inventory");
  const auto* first = report.cpu(1); const auto* second = report.cpu(8);
  check(first && second && !report.cpu(0), "CPU lookup included an unallowed processor");
  check(first->identity.vendor == "GenuineIntel" && first->identity.model == 173 && first->identity.stepping == 0 &&
      second->identity.vendor == "AuthenticAMD" && second->identity.family == 26 && second->identity.model == 2,
      "heterogeneous CPU identity was collapsed or zero stepping lost");
  check(first->identity.hypervisor_advertised == true && second->identity.hypervisor_advertised == false &&
      first->advertised_features == std::optional(std::vector<std::string>{"avx", "hypervisor", "sse2"}),
      "advertised features were invented or not normalized");
  check(first->core_id == 0 && first->package_id == 0 && second->package_id == 1 &&
      first->thread_siblings == std::optional(std::vector{0, 1}) && first->package_cpus == std::optional(std::vector{0, 1, 2, 3}),
      "physical topology was intersected with allowed CPUs or zero IDs lost");
  check(first->caches.size() == 2 && first->caches[0].capacity_bytes == 49152 && first->caches[0].line_bytes == 64 &&
      first->caches[0].ways == 12 && first->caches[0].sets == 64 && first->caches[0].id == 0 &&
      first->caches[1].shared_cpus == std::optional(std::vector{0, 1, 2, 3, 8, 9, 10, 11}),
      "cache geometry/membership was reduced to a sharing width");
  check(first->numa_nodes == std::optional(std::vector{0}) && second->numa_nodes == std::optional(std::vector{2}) &&
      report.numa.size() == 2 && report.numa[0].node == 0 && report.numa[1].node == 2,
      "NUMA topology assumed contiguous node numbering");
  check(report.numa[0].distances && report.numa[0].distances->size() == 2 &&
      (*report.numa[0].distances)[1].node == 2 && (*report.numa[0].distances)[1].distance == 21 &&
      (*report.numa[1].distances)[0].node == 0 && (*report.numa[1].distances)[0].distance == 22,
      "NUMA distances lost their destination IDs or direction");
  check(report.pages.base_bytes == 4096 && report.pages.transparent_policy == TransparentPagePolicy::madvise &&
      report.pages.transparent_defrag == TransparentPageDefrag::defer_madvise && report.pages.transparent_sizes.size() == 2 &&
      report.pages.huge_sizes.size() == 1 && report.pages.huge_sizes[0].bytes == 2097152 &&
      report.pages.huge_sizes[0].free == 0 && report.pages.huge_sizes[0].reserved == 1,
      "page options or legitimate empty HugeTLB inventory were lost");
  const auto inherited = std::find_if(report.pages.transparent_sizes.begin(), report.pages.transparent_sizes.end(),
      [](const auto& option) { return option.bytes == 65536; });
  check(inherited != report.pages.transparent_sizes.end() && inherited->policy == TransparentPagePolicy::inherit,
      "per-size inherited THP policy was not represented");
  input.directories["/sys/devices/system/node"].push_back("node1");
  input.files["/sys/devices/system/node/node1/cpulist"] = "";
  const auto offline = input.describe();
  check(offline && offline->numa.size() == 3 && offline->numa[0].distances &&
      (*offline->numa[0].distances)[1].node == 2 && offline->numa[1].cpus == std::optional(std::vector<int>{}),
      "offline/memory-only node changed online distance column identities");
  input.directories["/sys/devices/system/node"].pop_back();

  const std::string root = "/sys/devices/system/cpu/cpu1";
  input.files.erase(root + "/cache/index0/coherency_line_size");
  input.files[root + "/cache/index0/ways_of_associativity"] = "0";
  input.files[root + "/cache/index0/size"] = "48KB";
  input.files[root + "/cache/index0/shared_cpu_list"] = "0-1,1-2";
  input.files[root + "/topology/thread_siblings_list"] = "2-3"; // Well-formed, but excludes this CPU.
  input.files[root + "/topology/physical_package_id"] = "-1";
  input.files["/sys/devices/system/node/node2/distance"] = "10";
  input.files["/sys/kernel/mm/transparent_hugepage/enabled"] = "[always] [never]";
  input.files["/sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages"] = "not-exposed";
  const auto partial = input.describe();
  check(partial.has_value(), "one malformed field rejected independent valid reports");
  const auto& bad = *partial->cpu(1);
  check(!bad.caches[0].line_bytes && !bad.caches[0].ways && !bad.caches[0].capacity_bytes &&
      !bad.caches[0].shared_cpus && !bad.thread_siblings && !bad.package_id &&
      bad.caches[1].capacity_bytes == 33554432 && !partial->numa[1].distances &&
      !partial->pages.transparent_policy && !partial->pages.huge_sizes[0].free,
      "malformed facts became zero or invalidated unrelated facts");
  Fixture unavailable;
  const auto unknown = unavailable.describe();
  check(unknown && !unknown->cpu(1)->advertised_features && !unknown->cpu(1)->identity.hypervisor_advertised &&
      !unknown->cpu(1)->cache_directory_reported && !unknown->cpu(1)->numa_nodes &&
      !unknown->numa_directory_reported && !unknown->pages.transparent_directory_reported &&
      !unknown->pages.huge_directory_reported && !unknown->pages.transparent_policy,
      "permission-limited discovery invented negative hardware facts");
  check(!input.describe(0) && !input.describe(-2), "CPU outside caller mask was accepted");
  constexpr std::array overlapping{1, 1, 8};
  check(!detail::describe_linux_platform(1, overlapping, 4096, {}, {}, input.source()), "duplicated allowed mask was accepted");
  input.files["/proc/cpuinfo"] += "\nprocessor : 1\nvendor_id : contradictory duplicate\n";
  const auto duplicate = input.describe();
  check(duplicate && !duplicate->cpu(1)->identity.vendor && !duplicate->cpu(1)->advertised_features &&
      duplicate->cpu(8)->identity.vendor == "AuthenticAMD", "duplicate CPU identity was resolved by arbitrary last/first block");

  input.files["/proc/cpuinfo"] = "processor : 1\nCPU implementer : 0x41\nCPU part : 0xd4f\nCPU variant : 0x0\nCPU revision : 1\nFeatures : fp asimd\n";
  input.files[root + "/regs/identification/midr_el1"] = "0x410fd4f1";
  const auto arm = input.describe();
  check(arm && arm->cpu(1)->identity.implementer == 65 && arm->cpu(1)->identity.part == 3407 &&
      arm->cpu(1)->identity.variant == 0 && arm->cpu(1)->identity.midr == 0x410fd4f1 &&
      !arm->cpu(8)->identity.vendor && !arm->cpu(8)->advertised_features,
      "ARM numeric identity or missing per-CPU identity inherited another processor's report");
}

void actual_linux() {
  const auto before = sched_getcpu();
  const auto report = discover_platform(before);
  check(report.has_value(), "actual Linux platform discovery failed");
  check(report->selected_cpu == before && report->cpu(before) && report->cpus.size() == report->allowed_cpus.size() &&
      std::binary_search(report->allowed_cpus.begin(), report->allowed_cpus.end(), before), "actual allowed inventory lost selected CPU");
  check(report->pages.base_bytes == std::uint64_t(sysconf(_SC_PAGESIZE)), "actual base page did not match Linux sysconf");
  check(report->isa && report->kernel, "actual uname report was unavailable");
  check(!discover_platform(-2), "invalid native CPU was accepted");
  std::printf("platform: selected=%d allowed=%zu caches=%zu numa=%zu base-page=%llu\n", report->selected_cpu,
      report->allowed_cpus.size(), report->cpu(before)->caches.size(), report->numa.size(),
      static_cast<unsigned long long>(*report->pages.base_bytes));
}
} // namespace
int main() { parsers(); reports(); actual_linux(); }
