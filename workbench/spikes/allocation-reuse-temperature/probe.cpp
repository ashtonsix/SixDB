// Isolated reuse-policy experiment: selection and payload reuse, not VM allocation.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t kib = 1024;
constexpr std::size_t mib = 1024 * kib;
constexpr int absent = -1;
enum Temperature : unsigned { hot, warm, cold };
enum class Kind { shared_mru, bucket_mru, bounded_mru, bounded_fifo };

struct Node {
  int next{absent}, previous{absent};
  unsigned queue{};
  bool idle{};
  std::size_t capacity{};
  Temperature hint{cold};
  int cpu{};
};

struct Queue {
  int first{absent}, last{absent}; // Newest and oldest release respectively.
  std::size_t bytes{}, count{};
};

struct Request {
  std::size_t bytes{};
  Temperature preferred{}, released{};
  int cpu{}, released_cpu{};
};

struct Counters {
  std::uint64_t examined{}, reused{}, fresh{}, hot_demotions{}, warm_demotions{};
  std::uint64_t hint_mismatches{}, tier_mismatches{}, cpu_mismatches{}, payload_bytes{};
};

struct Case {
  std::string_view name;
  std::size_t initial{}, batch{};
  std::vector<std::size_t> capacities;
  std::vector<Temperature> hints;
  std::vector<int> cpus;
  std::vector<Request> requests;
};

