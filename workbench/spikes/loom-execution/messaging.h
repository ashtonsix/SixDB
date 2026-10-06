#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace loom_spike::messaging {

// Fixed, copied packet: no borrowed native stack or per-message allocation.
// Source/sequence survive routing; hops counts actual successful queue writes.
// Send borrows only for the call, copies into an accepted slot synchronously,
// and leaves the caller's packet unchanged on acceptance and refusal.
struct Packet {
  std::uint64_t identity{}, sequence{}, payload{}, offered_ns{};
  std::uint32_t source{}, destination{}, hops{}, reserved{};
};
using Edge = std::pair<unsigned, unsigned>;
// The measured fleet uses 64-byte lines. Separate thread observers must not
// share a line through adjacent caller arrays or stack objects.
struct alignas(64) Counters {
  std::uint64_t enqueue_probes{}, dequeue_probes{}, empty_polls{};
  std::uint64_t cas_retries{}, contention_refusals{}, forwarded{}, blocked_routes{};
};
bool check_slot_indexing() noexcept;

// Each directed queue has exactly one native producer and one native consumer.
// Sparse storage has lists only for configured edges; direct_index deliberately
// adds an N*N index for the dense candidate. Channels are fixed before use.
class PairChannels {
 public:
  PairChannels(unsigned workers, std::size_t capacity, bool padded,
      std::span<const Edge> edges, bool direct_index);
  ~PairChannels();
  bool send(unsigned worker, const Packet&, Counters&) noexcept;
  bool receive(unsigned worker, Packet&, Counters&) noexcept;
  std::uint64_t storage_bytes() const noexcept;
  std::uint64_t queue_count() const noexcept;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Bounded sequence-cell inboxes. One consumer per inbox; producers retry at most
// 64 CAS attempts per send. Full and contention refusals leave the packet local.
class InboxChannels {
 public:
  InboxChannels(unsigned workers, std::size_t capacity, bool padded);
  ~InboxChannels();
  bool send(unsigned worker, const Packet&, Counters&) noexcept;
  bool receive(unsigned worker, Packet&, Counters&) noexcept;
  std::uint64_t storage_bytes() const noexcept;
  std::uint64_t queue_count() const noexcept;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// First worker of each group is the coordinator. Ingress, inter-coordinator,
// egress and optional intra-group directed queues are distinct SPSC channels.
// Routing retains one blocked packet per input; never drops an accepted packet.
// Coordinators service only their own inputs. This is real forwarding, not a
// modeled latency surcharge. It adds no execution/publication authority.
class GroupChannels {
 public:
  GroupChannels(unsigned workers, unsigned group_size, std::size_t capacity,
      bool padded, bool direct_within_group);
  ~GroupChannels();
  bool send(unsigned worker, const Packet&, Counters&) noexcept;
  bool receive(unsigned worker, Packet&, Counters&) noexcept;
  void route(unsigned worker, std::uint64_t probe_budget, Counters&) noexcept;
  bool coordinator(unsigned worker) const noexcept;
  std::uint64_t storage_bytes() const noexcept;
  std::uint64_t queue_count() const noexcept;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

enum class Variant { dense, sparse, inbox, group_direct, group_routed };
class Channels {
 public:
  Channels(Variant, unsigned workers, unsigned group_size, std::size_t capacity,
      bool padded, std::span<const Edge> active_edges);
  bool send(unsigned worker, const Packet&, Counters&) noexcept;
  bool receive(unsigned worker, Packet&, Counters&) noexcept;
  void route(unsigned worker, std::uint64_t probe_budget, Counters&) noexcept;
  bool coordinator(unsigned worker) const noexcept;
  std::uint64_t storage_bytes() const noexcept;
  std::uint64_t queue_count() const noexcept;
 private:
  std::unique_ptr<PairChannels> pairs_;
  std::unique_ptr<InboxChannels> inboxes_;
  std::unique_ptr<GroupChannels> groups_;
};

} // namespace loom_spike::messaging
