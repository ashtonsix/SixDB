#include "fixture.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>
#include <time.h>

namespace {
namespace f = orbital_conversion_fixture;
namespace a = orbital::allocation;

void check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "orbital representation conversion diagnostic: %s\n", message);
  std::abort();
}

template<class T, class E> T take(std::expected<T, E> result, const char* message) {
  check(result.has_value(), message);
  return std::move(*result);
}

std::uint64_t now(clockid_t clock) {
  timespec value{};
  check(::clock_gettime(clock, &value) == 0, "clock_gettime failed");
  return std::uint64_t(value.tv_sec) * 1'000'000'000 + std::uint64_t(value.tv_nsec);
}

struct Trial {
  std::uint64_t thread_cpu_ns{}, wall_ns{};
};

struct Shutdown {
  Trial time;
  std::uint64_t close_calls{}, polled_messages{}, final_charged_bytes{};
  a::Statistics final;
};

struct Runtime {
  orbital::worker::Lane lane;
  orbital::Ref<a::Arena> arena;
  a::Pool* pool{};

  Runtime() {
    check(orbital::worker::adopt(lane).has_value(), "Lane adoption failed");
    arena = take(a::open_arena(64 * 1024 * 1024), "Arena creation failed");
    pool = take(a::add_pool(*arena, lane), "Pool creation failed");
  }

  Shutdown finish() {
    check(pool != nullptr, "Pool was already removed");
    Shutdown result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    const auto wall_start = now(CLOCK_MONOTONIC);
    const auto cpu_start = now(CLOCK_THREAD_CPUTIME_ID);
    for (;;) {
      ++result.close_calls;
      const auto progress = a::close(*pool);
      check(progress.kind() != orbital::Progress::Kind::failed, "physical close failed");
      if (progress.kind() == orbital::Progress::Kind::done) break;
      check(std::chrono::steady_clock::now() < deadline, "physical close did not drain");
      result.polled_messages += orbital::worker::poll(lane, 1024);
      std::this_thread::yield();
    }
    result.final_charged_bytes = arena->charged_bytes.load();
    result.final = a::statistics(*pool);
    check(result.final_charged_bytes == 0, "physical close retained a byte charge");
    check(result.final.mapped_bytes == 0 && result.final.reserved_bytes == 0 &&
          result.final.metadata_bytes == 0 && result.final.idle_bytes == 0 &&
          result.final.preparing_bytes == 0 && result.final.uncertain_bytes == 0 &&
          result.final.preparing_reserved_bytes == 0, "physical close retained allocator storage");
    check(a::remove_pool(*pool).has_value(), "closed Pool removal failed");
    result.time.thread_cpu_ns = now(CLOCK_THREAD_CPUTIME_ID) - cpu_start;
    result.time.wall_ns = now(CLOCK_MONOTONIC) - wall_start;
    pool = nullptr;
    // Arena/helper teardown is separate from the Pool close diagnostic.
    orbital::worker::relinquish(lane);
    arena.reset();
    return result;
  }

  ~Runtime() { if (pool) (void)finish(); }
};

std::uint64_t mask(unsigned width) {
  return width == 64 ? UINT64_MAX : (std::uint64_t{1} << width) - 1;
}

std::uint64_t scramble(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

// The expected records depend only on integer operations and the fixture schema.
std::vector<f::Record> records(const f::Schema& schema, std::size_t count) {
  std::vector<f::Record> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    for (std::size_t field = 0; field < schema.widths.size(); ++field) {
      const auto width = schema.widths[field];
      std::uint64_t value;
      switch (row % 9) {
      case 0: value = 0; break;
      case 1: value = mask(width); break;
      case 2: value = std::uint64_t{1} << (width - 1); break;
      case 3: value = 1; break;
      case 4: value = 0xaaaaaaaaaaaaaaaaULL; break;
      case 5: value = 0x5555555555555555ULL; break;
      default: value = scramble(0x123456789abcdef0ULL + row * 0x10001 + field * 0x100000001ULL); break;
      }
      result[row][field] = value & mask(width);
    }
  }
  return result;
}

