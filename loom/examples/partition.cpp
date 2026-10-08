#include <loom/ring.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {
namespace alloc = orbital::allocation;
using orbital::Progress;
using Clock = std::chrono::steady_clock;
constexpr std::size_t input_count = 64, output_bytes = 5 << 20;
constexpr std::size_t output_count = output_bytes / sizeof(std::uint64_t), grain = 63;
constexpr std::uint64_t identity = 41, arena_limit = 8 << 20;

void check(bool condition, const char* explanation) noexcept {
  if (condition) return;
  std::fprintf(stderr, "loom_example_partition: %s\n", explanation);
  std::abort();
}

template<class T> T take(orbital::Result<T> result, const char* explanation) {
  check(result.has_value(), explanation);
  return std::move(*result);
}

constexpr std::uint64_t source(std::uint64_t position) {
  return (position * 37 + 19) & 65535;
}

constexpr std::uint64_t expected(std::uint64_t position) {
  return (source(position % input_count) + identity) * 3;
}

struct NativeJob {
  const std::uint64_t* input{};
  std::uint64_t* output{};
  std::uint64_t steps{}, sum{};
  bool complete{};
};

using Ring = loom::Ring<std::uint64_t, std::uint64_t, std::uint64_t, NativeJob*>;

// The operation has already captured every address and extent. Native callbacks
// do bounded ordinary work, with no allocation, wait, clock or shared counter.
LOOM_NATIVE_CC void background(Ring* ring, Ring::Slot* slot, std::uint64_t cursor,
    std::uint64_t left, std::uint64_t sum, NativeJob* job) {
  sum += job->input[cursor % input_count];
  ++cursor; --left; ++job->steps; job->sum = sum;
  if (!left) { job->complete = true; ring->complete(*slot); }
  [[clang::musttail]] return Ring::yield(ring, slot, cursor, left, sum, job);
}

LOOM_NATIVE_CC void produce(Ring* ring, Ring::Slot* slot, std::uint64_t cursor,
    std::uint64_t left, std::uint64_t sum, NativeJob* job) {
  const auto count = std::min<std::uint64_t>(left, grain);
  for (std::uint64_t i = 0; i < count; ++i, ++cursor) {
    const auto value = (job->input[cursor % input_count] + identity) * 3;
    job->output[cursor] = value;
    sum += value;
  }
  left -= count; ++job->steps; job->sum = sum;
  if (!left) { job->complete = true; ring->complete(*slot); }
  [[clang::musttail]] return Ring::yield(ring, slot, cursor, left, sum, job);
}

LOOM_NATIVE_CC void consume(Ring* ring, Ring::Slot* slot, std::uint64_t cursor,
    std::uint64_t left, std::uint64_t sum, NativeJob* job) {
  const auto count = std::min<std::uint64_t>(left, grain);
  for (std::uint64_t i = 0; i < count; ++i, ++cursor) {
    const auto value = job->input[cursor];
    check(value == expected(cursor), "actual output differs from independent oracle");
    sum += value;
  }
  left -= count; ++job->steps; job->sum = sum;
  if (!left) { job->complete = true; ring->complete(*slot); }
  [[clang::musttail]] return Ring::yield(ring, slot, cursor, left, sum, job);
}

struct Owner;
struct Shared;
// Fixed independent records live through both owner joins. The accepted route
// reference and each Message retain the record independently of its caller Task.
// This private protocol needs one credit and three pre-reserved queue records.
struct Partition {
  std::atomic<unsigned> references{1};
  Shared* shared{};
  Owner *caller{}, *receiver{};
  std::uint64_t id{identity};
  alloc::Hold input, output;
  NativeJob producing, consuming;
  orbital::Signal done;
  orbital::worker::Message dispatch, finished, acknowledged;
  loom::Task caller_task, build_task, consume_task;
  bool output_reserved{}, completion_seen{}, ack_sent{}, ack_seen{};
  std::uint64_t allocation_attempts{}, capacity_refusals{};
};

