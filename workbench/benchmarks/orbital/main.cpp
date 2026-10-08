#include "workload.h"
#include "pmu.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <time.h>

namespace b = orbital_bench;
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t cpu_ns(clockid_t clock = CLOCK_THREAD_CPUTIME_ID) {
  timespec time{};
  if (clock_gettime(clock, &time)) throw std::runtime_error("CPU clock failed");
  return std::uint64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
}
double ns(Clock::duration duration) { return std::chrono::duration<double, std::nano>(duration).count(); }
struct Options {
  std::string backend{"packed-owner"}, workload{"point-hit"}, mode{"latency"}, output;
  std::string read_execution{"serial"}, cache_setup{"loaded"}, memory_receipt{"off"};
  std::size_t records{8192}, operations{20000}, batch_width{1}, read_active{16}, warmup_passes{1};
  std::uint64_t seed{20261007};
  std::size_t prepared_pages{};
  int preparer_cpu{-1};
  bool prepared_prefill{true};
  b::pmu::Mode pmu{b::pmu::Mode::off};
  std::vector<b::pmu::EventSpec> pmu_events;
};
const char* pmu_name(b::pmu::Mode mode) {
  switch (mode) {
    case b::pmu::Mode::off: return "off";
    case b::pmu::Mode::basic: return "basic";
    case b::pmu::Mode::stalls: return "stalls";
  }
  throw std::invalid_argument("unknown PMU mode");
}
Options options(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 == argc) throw std::runtime_error("every option needs a value");
    const std::string key = argv[i], value = argv[i + 1];
    if (key == "--backend") o.backend = value;
    else if (key == "--workload") o.workload = value;
    else if (key == "--mode") o.mode = value;
    else if (key == "--output") o.output = value;
    else if (key == "--records") o.records = std::stoull(value);
    else if (key == "--operations") o.operations = std::stoull(value);
    else if (key == "--prepared-pages") o.prepared_pages=std::stoull(value);
    else if (key == "--preparer-cpu") o.preparer_cpu=std::stoi(value);
    else if (key == "--prepared-prefill") {
      if (value!="on" && value!="off") throw std::invalid_argument("prepared prefill must be on/off");
      o.prepared_prefill=value=="on";
    }
    else if (key == "--seed") o.seed = std::stoull(value);
    else if (key == "--batch-width") o.batch_width = std::stoull(value);
    else if (key == "--read-execution") o.read_execution = value;
    else if (key == "--read-active") o.read_active = std::stoull(value);
    else if (key == "--cache-setup") o.cache_setup = value;
    else if (key == "--memory-receipt") o.memory_receipt = value;
    else if (key == "--warmup-passes") o.warmup_passes = std::stoull(value);
    else if (key == "--pmu") {
      if (value == "off") o.pmu = b::pmu::Mode::off;
      else if (value == "basic") o.pmu = b::pmu::Mode::basic;
      else if (value == "stalls") o.pmu = b::pmu::Mode::stalls;
      else throw std::invalid_argument("PMU mode must be off, basic or stalls");
    }
    else if (key == "--pmu-event") o.pmu_events.push_back(b::pmu::parse_event_spec(value));
    else throw std::runtime_error("unknown option: " + key);
  }
  if (o.output.empty() || (o.mode != "latency" && o.mode != "throughput" && o.mode != "validate" && o.mode != "events") ||
      (o.backend != "packed" && o.backend != "packed-owner" && o.backend != "packed-retained" && o.backend != "lmdb" &&
       o.backend != "lmdb-read-batch64") || !o.batch_width || o.batch_width > 64 ||
      !o.read_active || o.read_active > 64 || !o.warmup_passes || o.warmup_passes > 16 ||
      (o.cache_setup != "loaded" && o.cache_setup != "warm-trace") ||
      (o.memory_receipt != "off" && o.memory_receipt != "on") ||
      (o.read_execution != "serial" && o.read_execution != "native" &&
       o.read_execution != "manual" && o.read_execution != "ring")) throw std::runtime_error("invalid options");
  if (o.read_execution != "serial" && o.backend != "packed-owner" && !o.backend.starts_with("lmdb"))
    throw std::runtime_error("staged read batch requires synchronous owner access");
  if (o.pmu != b::pmu::Mode::off && o.mode != "throughput")
    throw std::invalid_argument("PMU collection requires a separate throughput run");
  if (o.memory_receipt == "on" && o.mode != "throughput")
    throw std::invalid_argument("memory receipts require a separate throughput run");
  if (o.pmu == b::pmu::Mode::off && !o.pmu_events.empty())
    throw std::invalid_argument("PMU events require basic or stalls mode");
  if (o.mode == "events" && (o.batch_width != 1 || o.read_execution != "serial" ||
      o.cache_setup != "loaded" || o.backend.starts_with("lmdb")))
    throw std::invalid_argument("event replay requires scalar loaded packed requests");
  if (o.prepared_pages>4096 || o.preparer_cpu < -1 ||
      (o.prepared_pages && (o.backend.starts_with("lmdb") || o.mode=="events")))
    throw std::invalid_argument("prepared pages require Packed timing/validation and count<=4096");
  return o;
}
std::uint64_t ordered_hash(b::Store& store) {
  std::uint64_t result = 0;
  b::Key key = 0;
  std::array<b::Row, 64> rows;
  store.begin(false);
  for (;;) {
    auto count = store.scan(key, rows);
    if (!count) break;
    const auto part = b::fingerprint(std::span(rows).first(count));
    result ^= part + 0x9e3779b97f4a7c15ull + (result << 6) + (result >> 2);
    if (rows[count - 1].key == std::numeric_limits<b::Key>::max()) break;
    key = rows[count - 1].key + 1;
  }
  store.end(); return result;
}
void memory_receipt(const std::filesystem::path& output, b::Store& store,
                    const std::vector<b::Row>& rows, const b::Trace& trace) {
  auto regions = store.memory_regions();
  regions.push_back({reinterpret_cast<std::uintptr_t>(rows.data()), rows.size() * sizeof(b::Row), "initial_rows"});
  regions.push_back({reinterpret_cast<std::uintptr_t>(trace.operations.data()),
                     trace.operations.size() * sizeof(b::Operation), "request_trace"});
  std::ofstream receipt(output / "memory-regions.json");
  receipt << "{\n  \"schema\": 1,\n"
          << "  \"phase\": \"post request loop and drain, before final validation; Store live\",\n"
          << "  \"scope\": \"representation extents; overlapping regions must be unioned; not observed accessed lines\",\n"
          << "  \"unreported\": \"stack and request output scratch; execution bank unless enumerated; empty store list means unreported\",\n"
          << "  \"observed_load_service_tier\": null,\n"
          << "  \"input_fingerprint\": " << b::fingerprint(rows) << ",\n"
          << "  \"trace_fingerprint\": " << trace.fingerprint << ",\n"
          << "  \"completed_requests\": " << trace.operations.size() << ",\n  \"regions\": [\n";
  for (std::size_t i = 0; i < regions.size(); ++i) {
    const auto& region = regions[i];
    receipt << "    {\"address\": \"0x" << std::hex << region.address << std::dec
            << "\", \"bytes\": " << region.bytes << ", \"kind\": \"" << region.kind
            << "\"}" << (i + 1 == regions.size() ? "\n" : ",\n");
  }
  receipt << "  ],\n  \"kernel_mappings\": [\n";
  // Preserve actual kernel mapping observations rather than infer them from
  // allocator counts or requested advice. These reads occur after PMU stop.
  for (const auto* name : {"smaps", "numa_maps"}) {
    errno = 0;
    std::ifstream source(std::string("/proc/self/") + name);
    const auto open_errno = errno;
    const auto file = std::string("memory-") + name + ".txt";
    receipt << "    {\"source\": \"" << name << "\", \"status\": \"";
    if (!source) receipt << "unavailable\", \"errno\": " << open_errno;
    else {
      std::ofstream target(output / file);
      target << source.rdbuf();
      if (!target || source.bad()) throw std::runtime_error("kernel mapping receipt write/read failed");
      receipt << "collected\", \"file\": \"" << file << "\"";
    }
    receipt << "}" << (std::string_view(name) == "smaps" ? ",\n" : "\n");
  }
  receipt << "  ]\n}\n";
  if (!receipt) throw std::runtime_error("memory region receipt write failed");
}
void write_preparation_stats(std::ostream& output,const b::PreparationStats& s) {
  output << "{\n";
  output << "    \"storage_observed\": " << (s.storage_observed ? "true" : "false") << ",\n";
  output << "    \"inventory_limit\": " << s.inventory_limit << ",\n";
  output << "    \"inventory\": " << s.inventory << ",\n";
  output << "    \"ready\": " << s.ready << ",\n";
  output << "    \"inflight\": " << s.inflight << ",\n";
  output << "    \"repair_wait\": " << s.repair_wait << ",\n";
  output << "    \"peak_inventory\": " << s.peak_inventory << ",\n";
  output << "    \"peak_ready\": " << s.peak_ready << ",\n";
  output << "    \"peak_arena_charge\": " << s.peak_arena_charge << ",\n";
  output << "    \"arena_charge\": " << s.arena_charge << ",\n";
  output << "    \"ready_backing_bytes\": " << s.ready_backing_bytes << ",\n";
  output << "    \"preparing_bytes\": " << (s.storage_observed ? std::to_string(s.preparing_bytes) : "null") << ",\n";
  output << "    \"preparing_reserved_bytes\": " << (s.storage_observed ? std::to_string(s.preparing_reserved_bytes) : "null") << ",\n";
  output << "    \"submissions\": " << s.submissions << ",\n";
  output << "    \"admission_failures\": " << s.admission_failures << ",\n";
  output << "    \"demands\": " << s.demands << ",\n";
  output << "    \"ready_hits\": " << s.ready_hits << ",\n";
  output << "    \"stockouts\": " << s.stockouts << ",\n";
  output << "    \"inline_fallbacks\": " << s.inline_fallbacks << ",\n";
  output << "    \"owner_messages\": " << s.owner_messages << ",\n";
  output << "    \"cleanup_retries\": " << s.cleanup_retries << ",\n";
  output << "    \"unused_inventory\": " << s.unused_inventory << ",\n";
  output << "    \"discarded_completed_pages\": " << s.discarded_completed_pages << ",\n";
  output << "    \"discarded_pack_bytes\": " << s.discarded_pack_bytes << ",\n";
  output << "    \"discarded_pack_spans\": " << s.discarded_pack_spans << ",\n";
  output << "    \"discarded_index_bytes\": " << s.discarded_index_bytes << ",\n";
  output << "    \"discarded_index_spans\": " << s.discarded_index_spans << ",\n";
  output << "    \"private_metadata_bound_bytes\": " << s.private_metadata_bound_bytes << ",\n";
  output << "    \"completed_pages\": " << s.completed_pages << ",\n";
  output << "    \"completed_pack_bytes\": " << s.completed_pack_bytes << ",\n";
  output << "    \"completed_pack_spans\": " << s.completed_pack_spans << ",\n";
  output << "    \"completed_index_bytes\": " << s.completed_index_bytes << ",\n";
  output << "    \"completed_index_spans\": " << s.completed_index_spans << ",\n";
  output << "    \"constructor_failures\": " << s.constructor_failures << ",\n";
  output << "    \"startup_ns\": " << s.startup_ns << ",\n";
  output << "    \"startup_process_cpu_ns\": " << s.startup_process_cpu_ns << ",\n";
  output << "    \"startup_producer_cpu_ns\": " << s.startup_producer_cpu_ns << ",\n";
  output << "    \"producer_cpu_ns\": " << s.producer_cpu_ns << ",\n";
  output << "    \"final_producer_cpu_ns\": " << s.final_producer_cpu_ns << ",\n";
  output << "    \"drain_ns\": " << s.drain_ns << ",\n";
  output << "    \"requested_cpu\": " << s.requested_cpu << ",\n";
  output << "    \"observed_cpu\": " << s.observed_cpu << ",\n";
  output << "    \"owner_cpu\": " << s.owner_cpu << "\n";
  output << "  }";
}
} // namespace

