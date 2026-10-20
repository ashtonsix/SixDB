#include "calls.h"
#include <loom/hints.h>
#include <limits>

extern "C" std::uint64_t prototype_bind_demand(std::size_t bytes, std::size_t minimum,
                                               unsigned pattern) noexcept {
  const auto result = loom::bind_demand({loom::MemoryPattern(pattern), bytes}, minimum);
  return result ? result->code() : std::numeric_limits<std::uint64_t>::max();
}
