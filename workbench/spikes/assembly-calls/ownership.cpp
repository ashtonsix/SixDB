// Real Loom ownership seams for ABI inspection, not a performance benchmark.
#include <loom/worker.h>
#include <orbital/representation.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

namespace assembly_calls::ownership {
namespace oa = orbital::allocation;

[[gnu::noinline]] orbital::Result<oa::Allocation> forward_allocate(
    oa::Pool& pool, const oa::AllocationRequest& request) {
  return oa::allocate(pool, request);
}

// This destination may already own storage, so move-assignment must retire it.
[[gnu::noinline]] bool acquire_into(loom::Worker& worker,
    const oa::AllocationRequest& request, oa::Allocation& destination) {
  auto result = loom::allocate(worker, request);
  if (!result) return false;
  destination = std::move(*result);
  return true;
}

struct AllocationFrame {
  orbital::Result<oa::Allocation> allocation;
  AllocationFrame(loom::Worker& worker, const oa::AllocationRequest& request)
      : allocation(loom::allocate(worker, request)) {}
};

// Caller storage is aligned/unoccupied. The whole Frame stays live until destroy.
[[gnu::noinline]] AllocationFrame* initialize_frame(void* storage, loom::Worker& worker,
    const oa::AllocationRequest& request) {
  return ::new(storage) AllocationFrame(worker, request);
}
[[gnu::noinline]] void destroy_frame(AllocationFrame* frame) noexcept {
  std::destroy_at(frame);
}

[[gnu::noinline]] orbital::MutableBytes borrow_bytes(const oa::Allocation& value) noexcept {
  return loom::bytes(value);
}
} // namespace assembly_calls::ownership

int main() {
  namespace p = assembly_calls::ownership;
  namespace oa = orbital::allocation;
  loom::Worker worker;
  assert(loom::adopt(worker));
  const oa::AllocationRequest request{.bytes=64, .alignment=64};
  alignas(p::AllocationFrame) std::byte storage[sizeof(p::AllocationFrame)];

  // Error is a constructed result too; destruction must not invent an owner.
  auto* frame = p::initialize_frame(storage, worker, request);
  assert(!frame->allocation && frame->allocation.error() == orbital::Error::unavailable);
  p::destroy_frame(frame);

  auto opened = oa::open_arena(8 << 20);
  assert(opened);
  auto arena = std::move(*opened);
  assert(loom::configure(worker, arena, 0));
  {
    oa::Allocation destination;
    assert(p::acquire_into(worker, request, destination));
    std::fill(p::borrow_bytes(destination).begin(), p::borrow_bytes(destination).end(), std::byte{17});
    frame = p::initialize_frame(storage, worker, request);
    assert(frame->allocation);
    auto direct = p::borrow_bytes(*frame->allocation);
    assert(direct.size() == 64 && direct.data() != destination.address);
    std::fill(direct.begin(), direct.end(), std::byte{29});
    assert(std::all_of(p::borrow_bytes(destination).begin(), p::borrow_bytes(destination).end(),
                      [](auto b) { return b == std::byte{17}; }));
    p::destroy_frame(frame);
    // Assigning into an occupied owner has extra work a raw output store omits.
    assert(p::acquire_into(worker, request, destination));
    assert(destination && destination.size == 64);
    // Reuse storage only after its previous object's lifetime has ended.
    frame = p::initialize_frame(storage, worker, {.bytes=0});
    assert(!frame->allocation && frame->allocation.error() == orbital::Error::invalid_argument);
    p::destroy_frame(frame);
    auto forwarded = p::forward_allocate(*worker.memory, request);
    assert(forwarded && p::borrow_bytes(*forwarded).size() == 64);
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    const auto progress = loom::shutdown(worker);
    assert(progress.kind() != orbital::Progress::Kind::failed);
    if (progress.kind() == orbital::Progress::Kind::done) break;
    assert(std::chrono::steady_clock::now() < deadline);
    loom::poll(worker, {.messages=64, .tasks=0});
    std::this_thread::yield();
  }
  assert(!worker.memory && arena->charged_bytes.load() == 0);
  loom::relinquish(worker);
  std::printf("Allocation=%zu Result<Allocation>=%zu Frame=%zu PartSource=%zu Result<PartSource>=%zu\n",
      sizeof(oa::Allocation), sizeof(orbital::Result<oa::Allocation>), sizeof(p::AllocationFrame),
      sizeof(orbital::PartSource), sizeof(orbital::Result<orbital::PartSource>));
  std::printf("owning-result construction, failure, replacement and cleanup passed\n");
}
