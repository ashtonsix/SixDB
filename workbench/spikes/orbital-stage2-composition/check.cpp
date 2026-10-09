#include "caller.h"
#include <orbital/allocation.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  namespace allocation = orbital::allocation;
  namespace worker = orbital::worker;
  auto arena = allocation::open_arena(64 * 1024 * 1024); assert(arena);
  worker::Lane lane; assert(worker::adopt(lane));
  auto pool = allocation::add_pool(**arena, lane); assert(pool);

  stage2_composition::histories(**pool);

  // Every selected descriptor, pending request and fixture owner must have
  // released its actual storage before the owning Pool can finish closing.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (allocation::close(**pool).kind() != orbital::Progress::Kind::done) {
    assert(std::chrono::steady_clock::now() < deadline);
    worker::poll(lane, 1024);
    std::this_thread::yield();
  }
  assert((*arena)->charged_bytes.load() == 0);
  assert(allocation::remove_pool(**pool));
  worker::relinquish(lane);
  std::cout << "composed selected access and SeriesPack/TuplePack histories pass\n";
}
