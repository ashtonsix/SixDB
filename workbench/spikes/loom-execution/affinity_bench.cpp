#include "support.h"
#include "messaging.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#endif

namespace {
using loom_spike::Options;
using loom_spike::Record;
constexpr std::uint64_t salt = 0x9e3779b97f4a7c15ULL;
constexpr std::uint32_t empty = std::numeric_limits<std::uint32_t>::max();

std::uint64_t mix(std::uint64_t x) noexcept {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

int actual_cpu() noexcept {
#if defined(__linux__)
  return sched_getcpu();
#else
  return -1;
#endif
}

std::string cpu_list(const std::vector<int>& cpus) {
  std::string out;
  for (int cpu : cpus) {
    if (!out.empty()) out += ',';
    out += std::to_string(cpu);
  }
  return out;
}

std::uint64_t percentile(std::vector<std::uint64_t> values, unsigned pct) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  return values[(values.size() - 1) * pct / 100];
}

struct alignas(64) LockStats {
  std::uint64_t acquisitions{};
  std::uint64_t failed_polls{};
  std::uint64_t failed_cas{};
  std::uint64_t useful_items{};
  std::uint64_t useful_sum{};
  std::uint64_t cpu_ns{};
  std::uint64_t pause_ns{};
  std::uint64_t sample_count{};
  int cpu_begin{-1};
  int cpu_end{-1};
  std::vector<std::uint64_t> waits;
};

struct alignas(64) QueueNode {
  std::atomic<QueueNode*> next{nullptr};
  std::atomic<bool> waiting{false};
};

enum class LockKind { ttas, backoff, mcs, useful };

class Lock {
 public:
  explicit Lock(const std::string& variant) : kind_(
    variant == "mcs" ? LockKind::mcs : variant == "backoff" ? LockKind::backoff :
    variant == "useful" ? LockKind::useful : LockKind::ttas) {}

  void acquire(QueueNode& node, LockStats& stats,
               const std::array<std::uint64_t, 1024>& independent) noexcept {
    if (kind_ == LockKind::mcs) {
      node.next.store(nullptr, std::memory_order_relaxed);
      node.waiting.store(true, std::memory_order_relaxed);
      auto* predecessor = tail_.exchange(&node, std::memory_order_acq_rel);
      if (!predecessor) return;
      predecessor->next.store(&node, std::memory_order_release);
      while (node.waiting.load(std::memory_order_acquire)) {
        ++stats.failed_polls;
        loom_spike::relax();
      }
      return;
    }
    unsigned backoff = 1;
    for (;;) {
      while (held_.load(std::memory_order_relaxed)) {
        ++stats.failed_polls;
        if (kind_ == LockKind::useful) {
          // Independent admitted work has no access to the protected counter.
          for (unsigned i = 0; i != 8; ++i) {
            stats.useful_sum += independent[stats.useful_items & 1023];
            ++stats.useful_items;
          }
        } else if (kind_ == LockKind::backoff) {
          for (unsigned i = 0; i != backoff; ++i) loom_spike::relax();
          backoff = std::min(256U, backoff * 2);
        } else {
          loom_spike::relax();
        }
      }
      bool expected = false;
      if (held_.compare_exchange_weak(expected, true, std::memory_order_acquire,
                                     std::memory_order_relaxed)) return;
      ++stats.failed_cas;
    }
  }

  void release(QueueNode& node, LockStats& stats) noexcept {
    if (kind_ != LockKind::mcs) {
      held_.store(false, std::memory_order_release);
      return;
    }
    auto* successor = node.next.load(std::memory_order_acquire);
    if (!successor) {
      auto* expected = &node;
      if (tail_.compare_exchange_strong(expected, nullptr,
                                        std::memory_order_release,
                                        std::memory_order_relaxed)) return;
      do {
        ++stats.failed_polls;
        loom_spike::relax();
        successor = node.next.load(std::memory_order_acquire);
      } while (!successor);
    }
    successor->waiting.store(false, std::memory_order_release);
  }