void verify(const f::Representation& representation, const f::Schema& schema,
            const std::vector<f::Record>& oracle, f::Kind kind, f::LogicalIdentity logical) {
  const auto description = representation.describe();
  check(description.logical == logical && description.kind == kind &&
        description.records == oracle.size(), "conversion changed identity, kind or record count");
  check(description.field_widths.size() == schema.widths.size() &&
        std::equal(description.field_widths.begin(), description.field_widths.end(), schema.widths.begin()),
        "conversion changed field interpretation");
  check(description.parts.size() == (kind == f::Kind::columns ? schema.widths.size() : 1),
        "conversion returned the wrong Part count");
  const auto decoded = take(f::read_records(representation), "oracle readback failed");
  check(decoded == oracle, "all-field record oracle mismatch");
}

std::uint64_t body_bytes(const f::Representation& representation) {
  std::uint64_t result = 0;
  for (const auto& part : representation.describe().parts) result += part.body.size();
  return result;
}

double median(std::vector<std::uint64_t> values, std::size_t conversions) {
  std::sort(values.begin(), values.end());
  const auto middle = values.size() / 2;
  const double sum = values.size() % 2 ? double(values[middle]) :
      (double(values[middle - 1]) + double(values[middle])) / 2;
  return sum / double(conversions);
}

void costs_json(const f::Costs& costs) {
  std::cout << "{\"source_values\":" << costs.source_values
            << ",\"target_values\":" << costs.target_values
            << ",\"decode_bindings\":" << costs.decode_bindings
            << ",\"construction_bindings\":" << costs.construction_bindings
            << ",\"allocation_requests\":" << costs.allocations
            << ",\"allocated_body_bytes\":" << costs.allocated_body_bytes
            << ",\"padding_bytes\":" << costs.padding_bytes
            << ",\"issued_bytes\":" << costs.issued_bytes
            << ",\"issued_spans\":" << costs.issued_spans
            << ",\"completed_units\":" << costs.completed_units
            << ",\"scratch_bytes\":" << costs.scratch_bytes
            << ",\"descriptor_bytes\":" << costs.descriptor_bytes
            << ",\"construction_calls\":" << costs.construction_calls
            << ",\"guard_initialization_bytes\":" << costs.guard_initialization_bytes << '}';
}

void statistics_json(const a::Statistics& stats) {
  std::cout << "{\"mapped_bytes\":" << stats.mapped_bytes
            << ",\"reserved_bytes\":" << stats.reserved_bytes
            << ",\"metadata_bytes\":" << stats.metadata_bytes
            << ",\"idle_bytes\":" << stats.idle_bytes
            << ",\"preparing_bytes\":" << stats.preparing_bytes
            << ",\"uncertain_bytes\":" << stats.uncertain_bytes
            << ",\"preparing_reserved_bytes\":" << stats.preparing_reserved_bytes
            << ",\"allocations\":" << stats.allocations
            << ",\"reuses\":" << stats.reuses
            << ",\"growths\":" << stats.growths
            << ",\"relocations\":" << stats.relocations
            << ",\"foreign_returns\":" << stats.foreign_returns
            << ",\"trim_pages\":" << stats.trim_pages << '}';
}

