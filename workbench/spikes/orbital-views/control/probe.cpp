#include "mechanisms.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <thread>

namespace {
using Words = std::array<std::uint64_t, 8>;
struct Version { std::uint64_t epoch; Words bytes; std::uint64_t sum; bool summary_valid; };
Version expected(std::uint64_t epoch) {
  Version v{epoch, {}, 0, true};
  for (unsigned i = 0; i < 8; ++i) { v.bytes[i] = epoch * 13 + i * 7; v.sum += v.bytes[i]; }
  return v;
}
void verify(const Version& v) {
  const auto oracle = expected(v.epoch);
  assert(v.bytes == oracle.bytes && v.sum == oracle.sum && v.summary_valid);
}

void prefix_and_publication() {
  // Separate atomics make the forbidden state observable even with no data race.
  std::atomic<std::uint64_t> bad_frontier{0};
  auto base = expected(0), next = expected(1);
  std::atomic<const Version*> bad_head{&base};
  bad_frontier.store(1, std::memory_order_release);
  assert(bad_head.load(std::memory_order_acquire)->epoch < bad_frontier.load());
  bad_head.store(&next, std::memory_order_release);
  std::atomic<std::uint64_t> bad_sum{base.sum};
  std::atomic<bool> bad_valid{true};
  assert(bad_valid.load() && bad_sum.load() != bad_head.load()->sum);
  // Validity from an older summary cannot certify newly installed bytes.

  constexpr std::size_t count = 1025;
  std::vector<Version> versions;
  versions.reserve(count + 1);
  for (std::size_t e = 0; e <= count; ++e) versions.push_back(expected(e));
  auto installed = std::make_unique<std::atomic<const Version*>[]>(count + 1);
  for (std::size_t e = 0; e <= count; ++e) installed[e].store(nullptr);
  installed[0].store(&versions[0]);
  control::Prefix prefix(count);
  std::array<std::atomic<const Version*>, 2> local_heads;
  for (auto& head : local_heads) head.store(&versions[0]);
  std::atomic<unsigned> done{0};
  auto writer = [&](unsigned parity) {
    for (std::size_t e = parity + 1; e <= count; e += 2) {
      installed[e].store(&versions[e], std::memory_order_release);
      local_heads[parity].store(&versions[e], std::memory_order_release);
      prefix.complete(e);
    }
    done.fetch_add(1, std::memory_order_release);
  };
  std::thread a(writer, 0), b(writer, 1);
  std::size_t seen = 0;
  do {
    const auto cut = prefix.load();
    assert(cut >= seen); seen = cut;
    const auto* version = installed[cut].load(std::memory_order_acquire);
    assert(version && version->epoch == cut); verify(*version);
    for (auto& head : local_heads) verify(*head.load(std::memory_order_acquire));
  } while (done.load(std::memory_order_acquire) != 2);
  a.join(); b.join();
  assert(prefix.load() == count);
  for (std::size_t e = 1; e <= count; ++e) verify(*installed[e].load());

  // Later local publication is usable locally, but cannot leak into a cut at 0.
  control::Prefix held(2);
  auto object_b_before = expected(0), object_b_after = expected(2);
  const Version* object_b_latest = &object_b_after;
  held.complete(2);
  assert(object_b_latest->epoch == 2 && held.load() == 0);
  const auto* at_cut = held.load() < 2 ? &object_b_before : object_b_latest;
  assert(at_cut->epoch == 0);
  held.complete(1);
  assert(held.load() == 2);
  // A failed operation leaves its prefix hole until a terminal outcome is supplied.
  control::Prefix failed(3);
  failed.complete(1); failed.complete(3);
  assert(failed.load() == 1);
}

void blocked_owner() {
  for (auto policy : {control::Owners::global, control::Owners::group, control::Owners::object}) {
    std::mutex global, owner_a, owner_b;
    auto& first = policy == control::Owners::global ? global : owner_a;
    auto& second = policy == control::Owners::global ? global : owner_b;
    std::unique_lock blocked(first);
    control::Prefix prefix(2);
    std::atomic<std::uint64_t> a{0}, b{0};
    auto probe = std::async(std::launch::async, [&] {
      std::unique_lock other(second, std::try_to_lock);
      if (!other.owns_lock()) return false;
      b.store(2, std::memory_order_release); prefix.complete(2); return true;
    });
    const bool progressed = probe.get();
    assert(progressed == (policy != control::Owners::global));
    assert(prefix.load() == 0);
    a.store(1, std::memory_order_release); prefix.complete(1);
    blocked.unlock();
    if (!progressed) { std::lock_guard hold(second); b.store(2); prefix.complete(2); }
    assert(a.load() == 1 && b.load() == 2 && prefix.load() == 2);
  }
}

void overlapping_stores() {
  // Two logical 16-bit fields have disjoint locks but share a native 32-bit store.
  // Atomics keep the deliberately unsafe example defined; it still loses data.
  std::atomic<std::uint32_t> word{0};
  const auto a = word.load(), b = word.load();
  word.store((a & 0xffff0000U) | 0x1234U);
  word.store((b & 0x0000ffffU) | 0x56780000U);
  assert(word.load() == 0x56780000U);
  word.store(0);
  auto set = [&](std::uint32_t mask, std::uint32_t value) {
    auto old = word.load(std::memory_order_relaxed);
    while (!word.compare_exchange_weak(old, (old & ~mask) | value, std::memory_order_relaxed)) {}
  };
  std::thread low([&] { for (unsigned i = 0; i < 1000; ++i) set(0xffffU, 0x1234U); });
  std::thread high([&] { for (unsigned i = 0; i < 1000; ++i) set(0xffff0000U, 0x56780000U); });
  low.join(); high.join(); assert(word.load() == 0x56781234U);
  // A page-level writable grant cannot establish this physical merge obligation.
}

enum class Status { idle, pending, ready, failed };
struct Source { std::uint64_t identity, revision, physical_slot; Words bytes; };
struct Slot {
  std::uint64_t generation{};
  Status status{Status::idle};
  std::shared_ptr<const Words> ready;
};
struct Subscriber { std::weak_ptr<Slot> slot; std::uint64_t generation; };
struct Transfer {
  std::shared_ptr<const Source> source;
  std::shared_ptr<Words> destination = std::make_shared<Words>();
  std::vector<Subscriber> subscribers;
  bool finished{};
};
class Pending {
 public:
  std::shared_ptr<Transfer> request(const std::shared_ptr<Slot>& slot,
                                  const std::shared_ptr<const Source>& source) {
    const auto key = std::pair{source->identity, source->revision};
    auto transfer = jobs_[key].lock();
    if (!transfer || transfer->finished) {
      transfer = std::make_shared<Transfer>(); transfer->source = source;
      jobs_[key] = transfer; ++submitted;
    } else { assert(transfer->source->bytes == source->bytes); ++coalesced; }
    ++slot->generation; slot->status = Status::pending; slot->ready.reset();
    transfer->subscribers.push_back({slot, slot->generation});
    return transfer;
  }
  void cancel(Slot& slot) { ++slot.generation; slot.status = Status::idle; slot.ready.reset(); }
  void finish(const std::shared_ptr<Transfer>& transfer, unsigned words, bool success) {
    assert(!transfer->finished && words <= 8);
    std::copy_n(transfer->source->bytes.begin(), words, transfer->destination->begin());
    transfer->finished = true;
    for (auto subscriber : transfer->subscribers) if (auto slot = subscriber.slot.lock()) {
      if (slot->generation != subscriber.generation || slot->status != Status::pending) { ++obsolete; continue; }
      if (success && words == 8) { slot->ready = transfer->destination; slot->status = Status::ready; }
      else { slot->status = Status::failed; ++failed; }
    }
  }
  std::uint64_t submitted{}, coalesced{}, obsolete{}, failed{};
 private:
  std::map<std::pair<std::uint64_t, std::uint64_t>, std::weak_ptr<Transfer>> jobs_;
};

void source_lifecycle() {
  Pending pending;
  auto old_source = std::make_shared<Source>(Source{41, 1, 0x1000, expected(1).bytes});
  auto new_source = std::make_shared<Source>(Source{42, 1, 0x1000, expected(2).bytes});
  assert(old_source->physical_slot == new_source->physical_slot);
  auto cancelled = std::make_shared<Slot>(), live = std::make_shared<Slot>();
  auto old = pending.request(cancelled, old_source);
  auto duplicate = pending.request(live, old_source);
  assert(old == duplicate && !live->ready && pending.submitted == 1);
  pending.cancel(*cancelled);
  pending.finish(old, 8, true);
  assert(cancelled->status == Status::idle && !cancelled->ready);
  assert(live->status == Status::ready && *live->ready == expected(1).bytes);

  auto replacement = pending.request(live, new_source);
  assert(replacement != old && !live->ready);
  pending.finish(replacement, 4, true);
  assert(live->status == Status::failed && !live->ready);
  auto retry = pending.request(live, new_source);
  auto late = pending.request(live, old_source);
  pending.finish(retry, 8, true);
  assert(live->status == Status::pending && !live->ready);
  pending.finish(late, 8, true);
  assert(*live->ready == expected(1).bytes);

  auto dead = std::make_shared<Slot>();
  auto backend = pending.request(dead, new_source);
  std::weak_ptr<Words> target = backend->destination;
  std::weak_ptr<const Source> source = new_source;
  pending.cancel(*dead); dead.reset(); new_source.reset(); replacement.reset(); retry.reset();
  assert(!target.expired() && !source.expired());
  pending.finish(backend, 8, false); backend.reset();
  assert(target.expired() && source.expired());
  assert(pending.submitted == 5 && pending.coalesced == 1 && pending.failed == 1);
  assert(pending.obsolete == 2);
}

void unchanged_epoch() {
  auto captured = std::make_shared<Version>(expected(7));
  auto cache = captured;
  const auto* address = cache.get();
  unsigned rebinds = 0;
  for (unsigned shard_epoch = 8; shard_epoch < 20; ++shard_epoch) {
    (void)shard_epoch;
    const auto object_revision = 7U;
    if (cache->epoch != object_revision) { cache = captured; ++rebinds; }
    verify(*cache); assert(cache.get() == address);
  }
  assert(rebinds == 0);
}
}  // namespace

