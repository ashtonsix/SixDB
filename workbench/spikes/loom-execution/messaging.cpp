#include "messaging.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <limits>
#include <stdexcept>

namespace loom_spike::messaging {
namespace {
class SlotIndex {
 public:
  explicit SlotIndex(std::size_t capacity) : capacity_(capacity) {
    static_assert(sizeof(std::size_t) == 8, "measured 64-bit Linux targets");
    if (!capacity) throw std::invalid_argument("zero queue capacity");
    if (std::has_single_bit(capacity)) mask_ = capacity - 1;
    else multiplier_ = static_cast<std::uint64_t>((static_cast<__uint128_t>(1) << 64) / capacity);
  }
  std::size_t operator()(std::size_t position) const noexcept {
    if (mask_ != std::numeric_limits<std::size_t>::max()) return position & mask_;
    // floor(2^64 / d) underestimates the quotient by at most one for x<2^64:
    // x/d - x*floor(2^64/d)/2^64 < 1. One remainder correction is exact.
    const auto quotient = static_cast<std::uint64_t>((static_cast<__uint128_t>(position) * multiplier_) >> 64);
    const auto remainder = position - quotient * capacity_;
    return remainder >= capacity_ ? remainder - capacity_ : remainder;
  }
 private:
  std::size_t capacity_, mask_{std::numeric_limits<std::size_t>::max()};
  std::uint64_t multiplier_{};
};
struct alignas(64) PaddedCounter { std::atomic<std::size_t> value{}; };
struct alignas(64) LocalCursor { std::size_t value{}; };
struct Controls {
  std::unique_ptr<std::atomic<std::size_t>[]> compact;
  std::unique_ptr<PaddedCounter[]> padded;
  std::atomic<std::size_t>* head{};
  std::atomic<std::size_t>* tail{};
  explicit Controls(bool pad) {
    if (pad) {
      padded = std::make_unique<PaddedCounter[]>(2);
      head = &padded[0].value; tail = &padded[1].value;
    } else {
      compact = std::make_unique<std::atomic<std::size_t>[]>(2);
      head = &compact[0]; tail = &compact[1];
    }
  }
  std::uint64_t bytes() const noexcept {
    return padded ? 2 * sizeof(PaddedCounter) : 2 * sizeof(std::atomic<std::size_t>);
  }
};
class Spsc {
 public:
  Spsc(std::size_t capacity, bool pad) : capacity_(capacity), index_(capacity), controls_(pad),
      packets_(std::make_unique<Packet[]>(capacity)) {
    if (!capacity) throw std::invalid_argument("zero queue capacity");
  }
  bool push(const Packet& packet, Counters& c) noexcept {
    ++c.enqueue_probes;
    const auto tail = controls_.tail->load(std::memory_order_relaxed);
    if (tail - controls_.head->load(std::memory_order_acquire) == capacity_) return false;
    const auto hops = packet.hops + 1;
    auto& slot = packets_[index_(tail)];
    slot = packet;
    slot.hops = hops;
    controls_.tail->store(tail + 1, std::memory_order_release);
    return true;
  }
  bool pop(Packet& packet, Counters& c) noexcept {
    ++c.dequeue_probes;
    const auto head = controls_.head->load(std::memory_order_relaxed);
    if (head == controls_.tail->load(std::memory_order_acquire)) {
      ++c.empty_polls; return false;
    }
    packet = packets_[index_(head)];
    controls_.head->store(head + 1, std::memory_order_release);
    return true;
  }
  std::uint64_t bytes() const noexcept { return sizeof(*this) + controls_.bytes() + capacity_ * sizeof(Packet); }
 private:
  std::size_t capacity_;
  SlotIndex index_;
  Controls controls_;
  std::unique_ptr<Packet[]> packets_;
};
class Mpsc {
  struct Cell { std::atomic<std::size_t> sequence{}; Packet packet; };
 public:
  Mpsc(std::size_t capacity, bool pad) : capacity_(capacity), index_(capacity), controls_(pad),
      cells_(std::make_unique<Cell[]>(capacity)) {
    // Capacity one aliases the vacant/full sequence values in this algorithm.
    if (capacity < 2) throw std::invalid_argument("inbox capacity must be at least two");
    for (std::size_t i = 0; i < capacity; ++i) cells_[i].sequence.store(i);
  }
  bool push(const Packet& packet, Counters& c) noexcept {
    ++c.enqueue_probes;
    auto pos = controls_.tail->load(std::memory_order_relaxed);
    for (unsigned retry = 0; retry < 64; ++retry) {
      auto& cell = cells_[index_(pos)];
      const auto seq = cell.sequence.load(std::memory_order_acquire);
      const auto difference = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
      if (difference < 0) return false;
      if (difference > 0) { pos = controls_.tail->load(std::memory_order_relaxed); ++c.cas_retries; continue; }
      if (!controls_.tail->compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
        ++c.cas_retries; continue;
      }
      const auto hops = packet.hops + 1;
      cell.packet = packet;
      cell.packet.hops = hops;
      cell.sequence.store(pos + 1, std::memory_order_release);
      return true;
    }
    ++c.contention_refusals;
    return false;
  }
  bool pop(Packet& packet, Counters& c) noexcept {
    ++c.dequeue_probes;
    const auto pos = controls_.head->load(std::memory_order_relaxed);
    auto& cell = cells_[index_(pos)];
    if (cell.sequence.load(std::memory_order_acquire) != pos + 1) {
      ++c.empty_polls; return false;
    }
    packet = cell.packet;
    cell.sequence.store(pos + capacity_, std::memory_order_release);
    controls_.head->store(pos + 1, std::memory_order_relaxed);
    return true;
  }
  std::uint64_t bytes() const noexcept { return sizeof(*this) + controls_.bytes() + capacity_ * sizeof(Cell); }
 private:
  std::size_t capacity_;
  SlotIndex index_;
  Controls controls_;
  std::unique_ptr<Cell[]> cells_;
};
} // namespace

bool check_slot_indexing() noexcept {
  for (const std::size_t capacity : {1U, 2U, 3U, 7U, 8U, 63U, 64U, 112U, 248U}) {
    const SlotIndex index(capacity);
    const auto maximum = std::numeric_limits<std::size_t>::max();
    const std::size_t fixed[]{0, 1, capacity - 1, capacity, capacity + 1,
      maximum, maximum - 1, maximum - capacity, (std::size_t{1} << 63) - 1, std::size_t{1} << 63};
    for (auto position : fixed) if (index(position) != position % capacity) return false;
    // Arithmetic wraps unsigned positions deliberately. Hardware remainder is
    // an independent oracle for the precomputed reciprocal; no timer involved.
    std::size_t position = maximum - 1023;
    for (unsigned i = 0; i < 4096; ++i, ++position) if (index(position) != position % capacity) return false;
    position = 0x1378abc9def01234ULL;
    for (unsigned i = 0; i < 4096; ++i) {
      position = position * 6364136223846793005ULL + 1442695040888963407ULL;
      if (index(position) != position % capacity) return false;
    }
  }
  return true;
}

struct PairChannels::Impl {
  struct Out { unsigned destination; Spsc* queue; };
  unsigned workers;
  std::vector<std::unique_ptr<Spsc>> queues;
  std::vector<std::vector<Out>> outgoing;
  std::vector<std::vector<Spsc*>> incoming;
  std::vector<Spsc*> direct;
  std::vector<LocalCursor> cursor;
  std::uint64_t bytes{};
  Impl(unsigned n, std::size_t cap, bool pad, std::span<const Edge> edges, bool index)
      : workers(n), outgoing(n), incoming(n), cursor(n) {
    if (index) direct.resize(static_cast<std::size_t>(n) * n);
    std::vector<Edge> sorted(edges.begin(), edges.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    for (const auto [source, destination] : sorted) {
      if (source >= n || destination >= n) throw std::invalid_argument("invalid edge");
      auto queue = std::make_unique<Spsc>(cap, pad);
      bytes += queue->bytes();
      outgoing[source].push_back({destination, queue.get()});
      incoming[destination].push_back(queue.get());
      if (index) direct[static_cast<std::size_t>(source) * n + destination] = queue.get();
      queues.push_back(std::move(queue));
    }
    bytes += sizeof(*this) + n * (2 * sizeof(std::vector<Spsc*>) + sizeof(LocalCursor));
    bytes += direct.capacity() * sizeof(Spsc*) + queues.capacity() * sizeof(std::unique_ptr<Spsc>);
    for (unsigned i = 0; i < n; ++i) bytes += outgoing[i].capacity() * sizeof(Out) + incoming[i].capacity() * sizeof(Spsc*);
  }
};
PairChannels::PairChannels(unsigned n, std::size_t cap, bool pad, std::span<const Edge> edges, bool index)
    : impl_(std::make_unique<Impl>(n, cap, pad, edges, index)) {}
PairChannels::~PairChannels() = default;
bool PairChannels::send(unsigned worker, const Packet& packet, Counters& c) noexcept {
  assert(packet.source == worker && packet.destination < impl_->workers);
  Spsc* queue{};
  if (!impl_->direct.empty()) queue = impl_->direct[static_cast<std::size_t>(worker) * impl_->workers + packet.destination];
  else {
    const auto& out = impl_->outgoing[worker];
    const auto it = std::lower_bound(out.begin(), out.end(), packet.destination,
        [](const Impl::Out& item, unsigned destination) { return item.destination < destination; });
    if (it != out.end() && it->destination == packet.destination) queue = it->queue;
  }
  return queue && queue->push(packet, c);
}
bool PairChannels::receive(unsigned worker, Packet& packet, Counters& c) noexcept {
  auto& incoming = impl_->incoming[worker];
  auto& cursor = impl_->cursor[worker].value;
  for (std::size_t count = 0; count < incoming.size(); ++count) {
    auto* queue = incoming[cursor];
    if (++cursor == incoming.size()) cursor = 0;
    if (queue->pop(packet, c)) return true;
  }
  return false;
}
std::uint64_t PairChannels::storage_bytes() const noexcept { return impl_->bytes; }
std::uint64_t PairChannels::queue_count() const noexcept { return impl_->queues.size(); }

struct InboxChannels::Impl { std::vector<std::unique_ptr<Mpsc>> queues; std::uint64_t bytes{}; };
InboxChannels::InboxChannels(unsigned n, std::size_t cap, bool pad) : impl_(std::make_unique<Impl>()) {
  for (unsigned i = 0; i < n; ++i) {
    auto queue = std::make_unique<Mpsc>(cap, pad); impl_->bytes += queue->bytes();
    impl_->queues.push_back(std::move(queue));
  }
  impl_->bytes += sizeof(Impl) + impl_->queues.capacity() * sizeof(std::unique_ptr<Mpsc>);
}
InboxChannels::~InboxChannels() = default;
bool InboxChannels::send(unsigned worker, const Packet& packet, Counters& c) noexcept {
  assert(packet.source == worker); return impl_->queues[packet.destination]->push(packet, c);
}
bool InboxChannels::receive(unsigned worker, Packet& packet, Counters& c) noexcept { return impl_->queues[worker]->pop(packet, c); }
std::uint64_t InboxChannels::storage_bytes() const noexcept { return impl_->bytes; }
std::uint64_t InboxChannels::queue_count() const noexcept { return impl_->queues.size(); }

struct GroupChannels::Impl {
  struct Input { Spsc* queue{}; Packet pending; bool held{}; };
  unsigned workers, size, groups;
  bool direct;
  std::vector<std::unique_ptr<Spsc>> ingress, egress, between;
  std::unique_ptr<PairChannels> local;
  std::vector<std::vector<Input>> inputs;
  std::vector<LocalCursor> cursor, receive_turn;
  std::vector<unsigned> worker_group, coordinator_group;
  std::uint64_t bytes{};
  Impl(unsigned n, unsigned group_size, std::size_t cap, bool pad, bool direct_inside)
      : workers(n), size(group_size), groups((n + group_size - 1) / group_size), direct(direct_inside),
        between(static_cast<std::size_t>(groups) * groups), inputs(groups), cursor(groups), receive_turn(n),
        worker_group(n), coordinator_group(n, std::numeric_limits<unsigned>::max()) {
    for (unsigned i = 0; i < n; ++i) {
      worker_group[i] = i / size;
      if (i % size == 0) coordinator_group[i] = worker_group[i];
      ingress.push_back(std::make_unique<Spsc>(cap, pad)); egress.push_back(std::make_unique<Spsc>(cap, pad));
      bytes += ingress.back()->bytes() + egress.back()->bytes();
      inputs[i / size].push_back({ingress.back().get(), {}, false});
    }
    for (unsigned source = 0; source < groups; ++source) for (unsigned destination = 0; destination < groups; ++destination) {
      if (source == destination) continue;
      auto& queue = between[static_cast<std::size_t>(source) * groups + destination];
      queue = std::make_unique<Spsc>(cap, pad); bytes += queue->bytes();
      inputs[destination].push_back({queue.get(), {}, false});
    }
    if (direct) {
      std::vector<Edge> edges;
      for (unsigned source = 0; source < n; ++source) for (unsigned destination = source / size * size;
          destination < std::min(n, (source / size + 1) * size); ++destination) edges.emplace_back(source, destination);
      local = std::make_unique<PairChannels>(n, cap, pad, edges, false); bytes += local->storage_bytes();
    }
    bytes += sizeof(*this) + (ingress.capacity() + egress.capacity() + between.capacity()) * sizeof(std::unique_ptr<Spsc>);
    bytes += inputs.capacity() * sizeof(std::vector<Input>) + (cursor.capacity() + receive_turn.capacity()) * sizeof(LocalCursor);
    bytes += (worker_group.capacity() + coordinator_group.capacity()) * sizeof(unsigned);
    for (const auto& input : inputs) bytes += input.capacity() * sizeof(Input);
  }
};
GroupChannels::GroupChannels(unsigned n, unsigned group_size, std::size_t cap, bool pad, bool direct)
    : impl_(std::make_unique<Impl>(n, group_size, cap, pad, direct)) {}
GroupChannels::~GroupChannels() = default;
bool GroupChannels::send(unsigned worker, const Packet& packet, Counters& c) noexcept {
  assert(packet.source == worker);
  if (impl_->direct && impl_->worker_group[worker] == impl_->worker_group[packet.destination]) return impl_->local->send(worker, packet, c);
  return impl_->ingress[worker]->push(packet, c);
}
bool GroupChannels::receive(unsigned worker, Packet& packet, Counters& c) noexcept {
  if (!impl_->direct) return impl_->egress[worker]->pop(packet, c);
  const bool local_first = impl_->receive_turn[worker].value != 0; impl_->receive_turn[worker].value = !local_first;
  if (local_first) return impl_->local->receive(worker, packet, c) || impl_->egress[worker]->pop(packet, c);
  return impl_->egress[worker]->pop(packet, c) || impl_->local->receive(worker, packet, c);
}
void GroupChannels::route(unsigned worker, std::uint64_t budget, Counters& c) noexcept {
  const auto group = impl_->coordinator_group[worker];
  if (group == std::numeric_limits<unsigned>::max()) return;
  auto& input = impl_->inputs[group]; auto& cursor = impl_->cursor[group].value;
  for (std::uint64_t count = 0; count < budget; ++count) {
    auto& item = input[cursor];
    if (++cursor == input.size()) cursor = 0;
    if (!item.held) item.held = item.queue->pop(item.pending, c);
    if (!item.held) continue;
    const auto destination_group = impl_->worker_group[item.pending.destination];
    auto* out = destination_group == group ? impl_->egress[item.pending.destination].get()
        : impl_->between[static_cast<std::size_t>(group) * impl_->groups + destination_group].get();
    if (out->push(item.pending, c)) { item.held = false; ++c.forwarded; }
    else ++c.blocked_routes;
  }
}
bool GroupChannels::coordinator(unsigned worker) const noexcept { return impl_->coordinator_group[worker] != std::numeric_limits<unsigned>::max(); }
std::uint64_t GroupChannels::storage_bytes() const noexcept { return impl_->bytes; }
std::uint64_t GroupChannels::queue_count() const noexcept {
  return impl_->workers * 2 + impl_->groups * (impl_->groups - 1) + (impl_->local ? impl_->local->queue_count() : 0);
}

Channels::Channels(Variant variant, unsigned n, unsigned group_size, std::size_t cap, bool pad, std::span<const Edge> edges) {
  if (!n || !group_size) throw std::invalid_argument("zero workers/group size");
  if (variant == Variant::inbox) inboxes_ = std::make_unique<InboxChannels>(n, cap, pad);
  else if (variant == Variant::group_direct || variant == Variant::group_routed)
    groups_ = std::make_unique<GroupChannels>(n, group_size, cap, pad, variant == Variant::group_direct);
  else {
    std::vector<Edge> dense;
    if (variant == Variant::dense) {
      for (unsigned source = 0; source < n; ++source) for (unsigned destination = 0; destination < n; ++destination) dense.emplace_back(source, destination);
      edges = dense;
    }
    pairs_ = std::make_unique<PairChannels>(n, cap, pad, edges, variant == Variant::dense);
  }
}
bool Channels::send(unsigned worker, const Packet& packet, Counters& c) noexcept {
  if (pairs_) return pairs_->send(worker, packet, c);
  if (inboxes_) return inboxes_->send(worker, packet, c);
  return groups_->send(worker, packet, c);
}
bool Channels::receive(unsigned worker, Packet& packet, Counters& c) noexcept {
  if (pairs_) return pairs_->receive(worker, packet, c);
  if (inboxes_) return inboxes_->receive(worker, packet, c);
  return groups_->receive(worker, packet, c);
}
void Channels::route(unsigned worker, std::uint64_t budget, Counters& c) noexcept { if (groups_) groups_->route(worker, budget, c); }
bool Channels::coordinator(unsigned worker) const noexcept { return groups_ && groups_->coordinator(worker); }
std::uint64_t Channels::storage_bytes() const noexcept {
  return pairs_ ? pairs_->storage_bytes() : inboxes_ ? inboxes_->storage_bytes() : groups_->storage_bytes();
}
std::uint64_t Channels::queue_count() const noexcept {
  return pairs_ ? pairs_->queue_count() : inboxes_ ? inboxes_->queue_count() : groups_->queue_count();
}
} // namespace loom_spike::messaging