std::uint64_t random(std::uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

Case make_case(std::string_view name, std::size_t operations) {
  Case c{name, 256, 16, {}, {}, {}, {}};
  if (name == "hot-one" || name == "warm-one") c.batch = 1;
  if (name == "delayed-revisit") { c.initial = 1024; c.batch = 128; }
  if (name == "oversized") c.batch = 8;
  operations = std::max(c.batch, operations / c.batch * c.batch);
  for (std::size_t i = 0; i != c.initial; ++i) {
    const auto size = name == "mixed-extent" ? std::array{4*kib, 8*kib, 16*kib, 64*kib}[i%4] :
        name == "oversized" && i % 32 == 0 ? 64*kib : 8*kib;
    c.capacities.push_back(size);
    c.hints.push_back(name == "warm-one" ? warm : name == "cold-contamination" ? (i < 32 ? hot : cold) :
        name == "cpu-mix" ? hot : i % 3 == 0 ? warm : hot);
    c.cpus.push_back(name == "cpu-mix" ? int(i%4) : 0);
  }
  std::uint64_t seed = 0x5349584442434143ull;
  for (std::size_t i = 0; i != operations; ++i) {
    Request r{8*kib, hot, hot, 0, 0};
    if (name == "warm-one") r.preferred = r.released = warm;
    if (name == "mixed-extent") {
      r.bytes = std::array{4*kib, 8*kib, 16*kib, 64*kib}[random(seed)%4];
      r.preferred = static_cast<Temperature>(random(seed)%3);
      r.released = static_cast<Temperature>(random(seed)%3);
    }
    if (name == "oversized") r.bytes = i % 8 == 0 ? 64*kib : 8*kib;
    if (name == "cold-contamination") r.released = i % 5 == 0 ? cold : hot;
    if (name == "cpu-mix") {
      r.cpu = int(random(seed)%4);
      r.released_cpu = r.cpu;
      r.released = i % 8 == 0 ? warm : hot;
    }
    if (name == "delayed-revisit") {
      r.preferred = i / c.batch % 4 == 0 ? warm : hot;
      r.released = r.preferred;
    }
    c.requests.push_back(r);
  }
  return c;
}

template<Kind K>
struct Policy {
  std::vector<Node> nodes;
  std::array<Queue, 3> queues;
  std::size_t hot_limit{}, warm_limit{};
  Counters counters;

  Policy(const Case& c, std::size_t h, std::size_t w) : hot_limit(h), warm_limit(w) {
    nodes.reserve(c.initial + c.requests.size());
    for (std::size_t i = 0; i != c.initial; ++i) {
      nodes.push_back(Node{.capacity=c.capacities[i], .hint=c.hints[i], .cpu=c.cpus[i]});
      release(int(i), c.hints[i], c.cpus[i]);
    }
    counters = {};
  }

  void prepend(int id, unsigned q) {
    auto& n = nodes[id];
    auto& list = queues[q];
    n.queue = q; n.previous = absent; n.next = list.first; n.idle = true;
    if (list.first != absent) nodes[list.first].previous = id;
    else list.last = id;
    list.first = id;
    list.bytes += n.capacity; ++list.count;
  }

  void remove(int id) {
    auto& n = nodes[id];
    auto& list = queues[n.queue];
    if (n.previous == absent) list.first = n.next;
    else nodes[n.previous].next = n.next;
    if (n.next == absent) list.last = n.previous;
    else nodes[n.next].previous = n.previous;
    list.bytes -= n.capacity; --list.count;
    n.next = n.previous = absent; n.idle = false;
  }

  void demote(unsigned q, std::size_t limit) {
    while (queues[q].bytes > limit) {
      const auto id = queues[q].last;
      remove(id);
      prepend(id, q + 1);
      if (q == hot) ++counters.hot_demotions;
      else ++counters.warm_demotions;
    }
  }

  void release(int id, Temperature temperature, int cpu) {
    auto& n = nodes[id];
    n.hint = temperature; n.cpu = cpu;
    if constexpr (K == Kind::shared_mru) prepend(id, 0);
    else {
      auto q = unsigned(temperature);
      if constexpr (K == Kind::bounded_mru || K == Kind::bounded_fifo) {
        // A single large return cannot expel a queue of small useful extents.
        if (q == hot && n.capacity > hot_limit) q = warm;
        if (q == warm && n.capacity > warm_limit) q = cold;
      }
      prepend(id, q);
    }
    if constexpr (K == Kind::bounded_mru || K == Kind::bounded_fifo) {
      demote(hot, hot_limit);
      demote(warm, warm_limit);
    }
  }

  int acquire(const Request& r) {
    int selected = absent;
    auto best_extra = std::numeric_limits<std::size_t>::max();
    unsigned best = 10, hint_budget = 4;
    bool ready = false, done = false;
    const std::array<unsigned, 3> order{unsigned(r.preferred),
        unsigned(r.preferred == hot ? warm : hot), unsigned(r.preferred == cold ? warm : cold)};
    // Exact current bounded preference scan for shared_mru. Bucket variants
    // change only traversal order and temperature demotion, not candidate rank.
    for (unsigned pass = 0; pass != (K == Kind::shared_mru ? 1u : 3u) && !done; ++pass) {
      const auto q = K == Kind::shared_mru ? 0u : order[pass];
      auto id = K == Kind::bounded_fifo ? queues[q].last : queues[q].first;
      while (id != absent) {
        if (ready) {
          if (!hint_budget) { done = true; break; }
          --hint_budget;
        }
        const auto& n = nodes[id];
        ++counters.examined;
        const auto following = K == Kind::bounded_fifo ? n.previous : n.next;
        if (n.capacity < r.bytes) { id = following; continue; }
        const auto extra = std::size_t{0}; // All modeled capacities are prepared.
        const auto available_tier = K == Kind::shared_mru ? unsigned(n.hint) : n.queue;
        unsigned rank = available_tier != r.preferred ? 2 : 0;
        if (r.preferred == hot && n.cpu != r.cpu) ++rank;
        if (extra < best_extra || (extra == best_extra && rank < best)) {
          selected = id; best_extra = extra; best = rank;
        }
        if (!rank) { done = true; break; }
        ready = true;
        id = following;
      }
    }
    if (selected == absent) {
      selected = int(nodes.size());
      nodes.push_back(Node{.capacity=r.bytes});
      ++counters.fresh;
    } else {
      if (nodes[selected].hint != r.preferred) ++counters.hint_mismatches;
      if constexpr (K != Kind::shared_mru)
        if (nodes[selected].queue != r.preferred) ++counters.tier_mismatches;
      if (r.preferred == hot && nodes[selected].cpu != r.cpu) ++counters.cpu_mismatches;
      remove(selected); ++counters.reused;
    }
    return selected;
  }

  void validate(std::size_t live = 0) const {
    std::vector<bool> seen(nodes.size());
    std::size_t idle = 0;
    for (unsigned q = 0; q != queues.size(); ++q) {
      int prior = absent;
      std::size_t bytes = 0, count = 0;
      for (int id = queues[q].first; id != absent; id = nodes[id].next) {
        if (id < 0 || std::size_t(id) >= nodes.size() || seen[id])
          throw std::runtime_error("cyclic/invalid reuse list");
        seen[id] = true;
        const auto& n = nodes[id];
        if (!n.idle || n.previous != prior || n.queue != q)
          throw std::runtime_error("broken reuse link");
        prior = id; bytes += n.capacity; ++count;
      }
      if (prior != queues[q].last || bytes != queues[q].bytes || count != queues[q].count)
        throw std::runtime_error("incorrect reuse accounting");
      idle += count;
    }
    if (idle + live != nodes.size()) throw std::runtime_error("lost window");
    if constexpr (K == Kind::bounded_mru || K == Kind::bounded_fifo)
      if (queues[hot].bytes > hot_limit || queues[warm].bytes > warm_limit)
        throw std::runtime_error("tier capacity exceeded");
  }
};

struct Free { void operator()(std::uint64_t* p) const { std::free(p); } };
using Buffer = std::unique_ptr<std::uint64_t, Free>;

std::uint64_t touch(std::uint64_t* payload, std::size_t bytes, std::uint64_t marker) {
  std::uint64_t checksum = 0;
  // One dependent read/write per 64-byte line: enough to expose address reuse.
  // The virtualized host's reported cache geometry is not a residency proof.
  for (std::size_t i = 0; i < bytes / sizeof(std::uint64_t); i += 8) {
    checksum += payload[i];
    payload[i] = marker;
  }
  return checksum;
}

template<Kind K>
void measure(const Case& c, std::size_t hot_limit, std::size_t warm_limit,
    bool payload, unsigned repetition, int cpu) {
  Policy<K> policy(c, hot_limit, warm_limit);
  policy.validate();
  // Storage is preallocated, and each physical candidate has a distinct range.
  // Fresh candidates pay policy bookkeeping only; no mmap/zeroing is priced.
  constexpr std::size_t stride = 64 * kib / sizeof(std::uint64_t);
  Buffer data;
  const auto payload_slots = c.initial + 4*c.batch;
  if (payload) {
    // There are four request extents. At most one batch of each can become
    // newly necessary; no candidate is destroyed in this policy-only probe.
    const auto bytes = payload_slots * stride * sizeof(std::uint64_t);
    data.reset(static_cast<std::uint64_t*>(std::aligned_alloc(64, bytes)));
    if (!data) throw std::bad_alloc();
    std::memset(data.get(), 0, bytes);
    for (std::size_t i = 0; i != c.initial; ++i)
      (void)touch(data.get() + i * stride, c.capacities[i], 1);
  }
  std::vector<int> live(c.batch);
  std::uint64_t checksum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t begin = 0; begin != c.requests.size(); begin += c.batch) {
    for (std::size_t i = 0; i != c.batch; ++i) {
      const auto& r = c.requests[begin + i];
      const auto id = policy.acquire(r);
      live[i] = id;
      if (payload) {
        if (std::size_t(id) >= payload_slots) throw std::runtime_error("payload reserve exhausted");
        checksum += touch(data.get() + std::size_t(id) * stride, r.bytes, begin + i + 2);
      }
      else checksum += std::uint64_t(id);
      policy.counters.payload_bytes += payload ? r.bytes : 0;
    }
    for (std::size_t i = 0; i != c.batch; ++i) {
      const auto& r = c.requests[begin + i];
      policy.release(live[i], r.released, r.released_cpu);
    }
  }
  const auto stop = std::chrono::steady_clock::now();
  policy.validate();
  const auto& n = policy.counters;
  if (n.reused + n.fresh != c.requests.size()) throw std::runtime_error("lost request");
  std::array<std::size_t, 3> idle_bytes{};
  for (const auto& node : policy.nodes) if (node.idle)
    idle_bytes[K == Kind::shared_mru ? unsigned(node.hint) : node.queue] += node.capacity;
  const auto name = K == Kind::shared_mru ? "shared-mru" :
      K == Kind::bucket_mru ? "bucket-mru" : K == Kind::bounded_mru ? "bounded-mru" : "bounded-fifo";
  const auto ns = std::chrono::duration<double, std::nano>(stop - start).count();
  std::cout << c.name << ',' << name << ',' << (payload ? "touch-lines" : "metadata")
      << ',' << repetition << ',' << cpu << ',' << hot_limit << ',' << warm_limit
      << ',' << c.initial << ',' << c.batch << ',' << c.requests.size() << ',' << ns / c.requests.size()
      << ',' << n.examined << ',' << n.reused << ',' << n.fresh << ',' << n.hot_demotions
      << ',' << n.warm_demotions << ',' << n.hint_mismatches << ',' << n.tier_mismatches << ',' << n.cpu_mismatches
      << ',' << n.payload_bytes << ',' << idle_bytes[hot]
      << ',' << idle_bytes[warm] << ',' << idle_bytes[cold] << ',' << checksum << '\n';
}