 private:
  LockKind kind_;
  alignas(64) std::atomic<bool> held_{false};
  alignas(64) std::atomic<QueueNode*> tail_{nullptr};
};

bool lock_case(const Options& options, const std::string& variant,
               const std::vector<int>& cpus, std::uint64_t repeat, bool check) {
  if (cpus.empty()) throw std::runtime_error("no CPUs available");
  const auto duration_ms = check ? 0 : options.number("duration-ms", 100);
  const auto max_ops = check ? 256 : options.number("max-ops", 500000);
  const auto pause_us = options.number("holder-pause-us", check ? 50 : 0);
  const auto critical_work = options.number("critical-work", 8);
  if (!max_ops || max_ops > 100000000 || duration_ms > 60000 || pause_us > 1000000 ||
      critical_work > 100000)
    throw std::runtime_error("lock bounds exceeded");
  Lock lock(variant);
  auto stats = std::make_unique<LockStats[]>(cpus.size());
  auto nodes = std::make_unique<QueueNode[]>(cpus.size());
  std::array<std::uint64_t, 1024> independent{};
  for (std::size_t i = 0; i != independent.size(); ++i) independent[i] = mix(i + 7);
  for (std::size_t i = 0; i != cpus.size(); ++i) stats[i].waits.resize((max_ops + 63) / 64);
  std::atomic<std::uint64_t> ready{0};
  std::atomic<bool> go{false};
  std::atomic<bool> failed{false};
  std::uint64_t deadline = 0;
  std::uint64_t protected_count = 0;
  std::uint64_t protected_sum = 0;
  std::uint64_t protected_work = 1;
  std::vector<std::thread> threads;
  for (std::size_t i = 0; i != cpus.size(); ++i) {
    threads.emplace_back([&, i] {
      try { loom_spike::pin(cpus[i]); }
      catch (...) { failed.store(true, std::memory_order_relaxed); }
      stats[i].cpu_begin = actual_cpu();
      // The paused-holder control starts with the holder already owning the
      // lock, so competing writers cannot finish before its first hold.
      const bool initially_held = i == 0 && pause_us;
      if (initially_held) lock.acquire(nodes[i], stats[i], independent);
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) loom_spike::relax();
      const auto cpu_start = loom_spike::thread_cpu_ns();
      for (std::uint64_t operation = 0; operation != max_ops; ++operation) {
        if (duration_ms && operation && (operation & 63) == 0 &&
            loom_spike::now_ns() >= deadline) break;
        const bool sample = (operation & 63) == 0;
        const auto before = sample ? loom_spike::now_ns() : 0;
        if (!initially_held || operation != 0) lock.acquire(nodes[i], stats[i], independent);
        if (sample) stats[i].waits[stats[i].sample_count++] = loom_spike::now_ns() - before;
        ++protected_count;
        protected_sum += mix(i + 1);
        for (std::uint64_t j = 0; j != critical_work; ++j)
          protected_work = std::rotl(protected_work, 5) ^ salt;
        if (i == 0 && operation == 0 && pause_us) {
          const auto pause_start = loom_spike::now_ns();
          std::this_thread::sleep_for(std::chrono::microseconds(pause_us));
          stats[i].pause_ns = loom_spike::now_ns() - pause_start;
        }
        lock.release(nodes[i], stats[i]);
        ++stats[i].acquisitions;
      }
      stats[i].cpu_ns = loom_spike::thread_cpu_ns() - cpu_start;
      stats[i].cpu_end = actual_cpu();
    });
  }
  while (ready.load(std::memory_order_acquire) != cpus.size()) loom_spike::relax();
  const auto start = loom_spike::now_ns();
  deadline = start + duration_ms * 1000000;
  go.store(true, std::memory_order_release);
  for (auto& thread : threads) thread.join();
  const auto elapsed = loom_spike::now_ns() - start;
  std::uint64_t expected_count = 0, expected_sum = 0, polls = 0, cas = 0;
  std::uint64_t useful_items = 0, useful_sum = 0, cpu_ns = 0;
  std::uint64_t min_ops = max_ops, max_acquisitions = 0;
  bool verified = !failed.load(std::memory_order_relaxed);
  std::vector<std::uint64_t> waits;
  Record record;
  record.family = "locks";
  record.variant = variant;
  record.unit = "acquisition";
  record.repeat = repeat;
  record.elapsed_ns = elapsed;
  record.parameters = {{"requested_cpus", cpu_list(cpus)},
    {"duration_ms", std::to_string(duration_ms)}, {"max_ops_per_thread", std::to_string(max_ops)},
    {"holder_pause_us", std::to_string(pause_us)}, {"critical_work", std::to_string(critical_work)},
    {"wait_sample_stride", "64"}, {"check", check ? "1" : "0"}};
  for (std::size_t i = 0; i != cpus.size(); ++i) {
    auto& s = stats[i];
    expected_count += s.acquisitions;
    expected_sum += s.acquisitions * mix(i + 1);
    polls += s.failed_polls; cas += s.failed_cas;
    useful_items += s.useful_items; useful_sum += s.useful_sum; cpu_ns += s.cpu_ns;
    min_ops = std::min(min_ops, s.acquisitions);
    max_acquisitions = std::max(max_acquisitions, s.acquisitions);
    const auto cycle_sum = std::accumulate(independent.begin(), independent.end(), std::uint64_t{0});
    auto expected_useful = (s.useful_items / 1024) * cycle_sum;
    for (std::uint64_t j = 0; j != s.useful_items % 1024; ++j) expected_useful += independent[j];
    verified &= s.useful_sum == expected_useful && s.acquisitions != 0;
    if (check) verified &= s.acquisitions == max_ops;
    verified &= s.cpu_begin == cpus[i] && s.cpu_end == cpus[i];
    waits.insert(waits.end(), s.waits.begin(), s.waits.begin() + s.sample_count);
    record.parameters["actual_cpu_" + std::to_string(i)] =
      std::to_string(s.cpu_begin) + ":" + std::to_string(s.cpu_end);
    record.counters["acquisitions_" + std::to_string(i)] = s.acquisitions;
    record.counters["cpu_ns_" + std::to_string(i)] = s.cpu_ns;
    record.counters["sample_wait_max_ns_" + std::to_string(i)] = s.sample_count ?
      *std::max_element(s.waits.begin(), s.waits.begin() + s.sample_count) : 0;
    record.counters["wait_samples_" + std::to_string(i)] = s.sample_count;
    record.counters["failed_polls_" + std::to_string(i)] = s.failed_polls;
    record.counters["useful_items_" + std::to_string(i)] = s.useful_items;
  }
  verified &= protected_count == expected_count && protected_sum == expected_sum;
  if (pause_us) verified &= stats[0].pause_ns >= pause_us * 1000;
  record.items = expected_count;
  record.verified = verified;
  record.counters.insert({{"protected_count", protected_count}, {"protected_sum", protected_sum},
    {"critical_work_result", protected_work}, {"failed_polls", polls}, {"failed_cas", cas},
    {"useful_items", useful_items}, {"useful_sum", useful_sum}, {"cpu_ns", cpu_ns},
    {"min_thread_acquisitions", min_ops}, {"max_thread_acquisitions", max_acquisitions},
    {"holder_pause_actual_ns", stats[0].pause_ns},
    {"holder_post_pause_acquisitions", stats[0].acquisitions ? stats[0].acquisitions - 1 : 0},
    {"wait_samples", waits.size()}, {"sample_wait_p50_ns", percentile(waits, 50)},
    {"sample_wait_p99_ns", percentile(waits, 99)}, {"sample_wait_max_ns", percentile(waits, 100)}});
  loom_spike::emit(record);
  return verified;
}