struct Owner {
  loom::Worker worker;
  Ring ring;
  alloc::Allocation input;
  alloc::Hold input_use;
  NativeJob useful;
  loom::Task background_task;
  bool sender{};
  std::uint64_t visits{};
};

struct Shared {
  orbital::Ref<alloc::Arena> arena;
  Owner first, second;
  Partition work, refused;
  std::atomic<bool> first_ready{}, second_ready{}, blocked{}, detached{};
  std::atomic<std::uint64_t> second_useful{};
  std::atomic<unsigned> credit{1};
  Clock::time_point deadline{Clock::now() + std::chrono::seconds(10)};
  std::uint64_t idle_inventory{}, inventory_mapped{}, inventory_charged{}, refused_offers{};
  std::uint64_t useful_second_when_blocked{}, useful_first_at_pressure{}, useful_second_at_pressure{}, trim_pages{};
};

void retain(orbital::worker::Message& message) noexcept {
  auto& work = *static_cast<Partition*>(message.context);
  check(work.references.fetch_add(1, std::memory_order_relaxed) >= 1, "retaining retired record");
}

void release(orbital::worker::Message& message) noexcept {
  auto& work = *static_cast<Partition*>(message.context);
  check(work.references.fetch_sub(1, std::memory_order_acq_rel) > 1, "Message dropped base record");
}

void check_lane(Partition& work, orbital::worker::Lane& lane, Owner& owner) noexcept {
  check(work.id == identity && &owner.worker.lane == &lane && orbital::worker::on_lane(lane),
        "partition identity or assigned owner changed");
}

Progress build(loom::Task& task) noexcept {
  auto& work = *static_cast<Partition*>(task.context);
  auto& owner = *work.receiver;
  if (!work.output_reserved) {
    const auto observed = owner.worker.memory->capacity_changed.generation;
    ++work.allocation_attempts;
    auto output = loom::allocate(owner.worker, {.bytes = output_bytes, .alignment = 64});
    if (!output) {
      check(output.error() == orbital::Error::capacity, "output failed for an unexpected reason");
      ++work.capacity_refusals;
      if (work.capacity_refusals == 1) work.shared->useful_second_when_blocked = owner.useful.steps;
      work.shared->blocked.store(true, std::memory_order_release);
      return Progress::wait(owner.worker.memory->capacity_changed, observed);
    }
    // No producer callback is admitted until its complete output window exists.
    work.output = take(loom::hold(*output, {0, output_bytes}), "capture output window");
    loom::release(*output);
    work.producing.input = reinterpret_cast<const std::uint64_t*>(loom::bytes(work.input).data());
    work.producing.output = reinterpret_cast<std::uint64_t*>(loom::bytes(work.output).data());
    check(owner.ring.push(produce, {0, output_count, 0, &work.producing}), "producer Ring admission");
    work.output_reserved = true;
  }
  if (!work.producing.complete) return Progress::yield();
  orbital::worker::post(work.caller->worker.lane, work.finished);
  return Progress::done();
}

Progress consume_output(loom::Task& task) noexcept {
  auto& work = *static_cast<Partition*>(task.context);
  if (!work.consuming.complete) return Progress::yield();
  // The actual foreign read ended. Physical returns and ACK are independent
  // reserved Messages; the ACK never sits behind workload credit acquisition.
  loom::release(work.output);
  loom::release(work.input);
  work.ack_sent = true;
  orbital::worker::post(work.receiver->worker.lane, work.acknowledged);
  return Progress::done();
}

const orbital::worker::MessageOps dispatch_ops{
  retain,
  [](orbital::worker::Message& message, orbital::worker::Lane& lane) noexcept {
    auto& work = *static_cast<Partition*>(message.context);
    check_lane(work, lane, *work.receiver);
    check(loom::start(work.receiver->worker, work.build_task), "reserved receiver Task slot lost");
    loom::Task refused;
    refused.step = [](loom::Task&) noexcept { return Progress::done(); };
    check(!loom::start(work.receiver->worker, refused), "receiver work capacity was not full");
  }, release};