void checks() {
  Case c{"checks", 0, 1, {}, {}, {}, {}};
  Policy<Kind::bounded_mru> p(c, 8*kib, 16*kib);
  p.nodes.push_back(Node{.capacity=8*kib});
  p.nodes.push_back(Node{.capacity=8*kib});
  p.nodes.push_back(Node{.capacity=8*kib});
  p.nodes.push_back(Node{.capacity=8*kib});
  for (int i = 0; i != 4; ++i) p.release(i, hot, 0);
  // Independent expected result: only newest HOT; two later WARM; oldest COLD.
  if (p.nodes[3].queue != hot || p.nodes[2].queue != warm || p.nodes[1].queue != warm ||
      p.nodes[0].queue != cold || p.counters.hot_demotions != 3 || p.counters.warm_demotions != 1)
    throw std::runtime_error("oldest-first overflow differs from expected cascade");
  p.validate();
  const Request hot_request{8*kib, hot, hot, 0, 0};
  if (p.acquire(hot_request) != 3) throw std::runtime_error("HOT candidate not preferred");
  p.validate(1);
  p.release(3, hot, 0);
  p.nodes.push_back(Node{.capacity=64*kib});
  p.release(4, hot, 0);
  if (p.nodes[4].queue != cold || p.nodes[3].queue != hot)
    throw std::runtime_error("oversized candidate should bypass tier ceilings without expelling HOT");
  for (const auto& node : p.nodes)
    if (node.hint != hot) throw std::runtime_error("demotion rewrote release advice");
  p.validate();

  Policy<Kind::bounded_fifo> f(c, 64*kib, 64*kib);
  for (int i = 0; i != 3; ++i) { f.nodes.push_back(Node{.capacity=8*kib}); f.release(i, hot, 0); }
  if (f.acquire(hot_request) != 0) throw std::runtime_error("FIFO did not select oldest eligible");
  f.validate(1);
  Policy<Kind::bounded_mru> m(c, 64*kib, 64*kib);
  for (int i = 0; i != 3; ++i) { m.nodes.push_back(Node{.capacity=8*kib}); m.release(i, hot, 0); }
  if (m.acquire(hot_request) != 2) throw std::runtime_error("MRU did not select newest eligible");
  m.validate(1);
}

} // namespace