void run_case(std::size_t count, f::Kind source_kind, std::size_t trials,
              std::size_t conversions) {
  Runtime runtime;
  const f::Schema schema{{1, 7, 9, 20, 31, 64}};
  const f::LogicalIdentity logical{{0x434f4e56455254ULL, count}, 7};
  const auto oracle = records(schema, count);
  const auto target_kind = source_kind == f::Kind::columns ? f::Kind::rows : f::Kind::columns;
  f::Costs source_costs;
  auto source = take(f::construct(*runtime.pool, logical, schema, oracle, source_kind, source_costs),
                     "source construction failed");
  verify(*source, schema, oracle, source_kind, logical);
  const auto source_body = body_bytes(*source);
  const auto fresh_source_charge = runtime.arena->charged_bytes.load();
  const auto fresh_source_stats = a::statistics(*runtime.pool);
  {
    f::Costs warmup_costs;
    const auto warmup = take(f::convert(*runtime.pool, *source, target_kind, warmup_costs),
                            "warm-up conversion failed");
    verify(*warmup, schema, oracle, target_kind, logical);
  }
  const auto reused_source_charge = runtime.arena->charged_bytes.load();
  const auto before_trials_stats = a::statistics(*runtime.pool);
  std::vector<Trial> samples(trials);
  f::Costs last_costs;
  a::Statistics last_live_stats{}, last_released_stats{};
  std::uint64_t last_live_charge{}, last_released_charge{}, target_body{}, released_targets{};
  for (auto& trial : samples) {
    for (std::size_t iteration = 0; iteration < conversions; ++iteration) {
      f::Costs costs;
      const auto wall_start = now(CLOCK_MONOTONIC);
      const auto cpu_start = now(CLOCK_THREAD_CPUTIME_ID);
      auto converted = f::convert(*runtime.pool, *source, target_kind, costs);
      trial.thread_cpu_ns += now(CLOCK_THREAD_CPUTIME_ID) - cpu_start;
      trial.wall_ns += now(CLOCK_MONOTONIC) - wall_start;
      auto target = take(std::move(converted), "timed conversion failed");
      verify(*target, schema, oracle, target_kind, logical);
      check(source.use_count() == 1 && target.use_count() == 1,
            "diagnostic unexpectedly retained another immutable closure");
      last_costs = costs;
      target_body = body_bytes(*target);
      last_live_stats = a::statistics(*runtime.pool);
      last_live_charge = runtime.arena->charged_bytes.load();
      const std::weak_ptr<const f::Representation> lifetime = target;
      target.reset(); // Target readback and release are outside every conversion interval.
      check(lifetime.expired(), "target closure survived release");
      ++released_targets;
      last_released_stats = a::statistics(*runtime.pool);
      last_released_charge = runtime.arena->charged_bytes.load();
    }
  }
  verify(*source, schema, oracle, source_kind, logical);
  const std::weak_ptr<const f::Representation> source_lifetime = source;
  source.reset();
  check(source_lifetime.expired(), "source closure survived release");
  const auto before_close_charge = runtime.arena->charged_bytes.load();
  const auto before_close_stats = a::statistics(*runtime.pool);
  const auto shutdown = runtime.finish();
  std::vector<std::uint64_t> cpu_samples, wall_samples;
  for (const auto& trial : samples) {
    cpu_samples.push_back(trial.thread_cpu_ns);
    wall_samples.push_back(trial.wall_ns);
  }
  std::cout << "{\"records\":" << count << ",\"direction\":\""
            << (source_kind == f::Kind::columns ? "columns_to_rows" : "rows_to_columns")
            << "\",\"source_parts\":" << (source_kind == f::Kind::columns ? schema.widths.size() : 1)
            << ",\"target_parts\":" << (target_kind == f::Kind::columns ? schema.widths.size() : 1)
            << ",\"source_body_bytes\":" << source_body << ",\"target_body_bytes\":" << target_body
            << ",\"median_trial_mean_thread_cpu_ns_per_conversion\":" << median(cpu_samples, conversions)
            << ",\"median_trial_mean_wall_ns_per_conversion\":" << median(wall_samples, conversions)
            << ",\"trial_totals\":[";
  for (std::size_t i = 0; i < samples.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << "{\"thread_cpu_ns\":" << samples[i].thread_cpu_ns
              << ",\"wall_ns\":" << samples[i].wall_ns << '}';
  }
  std::cout << "],\"last_conversion_costs\":"; costs_json(last_costs);
  std::cout << ",\"source_construction_costs\":"; costs_json(source_costs);
  std::cout << ",\"fresh_source_only_charged_bytes\":" << fresh_source_charge
            << ",\"source_only_charged_bytes_after_warmup_release\":" << reused_source_charge
            << ",\"last_source_target_charged_bytes\":" << last_live_charge
            << ",\"last_source_only_charged_bytes_after_target_release\":" << last_released_charge
            << ",\"charged_bytes_after_all_closure_release\":" << before_close_charge
            << ",\"fresh_source_statistics\":"; statistics_json(fresh_source_stats);
  std::cout << ",\"before_trials_statistics\":"; statistics_json(before_trials_stats);
  std::cout << ",\"last_source_target_statistics\":"; statistics_json(last_live_stats);
  std::cout << ",\"last_target_released_statistics\":"; statistics_json(last_released_stats);
  std::cout << ",\"all_closures_released_statistics\":"; statistics_json(before_close_stats);
  std::cout << ",\"verified_timed_targets\":" << released_targets
            << ",\"expired_timed_target_closures\":" << released_targets
            << ",\"expired_source_closure\":true,\"pool_close_poll_remove\":{\"thread_cpu_ns\":"
            << shutdown.time.thread_cpu_ns << ",\"wall_ns\":" << shutdown.time.wall_ns
            << ",\"close_calls\":" << shutdown.close_calls
            << ",\"polled_messages\":" << shutdown.polled_messages
            << ",\"final_charged_bytes\":" << shutdown.final_charged_bytes
            << ",\"removed\":true,\"final_statistics\":";
  statistics_json(shutdown.final);
  std::cout << "}}";
}

