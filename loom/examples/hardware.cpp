// A cold inspection client: reported topology and conditioned observations are
// printed separately from the compact policy selected for native work.
#include <loom/hardware.h>
#include <orbital/platform.h>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
void write(std::string_view text) {
  constexpr char hex[] = "0123456789abcdef";
  std::cout << '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') std::cout << '\\' << char(c);
    else if (c < 32) std::cout << "\\u00" << hex[c >> 4] << hex[c & 15];
    else std::cout << char(c);
  }
  std::cout << '"';
}
void write(const std::string& value) { write(std::string_view(value)); }
void write(bool value) { std::cout << (value ? "true" : "false"); }
template<class T> requires std::is_arithmetic_v<T>
void write(T value) {
  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(value)) { std::cout << "null"; return; }
  }
  std::cout << value;
}
template<class T> void write(const std::vector<T>& values) {
  std::cout << '[';
  bool first = true;
  for (const auto& value : values) {
    if (!first) std::cout << ',';
    first = false;
    write(value);
  }
  std::cout << ']';
}
template<class T> void write(const std::optional<T>& value) {
  if (value) write(*value);
  else std::cout << "null";
}
class Object {
  bool first_{true};
  void key(std::string_view name) {
    if (!first_) std::cout << ',';
    first_ = false;
    write(name); std::cout << ':';
  }
public:
  Object() { std::cout << '{'; }
  ~Object() { std::cout << '}'; }
  template<class T> void member(std::string_view name, const T& value) { key(name); write(value); }
  template<class F> void object(std::string_view name, F body) { key(name); Object child; body(child); }
  template<class Range, class F> void objects(std::string_view name, const Range& values, F body) {
    key(name); std::cout << '[';
    bool first = true;
    for (const auto& value : values) {
      if (!first) std::cout << ',';
      first = false;
      Object child; body(child, value);
    }
    std::cout << ']';
  }
  template<class Range, class F> void optional_objects(std::string_view name, const std::optional<Range>& values, F body) {
    if (values) objects(name, *values, body);
    else { key(name); std::cout << "null"; }
  }
};
template<class E, std::size_t N> std::string_view name(E value, const char* const (&names)[N]) {
  const auto index = static_cast<std::size_t>(value);
  return index < N ? names[index] : "unknown";
}
template<class E, std::size_t N> std::optional<std::string_view> name(std::optional<E> value, const char* const (&names)[N]) {
  return value ? std::optional(name(*value, names)) : std::nullopt;
}
constexpr const char* cache_names[]{"data", "instruction", "unified"};
constexpr const char* page_names[]{"always", "madvise", "never", "inherit"};
constexpr const char* defrag_names[]{"always", "defer", "defer_madvise", "madvise", "never"};
constexpr const char* origin_names[]{"provisional", "supplied", "retained", "local"};
constexpr const char* qualification_names[]{"not_measured", "qualified", "unsupported", "incomplete", "weak", "mixed", "censored", "failed", "cancelled", "timed_out", "disabled"};
constexpr const char* method_names[]{"none", "supplied", "random_encoded_chains", "stream_training", "single_demand_forward"};
constexpr const char* page_mode_names[]{"unknown", "base", "huge", "mixed"};
constexpr const char* interference_names[]{"unknown", "isolated", "foreground_conditional"};
constexpr const char* state_names[]{"pending", "lookup", "calibrated_local", "failed", "cancelled", "timed_out", "disabled"};
constexpr const char* forward_names[]{"unknown", "observed_benefit", "no_observed_benefit"};