int main(int argc, char** argv) try {
  int cpu = 0;
  unsigned repetitions = 5;
  std::size_t operations = 8192;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--check") { checks(); std::cout << "policy checks passed\n"; return 0; }
    if (i + 1 == argc) throw std::runtime_error("missing option value");
    const auto value = std::stoull(argv[++i]);
    if (arg == "--cpu") cpu = int(value);
    else if (arg == "--repetitions") repetitions = unsigned(value);
    else if (arg == "--operations") operations = value;
    else throw std::runtime_error("unknown option");
  }
  if (cpu < 0 || cpu >= CPU_SETSIZE || !repetitions || operations < 128 || operations > 65536)
    throw std::runtime_error("invalid run bounds");
  cpu_set_t affinity;
  CPU_ZERO(&affinity); CPU_SET(cpu, &affinity);
  if (sched_setaffinity(0, sizeof(affinity), &affinity)) throw std::runtime_error("cannot pin CPU");
  checks();
  std::cout << "case,policy,work,repetition,cpu,hot_limit,warm_limit,initial,batch,operations,ns_per_pair,examined,reused,fresh,hot_demotions,warm_demotions,hint_mismatches,tier_mismatches,cpu_mismatches,payload_bytes,hot_bytes,warm_bytes,cold_bytes,checksum\n";
  for (const auto name : {"hot-one", "warm-one", "delayed-revisit", "mixed-extent", "oversized", "cold-contamination", "cpu-mix"}) {
    const auto c = make_case(name, operations);
    for (const bool payload : {false, true}) {
      for (unsigned r = 0; r != repetitions; ++r)
        measure<Kind::shared_mru>(c, 0, 0, payload, r, cpu);
      for (unsigned r = 0; r != repetitions; ++r)
        measure<Kind::bucket_mru>(c, 0, 0, payload, r, cpu);
      for (const auto limits : {std::array{64*kib, mib}, std::array{256*kib, 4*mib}, std::array{mib, 16*mib}}) {
        for (unsigned r = 0; r != repetitions; ++r)
          measure<Kind::bounded_mru>(c, limits[0], limits[1], payload, r, cpu);
        for (unsigned r = 0; r != repetitions; ++r)
          measure<Kind::bounded_fifo>(c, limits[0], limits[1], payload, r, cpu);
      }
    }
  }
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