int main(int argc, char** argv) try {
  const auto o = options(argc, argv);
  const std::filesystem::path output = o.output;
  if (std::filesystem::exists(output) && !std::filesystem::is_empty(output))
    throw std::runtime_error("output directory must be new/empty");
  std::filesystem::create_directories(output);
  const auto rows = b::initial_rows(o.records, o.seed);
  const auto trace = b::make_trace(o.workload, o.records, o.operations, o.seed + 1);
  const auto requests = trace.operations.size();
  const bool batched = o.batch_width > 1 || o.read_execution != "serial";
  const auto batches = (requests + o.batch_width - 1) / o.batch_width;
  if (batched && !std::all_of(trace.operations.begin(), trace.operations.end(),
        [](const b::Operation& op) { return b::lookup_batch_eligible(op.kind); }))
    throw std::runtime_error("batch timing supports point reads/misses and dependent reads only");
  if (o.cache_setup == "warm-trace" && !std::all_of(trace.operations.begin(), trace.operations.end(),
        [](const b::Operation& op) { return !b::mutates(op.kind); }))
    throw std::invalid_argument("warm-trace conditioning supports read-only traces");
  const auto execution = o.backend.starts_with("lmdb") || o.read_execution == "serial" ? b::ReadExecution::serial
                       : o.read_execution == "native" ? b::ReadExecution::native_direct
                       : o.read_execution == "manual" ? b::ReadExecution::manual : b::ReadExecution::ring;
  std::vector<b::Outcome> wanted;
  wanted.reserve(requests);
  std::uint64_t oracle_final_hash;
  {
    auto oracle = b::reference_store(); oracle->load(rows);
    for (const auto& op : trace.operations) wanted.push_back(b::execute(*oracle, op));
    oracle_final_hash = ordered_hash(*oracle);
  }
  // Untimed validation and conditioning use the same independently known trace.
  auto verify_trace = [&](b::Store& store) {
    if (!batched) {
      for (std::size_t i = 0; i < requests; ++i)
        if (b::execute(store,trace.operations[i]) != wanted[i])
          throw std::runtime_error("validation differs at request " + std::to_string(i));
    } else {
      std::array<b::Outcome, 64> output_rows;
      for (std::size_t first = 0; first < requests; first += o.batch_width) {
        const auto count = std::min(o.batch_width, requests - first);
        store.read_batch(std::span(trace.operations).subspan(first,count), std::span(output_rows).first(count), execution);
        for (std::size_t i = 0; i < count; ++i)
          if (output_rows[i] != wanted[first+i])
            throw std::runtime_error("validation differs at request " + std::to_string(first+i));
      }
    }
  };
  auto factory = [&](const std::string& name) {
    const auto bytes = std::max<std::size_t>(256u << 20, (o.records + o.operations) * 128);
    const auto access = o.backend == "packed-owner" ? b::Access::owner_ready
                        : o.backend == "packed-retained" ? b::Access::retained_hold : b::Access::fresh_hold;
    return !o.backend.starts_with("lmdb") ? b::packed_store(bytes, access)
                                : b::lmdb_store((output / name).string(), bytes, o.backend == "lmdb-read-batch64");
  };
  std::uint64_t final_hash;
  {
    auto store = factory("validation-db"); store->load(rows);
    if (batched) store->prepare_read_batch(execution, o.read_active);
    if (o.prepared_pages) store->start_preparation(o.prepared_pages,o.preparer_cpu,o.prepared_prefill);
    verify_trace(*store);
    store->drain(); store->audit(); final_hash = ordered_hash(*store);
    if (oracle_final_hash != final_hash) throw std::runtime_error("final ordered state mismatch");
  }
  double total_ns = 0, thread_ns = 0, process_ns = 0, drain_ns = 0, p50 = 0, p99 = 0, batch_p50 = 0, batch_p99 = 0, timer_p50 = 0;
  std::uint64_t consumed = 0, logical_rows = 0, changes = 0, nonempty = 0;
  b::Counters counters, before;
  double warmup_ns = 0;
  b::PreparationStats preparation_start,preparation_end,preparation_done;
  if (o.mode == "events") {
    // Untimed logical classification, adapted from the consumer's split-tail
    // investigation. Never add per-request observers to the primary loop.
    auto store = factory("events-db"); store->load(rows);
    before = store->counters();
    auto prior = store->counters(false);
    std::ofstream events(output / "request-events.csv");
    events << "request,kind,splits,pages_created,payload_capacity_added,pack_bytes,pack_spans,"
              "index_bytes,index_spans,lease_calls,rows,changes,outcome_digest,logical_live_rows_after\n";
    for (std::size_t i = 0; i < requests; ++i) {
      const auto value = b::execute(*store, trace.operations[i]);
      if (value != wanted[i]) throw std::runtime_error("event replay differs at request " + std::to_string(i));
      const auto after = store->counters(false);
      events << i << ',' << unsigned(trace.operations[i].kind) << ',' << after.splits - prior.splits
             << ',' << after.allocated_pages - prior.allocated_pages << ',' << after.payload_bytes - prior.payload_bytes
             << ',' << after.issued_write_bytes - prior.issued_write_bytes
             << ',' << after.issued_write_spans - prior.issued_write_spans
             << ',' << after.index_write_bytes - prior.index_write_bytes
             << ',' << after.index_write_spans - prior.index_write_spans
             << ',' << after.lease_calls - prior.lease_calls
             << ',' << value.rows << ',' << value.changes << ',' << value.digest
             << ',' << after.logical_live_rows << '\n';
      consumed ^= value.digest + i; logical_rows += value.rows; changes += value.changes;
      nonempty += bool(value.rows || value.changes);
      prior = after;
    }
    if (!events) throw std::runtime_error("request event write failed");
    store->drain();
    counters = store->counters();
    store->audit();
    if (ordered_hash(*store) != final_hash) throw std::runtime_error("event replay final state mismatch");
  } else if (o.mode != "validate") {
    auto store = factory("timing-db"); store->load(rows);
    // Prepare the reusable bounded execution bank without reading any row.
    // First-use Ring configuration is setup, not a request or batch latency.
    if (batched) store->prepare_read_batch(execution, o.read_active);
    // Setup/first-touch is excluded. Replaying a fresh copy of the exact trace
    // avoids timing the already-mutated validation state.
    std::vector<double> latencies(o.mode == "latency" ? batches : 0);
    std::vector<b::PreparationDemands> preparation_samples(o.prepared_pages && o.mode=="latency" ? batches : 0);
    std::vector<double> timer(4096);
    for (auto& value : timer) {
      auto start = Clock::now(); value = ns(Clock::now() - start);
    }
    std::sort(timer.begin(), timer.end()); timer_p50 = timer[timer.size() / 2];
    // Count only this caller thread over the request loop and physical drain.
    // Opening/reading events and writing receipts stay outside request timing.
    b::pmu::Session pmu(o.pmu, o.pmu_events);
    if (o.cache_setup == "warm-trace") {
      const auto warm_start = Clock::now();
      for (std::size_t pass = 0; pass < o.warmup_passes; ++pass) {
        verify_trace(*store);
        store->drain();
      }
      warmup_ns = ns(Clock::now() - warm_start);
    }
    // Producer starts only after loading, bank/sample/timer setup and conditioning.
    if (o.prepared_pages) {
      store->start_preparation(o.prepared_pages,o.preparer_cpu,o.prepared_prefill);
      preparation_start=store->preparation_stats(true);
    }
    // Actual starting storage/cumulative baselines include any owner-published
    // prefill. Refresh for every competitor outside timing after final setup.
    before=store->counters();
    // Whole-process CPU includes preparation/reclamation helpers that run in
    // this interval. Keep these boundary reads outside caller PMU/wall clocks.
    const auto process_start = cpu_ns(CLOCK_PROCESS_CPUTIME_ID);
    pmu.start();
    const auto cpu_start = cpu_ns(); const auto start = Clock::now();
    if (!batched) {
      b::PreparationDemands previous_demands;
      // Preserve the original scalar loop; batched bookkeeping adds no work to it.
      for (std::size_t i = 0; i < requests; ++i) {
        const auto request_start = o.mode == "latency" ? Clock::now() : Clock::time_point{};
        const auto result = b::execute(*store,trace.operations[i]);
        if (o.mode == "latency") latencies[i] = ns(Clock::now() - request_start);
        if (!preparation_samples.empty()) {
          // Actual cumulative observations, read after the request clock stops.
          // Packed begin/get/end carry no flag reset or inventory branch.
          const auto current=store->preparation_demands();
          preparation_samples[i]={current.demands-previous_demands.demands,
              current.ready_hits-previous_demands.ready_hits,current.stockouts-previous_demands.stockouts,
              current.inline_fallbacks-previous_demands.inline_fallbacks};
          previous_demands=current;
        }
        consumed ^= result.digest + i; logical_rows += result.rows; changes += result.changes;
        nonempty += bool(result.rows || result.changes);
      }
    } else {
      std::array<b::Outcome, 64> output_rows;
      for (std::size_t first = 0; first < requests; first += o.batch_width) {
        const auto count = std::min(o.batch_width, requests - first);
        const auto request_start = o.mode == "latency" ? Clock::now() : Clock::time_point{};
        store->read_batch(std::span(trace.operations).subspan(first,count), std::span(output_rows).first(count), execution);
        if (o.mode == "latency") latencies[first/o.batch_width] = ns(Clock::now() - request_start);
        for (std::size_t i = 0; i < count; ++i) {
          const auto& result = output_rows[i];
          consumed ^= result.digest + first+i; logical_rows += result.rows; changes += result.changes;
          nonempty += bool(result.rows || result.changes);
        }
      }
    }
    auto drain_start = Clock::now();
    if (o.prepared_pages) preparation_end=store->preparation_stats();
    store->drain();
    const auto end = Clock::now();
    const auto cpu_end = cpu_ns();
    pmu.stop();
    process_ns = cpu_ns(CLOCK_PROCESS_CPUTIME_ID) - process_start;
    if (o.prepared_pages) preparation_done=store->preparation_stats(true);
    total_ns = ns(end - start); drain_ns = ns(end - drain_start); thread_ns = cpu_end - cpu_start;
    if (o.memory_receipt == "on") memory_receipt(output, *store, rows, trace);
    counters = store->counters();
    // Validate outside the timed interval too; a consumed checksum prevents
    // dead-code elimination but does not stand in for independently known rows.
    std::uint64_t wanted_consumed = 0;
    std::uint64_t wanted_rows = 0, wanted_changes = 0;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
      wanted_consumed ^= wanted[i].digest + i;
      wanted_rows += wanted[i].rows; wanted_changes += wanted[i].changes;
    }
    if (consumed != wanted_consumed || logical_rows != wanted_rows || changes != wanted_changes ||
        ordered_hash(*store) != final_hash)
      throw std::runtime_error("timed execution differs from validated trace");
    store->audit();
    if (o.pmu != b::pmu::Mode::off) {
      std::ofstream hardware(output / "pmu.json");
      hardware << "{\n  \"schema\": 1,\n"
               << "  \"scope\": \"caller thread; request loop, result aggregation, final drain and boundary clocks; setup and validation excluded\",\n"
               << "  \"input_fingerprint\": " << b::fingerprint(rows) << ",\n"
               << "  \"trace_fingerprint\": " << trace.fingerprint << ",\n"
               << "  \"completed_requests\": " << requests << ",\n"
               << "  \"cache_setup\": \"" << o.cache_setup << "\",\n"
               << "  \"warmup_passes\": " << (o.cache_setup == "warm-trace" ? o.warmup_passes : 0) << ",\n"
               << "  \"warmup_ns\": " << warmup_ns << ",\n"
               << "  \"read_active\": " << o.read_active << ",\n"
         << "  \"prepared_pages\": " << o.prepared_pages << ",\n"
         << "  \"preparation_file\": " << (o.prepared_pages && o.mode!="validate" ? "\"preparation.json\"" : "null") << ",\n"
               << "  \"measurement\": ";
      pmu.write_json(hardware);
      hardware << "\n}\n";
      if (!hardware) throw std::runtime_error("PMU receipt write failed");
    }
    if (o.prepared_pages) {
      std::ofstream preparation(output / "preparation.json");
      preparation << "{\n  \"schema\": 1,\n"
          << "  \"scope\": \"maintained Orbital/Loom integration; explicit startup after loading/calibration/conditioning; physical preparation drain and helper join in measured final drain\",\n"
          << "  \"input_fingerprint\": " << b::fingerprint(rows) << ",\n"
          << "  \"trace_fingerprint\": " << trace.fingerprint << ",\n"
          << "  \"completed_requests\": " << requests << ",\n"
          << "  \"completed_work_scope\": \"all successfully completed producer Page construction, including unused/cancelled stock; partial failed construction writes not quantified\",\n"
          << "  \"inflight_scope\": \"accepted work with backend ownership, including helper completion awaiting owner delivery\",\n"
          << "  \"charge_scope\": \"whole Arena charge observed at creation/start/drain boundaries, including ordinary live Pages and metadata; private heap bound is separate and excludes thread stack/temporary work; not preparation-only peak\",\n"
          << "  \"producer_cpu_scope\": \"placed helper thread, cumulative thread-clock snapshots; request/drain includes wake and service overhead\",\n"
          << "  \"resource_scope\": \"default -1 selects from caller affinity; after one-CPU pinning it uses the owner CPU; a distinct helper CPU is an additional resource, not one-vCPU headroom\",\n"
          << "  \"prefill\": " << (o.prepared_prefill ? "true" : "false") << ",\n"
          << "  \"producer_request_and_drain_cpu_ns\": " << preparation_done.producer_cpu_ns-preparation_start.producer_cpu_ns << ",\n"
          << "  \"request_interval_completed_pages\": " << preparation_end.completed_pages-preparation_start.completed_pages << ",\n"
          << "  \"drain_completed_pages\": " << preparation_done.completed_pages-preparation_end.completed_pages << ",\n"
          << "  \"startup\": ";
      write_preparation_stats(preparation,preparation_start);
      preparation << ",\n  \"before_drain\": "; write_preparation_stats(preparation,preparation_end);
      preparation << ",\n  \"settled\": "; write_preparation_stats(preparation,preparation_done);
      preparation << "\n}\n";
      if (!preparation) throw std::runtime_error("preparation receipt write failed");
      if (!preparation_samples.empty()) {
        std::ofstream flags(output / "preparation-samples.csv");
        flags << "first_request,requests,latency_ns,demands,ready_hits,stockouts,inline_fallbacks\n";
        for (std::size_t i=0;i<preparation_samples.size();++i) {
          const auto& flag=preparation_samples[i];
          flags << i*o.batch_width << ',' << std::min(o.batch_width,requests-i*o.batch_width) << ',' << latencies[i]
                << ',' << flag.demands << ',' << flag.ready_hits << ',' << flag.stockouts << ',' << flag.inline_fallbacks << '\n';
        }
        if (!flags) throw std::runtime_error("preparation sample write failed");
      }
    }
    if (!latencies.empty()) {
      std::ofstream samples(output / (!batched ? "latencies.csv" : "batch-latencies.csv"));
      if (!batched) samples << "request,latency_ns\n";
      else samples << "first_request,requests,batch_latency_ns\n";
      for (std::size_t i = 0; i < latencies.size(); ++i) {
        samples << i*o.batch_width << ',';
        if (batched) samples << std::min(o.batch_width,requests-i*o.batch_width) << ',';
        samples << latencies[i] << '\n';
      }
      std::sort(latencies.begin(), latencies.end());
      const auto median = latencies[(latencies.size() - 1) * 50 / 100];
      const auto tail = latencies[(latencies.size() - 1) * 99 / 100];
      if (!batched) { p50 = median; p99 = tail; }
      else { batch_p50 = median; batch_p99 = tail; }
    }
  }
  // These databases were created exclusively under this fresh result directory.
  // Keep reproducible traces, samples and receipts, not replayable scratch DBs.
  if (o.backend.starts_with("lmdb")) {
    std::filesystem::remove_all(output / "validation-db");
    std::filesystem::remove_all(output / "timing-db");
  }
  counters.lease_calls -= before.lease_calls;
  counters.issued_write_bytes -= before.issued_write_bytes;
  counters.issued_write_spans -= before.issued_write_spans;
  counters.splits -= before.splits;
  counters.index_write_bytes -= before.index_write_bytes;
  counters.index_write_spans -= before.index_write_spans;
  counters.pool_allocations -= before.pool_allocations;
  counters.pool_reuses -= before.pool_reuses;
  counters.pool_growths -= before.pool_growths;
  counters.pool_trim_pages -= before.pool_trim_pages;

  std::ofstream result(output / "result.json");
  result << std::setprecision(12) << "{\n"
         << "  \"schema\": 1,\n  \"backend\": \"" << o.backend << "\",\n"
         << "  \"workload\": \"" << o.workload << "\",\n  \"mode\": \"" << o.mode << "\",\n"
         << "  \"pmu_mode\": \"" << pmu_name(o.pmu) << "\",\n"
         << "  \"pmu_file\": " << (o.pmu == b::pmu::Mode::off ? "null" : "\"pmu.json\"") << ",\n"
         << "  \"memory_receipt\": \"" << o.memory_receipt << "\",\n"
         << "  \"records\": " << o.records << ",\n  \"requests\": " << requests << ",\n"
         << "  \"batch_width\": " << o.batch_width << ",\n  \"batches\": " << batches << ",\n"
         << "  \"read_active\": " << o.read_active << ",\n"
         << "  \"prepared_pages\": " << o.prepared_pages << ",\n"
         << "  \"preparation_file\": " << (o.prepared_pages && o.mode!="validate" ? "\"preparation.json\"" : "null") << ",\n"
         << "  \"cache_setup\": \"" << o.cache_setup << "\",\n"
         << "  \"warmup_passes\": " << (o.mode != "validate" && o.cache_setup == "warm-trace" ? o.warmup_passes : 0) << ",\n"
         << "  \"warmup_ns\": " << warmup_ns << ",\n"
         << "  \"observed_load_service_tier\": null,\n"
         << "  \"read_execution\": \"" << (execution == b::ReadExecution::serial ? "serial" : o.read_execution) << "\",\n"
         << "  \"latency_unit\": \"" << (!batched ? "logical request" : "whole read batch") << "\",\n"
         << "  \"requested_trace_length\": " << o.operations << ",\n  \"offered_requests\": " << requests << ",\n"
         << "  \"completed_requests\": " << (o.mode == "validate" ? 0 : requests) << ",\n"
         << "  \"requests_with_rows_or_changes\": " << nonempty << ",\n"
         << "  \"requests_without_rows_or_changes\": " << (o.mode == "validate" ? 0 : requests - nonempty) << ",\n"
         << "  \"seed\": " << o.seed << ",\n  \"input_fingerprint\": " << b::fingerprint(rows) << ",\n"
         << "  \"trace_fingerprint\": " << trace.fingerprint << ",\n  \"final_fingerprint\": " << final_hash << ",\n"
         << "  \"validated\": true,\n  \"elapsed_ns\": " << total_ns << ",\n"
         << "  \"thread_cpu_ns\": " << thread_ns << ",\n  \"drain_ns\": " << drain_ns << ",\n"
         << "  \"thread_cpu_scope\": \"calling thread; request loop, result aggregation, drain and boundary clocks\",\n"
         << "  \"process_cpu_ns\": " << (o.mode == "validate" || o.mode == "events" ? "null" : std::to_string(process_ns)) << ",\n"
         << "  \"process_cpu_scope\": \"all process threads; same request/drain interval plus PMU boundary calls; setup and validation excluded\",\n"
         << "  \"requests_per_second\": " << (total_ns ? requests * 1e9 / total_ns : 0) << ",\n"
         << "  \"p50_ns\": " << p50 << ",\n  \"p99_ns\": " << p99 << ",\n  \"timer_pair_p50_ns\": " << timer_p50 << ",\n"
         << "  \"batch_p50_ns\": " << batch_p50 << ",\n  \"batch_p99_ns\": " << batch_p99 << ",\n"
         << "  \"logical_rows_read\": " << logical_rows << ",\n  \"logical_changes\": " << changes << ",\n"
         << "  \"candidate_payload_bytes\": " << counters.payload_bytes << ",\n"
         << "  \"candidate_start_live_payload_bytes\": " << before.live_payload_bytes << ",\n"
         << "  \"candidate_start_pool_mapped_bytes\": " << before.pool_mapped_bytes << ",\n"
         << "  \"candidate_live_payload_bytes\": " << counters.live_payload_bytes << ",\n"
         << "  \"candidate_logical_live_rows\": " << counters.logical_live_rows << ",\n"
         << "  \"candidate_logical_live_field_bytes\": " << counters.logical_live_field_bytes << ",\n"
         << "  \"candidate_pool_idle_bytes\": " << counters.pool_idle_bytes << ",\n"
         << "  \"candidate_pool_allocations\": " << counters.pool_allocations << ",\n"
         << "  \"candidate_pool_reuses\": " << counters.pool_reuses << ",\n"
         << "  \"candidate_pool_growths\": " << counters.pool_growths << ",\n"
         << "  \"candidate_pool_trim_pages\": " << counters.pool_trim_pages << ",\n"
         << "  \"candidate_metadata_bytes\": " << counters.metadata_bytes << ",\n"
         << "  \"candidate_pool_mapped_bytes\": " << counters.pool_mapped_bytes << ",\n"
         << "  \"candidate_pool_reserved_bytes\": " << counters.pool_reserved_bytes << ",\n"
         << "  \"candidate_pool_metadata_bytes\": " << counters.pool_metadata_bytes << ",\n"
         << "  \"candidate_arena_charged_bytes\": " << counters.arena_charged_bytes << ",\n"
         << "  \"lmdb_file_used_bytes\": " << counters.lmdb_file_used_bytes << ",\n"
         << "  \"candidate_pages\": " << counters.allocated_pages << ",\n  \"candidate_splits\": " << counters.splits << ",\n"
         << "  \"candidate_lease_calls\": " << counters.lease_calls << ",\n"
         << "  \"candidate_issued_write_bytes\": " << counters.issued_write_bytes << ",\n"
         << "  \"candidate_issued_write_spans\": " << counters.issued_write_spans << ",\n"
         << "  \"candidate_issued_write_scope\": \"owner-accounted pack maintenance and adopted page construction; unadopted producer work requires separate counters\",\n"
         << "  \"candidate_index_write_bytes\": " << counters.index_write_bytes << ",\n"
         << "  \"candidate_index_write_spans\": " << counters.index_write_spans << ",\n"
         << "  \"lmdb_version\": \"" << b::lmdb_version() << "\",\n"
         << "  \"lmdb_flags\": [\"MDB_NOSYNC\", \"MDB_NOMETASYNC\"],\n"
         << "  \"lmdb_max_adjacent_reads_per_transaction\": " << (o.backend == "lmdb-read-batch64" ? 64 : 1) << ",\n"
         << "  \"candidate_omits\": [\"Engine\", \"Shore\", \"versioning\", \"isolation\", \"durability\"],\n"
         << "  \"candidate_access\": \"" << (o.backend.starts_with("lmdb") ? "reference backend"
                : o.backend == "packed-owner" ? "synchronous owner Allocation borrow; no escaping use"
                : o.backend == "packed-retained" ? "actual Hold retained per page for bounded trial" : "fresh actual Hold per page access") << "\",\n"
         << "  \"latency_model\": \"closed-loop single assigned caller; no queue/load generator; no timer subtraction\"\n}\n";
  if (!result) throw std::runtime_error("result write failed");
  if (o.mode == "events") {
    std::cout << o.backend << '/' << o.workload << "/events: verified " << requests
              << " requests; untimed logical replay\n";
    return 0;
  }
  std::cout << o.backend << '/' << o.workload << '/' << o.mode << ": verified "
            << requests << " requests; " << std::fixed << std::setprecision(0)
            << (total_ns ? requests * 1e9 / total_ns : 0) << " req/s, "
            << (!batched ? "request" : "whole-batch") << " p50 " << (!batched ? p50 : batch_p50)
            << " ns, p99 " << (!batched ? p99 : batch_p99) << " ns\n";
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n'; return 1;
}