const orbital::worker::MessageOps finished_ops{
  retain,
  [](orbital::worker::Message& message, orbital::worker::Lane& lane) noexcept {
    auto& work = *static_cast<Partition*>(message.context);
    check_lane(work, lane, *work.caller);
    check(work.shared->detached.load(std::memory_order_acquire), "completion preceded required detach");
    check(work.producing.complete && work.output && !work.completion_seen, "invalid physical completion");
    work.completion_seen = true;
    orbital::complete(work.done, {});
    work.consuming.input = reinterpret_cast<const std::uint64_t*>(loom::bytes(work.output).data());
    check(work.caller->ring.push(consume, {0, output_count, 0, &work.consuming}), "consumer Ring admission");
    check(loom::start(work.caller->worker, work.consume_task), "detached caller did not release its Task slot");
  }, release};

const orbital::worker::MessageOps acknowledged_ops{
  retain,
  [](orbital::worker::Message& message, orbital::worker::Lane& lane) noexcept {
    auto& work = *static_cast<Partition*>(message.context);
    check_lane(work, lane, *work.receiver);
    check(work.ack_sent && work.consuming.complete && !work.output && !work.input && !work.ack_seen,
          "ACK preceded physical use or was duplicated");
    work.ack_seen = true;
  },
  [](orbital::worker::Message& message) noexcept {
    auto& work = *static_cast<Partition*>(message.context);
    release(message);
    // Return acceptance/credit only after the ACK's final queue touch.
    check(work.references.fetch_sub(1, std::memory_order_acq_rel) == 2, "accepted record has outstanding Messages");
    check(work.shared->credit.exchange(1, std::memory_order_release) == 0, "credit returned twice");
  }};

void prepare(Shared& shared, Partition& work) {
  work.shared = &shared; work.caller = &shared.first; work.receiver = &shared.second;
  work.dispatch.context = &work; work.dispatch.ops = &dispatch_ops;
  work.finished.context = &work; work.finished.ops = &finished_ops;
  work.acknowledged.context = &work; work.acknowledged.ops = &acknowledged_ops;
  work.caller_task.context = &work;
  work.caller_task.step = [](loom::Task& task) noexcept {
    auto& work = *static_cast<Partition*>(task.context);
    const auto observed = work.done.generation;
    return work.completion_seen ? Progress::done() : Progress::wait(work.done, observed);
  };
  work.build_task.context = &work; work.build_task.step = build;
  work.consume_task.context = &work; work.consume_task.step = consume_output;
}

bool offer(Shared& shared, Partition& work) {
  unsigned credit = 1;
  if (!shared.credit.compare_exchange_strong(credit, 0, std::memory_order_acq_rel)) return false;
  work.input = take(loom::hold(shared.first.input, {0, input_count * 8}), "capture partition input");
  work.references.fetch_add(1, std::memory_order_relaxed);
  orbital::worker::post(shared.second.worker.lane, work.dispatch);
  return true;
}

void deadline(Shared& shared) { check(Clock::now() < shared.deadline, "bounded progress deadline expired"); }

