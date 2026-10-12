#pragma once

// Cold Linux reports. These describe visible hardware and policy, not measured
// costs, allocation placement, or permission to execute advertised instructions.

#include <orbital/types.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace orbital {

struct CpuIdentity {
  std::optional<std::string> vendor, model_name, microcode;
  std::optional<unsigned> family, model, stepping;
  std::optional<unsigned> implementer, architecture, variant, part, revision;
  std::optional<std::uint64_t> midr;
  std::optional<bool> hypervisor_advertised;
};

enum class CacheType { data, instruction, unified };
struct CacheReport {
  std::optional<unsigned> id, level;
  std::optional<CacheType> type;
  std::optional<std::uint64_t> capacity_bytes, line_bytes, ways, sets;
  /// Physical reported membership, including CPUs outside this thread's mask.
  std::optional<std::vector<int>> shared_cpus;
};

struct CpuReport {
  int cpu{};
  CpuIdentity identity;
  /// Advertisement alone establishes neither OS support nor safe execution.
  std::optional<std::vector<std::string>> advertised_features;
  std::optional<unsigned> core_id, package_id, die_id;
  std::optional<std::vector<int>> thread_siblings, package_cpus, numa_nodes;
  std::vector<CacheReport> caches;
  /// Empty caches plus false means discovery was unavailable, not no caches.
  bool cache_directory_reported{};
};

struct NodeDistance { int node{}; unsigned distance{}; };
struct NumaReport {
  int node{};
  std::optional<std::vector<int>> cpus;
  /// Kernel relative distances with ordered online destination node IDs;
  /// unavailable online membership leaves this unknown. These are not latency.
  std::optional<std::vector<NodeDistance>> distances;
};

enum class TransparentPagePolicy { always, madvise, never, inherit };
enum class TransparentPageDefrag { always, defer, defer_madvise, madvise, never };
struct TransparentPageOption {
  std::optional<std::uint64_t> bytes;
  std::optional<TransparentPagePolicy> policy;
};
struct HugePageOption {
  std::optional<std::uint64_t> bytes, total, free, reserved, surplus;
};
struct PageReport {
  std::optional<std::uint64_t> base_bytes;
  std::optional<TransparentPagePolicy> transparent_policy;
  std::optional<TransparentPageDefrag> transparent_defrag;
  std::vector<TransparentPageOption> transparent_sizes;
  std::vector<HugePageOption> huge_sizes;
  bool transparent_directory_reported{}, huge_directory_reported{};
};

struct PlatformReport {
  int selected_cpu{};
  /// Snapshot of the calling thread's allowed mask, not placement authority.
  std::vector<int> allowed_cpus;
  std::optional<std::string> isa, kernel;
  /// One report per allowed CPU; heterogeneous identities remain distinct.
  std::vector<CpuReport> cpus;
  std::vector<NumaReport> numa;
  bool numa_directory_reported{};
  PageReport pages;
  [[nodiscard]] const CpuReport* cpu(int number) const noexcept;
};

/// Does not pin, allocate page pools, change policy, or cache global state.
/// Unreadable/malformed fields remain unknown; -1 selects sched_getcpu().
[[nodiscard]] Result<PlatformReport> discover_platform(int cpu = -1);

namespace detail {
/// Linux text grammar, bounded to 65,536 CPUs. Reject duplicates/overlap,
/// reversed ranges, junk and expansion beyond the bound.
[[nodiscard]] std::optional<std::vector<int>> parse_platform_cpu_list(std::string_view);
[[nodiscard]] std::optional<std::uint64_t> parse_platform_size(std::string_view);
/// Test seam: paths are absolute Linux report paths. Missing/inaccessible files
/// and directories return nullopt; a readable empty directory returns {}.
struct LinuxPlatformSource {
  std::optional<std::string> (*read)(std::string_view, void*){};
  std::optional<std::vector<std::string>> (*entries)(std::string_view, void*){};
  void* context{};
};
[[nodiscard]] Result<PlatformReport> describe_linux_platform(int cpu,
    std::span<const int> allowed_cpus, std::optional<std::uint64_t> base_page_bytes,
    std::optional<std::string> isa, std::optional<std::string> kernel,
    LinuxPlatformSource);
} // namespace detail
} // namespace orbital