int main() {
  prefix_and_publication(); blocked_owner(); overlapping_stores(); source_lifecycle(); unchanged_epoch();
  auto cpus = control::available_cpus();
  for (unsigned threads : {1U, 2U, 4U}) if (threads <= cpus.size())
    for (auto policy : {control::Owners::global, control::Owners::group, control::Owners::object})
      for (bool cut : {false, true}) control::ownership(policy, threads, true, cut, 1000, cpus);
  std::vector<std::size_t> selected{0, 63, 64, 127, 255};
  for (auto policy : {control::Metadata::ordered, control::Metadata::bitmap})
    for (bool cached : {false, true}) control::readiness(policy, cached, 256, selected, 17);
  for (auto policy : {control::Lifetime::window, control::Lifetime::page, control::Lifetime::store})
    control::lifetime(policy, 256, selected, 17);
  for (auto policy : {control::History::ordered, control::History::contiguous, control::History::captured})
    control::history(policy, 129, 509);
  for (bool compact : {false, true}) control::combined(compact, 256, selected, 17);
  std::cout << "{\"status\":\"passed\",\"cases\":\"prefix,publication,summary-validity,blocked-owner,packed-stores,source-ABA,coalescing,cancellation,partial,unchanged,metadata,lifetime,ownership,combined\"}\n";
}