void platform(Object& o, const orbital::PlatformReport& p) {
  o.member("selected_cpu", p.selected_cpu); o.member("allowed_cpus", p.allowed_cpus);
  o.member("isa", p.isa); o.member("kernel", p.kernel);
  o.objects("cpus", p.cpus, [](Object& c, const orbital::CpuReport& cpu) {
    c.member("cpu", cpu.cpu);
    c.object("identity", [&](Object& i) {
      const auto& v = cpu.identity;
      i.member("vendor", v.vendor); i.member("model_name", v.model_name); i.member("microcode", v.microcode);
      i.member("family", v.family); i.member("model", v.model); i.member("stepping", v.stepping);
      i.member("implementer", v.implementer); i.member("architecture", v.architecture); i.member("variant", v.variant);
      i.member("part", v.part); i.member("revision", v.revision); i.member("midr", v.midr);
      i.member("hypervisor_advertised", v.hypervisor_advertised);
    });
    c.member("advertised_features", cpu.advertised_features);
    c.member("core_id", cpu.core_id); c.member("package_id", cpu.package_id); c.member("die_id", cpu.die_id);
    c.member("thread_siblings", cpu.thread_siblings); c.member("package_cpus", cpu.package_cpus); c.member("numa_nodes", cpu.numa_nodes);
    c.member("cache_directory_reported", cpu.cache_directory_reported);
    c.objects("caches", cpu.caches, [](Object& v, const orbital::CacheReport& cache) {
      v.member("id", cache.id); v.member("level", cache.level); v.member("type", name(cache.type, cache_names));
      v.member("capacity_bytes", cache.capacity_bytes); v.member("line_bytes", cache.line_bytes);
      v.member("ways", cache.ways); v.member("sets", cache.sets); v.member("shared_cpus", cache.shared_cpus);
    });
  });
  o.member("numa_directory_reported", p.numa_directory_reported);
  o.objects("numa", p.numa, [](Object& n, const orbital::NumaReport& node) {
    n.member("node", node.node); n.member("cpus", node.cpus);
    n.optional_objects("distances", node.distances, [](Object& d, const orbital::NodeDistance& v) {
      d.member("node", v.node); d.member("distance", v.distance);
    });
  });
  o.object("pages", [&](Object& v) {
    v.member("base_bytes", p.pages.base_bytes);
    v.member("transparent_policy", name(p.pages.transparent_policy, page_names));
    v.member("transparent_defrag", name(p.pages.transparent_defrag, defrag_names));
    v.member("transparent_directory_reported", p.pages.transparent_directory_reported);
    v.member("huge_directory_reported", p.pages.huge_directory_reported);
    v.objects("transparent_sizes", p.pages.transparent_sizes, [](Object& s, const orbital::TransparentPageOption& x) {
      s.member("bytes", x.bytes); s.member("policy", name(x.policy, page_names));
    });
    v.objects("huge_sizes", p.pages.huge_sizes, [](Object& s, const orbital::HugePageOption& x) {
      s.member("bytes", x.bytes); s.member("total", x.total); s.member("free", x.free);
      s.member("reserved", x.reserved); s.member("surplus", x.surplus);
    });
  });
}
void field(Object& v, const loom::HardwareFieldObservation& f) {
  v.member("origin", name(f.origin, origin_names)); v.member("qualification", name(f.qualification, qualification_names));
  v.member("method", name(f.method, method_names));
  v.object("scope", [&](Object& s) {
    s.member("method_version", f.scope.method_version); s.member("control_protocol", f.scope.control_protocol);
    s.member("mapping_bytes", f.scope.mapping_bytes); s.member("base_page_bytes", f.scope.base_page_bytes);
    s.member("measurement_cpu", f.scope.measurement_cpu); s.member("page_mode", name(f.scope.page_mode, page_mode_names));
    s.member("interference", name(f.scope.interference, interference_names));
  });
}
void control(Object& v, const loom::HardwareControl& c) {
  v.member("cold_p10_ns", c.cold_p10); v.member("cold_median_ns", c.cold_median);
  v.member("hot_median_ns", c.hot_median); v.member("hot_p90_ns", c.hot_p90); v.member("trained_median_ns", c.trained_median);
}
void observations(Object& o, const loom::HardwareObservations& r) {
  o.object("width", [&](Object& f) { field(f, r.width); });
  o.object("training", [&](Object& f) { field(f, r.training); });
  o.objects("adjacent_forward", r.adjacent_forward, [](Object& f, const auto& v) { field(f, v); });
  o.member("width_candidates", r.width_candidates); o.member("retained_width_candidates", r.retained_width_candidates);
  o.member("local_attempted", r.local_attempted);
  o.member("retained_source_sha256", r.retained_source_sha256); o.member("retained_probe_format", r.retained_probe_format);
  o.member("retained_control_protocol", r.retained_control_protocol);
  o.member("retained_measurement_source_digests", r.retained_measurement_source_digests);
  o.objects("mapping_receipts", r.mapping_receipts, [](Object& p, const loom::HardwareMappingReceipt& m) {
    p.member("smaps_known", m.smaps_known); p.member("numa_known", m.numa_known); p.member("mapping_bytes", m.mapping_bytes);
    p.member("kernel_page_bytes", m.kernel_page_bytes); p.member("mmu_page_bytes", m.mmu_page_bytes);
    p.member("anonymous_huge_bytes", m.anonymous_huge_bytes); p.member("no_hugepage", m.no_hugepage);
    p.objects("residence", m.residence, [](Object& n, const auto& x) { n.member("node", x.node); n.member("pages", x.pages); });
  });
  o.object("local_evidence", [&](Object& e) {
    const auto& v = r.local_evidence;
    e.member("width_complete", v.width_complete); e.member("training_complete", v.training_complete);
    e.member("adjacent_even_complete", v.adjacent_complete[0]); e.member("adjacent_odd_complete", v.adjacent_complete[1]);
    e.objects("width_curves", v.ns_per_load, [](Object& row, const auto& values) {
      row.member("tested_widths", std::vector(loom::detail::hardware_widths.begin(), loom::detail::hardware_widths.end()));
      row.member("ns_per_load", std::vector(values.begin(), values.end()));
    });
    e.objects("training", v.training, [](Object& row, const auto& values) {
      row.objects("controls", values, [](Object& c, const auto& x) { control(c, x); });
    });
    e.objects("adjacent_forward", v.adjacent_forward, [](Object& row, const auto& values) {
      row.objects("controls", values, [](Object& c, const auto& x) { control(c, x); });
    });
  });
}
void policy(Object& o, const loom::HardwareSnapshot& s) {
  o.member("state", name(s.state, state_names)); o.member("revision", s.profile.revision);
  o.member("line_bytes", s.profile.geometry.line_bytes); o.member("page_bytes", s.profile.geometry.page_bytes);
  o.member("preferred_width", s.profile.preferred_width); o.member("training_accesses", s.profile.training_accesses);
  o.member("width_qualified", s.width_qualified); o.member("training_qualified", s.training_qualified);
  o.member("adjacent_even", name(s.adjacent_forward[0], forward_names));
  o.member("adjacent_odd", name(s.adjacent_forward[1], forward_names));
}
template<class T> bool parse(std::string_view text, T& value) {
  const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}
} // namespace

