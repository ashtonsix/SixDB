#include "messaging.h"
#include "support.h"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace loom_spike {
namespace {
using namespace messaging;
struct Config {
  Variant variant{Variant::inbox};
  std::string name{"inbox"}, pattern{"sparse"};
  unsigned workers{2}, group_size{2}, skew{}, offer_batch{8}, service_budget{8}, route_budget{16};
  std::size_t capacity{64};
  std::uint64_t messages{100000}, duration_ns{20000000}, drain_ns{200000000}, sample_every{64}, repeat{};
  std::uint64_t pause_after{}, pause_ns{}, pause_worker{std::numeric_limits<std::uint64_t>::max()};
  bool padded{true}, instrumented{}, empty{}, retry{}, receiver_only{}, require_complete{}, require_all_offered{};
  std::vector<int> cpus;
};
Variant variant_of(const std::string& name) {
  if (name == "dense") return Variant::dense;
  if (name == "sparse") return Variant::sparse;
  if (name == "inbox") return Variant::inbox;
  if (name == "group-direct") return Variant::group_direct;
  if (name == "group-routed") return Variant::group_routed;
  throw std::invalid_argument("unknown messaging variant");
}
unsigned destination(const Config& c, unsigned source, std::uint64_t sequence) {
  if (c.workers == 1) return 0;
  if (c.skew && (sequence * 37 + source * 17) % 100 < c.skew) return 0;
  if (c.pattern == "fanin" || c.pattern == "many-to-one") return 0;
  if (c.pattern == "fanout") return 1 + static_cast<unsigned>(sequence % (c.workers - 1));
  if (c.pattern == "fanout-fanin") return source ? 0 : 1 + static_cast<unsigned>(sequence % (c.workers - 1));
  if (c.pattern == "pingpong" || c.pattern == "pair") return (source % 2 == 0 ? source + 1 : source - 1) % c.workers;
  if (c.pattern == "all-to-all") return (source + 1 + static_cast<unsigned>(sequence % (c.workers - 1))) % c.workers;
  if (c.pattern == "sparse") return (source + 1 + static_cast<unsigned>(sequence % 2)) % c.workers;
  throw std::invalid_argument("unknown traffic pattern");
}
std::uint64_t production(const Config& c, unsigned source) {
  if (c.empty || (c.pattern == "fanout" && source != 0)) return 0;
  if (c.receiver_only && source == 0) return 0;
  return c.messages;
}
std::uint64_t payload(std::uint64_t identity) {
  std::uint64_t x = identity + 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
std::vector<Edge> active_edges(const Config& c) {
  std::vector<Edge> edges;
  // Destinations repeat within lcm(100, workers-1, 2); inspect that cycle once.
  const auto cycle = std::lcm<std::uint64_t>(100, std::lcm<std::uint64_t>(std::max(1U, c.workers - 1), 2));
  for (unsigned source = 0; source < c.workers; ++source)
    for (std::uint64_t sequence = 0; sequence < std::min(production(c, source), cycle); ++sequence)
      edges.emplace_back(source, destination(c, source, sequence));
  // Empty polling compares the channels of the authored topology, even without packets.
  if (c.empty) {
    auto nonempty = c; nonempty.empty = false;
    return active_edges(nonempty);
  }
  std::sort(edges.begin(), edges.end()); edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  return edges;
}
struct alignas(64) WorkerStats {
  Counters channels;
  std::uint64_t offered{}, offered_attempts{}, accepted{}, received{}, errors{}, hops{}, checksum{}, cpu_ns{}, active_cpu_ns{}, batches{};
  std::uint64_t latency_dropped{};
  std::uint64_t latency_count{}, paused_count{}, actual_pause_ns{}, first_pause_offered_id{std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t first_pause_unique_offered{};
  std::atomic<bool> producer_done{};
  std::atomic<std::uint64_t> received_snapshot{};
  std::vector<std::uint64_t> latencies, last_sequence;
  std::vector<std::uint8_t> has_last;
};

std::uint64_t estimated_channel_bytes(const Config& c, std::size_t active_edges_count) {
  const std::uint64_t n = c.workers;
  const auto groups = (n + c.group_size - 1) / c.group_size;
  std::uint64_t queues{};
  if (c.variant == Variant::dense) queues = n * n;
  else if (c.variant == Variant::sparse) queues = active_edges_count;
  else if (c.variant == Variant::inbox) queues = n;
  else {
    queues = 2 * n + groups * (groups - 1);
    if (c.variant == Variant::group_direct) for (std::uint64_t start = 0; start < n; start += c.group_size) {
      const auto members = std::min<std::uint64_t>(c.group_size, n - start);
      queues += members * members;
    }
  }
  // Covers the largest current sequence cell, controls, queue/container indexes,
  // pending coordinator records and local cursors; intentionally conservative.
  return queues * (c.capacity * (sizeof(Packet) + 16) + 512) + n * 512 + groups * groups * 256;
}

Record run_case(const Config& c) {
  pin(c.cpus.front());
  const auto edges = active_edges(c);
  const auto estimated_bytes = estimated_channel_bytes(c, edges.size());
  if (estimated_bytes > 512ULL * 1024 * 1024)
    throw std::invalid_argument("conservative channel storage exceeds 512 MiB fixture limit");
  const auto identities = static_cast<std::size_t>(c.workers) * c.messages;
  std::vector<std::size_t> sample_bounds(c.workers);
  if (c.instrumented) for (unsigned source = 0; source < c.workers; ++source)
    for (std::uint64_t sequence = 0; sequence < production(c, source); sequence += c.sample_every)
      ++sample_bounds[destination(c, source, sequence)];
  const auto sample_slots = std::accumulate(sample_bounds.begin(), sample_bounds.end(), std::uint64_t{});
  const auto identity_bytes = static_cast<std::uint64_t>(identities) * 2;
  const auto stat_bytes = c.workers * (sizeof(WorkerStats) + c.workers * (sizeof(std::uint64_t) + 1));
  const auto estimated_fixture_bytes = estimated_bytes + identity_bytes + sample_slots * sizeof(std::uint64_t) * 2 + stat_bytes;
  if (estimated_fixture_bytes > 512ULL * 1024 * 1024)
    throw std::invalid_argument("conservative channel/identity/sample storage exceeds 512 MiB fixture limit");
  Channels channels(c.variant, c.workers, c.group_size, c.capacity, c.padded, edges);
  std::vector<std::uint8_t> accepted(identities), observed(identities);
  auto stats = std::make_unique<WorkerStats[]>(c.workers);
  std::uint64_t latency_storage_bytes{};
  for (unsigned worker = 0; worker < c.workers; ++worker) {
    // Value initialization touches the full reserved sample footprint outside
    // timing. Assignment during reception cannot allocate or change observers.
    stats[worker].latencies.resize(sample_bounds[worker]);
    latency_storage_bytes += stats[worker].latencies.capacity() * sizeof(std::uint64_t);
    stats[worker].last_sequence.resize(c.workers); stats[worker].has_last.resize(c.workers);
  }
  std::barrier start(static_cast<std::ptrdiff_t>(c.workers + 1));
  std::uint64_t begin{};
  std::atomic<bool> pin_failure{};
  std::vector<std::thread> workers;
  workers.reserve(c.workers);
  for (unsigned worker = 0; worker < c.workers; ++worker) workers.emplace_back([&, worker] {
    try { pin(c.cpus[worker % c.cpus.size()]); } catch (...) { pin_failure.store(true); }
    start.arrive_and_wait();
    auto& s = stats[worker];
    const auto cpu_begin = thread_cpu_ns();
    const auto stop_offer = begin + c.duration_ns, stop_drain = stop_offer + c.drain_ns;
    auto clock = now_ns();
    std::uint64_t sequence{}, visits{};
    std::uint64_t first_offer_ns{};
    bool unique_pending{};
    bool paused{};
    for (;;) {
      if ((visits++ & 63) == 0) {
        clock = now_ns();
        s.received_snapshot.store(s.received, std::memory_order_relaxed);
        if (!s.producer_done.load(std::memory_order_relaxed) &&
            (clock >= stop_offer || (!c.empty && sequence == production(c, worker))))
          s.producer_done.store(true, std::memory_order_release);
        if (clock >= stop_drain) break;
        bool all_done = true;
        std::uint64_t all_accepted{}, all_received{};
        for (unsigned other = 0; other < c.workers; ++other) {
          if (!stats[other].producer_done.load(std::memory_order_acquire)) { all_done = false; break; }
          all_accepted += stats[other].accepted;
          all_received += stats[other].received_snapshot.load(std::memory_order_relaxed);
        }
        if (all_done && all_accepted == all_received) break;
      }
      if (!paused && worker == c.pause_worker && s.offered >= c.pause_after && c.pause_ns) {
        paused = true;
        ++s.paused_count;
        s.first_pause_unique_offered = s.offered;
        if (s.offered) s.first_pause_offered_id = static_cast<std::uint64_t>(worker) * c.messages + sequence - (unique_pending ? 0 : 1);
        const auto before_pause = now_ns();
        std::this_thread::sleep_for(std::chrono::nanoseconds(c.pause_ns));
        clock = now_ns();
        s.actual_pause_ns = clock - before_pause;
      }
      const auto active_begin = c.instrumented ? thread_cpu_ns() : 0;
      const auto before_received = s.received, before_forwarded = s.channels.forwarded;
      const auto before_offered = s.offered_attempts;
      if (!s.producer_done.load(std::memory_order_relaxed) && clock < stop_offer) {
        for (unsigned count = 0; count < c.offer_batch && sequence < production(c, worker); ++count) {
          const auto id = static_cast<std::uint64_t>(worker) * c.messages + sequence;
          if (!unique_pending) {
            ++s.offered;
            first_offer_ns = c.instrumented && sequence % c.sample_every == 0 ? now_ns() : 0;
            unique_pending = true;
          }
          Packet packet{id, sequence, payload(id), first_offer_ns, worker, destination(c, worker, sequence), 0, 0};
          ++s.offered_attempts;
          const bool sent = channels.send(worker, packet, s.channels);
          if (sent) { ++s.accepted; accepted[id] = 1; }
          if (!sent && c.retry) break;
          unique_pending = false;
          ++sequence;
        }
      }
      channels.route(worker, c.route_budget, s.channels);
      for (unsigned count = 0; count < c.service_budget; ++count) {
        Packet packet;
        if (!channels.receive(worker, packet, s.channels)) break;
        ++s.received; s.hops += packet.hops; s.checksum += packet.payload;
        if (packet.source >= c.workers || packet.sequence >= c.messages || packet.destination != worker ||
            packet.identity != static_cast<std::uint64_t>(packet.source) * c.messages + packet.sequence ||
            packet.payload != payload(packet.identity) || destination(c, packet.source, packet.sequence) != worker) {
          ++s.errors; continue;
        }
        // Every valid identity has one prescribed receiver, hence one writer.
        if (observed[packet.identity] != 0) ++s.errors;
        observed[packet.identity] = 1;
        if (s.has_last[packet.source] && packet.sequence <= s.last_sequence[packet.source]) ++s.errors;
        s.has_last[packet.source] = 1; s.last_sequence[packet.source] = packet.sequence;
        if (packet.offered_ns) {
          if (s.latency_count < s.latencies.size()) s.latencies[s.latency_count++] = now_ns() - packet.offered_ns;
          else ++s.latency_dropped;
        }
      }
      const bool active = before_offered != s.offered_attempts || before_received != s.received || before_forwarded != s.channels.forwarded;
      if (active) { ++s.batches; if (c.instrumented) s.active_cpu_ns += thread_cpu_ns() - active_begin; }
      else relax();
    }
    s.cpu_ns = thread_cpu_ns() - cpu_begin;
    s.received_snapshot.store(s.received, std::memory_order_relaxed);
  });
  begin = now_ns(); start.arrive_and_wait();
  for (auto& thread : workers) thread.join();
  const auto end = now_ns();
  Record record; record.family = "messaging"; record.variant = c.name; record.unit = "messages";
  record.repeat = c.repeat; record.elapsed_ns = end - begin;
  record.parameters = {{"workers", std::to_string(c.workers)}, {"group_size", std::to_string(c.group_size)},
    {"capacity", std::to_string(c.capacity)}, {"pattern", c.pattern}, {"controls", c.padded ? "padded" : "compact"},
    {"latency_mode", !c.instrumented ? "none" : c.sample_every == 1 ? "full" : "sampled"},
    {"sample_every", std::to_string(c.sample_every)}, {"empty", c.empty ? "1" : "0"},
    {"receiver_only", c.receiver_only ? "1" : "0"},
    {"admission", c.retry ? "retry_same_identity_after_service" : "one_attempt_per_identity"},
    {"queue_first_touch_cpu", std::to_string(c.cpus.front())},
    {"control_alignment_bytes", c.padded ? "64" : std::to_string(alignof(std::atomic<std::size_t>))},
    {"padded_control_alignment_bytes", "64"},
    {"duration_ns", std::to_string(c.duration_ns)}, {"drain_ns", std::to_string(c.drain_ns)},
    {"offer_batch", std::to_string(c.offer_batch)}, {"service_budget", std::to_string(c.service_budget)},
    {"route_budget", std::to_string(c.route_budget)}, {"skew_percent", std::to_string(c.skew)},
    {"pause_worker", c.pause_worker < c.workers ? std::to_string(c.pause_worker) : "none"},
    {"pause_after_offers", std::to_string(c.pause_after)}, {"pause_ns", std::to_string(c.pause_ns)},
    {"active_cpu_definition", c.instrumented ? "nonempty_batch_including_poll_scan" : "not_measured"},
    {"identity_validation", "all_completed_packets"}, {"storage_definition", "allocated_object_and_buffer_bytes_excludes_allocator_metadata"},
    {"pair_definition", "paired_bidirectional_offers_without_echo_dependency"}};
  std::string cpu_list;
  for (unsigned i = 0; i < c.workers; ++i) { if (i) cpu_list += ','; cpu_list += std::to_string(c.cpus[i % c.cpus.size()]); }
  record.parameters["cpus"] = cpu_list;
  record.counters["channels_bytes"] = channels.storage_bytes(); record.counters["queue_count"] = channels.queue_count();
  record.counters["active_edge_count"] = edges.size();
  record.counters["conservative_channel_bytes"] = estimated_bytes;
  record.counters["conservative_fixture_bytes"] = estimated_fixture_bytes;
  record.counters["identity_storage_bytes"] = identity_bytes;
  record.counters["latency_storage_bytes"] = latency_storage_bytes;
  record.counters["latency_upper_samples"] = sample_slots;
  record.counters["sample_merge_upper_bytes"] = sample_slots * sizeof(std::uint64_t);
  record.counters["worker_state_bytes"] = stat_bytes;
  record.counters["planned_offers"] = 0;
  std::vector<std::uint64_t> samples;
  std::size_t retained_samples{};
  for (unsigned worker = 0; worker < c.workers; ++worker) retained_samples += stats[worker].latency_count;
  samples.reserve(retained_samples);
  record.counters["sample_merge_storage_bytes"] = samples.capacity() * sizeof(std::uint64_t);
  std::uint64_t offered{}, unique_offered{}, total_accepted{}, received{}, errors{}, expected_checksum{}, actual_checksum{};
  for (unsigned worker = 0; worker < c.workers; ++worker) {
    const auto& s = stats[worker];
    record.counters["planned_offers"] += production(c, worker);
    offered += s.offered_attempts; unique_offered += s.offered; total_accepted += s.accepted; received += s.received; errors += s.errors;
    actual_checksum += s.checksum;
    record.counters["cpu_ns"] += s.cpu_ns; record.counters["active_cpu_ns"] += s.active_cpu_ns;
    record.counters["nonempty_service_batches"] += s.batches; record.counters["hops"] += s.hops;
    record.counters["enqueue_probes"] += s.channels.enqueue_probes;
    record.counters["dequeue_probes"] += s.channels.dequeue_probes; record.counters["empty_polls"] += s.channels.empty_polls;
    record.counters["cas_retries"] += s.channels.cas_retries; record.counters["contention_refusals"] += s.channels.contention_refusals;
    record.counters["coordinator_forwarded"] += s.channels.forwarded; record.counters["blocked_routes"] += s.channels.blocked_routes;
    record.counters["latency_samples_dropped"] += s.latency_dropped;
    record.counters["paused_count"] += s.paused_count;
    record.counters["actual_pause_ns"] += s.actual_pause_ns;
    if (s.paused_count) {
      record.counters["first_pause_had_offer"] = s.first_pause_unique_offered != 0;
      record.counters["first_pause_unique_offered"] = s.first_pause_unique_offered;
      record.counters["first_pause_offered_id"] = s.first_pause_offered_id;
    }
    record.counters["unique_offered_source_" + std::to_string(worker)] = s.offered;
    record.counters["offered_attempts_source_" + std::to_string(worker)] = s.offered_attempts;
    if (channels.coordinator(worker)) record.counters["coordinator_cpu_ns"] += s.cpu_ns;
    samples.insert(samples.end(), s.latencies.begin(), s.latencies.begin() + s.latency_count);
  }
  std::uint64_t observed_count{}, unfinished{}, invalid_observed{};
  std::vector<std::uint64_t> source_accepted(c.workers), source_completed(c.workers);
  for (std::size_t id = 0; id < identities; ++id) {
    observed_count += observed[id];
    source_accepted[id / c.messages] += accepted[id];
    source_completed[id / c.messages] += observed[id];
    if (accepted[id] && observed[id]) expected_checksum += payload(id);
    if (accepted[id] && !observed[id]) ++unfinished;
    if (!accepted[id] && observed[id]) ++invalid_observed;
  }
  std::uint64_t active_sources{}, completed_min = std::numeric_limits<std::uint64_t>::max(), completed_max{}, source_count_errors{};
  for (unsigned source = 0; source < c.workers; ++source) {
    record.counters["accepted_source_" + std::to_string(source)] = source_accepted[source];
    record.counters["completed_source_" + std::to_string(source)] = source_completed[source];
    if (source_accepted[source] != stats[source].accepted) ++source_count_errors;
    if (production(c, source)) {
      ++active_sources;
      completed_min = std::min(completed_min, source_completed[source]);
      completed_max = std::max(completed_max, source_completed[source]);
    }
  }
  record.counters["active_producers"] = active_sources;
  record.counters["active_source_completed_min"] = active_sources ? completed_min : 0;
  record.counters["active_source_completed_max"] = completed_max;
  record.counters["source_count_errors"] = source_count_errors;
  record.items = observed_count;
  record.counters["offered"] = offered; record.counters["accepted"] = total_accepted;
  record.counters["offered_attempts"] = offered; record.counters["unique_offered"] = unique_offered;
  record.counters["unique_unaccepted"] = unique_offered - total_accepted;
  record.counters["refused"] = offered - total_accepted; record.counters["unfinished"] = unfinished;
  record.counters["completed"] = observed_count; record.counters["received"] = received;
  record.counters["planned_unique"] = record.counters["planned_offers"];
  record.counters["unoffered"] = record.counters["planned_offers"] - unique_offered;
  record.counters["validation_errors"] = errors + invalid_observed;
  record.counters["checksum"] = actual_checksum; record.counters["expected_completed_checksum"] = expected_checksum;
  record.counters["latency_samples"] = samples.size(); record.counters["active_cpu_measured"] = c.instrumented;
  record.counters["pin_failure"] = pin_failure.load();
  if (!samples.empty()) {
    std::sort(samples.begin(), samples.end());
    record.counters["offer_to_consumed_p50_ns"] = samples[(samples.size() - 1) * 50 / 100];
    record.counters["offer_to_consumed_p99_ns"] = samples[(samples.size() - 1) * 99 / 100];
    record.counters["offer_to_consumed_max_ns"] = samples.back();
  }
  record.verified = !pin_failure.load() && !errors && !invalid_observed && !source_count_errors &&
    !record.counters["latency_samples_dropped"] && observed_count == received &&
    observed_count + unfinished == total_accepted && expected_checksum == actual_checksum &&
    total_accepted <= unique_offered && unique_offered <= offered &&
    (!c.require_complete || !unfinished) && (!c.require_all_offered || unique_offered == record.counters["planned_offers"]);
  return record;
}

bool pressure_check(Variant variant, const std::string& name, std::span<const int> cpus) {
  (void)cpus;
  const Edge edges[]{{0, 1}, {1, 0}};
  Channels channels(variant, 2, 2, 2, true, edges);
  Counters sent, received;
  std::vector<std::uint64_t> accepted_ids, observed_ids;
  std::uint64_t errors{};
  for (std::uint64_t sequence = 0; sequence < 12; ++sequence) {
    Packet packet{sequence, sequence, payload(sequence), 123, 0, 1, 13, 77};
    if (channels.send(0, packet, sent)) accepted_ids.push_back(sequence);
    if (packet.identity != sequence || packet.sequence != sequence || packet.payload != payload(sequence) ||
        packet.offered_ns != 123 || packet.source != 0 || packet.destination != 1 || packet.hops != 13 || packet.reserved != 77)
      ++errors;
  }
  for (unsigned visit = 0; visit < 128; ++visit) {
    channels.route(0, 4, received); channels.route(1, 4, received);
    Packet packet;
    while (channels.receive(1, packet, received)) {
      const auto expected_hops = variant == Variant::group_routed ? 15U : 14U;
      if (packet.source != 0 || packet.destination != 1 || packet.payload != payload(packet.identity) ||
          packet.sequence != packet.identity || packet.offered_ns != 123 || packet.hops != expected_hops || packet.reserved != 77)
        ++errors;
      observed_ids.push_back(packet.identity);
    }
  }
  // Independent authored expectation: first two identities accepted; no receiver
  // ran during offers. Capacity two must reject remaining ten, with FIFO drain.
  const std::vector<std::uint64_t> expected{0, 1};
  const bool verified = !errors && accepted_ids == expected && observed_ids == expected;
  Record record; record.family = "messaging-check"; record.variant = name; record.unit = "messages";
  record.items = observed_ids.size(); record.verified = verified;
  record.parameters = {{"case", "capacity_two_no_consumer_first_twelve_offers_retained_caller_packet"}};
  record.counters = {{"offered", 12}, {"accepted", accepted_ids.size()}, {"refused", 12 - accepted_ids.size()},
    {"completed", observed_ids.size()}, {"unfinished", accepted_ids.size() - observed_ids.size()}, {"errors", errors}};
  emit(record); return verified;
}
bool routing_check(Variant variant, const std::string& name) {
  constexpr unsigned n = 5, group_size = 3;
  std::vector<Edge> edges;
  for (unsigned source = 0; source < n; ++source) for (unsigned target = 0; target < n; ++target) edges.emplace_back(source, target);
  Channels channels(variant, n, group_size, 2, true, edges);
  Counters counters;
  std::vector<unsigned> seen(n * 7);
  std::uint64_t completed{}, errors{}, hops{};
  for (std::uint64_t sequence = 0; sequence < 7; ++sequence) for (unsigned source = 0; source < n; ++source) {
    const auto target = static_cast<unsigned>((source + sequence + 1) % n);
    const auto id = sequence * n + source;
    Packet packet{id, sequence, payload(id), 0, source, target, 0, 0};
    if (!channels.send(source, packet, counters)) ++errors;
    const auto before = completed;
    for (unsigned visit = 0; visit < 128 && completed == before; ++visit) {
      for (unsigned worker = 0; worker < n; ++worker) channels.route(worker, 16, counters);
      for (unsigned worker = 0; worker < n; ++worker) {
        Packet incoming;
        while (channels.receive(worker, incoming, counters)) {
          ++completed; hops += incoming.hops;
          const auto expected_hops = variant == Variant::group_routed ? (source / group_size == target / group_size ? 2U : 3U)
            : variant == Variant::group_direct && source / group_size != target / group_size ? 3U : 1U;
          if (incoming.identity != id || incoming.source != source || incoming.destination != target || worker != target ||
              incoming.sequence != sequence || incoming.payload != payload(id) || incoming.hops != expected_hops) ++errors;
          if (incoming.identity >= seen.size()) ++errors;
          else ++seen[incoming.identity];
        }
      }
    }
    if (completed != before + 1) ++errors;
  }
  const bool verified = !errors && completed == n * 7 && std::all_of(seen.begin(), seen.end(), [](unsigned count) { return count == 1; });
  Record record; record.family = "messaging-check"; record.variant = name; record.unit = "messages";
  record.items = completed; record.verified = verified;
  record.parameters = {{"case", "independent_identity_destination_hops_partial_final_group_wraparound"}, {"workers", "5"}, {"group_size", "3"}};
  record.counters = {{"expected_messages", n * 7}, {"completed", completed}, {"errors", errors}, {"hops", hops}};
  emit(record); return verified;
}
} // namespace
} // namespace loom_spike

int main(int argc, char** argv) {
  using namespace loom_spike;
  try {
    Options options(argc, argv);
    Config config; config.cpus = options.cpus();
    if (config.cpus.empty()) throw std::invalid_argument("no available CPUs");
    config.name = options.get("variant", "inbox"); config.variant = variant_of(config.name);
    config.workers = static_cast<unsigned>(options.number("workers", std::min<std::size_t>(4, config.cpus.size())));
    config.group_size = static_cast<unsigned>(options.number("group-size", 2)); config.capacity = options.number("capacity", 64);
    config.pattern = options.get("pattern", "sparse"); config.messages = options.number("messages", 100000);
    config.duration_ns = options.number("duration-ms", 20) * 1000000; config.drain_ns = options.number("drain-ms", 200) * 1000000;
    config.repeat = options.number("repeat", 0); config.offer_batch = static_cast<unsigned>(options.number("offer-batch", 8));
    config.service_budget = static_cast<unsigned>(options.number("service-budget", 8));
    config.route_budget = static_cast<unsigned>(options.number("route-budget", 16));
    config.skew = static_cast<unsigned>(options.number("skew", 0)); config.padded = !options.has("compact");
    config.instrumented = options.has("instrumented"); config.sample_every = options.number("sample-every", 64);
    config.empty = options.has("empty"); config.pause_worker = options.number("pause-worker", config.pause_worker);
    config.retry = options.has("retry");
    config.receiver_only = options.has("receiver-only");
    config.pause_after = options.number("pause-after", 0); config.pause_ns = options.number("pause-us", 0) * 1000;
    if (!config.workers || config.workers > 256 || !config.group_size || config.group_size > 256 || config.capacity < 2 ||
        config.capacity > 65536 || config.messages > 10000000 || config.messages * config.workers > 20000000 ||
        !config.offer_batch || !config.service_budget || !config.route_budget || !config.sample_every || config.skew > 100 ||
        config.duration_ns > 10000000000ULL || config.drain_ns > 10000000000ULL || config.pause_ns > 1000000000ULL)
      throw std::invalid_argument("invalid/beyond bounded fixture limits");
    (void)destination(config, 0, 0);
    if (config.receiver_only && config.pattern != "fanin" && config.pattern != "many-to-one")
      throw std::invalid_argument("--receiver-only requires fanin/many-to-one");
    if (options.has("check")) {
      bool all = check_slot_indexing();
      Record indexing; indexing.family = "messaging-check"; indexing.variant = "slot-index"; indexing.unit = "case";
      indexing.verified = all; indexing.parameters["case"] = "hardware_modulo_oracle_fixed_large_positions_and_unsigned_wrap";
      indexing.counters["capacities"] = 9; emit(indexing);
      for (const auto* name : {"dense", "sparse", "inbox", "group-direct", "group-routed"}) {
        all &= pressure_check(variant_of(name), name, config.cpus);
        all &= routing_check(variant_of(name), name);
        for (const bool padded : {true, false}) for (const auto* pattern : {"fanin", "fanout-fanin", "all-to-all"}) {
          auto check = config; check.name = name; check.variant = variant_of(name); check.workers = 4; check.group_size = 2;
          check.messages = 257; check.duration_ns = 500000000; check.drain_ns = 500000000;
          check.capacity = 2; check.offer_batch = 8; check.padded = padded; check.pattern = pattern;
          check.instrumented = true; check.sample_every = 1; check.pause_worker = 0; check.pause_after = 16; check.pause_ns = 1000000;
          check.require_complete = true; check.require_all_offered = true; check.empty = false; check.retry = false; check.receiver_only = false;
          auto record = run_case(check); all &= record.verified; emit(record);
        }
        auto retry = config; retry.name = name; retry.variant = variant_of(name); retry.workers = 4; retry.group_size = 2;
        retry.messages = 257; retry.duration_ns = 500000000; retry.drain_ns = 500000000;
        retry.capacity = 2; retry.offer_batch = 8; retry.pattern = "fanin"; retry.retry = true;
        retry.instrumented = true; retry.sample_every = 1; retry.pause_worker = 0; retry.pause_after = 16; retry.pause_ns = 1000000;
        retry.require_complete = true; retry.require_all_offered = true; retry.empty = false; retry.receiver_only = false;
        auto record = run_case(retry);
        record.verified = record.verified && record.counters["accepted"] == record.counters["planned_unique"] &&
          !record.counters["unique_unaccepted"] && !record.counters["unoffered"];
        all &= record.verified; emit(record);
        retry.receiver_only = true; retry.pause_after = 0; retry.sample_every = 64;
        record = run_case(retry);
        record.verified = record.verified && record.counters["accepted"] == 771 && record.counters["unique_offered"] == 771 &&
          record.counters["paused_count"] == 1 && record.counters["actual_pause_ns"] >= retry.pause_ns &&
          !record.counters["first_pause_had_offer"] && !record.counters["accepted_source_0"] &&
          record.counters["latency_samples"] == 15 && !record.counters["latency_samples_dropped"];
        all &= record.verified; emit(record);
        retry.capacity = 3;
        record = run_case(retry);
        record.verified = record.verified && record.counters["accepted"] == 771 &&
          !record.counters["unique_unaccepted"] && !record.counters["unfinished"];
        all &= record.verified; emit(record);
      }
      // Exceeds the old 16,384-sample-per-receiver prefix. Every admitted
      // identity completes, so the full and sampled expected sizes are known.
      for (const std::uint64_t period : {1ULL, 64ULL}) {
        auto observer = config; observer.name = "inbox"; observer.variant = Variant::inbox;
        observer.workers = 2; observer.messages = 20000; observer.pattern = "pair"; observer.empty = false;
        observer.retry = true; observer.receiver_only = false; observer.capacity = 64; observer.instrumented = true;
        observer.sample_every = period; observer.duration_ns = 1000000000; observer.drain_ns = 500000000;
        observer.pause_ns = 0; observer.require_complete = true; observer.require_all_offered = true;
        auto record = run_case(observer);
        record.verified = record.verified && record.counters["accepted"] == 40000 &&
          record.counters["latency_samples"] == 2 * ((observer.messages + period - 1) / period) &&
          !record.counters["latency_samples_dropped"];
        all &= record.verified; emit(record);
      }
      return all ? 0 : 1;
    }
    auto record = run_case(config); emit(record); return record.verified ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "loom_messaging: " << error.what() << '\n'; return 2;
  }
}
