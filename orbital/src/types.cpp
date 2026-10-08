#include <orbital/types.h>
#include <orbital/worker.h>
#include <cassert>
#include <limits>

namespace orbital {
namespace {
void notify(Signal& source) noexcept {
  while (source.waiters) {
    auto* link = source.waiters;
    source.waiters = link->next;
    link->source = nullptr;
    link->next = nullptr;
    if (link->notify) link->notify(link->waiter);
  }
}
}

void pulse(Signal& source) noexcept {
  assert(source.lane && worker::on_lane(*source.lane));
  assert(source.generation != std::numeric_limits<std::uint64_t>::max());
  ++source.generation;
  notify(source);
}

void complete(Signal& source, Result<void> result) noexcept {
  assert(source.state == Readiness::idle || source.state == Readiness::pending);
  source.state = result ? Readiness::ready : Readiness::failed;
  if (!result) source.failure = result.error();
  pulse(source);
}
}