bool owner_lock_case(const Options& options, const std::vector<int>& cpus,
                     std::uint64_t repeat, bool check) {
  namespace messaging = loom_spike::messaging;
  const int owner_cpu = cpus.front();
  const std::vector<int> clients = cpus.size() > 1 ?
    std::vector<int>(cpus.begin() + 1, cpus.end()) : cpus;
  const auto duration_ms = check ? 0 : options.number("duration-ms", 100);
  const auto max_ops = check ? 256 : options.number("max-ops", 500000);
  const auto pause_us = options.number("holder-pause-us", check ? 50 : 0);
  const auto critical_work = options.number("critical-work", 8);
  const auto capacity = options.number("route-cap", 4);
  const auto timeout_ms = options.number("route-timeout-ms", 5000);
  if (!max_ops || max_ops > 100000000 || duration_ms > 60000 || pause_us > 1000000 ||
      critical_work > 100000 || !capacity || capacity > 1048576 || !timeout_ms || timeout_ms > 60000)
    throw std::runtime_error("invalid owner lock bounds");
  std::vector<messaging::Edge> edges;
  for (unsigned i = 1; i <= clients.size(); ++i) {
    edges.emplace_back(i, 0); edges.emplace_back(0, i);
  }
  // Single shared bounded inbox exercises many writers without a lock; one
  // owner performs the same protected operation and acknowledges each result.
  messaging::Channels channels(messaging::Variant::inbox, clients.size() + 1,
    1, capacity, true, edges);
  auto stats = std::make_unique<LockStats[]>(clients.size());
  std::vector<messaging::Counters> channels_stats(clients.size() + 1);
  std::vector<std::uint64_t> next_sequence(clients.size(), 0);
  for (std::size_t i = 0; i != clients.size(); ++i) stats[i].waits.resize((max_ops + 63) / 64);
  std::atomic<std::uint64_t> ready{0}, finished{0};
  std::atomic<bool> go{false}, abort{false}, failed{false};
  std::uint64_t deadline = 0, drain_deadline = 0;
  std::uint64_t protected_count = 0, protected_sum = 0, protected_work = 1;
  std::uint64_t owner_cpu_ns = 0, owner_pause_ns = 0;
  int owner_actual_begin = -1, owner_actual_end = -1;
  std::vector<std::thread> threads;
  threads.emplace_back([&] {
    try { loom_spike::pin(owner_cpu); }
    catch (...) { failed.store(true); }
    owner_actual_begin = actual_cpu();
    ready.fetch_add(1, std::memory_order_release);
    while (!go.load(std::memory_order_acquire)) loom_spike::relax();
    const auto cpu_start = loom_spike::thread_cpu_ns();
    std::uint64_t turns = 0;
    while (finished.load(std::memory_order_acquire) != clients.size() && !abort.load()) {
      if ((++turns & 1023) == 0 && loom_spike::now_ns() > drain_deadline) {
        abort.store(true); break;
      }
      messaging::Packet request;
      if (!channels.receive(0, request, channels_stats[0])) { loom_spike::relax(); continue; }
      if (!request.source || request.source > clients.size() || request.destination != 0 ||
          request.identity != request.source || request.sequence != next_sequence[request.source - 1]++) {
        abort.store(true); break;
      }
      ++protected_count;
      protected_sum += mix(request.source);
      for (std::uint64_t j = 0; j != critical_work; ++j)
        protected_work = std::rotl(protected_work, 5) ^ salt;
      if (protected_count == 1 && pause_us) {
        const auto before = loom_spike::now_ns();
        std::this_thread::sleep_for(std::chrono::microseconds(pause_us));
        owner_pause_ns = loom_spike::now_ns() - before;
      }
      const messaging::Packet ack{request.identity, request.sequence, protected_count, 0,
        0, request.source, 0, 0};
      bool sent = false;
      while (!abort.load() && !(sent = channels.send(0, ack, channels_stats[0]))) {
        if ((++turns & 1023) == 0 && loom_spike::now_ns() > drain_deadline) abort.store(true);
        loom_spike::relax();
      }
      if (!sent) break;
    }
    owner_cpu_ns = loom_spike::thread_cpu_ns() - cpu_start;
    owner_actual_end = actual_cpu();
  });
  for (std::size_t client = 0; client != clients.size(); ++client) {
    threads.emplace_back([&, client] {
      try { loom_spike::pin(clients[client]); }
      catch (...) { failed.store(true); }
      auto& s = stats[client];
      auto& counters = channels_stats[client + 1];
      s.cpu_begin = actual_cpu();
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) loom_spike::relax();
      const auto cpu_start = loom_spike::thread_cpu_ns();
      std::uint64_t turns = 0;
      auto live = [&] {
        if (abort.load()) return false;
        if ((++turns & 1023) == 0 && loom_spike::now_ns() > drain_deadline) {
          abort.store(true); return false;
        }
        return true;
      };
      for (std::uint64_t operation = 0; operation != max_ops && live(); ++operation) {
        if (duration_ms && operation && (operation & 63) == 0 && loom_spike::now_ns() >= deadline) break;
        const bool sample = (operation & 63) == 0;
        const auto before = sample ? loom_spike::now_ns() : 0;
        const unsigned identity = client + 1;
        const messaging::Packet request{identity, operation, 0, 0, identity, 0, 0, 0};
        bool accepted = false;
        while (live() && !(accepted = channels.send(identity, request, counters))) {
          ++s.failed_cas; loom_spike::relax();
        }
        if (!accepted) break;
        bool acknowledged = false;
        messaging::Packet ack;
        while (live() && !acknowledged) {
          if (channels.receive(identity, ack, counters)) {
            acknowledged = ack.source == 0 && ack.destination == identity &&
              ack.identity == identity && ack.sequence == operation;
            if (!acknowledged) abort.store(true);
          } else { ++s.failed_polls; loom_spike::relax(); }
        }
        if (!acknowledged) break;
        if (sample) s.waits[s.sample_count++] = loom_spike::now_ns() - before;
        ++s.acquisitions;
      }
      s.cpu_ns = loom_spike::thread_cpu_ns() - cpu_start;
      s.cpu_end = actual_cpu();
      finished.fetch_add(1, std::memory_order_release);
    });
  }
  while (ready.load(std::memory_order_acquire) != clients.size() + 1) loom_spike::relax();
  const auto start = loom_spike::now_ns();
  deadline = start + duration_ms * 1000000;
  drain_deadline = deadline + timeout_ms * 1000000;
  go.store(true, std::memory_order_release);
  for (auto& thread : threads) thread.join();
  const auto elapsed = loom_spike::now_ns() - start;
  Record record;
  record.family = "locks"; record.variant = "owner"; record.unit = "acknowledged_operation";
  record.repeat = repeat; record.elapsed_ns = elapsed;
  record.parameters = {{"requested_cpus", cpu_list(cpus)}, {"writer_cpus", cpu_list(clients)},
    {"owner_cpu", std::to_string(owner_cpu)}, {"actual_owner_cpu", std::to_string(owner_actual_begin) + ":" + std::to_string(owner_actual_end)},
    {"duration_ms", std::to_string(duration_ms)}, {"max_ops_per_thread", std::to_string(max_ops)},
    {"holder_pause_us", std::to_string(pause_us)}, {"critical_work", std::to_string(critical_work)},
    {"route_cap", std::to_string(capacity)}, {"route_timeout_ms", std::to_string(timeout_ms)},
    {"route_variant", "inbox"}, {"wait_sample_stride", "64"}, {"check", check ? "1" : "0"}};
  std::uint64_t expected_count = 0, expected_sum = 0, cpu_ns = owner_cpu_ns, polls = 0, refusals = 0;
  std::vector<std::uint64_t> waits;
  bool verified = !failed.load() && !abort.load() && owner_actual_begin == owner_cpu && owner_actual_end == owner_cpu;
  for (std::size_t i = 0; i != clients.size(); ++i) {
    const auto& s = stats[i];
    expected_count += s.acquisitions; expected_sum += s.acquisitions * mix(i + 1);
    cpu_ns += s.cpu_ns; polls += s.failed_polls; refusals += s.failed_cas;
    verified &= s.acquisitions != 0 && s.cpu_begin == clients[i] && s.cpu_end == clients[i];
    if (check) verified &= s.acquisitions == max_ops;
    waits.insert(waits.end(), s.waits.begin(), s.waits.begin() + s.sample_count);
    record.parameters["actual_cpu_" + std::to_string(i)] = std::to_string(s.cpu_begin) + ":" + std::to_string(s.cpu_end);
    record.counters["acquisitions_" + std::to_string(i)] = s.acquisitions;
    record.counters["cpu_ns_" + std::to_string(i)] = s.cpu_ns;
    record.counters["sample_wait_max_ns_" + std::to_string(i)] = s.sample_count ?
      *std::max_element(s.waits.begin(), s.waits.begin() + s.sample_count) : 0;
    record.counters["wait_samples_" + std::to_string(i)] = s.sample_count;
    record.counters["failed_polls_" + std::to_string(i)] = s.failed_polls;
    record.counters["send_refusals_" + std::to_string(i)] = s.failed_cas;
  }
  verified &= expected_count == protected_count && expected_sum == protected_sum;
  if (pause_us) verified &= owner_pause_ns >= pause_us * 1000;
  record.items = expected_count; record.verified = verified;
  record.counters.insert({{"protected_count", protected_count}, {"protected_sum", protected_sum},
    {"critical_work_result", protected_work}, {"failed_polls", polls}, {"send_refusals", refusals},
    {"cpu_ns", cpu_ns}, {"owner_cpu_ns", owner_cpu_ns}, {"holder_pause_actual_ns", owner_pause_ns},
    {"holder_post_pause_acquisitions", protected_count ? protected_count - 1 : 0},
    {"useful_items", 0}, {"wait_samples", waits.size()}, {"sample_wait_p50_ns", percentile(waits, 50)},
    {"sample_wait_p99_ns", percentile(waits, 99)}, {"sample_wait_max_ns", percentile(waits, 100)},
    {"route_queue_storage_bytes", channels.storage_bytes()}});
  loom_spike::emit(record);
  return verified;
}

