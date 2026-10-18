#include <orbital/detail/object/representation_ops.h>
#include <algorithm>
#include <cassert>
namespace orbital::object_detail {
bool valid(Bytes input, std::size_t padding) {
  if (padding > SIZE_MAX - input.size()) return false;
  const auto extent = input.size() + padding;
  return !extent || (input.data() && extent <= UINTPTR_MAX - std::uintptr_t(input.data()));
}

bool SourceAccess::within(const PartSource& source, std::size_t padding) {
  const auto& old = source.retained;
  if (!valid(source.body, padding) || !valid(old.footprint_)) return false;
  const auto begin = std::uintptr_t(old.footprint_.data());
  const auto part = std::uintptr_t(source.body.data());
  const auto extent = old.footprint_.size();
  return part >= begin && part - begin <= extent && source.body.size() + padding <= extent - (part - begin);
}

void drop(orbital::allocation::Storage* owner) {
  orbital::allocation::Hold use;
  use.storage = owner;
  orbital::allocation::release(use);
}
Result<orbital::allocation::Storage*> pin(orbital::allocation::Storage* owner) {
  if (!owner) return static_cast<orbital::allocation::Storage*>(nullptr);
  // This local borrows its live physical token; duplication can refuse at capacity.
  orbital::allocation::Hold borrowed;
  borrowed.storage = owner;
  auto retained = orbital::allocation::retain(borrowed);
  borrowed.storage = nullptr;
  if (!retained) return std::unexpected(retained.error());
  return std::exchange(retained->storage, nullptr);
}

bool within(const orbital::allocation::Allocation& allocation, Bytes body, std::size_t padding) {
  if (!allocation || !valid(body, padding) || !valid(orbital::allocation::bytes(allocation), allocation.readable_padding)) return false;
  const auto begin = std::uintptr_t(allocation.address);
  const auto part = std::uintptr_t(body.data());
  const auto extent = allocation.size + allocation.readable_padding;
  return part >= begin && part - begin <= extent && body.size() + padding <= extent - (part - begin);
}

bool unique_allocations(std::span<const PartSource> bodies, std::span<orbital::allocation::Storage*> scratch) {
  const auto storage = [](const PartSource& body) { return body.allocation ? body.allocation->storage : nullptr; };
  if (bodies.size() <= 8) {
    for (std::size_t i = 0; i < bodies.size(); ++i)
      for (std::size_t j = 0; storage(bodies[i]) && j < i; ++j)
        if (storage(bodies[i]) == storage(bodies[j])) return false;
    return true;
  }
  assert(scratch.size() >= bodies.size());
  std::size_t used{};
  for (const auto& body : bodies) if (auto* owner = storage(body)) scratch[used++] = owner;
  auto owners = scratch.first(used);
  std::sort(owners.begin(), owners.end(), std::less<orbital::allocation::Storage*>{});
  return std::adjacent_find(owners.begin(), owners.end()) == owners.end();
}

} // namespace orbital::object_detail
