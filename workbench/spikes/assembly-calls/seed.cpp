#include "calls.h"
#include "table.h"
#include <loom/hints.h>
#include <limits>

// Emit this TU with compile_probe.py --assembly, edit it, then substitute the .s.
// Clang chooses the actual mangled symbol and std::expected ABI for the call.
extern "C" std::uint64_t prototype_seed(std::size_t bytes, std::size_t minimum,
                                       unsigned pattern, std::uint64_t salt) noexcept {
  const auto result = loom::bind_demand({loom::MemoryPattern(pattern), bytes}, minimum);
  const std::uint64_t code = result ? result->code() : std::numeric_limits<std::uint64_t>::max();
  return code ^ salt;
}

// A template has no separately callable symbol until instantiated. This seam is
// its actual compiled body, not another wrapper around an outlined find().
extern "C" std::uint64_t prototype_ikea_contains(const void* table, std::uint64_t key) noexcept {
  return static_cast<const PrototypeTable*>(table)->find(key, [](auto) {});
}