struct BuildRow { std::uint64_t key; std::uint64_t payload; std::uint32_t ordinal; };
struct ProbeRow { std::uint64_t key; std::uint64_t ordinal; };
struct OutputRow { std::uint64_t probe; std::uint64_t build; std::uint64_t payload; };

struct Table {
  std::vector<BuildRow> rows;
  std::vector<std::uint32_t> head;
  std::vector<std::uint32_t> next;
  std::size_t mask{};

  void make(std::uint64_t count, std::uint64_t duplicates, std::uint64_t seed) {
    std::size_t buckets = 2;
    while (buckets < count * 2) buckets *= 2;
    mask = buckets - 1;
    head.assign(buckets, empty);
    rows.resize(count); next.resize(count);
    for (std::uint32_t i = 0; i != count; ++i) {
      const auto key = i / duplicates;
      rows[i] = {key, mix(seed + i), i};
      auto bucket = mix(key) & mask;
      while (head[bucket] != empty && rows[head[bucket]].key != key)
        bucket = (bucket + 1) & mask;
      next[i] = head[bucket];
      head[bucket] = i;
    }
  }

  std::uint32_t find(std::uint64_t key) const noexcept {
    auto bucket = mix(key) & mask;
    while (head[bucket] != empty) {
      const auto row = head[bucket];
      if (rows[row].key == key) return row;
      bucket = (bucket + 1) & mask;
    }
    return empty;
  }

  std::uint64_t bytes() const noexcept {
    return rows.size() * sizeof(BuildRow) + head.size() * sizeof(std::uint32_t) +
      next.size() * sizeof(std::uint32_t);
  }
};

struct alignas(64) JoinResult {
  std::uint64_t count{};
  std::uint64_t hash{};
  std::uint64_t pages{};
  std::uint64_t covered{};
  std::uint64_t coverage_sum{};
  std::uint64_t cpu_ns{};
  int cpu_begin{-1};
  int cpu_end{-1};
  std::vector<OutputRow> page;
};

// Numeric observations survive resetting the reusable output pages. No output
// page is copied or reallocated between uses, and verification follows all uses.
struct JoinObservation {
  std::uint64_t count{}, hash{}, pages{}, covered{}, coverage_sum{}, cpu_ns{};
  int cpu_begin{-1}, cpu_end{-1};
};

struct JoinUse {
  std::vector<JoinObservation> partitions;
  std::vector<std::pair<int, int>> routing_cpus;
  std::uint64_t native_ns{}, worker_wall_ns{}, routing_cpu_ns{}, routing_refusals{};
  std::uint64_t enqueue_probes{}, dequeue_probes{}, forwarded{}, cas_retries{}, empty_polls{};
  std::uint64_t routed_acknowledgements{};
  bool routes_settled{true};
};

std::uint64_t output_hash(const OutputRow& row) noexcept {
  return mix(row.probe + salt) ^ mix(row.build * 17 + row.payload);
}

void probe_partition(const Table& table, const std::vector<ProbeRow>& probes,
                     std::size_t begin, std::size_t end, JoinResult& result) noexcept {
  std::size_t used = 0;
  auto drain = [&] {
    for (std::size_t i = 0; i != used; ++i) {
      result.hash += output_hash(result.page[i]);
      ++result.count;
    }
    if (used) ++result.pages;
    used = 0;
  };
  for (std::size_t p = begin; p != end; ++p) {
    ++result.covered;
    result.coverage_sum += p;
    for (auto row = table.find(probes[p].key); row != empty; row = table.next[row]) {
      result.page[used++] = {probes[p].ordinal, table.rows[row].ordinal, table.rows[row].payload};
      if (used == result.page.size()) drain();
    }
  }
  drain();
}