int main(int argc, char** argv) {
  int cpu = -1;
  unsigned mib = 64, budget_ms = 5000;
  bool calibrate{}, force_miss{};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--calibrate") calibrate = true;
    else if (arg == "--force-miss") force_miss = true;
    else if (arg == "--cpu" && i + 1 < argc && parse(std::string_view(argv[++i]), cpu)) {}
    else if (arg == "--mib" && i + 1 < argc && parse(std::string_view(argv[++i]), mib)) {}
    else if (arg == "--budget-ms" && i + 1 < argc && parse(std::string_view(argv[++i]), budget_ms)) {}
    else { std::cerr << "usage: loom_example_hardware [--cpu N] [--calibrate] [--force-miss] [--mib N] [--budget-ms N]\n"; return 2; }
  }
  if (mib < 1 || mib > 512 || !budget_ms || budget_ms > 30000) return 2;
  const auto reported = orbital::discover_platform(cpu);
  if (!reported) { std::cerr << "platform discovery failed\n"; return 1; }
  const auto context = loom::discover_hardware(reported->selected_cpu);
  if (!context) { std::cerr << "behavior context discovery failed\n"; return 1; }
  loom::HardwareOptions options;
  options.calibrate = calibrate;
  if (force_miss) options.table = {};
  options.limits.mapping_bytes = std::size_t(mib) * 1024 * 1024;
  options.limits.memory_bytes = options.limits.mapping_bytes * 2;
  options.limits.wall_time = std::chrono::milliseconds(budget_ms);
  loom::HardwareService service(options);
  const auto handle = service.request(*context);
  if (!handle) { std::cerr << "hardware request refused\n"; return 1; }
  const auto initial = (*handle)->inspect().snapshot;
  auto final = initial;
  while (final.state == loom::HardwareState::pending) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    final = (*handle)->snapshot();
  }
  const auto inspection = (*handle)->inspect();
  final = inspection.snapshot;
  const auto& evidence = inspection.observations;
  if (!evidence) { std::cerr << "missing cold observations\n"; return 1; }
  std::cout << std::setprecision(17);
  {
    Object output;
    output.member("format", 1); output.member("calibration_enabled", calibrate);
    output.object("calibration_limits", [&](Object& o) {
      o.member("mapping_bytes", options.limits.mapping_bytes); o.member("memory_bytes", options.limits.memory_bytes);
      o.member("wall_time_ms", options.limits.wall_time.count());
    });
    output.object("reported", [&](Object& o) { platform(o, *reported); });
    output.member("behavior_context", context->signature); output.member("context_qualified", context->qualified);
    output.object("initial_policy", [&](Object& o) { policy(o, initial); });
    output.object("policy", [&](Object& o) { policy(o, final); });
    output.object("observations", [&](Object& o) { observations(o, *evidence); });
  }
  std::cout << '\n';
}