void configure(Shared& shared, Owner& owner) {
  check(loom::adopt(owner.worker).has_value(), "adopt owner thread");
  check(loom::configure(owner.worker, shared.arena, 2).has_value(), "configure shared-Arena Worker");
  check(owner.ring.configure(owner.worker, {8, 4}).has_value(), "configure actual owner Ring");
  owner.input = take(loom::allocate(owner.worker, {.bytes = input_count * 8, .alignment = 64}), "allocate native input");
  auto* values = reinterpret_cast<std::uint64_t*>(loom::bytes(owner.input).data());
  for (std::size_t i = 0; i < input_count; ++i) values[i] = source(i);
  owner.input_use = take(loom::hold(owner.input, {0, input_count * 8}), "capture background input");
  owner.useful.input = values;
  check(owner.ring.push(background, {0, 1ull << 40, 0, &owner.useful}), "background Ring admission");
  owner.background_task.context = &owner;
  owner.background_task.step = [](loom::Task& task) noexcept {
    auto& owner = *static_cast<Owner*>(task.context);
    owner.visits += owner.ring.spin({32});
    return Progress::yield();
  };
  check(loom::start(owner.worker, owner.background_task), "background Task admission");
}
void close(Shared& shared, Owner& owner) {
  loom::detach(owner.background_task);
  owner.ring.discard();
  check(owner.ring.close().has_value(), "close native owner Ring");
  loom::release(owner.input_use); loom::release(owner.input);
  for (;;) {
    deadline(shared);
    const auto progress = loom::shutdown(owner.worker);
    check(progress.kind() != Progress::Kind::failed, "owner shutdown failed");
    if (progress.kind() == Progress::Kind::done) break;
    if (!loom::poll(owner.worker, {8, 2})) std::this_thread::yield();
  }
  check(!orbital::worker::pending(owner.worker.lane), "owner left physical Messages undrained");
  loom::relinquish(owner.worker);
}

void first(Shared& shared) {
  auto& owner = shared.first;
  configure(shared, owner);
  shared.work.done.lane = &owner.worker.lane;
  shared.first_ready.store(true, std::memory_order_release);
  while (!shared.second_ready.load(std::memory_order_acquire)) { deadline(shared); std::this_thread::yield(); }

  // Real finite 3,072-byte homes, including initialized payload, occupy budget.
  // Their owner release leaves charged prepared inventory in this Pool.
  std::vector<alloc::Allocation> inventory;
  inventory.reserve(4096);
  const auto pressure_charge = arena_limit - output_bytes + (1 << 20);
  while (shared.arena->charged_bytes.load(std::memory_order_acquire) < pressure_charge) {
    deadline(shared);
    check(inventory.size() != 4096, "finite pressure inventory bound exhausted");
    auto memory = take(loom::allocate(owner.worker, {.bytes = 3072, .alignment = 16}), "prepare finite idle inventory");
    std::memset(loom::bytes(memory).data(), int(inventory.size() % 251), memory.size);
    inventory.push_back(std::move(memory));
  }
  shared.idle_inventory = inventory.size();
  for (auto& memory : inventory) loom::release(memory, {.temperature = alloc::Temperature::warm});
  shared.inventory_charged = shared.arena->charged_bytes.load(std::memory_order_acquire);
  shared.inventory_mapped = alloc::statistics(*owner.worker.memory).mapped_bytes;
  check(shared.inventory_charged > arena_limit - output_bytes, "idle inventory did not hold shared budget");
  check(offer(shared, shared.work), "first partition offer refused");
  const auto refused_refs = shared.refused.references.load();
  check(!offer(shared, shared.refused) && shared.refused.references.load() == refused_refs && !shared.refused.input,
        "full route changed refused partition ownership");
  ++shared.refused_offers;
  check(loom::start(owner.worker, shared.work.caller_task), "caller Task admission");
  bool trim_started = false;
  bool saw_pressure = false;
  std::uint64_t first_when_blocked = 0;
  while (shared.credit.load(std::memory_order_acquire) == 0) {
    deadline(shared);
    loom::poll(owner.worker, {8, 2});
    if (!saw_pressure && shared.blocked.load(std::memory_order_acquire)) {
      saw_pressure = true; first_when_blocked = owner.useful.steps;
    }
    if (!trim_started && saw_pressure && owner.useful.steps >= first_when_blocked + 128 &&
        shared.second_useful.load(std::memory_order_acquire) >= shared.useful_second_when_blocked + 128) {
      check(shared.work.caller_task.state == loom::TaskState::waiting, "caller did not park before detach");
      check(owner.worker.active_tasks == 2, "caller full work capacity changed under pressure");
      shared.useful_first_at_pressure = owner.useful.steps - first_when_blocked;
      shared.useful_second_at_pressure = shared.second_useful.load(std::memory_order_acquire) - shared.useful_second_when_blocked;
      loom::detach(shared.work.caller_task);
      shared.detached.store(true, std::memory_order_release);
      const auto progress = alloc::trim_cold(*owner.worker.memory, 0);
      check(progress.kind() == Progress::Kind::waiting, "idle finite trim did not start physical reclamation");
      trim_started = true;
    }
  }
  check(trim_started && shared.work.caller_task.state == loom::TaskState::detached,
        "partition completed without required pressure/detach history");
  check(shared.work.consume_task.state == loom::TaskState::done, "consumer Task did not retire");
  shared.trim_pages = alloc::statistics(*owner.worker.memory).trim_pages;
  check(shared.trim_pages != 0, "owner never reclaimed idle physical pages");
  close(shared, owner);
}