bool join_case(const Options& options, const std::string& variant,
               const std::vector<int>& cpus, std::uint64_t repeat, bool check,
               const std::string& distribution) {
  if (cpus.empty()) throw std::runtime_error("no CPUs available");
  const auto build_count = check ? 257 : options.number("build-rows", 262144);
  const auto probe_count = check ? 1009 : options.number("probe-rows", 524288);
  const auto duplicates = options.number("duplicates", check ? 4 : 4);
  const auto output_cap = options.number("output-cap", check ? 7 : 256);
  const auto uses = options.number("uses", check ? 3 : 1);
  const auto seed = options.number("seed", 1234);
  const auto max_output_rows = options.number("max-output-rows", 100000000);
  const bool at_owner = variant == "owner" || variant == "owner-direct";
  const bool routed = variant == "owner";
  if (!build_count || build_count > 10000000 || !probe_count || probe_count > 100000000 ||
      !duplicates || duplicates > build_count || !output_cap || output_cap > 1048576 ||
      !uses || uses > 16)
    throw std::runtime_error("join bounds exceeded");
  const int owner_cpu = cpus.front();
  std::vector<int> workers = cpus.size() > 1 ? std::vector<int>(cpus.begin() + 1, cpus.end()) : cpus;
  if (probe_count * duplicates * uses > max_output_rows)
    throw std::runtime_error("potential output exceeds --max-output-rows work bound");
  const auto memory_cap_mib = options.number("memory-cap-mib", 1024);
  const auto copy_count = variant == "replicate" ? workers.size() : variant == "copy" ? 1 : 0;
  const auto bounded_bytes = build_count * 48 * (copy_count + 1) +
    probe_count * sizeof(ProbeRow) + output_cap * workers.size() * sizeof(OutputRow) +
    build_count * 16;
  if (!memory_cap_mib || memory_cap_mib > 1048576 ||
      bounded_bytes > memory_cap_mib * 1024 * 1024)
    throw std::runtime_error("join working storage exceeds --memory-cap-mib");
  const auto fixture_start = loom_spike::now_ns();
  Table original;
  std::vector<ProbeRow> probes;
  std::uint64_t build_ns = 0, input_ns = 0;
  int build_actual = -1, input_actual = -1;
  std::atomic<bool> failed{false};
  std::thread build_thread([&] {
    try {
      loom_spike::pin(owner_cpu);
      build_actual = actual_cpu();
      const auto start = loom_spike::now_ns();
      original.make(build_count, duplicates, seed);
      build_ns = loom_spike::now_ns() - start;
    } catch (...) { failed.store(true, std::memory_order_relaxed); }
  });
  build_thread.join();
  if (failed.load()) throw std::runtime_error("build owner setup failed");
  const auto key_count = (build_count + duplicates - 1) / duplicates;
  std::thread input_thread([&] {
    try {
      loom_spike::pin(workers.front());
      input_actual = actual_cpu();
      const auto start = loom_spike::now_ns();
      probes.resize(probe_count);
      for (std::uint64_t p = 0; p != probe_count; ++p) {
        const auto random = mix(seed + p * salt);
        auto key = random % key_count;
        if (distribution == "skew" && p % 10 < 9) key = random % std::min(key_count, std::uint64_t{8});
        if (p % 16 == 0) key += key_count; // Fixed miss cohort in every placement arm.
        probes[p] = {key, p};
      }
      input_ns = loom_spike::now_ns() - start;
    } catch (...) { failed.store(true, std::memory_order_relaxed); }
  });
  input_thread.join();
  if (failed.load()) throw std::runtime_error("probe input setup failed");

  std::vector<Table> copies(variant == "replicate" ? workers.size() : variant == "copy" ? 1 : 0);
  std::uint64_t copy_ns = 0, copy_bytes = 0;
  std::vector<int> copy_actual(copies.size(), -1);
  const auto copy_start = loom_spike::now_ns();
  // Sequential copying is deliberate: separately charge each retained replica,
  // without a bandwidth advantage from parallel construction in one arm.
  for (std::size_t i = 0; i != copies.size(); ++i) {
    std::thread copy_thread([&, i] {
      try {
        loom_spike::pin(workers[i]);
        copy_actual[i] = actual_cpu();
        copies[i] = original;
      } catch (...) { failed.store(true, std::memory_order_relaxed); }
    });
    copy_thread.join();
    copy_bytes += original.bytes();
  }
  if (!copies.empty()) copy_ns = loom_spike::now_ns() - copy_start;
  if (failed.load()) throw std::runtime_error("immutable copy setup failed");
  auto results = std::make_unique<JoinResult[]>(workers.size());
  const auto output_setup_start = loom_spike::now_ns();
  for (std::size_t i = 0; i != workers.size(); ++i) {
    std::thread output_thread([&, i] {
      try {
        loom_spike::pin(at_owner ? owner_cpu : workers[i]);
        results[i].page.resize(output_cap);
      } catch (...) { failed.store(true, std::memory_order_relaxed); }
    });
    output_thread.join();
  }
  const auto output_setup_ns = loom_spike::now_ns() - output_setup_start;
  if (failed.load()) throw std::runtime_error("output setup failed");

  namespace messaging = loom_spike::messaging;
  const auto route_cap = options.number("route-cap", 4);
  const auto route_timeout_ms = options.number("route-timeout-ms", 5000);
  if (!route_cap || route_cap > 1048576 || !route_timeout_ms || route_timeout_ms > 60000)
    throw std::runtime_error("invalid owner route bounds");
  std::vector<messaging::Edge> route_edges;
  for (unsigned i = 1; i <= workers.size(); ++i) {
    route_edges.emplace_back(i, 0);
    route_edges.emplace_back(0, i);
  }
  std::unique_ptr<messaging::Channels> routes;
  const auto route_variant = options.get("route-variant", "sparse");
  if (routed) {
    const auto kind = route_variant == "inbox" ? messaging::Variant::inbox :
      route_variant == "group-routed" ? messaging::Variant::group_routed :
      route_variant == "sparse" ? messaging::Variant::sparse : messaging::Variant::dense;
    if (route_variant != "inbox" && route_variant != "sparse" &&
        route_variant != "group-routed" && route_variant != "dense")
      throw std::runtime_error("unknown route-variant");
    routes = std::make_unique<messaging::Channels>(kind, workers.size() + 1,
      options.number("group-size", 2), route_cap, true, route_edges);
  }
  std::vector<messaging::Counters> route_stats(workers.size() + 1);
  std::vector<std::uint64_t> route_cpu_ns(workers.size() + 1, 0);
  std::vector<std::uint64_t> route_refusals(workers.size() + 1, 0);
  std::vector<int> route_actual_begin(workers.size() + 1, -1);
  std::vector<int> route_actual_end(workers.size() + 1, -1);
  std::vector<bool> serviced(workers.size(), false);
  std::atomic<bool> route_abort{false};
  std::atomic<std::uint64_t> route_acks{0};
  std::uint64_t route_deadline = 0;


  std::atomic<std::uint64_t> ready{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  const auto thread_count = routed ? workers.size() + 1 : at_owner ? std::size_t{1} : workers.size();
  threads.reserve(thread_count);
  std::vector<JoinUse> observations(uses);
  for (auto& observation : observations) {
    observation.partitions.resize(workers.size());
    observation.routing_cpus.resize(route_stats.size(), {-1, -1});
  }
  std::uint64_t native_ns = 0, worker_wall_ns = 0, fixture_end = 0;
  for (std::size_t use = 0; use != uses; ++use) {
    for (std::size_t i = 0; i != workers.size(); ++i) {
      auto& result = results[i];
      result.count = result.hash = result.pages = result.covered = result.coverage_sum = result.cpu_ns = 0;
      result.cpu_begin = result.cpu_end = -1;
    }
    std::fill(route_stats.begin(), route_stats.end(), messaging::Counters{});
    std::fill(route_cpu_ns.begin(), route_cpu_ns.end(), 0);
    std::fill(route_refusals.begin(), route_refusals.end(), 0);
    std::fill(route_actual_begin.begin(), route_actual_begin.end(), -1);
    std::fill(route_actual_end.begin(), route_actual_end.end(), -1);
    std::fill(serviced.begin(), serviced.end(), false);
    route_abort.store(false, std::memory_order_relaxed);
    route_acks.store(0, std::memory_order_relaxed);
    ready.store(0, std::memory_order_relaxed);
    go.store(false, std::memory_order_relaxed);
    threads.clear();
    const auto worker_start = loom_spike::now_ns();
    for (std::size_t thread = 0; thread != thread_count; ++thread) {
      threads.emplace_back([&, thread] {
        const int cpu = routed ? thread == 0 ? owner_cpu : workers[thread - 1] :
          at_owner ? owner_cpu : workers[thread];
        try { loom_spike::pin(cpu); }
        catch (...) { failed.store(true, std::memory_order_relaxed); }
        // Bind the service role before timing. Direct pair/inbox endpoints and
        // non-coordinator group workers have no routing work on any poll.
        const bool coordinator = routed && routes->coordinator(thread);
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) loom_spike::relax();
        auto execute = [&](std::size_t partition) {
          auto& result = results[partition];
          result.cpu_begin = actual_cpu();
          const auto cpu_start = loom_spike::thread_cpu_ns();
          const Table& table = variant == "copy" ? copies[0] :
            variant == "replicate" ? copies[partition] : original;
          probe_partition(table, probes, probes.size() * partition / workers.size(),
                          probes.size() * (partition + 1) / workers.size(), result);
          result.cpu_ns = loom_spike::thread_cpu_ns() - cpu_start;
          result.cpu_end = actual_cpu();
        };
        if (!routed) {
          const auto first = at_owner ? 0 : thread;
          const auto last = at_owner ? workers.size() : thread + 1;
          for (std::size_t partition = first; partition != last; ++partition) execute(partition);
          return;
        }
        const auto route_cpu_start = loom_spike::thread_cpu_ns();
        route_actual_begin[thread] = actual_cpu();
        auto& counters = route_stats[thread];
        std::uint64_t turns = 0;
        auto keep_running = [&] {
          if (route_abort.load(std::memory_order_relaxed)) return false;
          if ((++turns & 1023) == 0 && loom_spike::now_ns() > route_deadline) {
            route_abort.store(true, std::memory_order_relaxed);
            return false;
          }
          if (coordinator) routes->route(thread, 32, counters);
          return true;
        };
        if (thread != 0) {
          // Only indices cross the queue. The enclosing attempt retains every
          // immutable input, output page and result until both endpoints join.
          const auto identity = use * thread_count + thread;
          const messaging::Packet request{identity, 0, 0, 0, static_cast<unsigned>(thread), 0, 0, 0};
          bool accepted = false;
          while (keep_running() && !(accepted = routes->send(thread, request, counters))) {
            ++route_refusals[thread]; loom_spike::relax();
          }
          bool acknowledged = false;
          messaging::Packet ack;
          while (accepted && keep_running() && !acknowledged) {
            if (routes->receive(thread, ack, counters)) {
              acknowledged = ack.source == 0 && ack.destination == thread && ack.identity == identity;
              if (!acknowledged) route_abort.store(true, std::memory_order_relaxed);
            } else loom_spike::relax();
          }
          if (!acknowledged) failed.store(true, std::memory_order_relaxed);
          if (acknowledged) route_acks.fetch_add(1, std::memory_order_release);
          // A coordinator remains serviceable until the whole routed operation
          // has settled, even when its own acknowledgement arrived first.
          while (route_acks.load(std::memory_order_acquire) != workers.size() && keep_running())
            loom_spike::relax();
        } else {
          std::size_t complete = 0;
          while (complete != workers.size() && keep_running()) {
            messaging::Packet request;
            if (!routes->receive(0, request, counters)) { loom_spike::relax(); continue; }
            if (!request.source || request.source > workers.size() || request.destination != 0 ||
                request.identity != use * thread_count + request.source || serviced[request.source - 1]) {
              route_abort.store(true, std::memory_order_relaxed); break;
            }
            const auto partition = request.source - 1;
            execute(partition);
            serviced[partition] = true;
            messaging::Packet ack{request.identity, 0, results[partition].count, 0, 0, request.source, 0, 0};
            bool accepted = false;
            while (keep_running() && !(accepted = routes->send(0, ack, counters))) {
              ++route_refusals[0]; loom_spike::relax();
            }
            if (!accepted) break;
            ++complete;
          }
          if (complete != workers.size()) failed.store(true, std::memory_order_relaxed);
          while (route_acks.load(std::memory_order_acquire) != workers.size() && keep_running())
            loom_spike::relax();
        }
        route_cpu_ns[thread] = loom_spike::thread_cpu_ns() - route_cpu_start;
        route_actual_end[thread] = actual_cpu();
      });
    }
    while (ready.load(std::memory_order_acquire) != thread_count) loom_spike::relax();
    const auto native_start = loom_spike::now_ns();
    route_deadline = native_start + route_timeout_ms * 1000000;
    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    const auto native_end = loom_spike::now_ns();
    auto& observation = observations[use];
    observation.native_ns = native_end - native_start;
    observation.worker_wall_ns = native_end - worker_start;
    native_ns += observation.native_ns;
    worker_wall_ns += observation.worker_wall_ns;
    fixture_end = native_end;
    for (std::size_t i = 0; i != workers.size(); ++i) {
      const auto& result = results[i];
      observation.partitions[i] = {result.count, result.hash, result.pages, result.covered,
        result.coverage_sum, result.cpu_ns, result.cpu_begin, result.cpu_end};
    }
    for (std::size_t i = 0; i != route_stats.size(); ++i) {
      observation.routing_cpu_ns += route_cpu_ns[i];
      observation.routing_refusals += route_refusals[i];
      observation.enqueue_probes += route_stats[i].enqueue_probes;
      observation.dequeue_probes += route_stats[i].dequeue_probes;
      observation.forwarded += route_stats[i].forwarded;
      observation.cas_retries += route_stats[i].cas_retries;
      observation.empty_polls += route_stats[i].empty_polls;
      observation.routing_cpus[i] = {route_actual_begin[i], route_actual_end[i]};
    }
    observation.routed_acknowledgements = route_acks.load();
    observation.routes_settled = !route_abort.load() && route_acks.load() == workers.size() &&
      std::all_of(serviced.begin(), serviced.end(), [](bool done) { return done; });
    // Every worker has joined before its result page, service state, or route
    // counters can be reset for the next use of these retained inputs.
  }
  const auto fixture_wall_ns = fixture_end - fixture_start;
  // Validate after timing so the oracle cannot warm the candidate inputs.
  std::vector<std::pair<std::uint64_t, std::uint32_t>> ordered;
  ordered.reserve(build_count);
  for (std::uint32_t i = 0; i != build_count; ++i) ordered.emplace_back(i / duplicates, i);
  std::sort(ordered.begin(), ordered.end());
  std::vector<std::pair<std::uint64_t, std::uint64_t>> expected(workers.size());
  // Independent sort/search oracle, never the candidate hash-table traversal.
  for (std::size_t partition = 0; partition != workers.size(); ++partition) {
    const auto begin = probes.size() * partition / workers.size();
    const auto end = probes.size() * (partition + 1) / workers.size();
    for (std::size_t p = begin; p != end; ++p) {
      auto first = std::lower_bound(ordered.begin(), ordered.end(), std::pair{probes[p].key, std::uint32_t{0}});
      for (; first != ordered.end() && first->first == probes[p].key; ++first) {
        ++expected[partition].first;
        expected[partition].second += output_hash({p, first->second, mix(seed + first->second)});
      }
    }
  }

  Record record;
  record.family = "join";
  record.variant = variant;
  record.unit = "probe_row";
  record.repeat = repeat;
  record.elapsed_ns = native_ns;
  record.items = probe_count * uses;
  record.parameters = {{"requested_cpus", cpu_list(cpus)}, {"owner_cpu", std::to_string(owner_cpu)},
    {"build_first_touch_actual_cpu", std::to_string(build_actual)},
    {"probe_first_touch_actual_cpu", std::to_string(input_actual)},
    {"distribution", distribution}, {"build_rows", std::to_string(build_count)},
    {"probe_rows", std::to_string(probe_count)}, {"duplicates", std::to_string(duplicates)},
    {"uses", std::to_string(uses)}, {"reuse", "same retained build, copies, probe input and output pages"},
    {"max_output_rows", std::to_string(max_output_rows)}, {"memory_cap_mib", std::to_string(memory_cap_mib)},
    {"output_cap_rows", std::to_string(output_cap)}, {"partitions", std::to_string(workers.size())},
    {"seed", std::to_string(seed)}, {"check", check ? "1" : "0"},
    {"owner_routing", routed ? "bounded request and acknowledgement packets" : "direct; queue cost excluded"},
    {"route_variant", routed ? route_variant : "none"}, {"route_cap", std::to_string(route_cap)},
    {"route_timeout_ms", std::to_string(route_timeout_ms)},
    {"output_sink", "fixed page; synchronous counted/hash drain"},
    {"fixture_wall_scope", "original table construction through final worker joins; excludes independent verification"},
    {"component_total_scope", "sum of recorded components; excludes some thread and routing setup"}};
  std::uint64_t outputs = 0, hash = 0, pages = 0, covered = 0, coverage_sum = 0, cpu_ns = 0;
  std::uint64_t routing_cpu = 0, routing_refusals = 0, enqueue_probes = 0, dequeue_probes = 0;
  std::uint64_t forwarded = 0, cas_retries = 0, empty_polls = 0, routed_acknowledgements = 0;
  bool verified = !failed.load() && build_actual == owner_cpu && input_actual == workers.front();
  for (std::size_t use = 0; use != uses; ++use) {
    const auto& observation = observations[use];
    const auto prefix = "use_" + std::to_string(use) + "_";
    std::uint64_t use_outputs = 0, use_hash = 0, use_pages = 0, use_covered = 0;
    std::uint64_t use_coverage_sum = 0, use_cpu_ns = 0;
    for (std::size_t i = 0; i != workers.size(); ++i) {
      const auto& result = observation.partitions[i];
      use_outputs += result.count; use_hash += result.hash; use_pages += result.pages;
      use_covered += result.covered; use_coverage_sum += result.coverage_sum; use_cpu_ns += result.cpu_ns;
      const int requested = at_owner ? owner_cpu : workers[i];
      verified &= result.count == expected[i].first && result.hash == expected[i].second;
      verified &= result.cpu_begin == requested && result.cpu_end == requested;
      const auto begin = probes.size() * i / workers.size();
      const auto end = probes.size() * (i + 1) / workers.size();
      verified &= result.covered == end - begin;
      const auto placement = std::to_string(result.cpu_begin) + ":" + std::to_string(result.cpu_end);
      record.parameters[prefix + "actual_probe_cpu_" + std::to_string(i)] = placement;
      if (!use) record.parameters["actual_probe_cpu_" + std::to_string(i)] = placement;
      record.counters["partition_output_rows_" + std::to_string(i)] += result.count;
    }
    verified &= use_covered == probe_count && use_coverage_sum == probe_count * (probe_count - 1) / 2;
    outputs += use_outputs; hash += use_hash; pages += use_pages;
    covered += use_covered; coverage_sum += use_coverage_sum; cpu_ns += use_cpu_ns;
    routing_cpu += observation.routing_cpu_ns; routing_refusals += observation.routing_refusals;
    enqueue_probes += observation.enqueue_probes; dequeue_probes += observation.dequeue_probes;
    forwarded += observation.forwarded; cas_retries += observation.cas_retries;
    empty_polls += observation.empty_polls; routed_acknowledgements += observation.routed_acknowledgements;
    if (routed) {
      verified &= observation.routes_settled;
      for (std::size_t i = 0; i != observation.routing_cpus.size(); ++i) {
        const auto expected_cpu = i == 0 ? owner_cpu : workers[i - 1];
        const auto [begin, end] = observation.routing_cpus[i];
        verified &= begin == expected_cpu && end == expected_cpu;
        const auto placement = std::to_string(begin) + ":" + std::to_string(end);
        record.parameters[prefix + "actual_route_cpu_" + std::to_string(i)] = placement;
        if (!use) record.parameters["actual_route_cpu_" + std::to_string(i)] = placement;
      }
    }
    record.counters[prefix + "native_ns"] = observation.native_ns;
    record.counters[prefix + "worker_wall_ns"] = observation.worker_wall_ns;
    record.counters[prefix + "cpu_ns"] = routed ? observation.routing_cpu_ns : use_cpu_ns;
    record.counters[prefix + "native_probe_cpu_ns"] = use_cpu_ns;
    record.counters[prefix + "output_rows"] = use_outputs;
    record.counters[prefix + "output_hash"] = use_hash;
    record.counters[prefix + "output_pages"] = use_pages;
    record.counters[prefix + "covered_probe_rows"] = use_covered;
    record.counters[prefix + "probe_ordinal_sum"] = use_coverage_sum;
    record.counters[prefix + "routed_acknowledgements"] = observation.routed_acknowledgements;
  }
  for (std::size_t i = 0; i != copy_actual.size(); ++i) {
    verified &= copy_actual[i] == workers[i];
    record.parameters["copy_first_touch_actual_cpu_" + std::to_string(i)] = std::to_string(copy_actual[i]);
  }
  verified &= covered == probe_count * uses && coverage_sum == probe_count * (probe_count - 1) / 2 * uses;
  if (routed) verified &= routed_acknowledgements == workers.size() * uses;
  record.verified = verified;
  record.counters = [&] {
    auto counters = std::move(record.counters);
    counters.insert({{"output_rows", outputs}, {"output_hash", hash}, {"output_pages", pages},
      {"covered_probe_rows", covered}, {"probe_ordinal_sum", coverage_sum},
      {"cpu_ns", routed ? routing_cpu : cpu_ns}, {"native_probe_cpu_ns", cpu_ns},
      {"build_setup_ns", build_ns}, {"probe_setup_ns", input_ns}, {"copy_setup_ns", copy_ns},
      {"output_setup_ns", output_setup_ns}, {"native_ns", native_ns},
      {"worker_wall_ns", worker_wall_ns}, {"fixture_wall_ns", fixture_wall_ns},
      {"build_copy_native_ns", build_ns + copy_ns + native_ns},
      {"complete_setup_native_ns", build_ns + input_ns + copy_ns + output_setup_ns + native_ns},
      {"build_bytes", original.bytes()}, {"copy_bytes", copy_bytes},
      {"retained_build_bytes", original.bytes() + copy_bytes},
      {"bounded_output_bytes", output_cap * workers.size() * sizeof(OutputRow)}});
    counters.insert({{"routing_cpu_ns", routing_cpu}, {"route_send_refusals", routing_refusals},
      {"route_enqueue_probes", enqueue_probes}, {"route_dequeue_probes", dequeue_probes},
      {"route_empty_polls", empty_polls}, {"route_forwarded", forwarded}, {"route_cas_retries", cas_retries},
      {"routed_acknowledgements", routed_acknowledgements},
      {"route_queue_storage_bytes", routes ? routes->storage_bytes() : 0}});
    return counters;
  }();
  loom_spike::emit(record);
  return verified;
}