std::size_t option(std::string_view value) {
  std::size_t result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  check(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
        result > 0 && result <= 1000, "trial/conversion count must be an integer from 1 to 1000");
  return result;
}
} // namespace

int main(int argc, char** argv) {
  std::size_t trials = 5, conversions = 10;
  for (int i = 1; i < argc; ++i) {
    const std::string_view name{argv[i]};
    check((name == "--trials" || name == "--conversions") && i + 1 < argc,
          "usage: orbital_representation_conversion_bench [--trials N] [--conversions N]");
    const auto value = option(argv[++i]);
    if (name == "--trials") trials = value;
    else conversions = value;
  }
  std::cout.precision(12);
  std::cout << "{\"diagnostic\":true,\"schema_widths\":[1,7,9,20,31,64],\"trials\":" << trials
            << ",\"conversions_per_trial\":" << conversions
            << ",\"untimed_warmup_conversions_per_case\":1,"
               "\"timing_scope\":\"complete synchronous decode, allocation, reencode and immutable freeze\","
               "\"excluded_from_conversion_clock\":[\"source construction\",\"target oracle readback\","
               "\"target and source closure release\",\"Pool physical close\",\"Arena teardown\"],"
               "\"clocks\":{\"thread_cpu\":\"CLOCK_THREAD_CPUTIME_ID\",\"wall\":\"CLOCK_MONOTONIC\","
               "\"probe_overhead_subtracted\":false},"
               "\"limitations\":[\"local sequential repeated source with warmed target inventory\","
               "\"wall interval includes the CPU clock probes\",\"thread CPU excludes Arena helper CPU\","
               "\"fixture sentinel body initialization is included\","
               "\"charged bytes include idle storage and allocator metadata\","
               "\"scratch and descriptor bytes are fixture accounting, not process RSS\","
               "\"no request tails, cache-tier, hardware uplift or version-publication claims\"],\"cases\":[";
  bool first = true;
  for (const auto count : {std::size_t{1025}, std::size_t{4097}}) {
    for (const auto kind : {f::Kind::columns, f::Kind::rows}) {
      if (!first) std::cout << ',';
      first = false;
      run_case(count, kind, trials, conversions);
    }
  }
  std::cout << "]}\n";
}