void second(Shared& shared) {
  auto& owner = shared.second;
  configure(shared, owner);
  shared.second_ready.store(true, std::memory_order_release);
  while (!shared.first_ready.load(std::memory_order_acquire)) { deadline(shared); std::this_thread::yield(); }
  for (;;) {
    deadline(shared);
    loom::poll(owner.worker, {8, 2});
    shared.second_useful.store(owner.useful.steps, std::memory_order_release);
    if (shared.work.ack_seen && shared.credit.load(std::memory_order_acquire) == 1) break;
  }
  check(shared.work.build_task.state == loom::TaskState::done, "producer Task did not retire");
  check(shared.work.capacity_refusals && shared.work.allocation_attempts > shared.work.capacity_refusals,
        "receiver did not refuse then retry actual allocation");
  close(shared, owner);
}

std::uint64_t background_expected(std::uint64_t steps) {
  std::uint64_t full = 0, tail = 0;
  for (std::uint64_t i = 0; i < input_count; ++i) full += source(i);
  for (std::uint64_t i = 0; i < steps % input_count; ++i) tail += source(i);
  return steps / input_count * full + tail;
}
} // namespace

int main() {
  Shared shared;
  shared.arena = take(alloc::open_arena(arena_limit), "open shared Arena");
  prepare(shared, shared.work); prepare(shared, shared.refused);
  std::thread sender([&] { first(shared); });
  std::thread receiver([&] { second(shared); });
  sender.join(); receiver.join();
  std::uint64_t wanted = 0;
  for (std::uint64_t i = 0; i < output_count; ++i) wanted += expected(i);
  check(shared.work.producing.sum == wanted && shared.work.consuming.sum == wanted,
        "producer/physical consumer sums differ from independent oracle");
  check(shared.first.useful.sum == background_expected(shared.first.useful.steps) &&
        shared.second.useful.sum == background_expected(shared.second.useful.steps), "background useful work oracle");
  check(shared.work.references.load() == 1 && shared.refused.references.load() == 1 && shared.credit.load() == 1,
        "accepted/refused record or route credit leaked");
  check(!shared.work.input && !shared.work.output && shared.work.ack_seen,
        "accepted physical uses did not finish/ACK");
  check(shared.arena->charged_bytes.load() == 0, "shared Arena retained closed-Worker charges");
  std::printf("loom_example_partition: accepted=1 refused=%llu output=%zu checksum=%llu "
      "allocation_attempts=%llu capacity_refusals=%llu idle_homes=%llu charged_before_trim=%llu "
      "mapped_before_trim=%llu trim_pages=%llu useful_while_full=%llu/%llu "
      "caller_detached=1 completion=1 ack=1 charged_after_drain=0\n",
      static_cast<unsigned long long>(shared.refused_offers), output_count,
      static_cast<unsigned long long>(wanted), static_cast<unsigned long long>(shared.work.allocation_attempts),
      static_cast<unsigned long long>(shared.work.capacity_refusals), static_cast<unsigned long long>(shared.idle_inventory),
      static_cast<unsigned long long>(shared.inventory_charged), static_cast<unsigned long long>(shared.inventory_mapped),
      static_cast<unsigned long long>(shared.trim_pages), static_cast<unsigned long long>(shared.useful_first_at_pressure),
      static_cast<unsigned long long>(shared.useful_second_at_pressure));
}