std::vector<std::string> variants(const Options& options, const std::string& family) {
  const auto selected = options.get("variant", "all");
  const std::vector<std::string> choices = family == "locks" ?
    std::vector<std::string>{"ttas", "backoff", "mcs", "useful", "owner"} :
    std::vector<std::string>{"owner", "owner-direct", "shared", "copy", "replicate"};
  if (selected == "all") return choices;
  if (std::find(choices.begin(), choices.end(), selected) == choices.end())
    throw std::runtime_error("variant is not valid for selected family");
  return {selected};
}
} // namespace

int main(int argc, char** argv) {
  try {
    Options options(argc, argv);
    const bool check = options.has("check");
    const auto cpus = options.cpus();
    if (cpus.empty() || cpus.size() > 64) throw std::runtime_error("need 1..64 CPUs");
    const auto family = options.get("family", "all");
    if (family != "all" && family != "locks" && family != "join")
      throw std::runtime_error("family must be locks, join or all");
    const auto repeat = options.number("repeat", 0);
    bool verified = true;
      if (family == "all" || family == "locks")
        for (const auto& variant : variants(options, "locks"))
          verified &= variant == "owner" ? owner_lock_case(options, cpus, repeat, check) :
            lock_case(options, variant, cpus, repeat, check);
      if (family == "all" || family == "join") {
        const auto distribution = options.get("distribution", check ? "all" : "uniform");
        if (distribution != "uniform" && distribution != "skew" && distribution != "all")
          throw std::runtime_error("distribution must be uniform, skew or all");
        for (const auto& variant : variants(options, "join")) {
          if (distribution == "uniform" || distribution == "all")
            verified &= join_case(options, variant, cpus, repeat, check, "uniform");
          if (distribution == "skew" || distribution == "all")
            verified &= join_case(options, variant, cpus, repeat, check, "skew");
        }
      }
    return verified ? 0 : 1;
  } catch (const std::exception& error) {
    Record record;
    record.family = "affinity_error";
    record.variant = "invalid_or_failed_setup";
    record.unit = "case";
    record.verified = false;
    record.parameters["error"] = error.what();
    loom_spike::emit(record);
    return 2;
  }
}
