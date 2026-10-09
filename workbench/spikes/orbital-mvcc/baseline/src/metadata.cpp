#include "internal.h"

namespace orbital::object::detail {

void* Domain::do_allocate(std::size_t bytes, std::size_t alignment) {
  owner();
  if (bytes > limit - charged) throw std::bad_alloc();
  auto* memory = std::pmr::new_delete_resource()->allocate(bytes, alignment);
  charged += bytes;
  peak = std::max(peak, charged);
  return memory;
}

void Domain::do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) {
  owner();
  assert(charged >= bytes + sizeof(Domain));
  std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  charged -= bytes;
}

void retain_domain(Domain& d) noexcept { d.owner(); ++d.references; }

void release_domain(Domain& d) noexcept {
  d.owner();
  if (--d.references) return;
  assert(!d.first && !d.queued && d.charged == sizeof(Domain));
  delete &d;
}

void retain_base(Base& base) noexcept {
  base.domain->owner();
  assert(!base.retirement_next);
  ++base.references;
}

void release_base(Base& base) noexcept {
  auto& d = *base.domain;
  d.owner();
  assert(base.references);
  if (--base.references) return;
  if (d.detached) { base.destroy(&base); return; }
  if (d.last) d.last->retirement_next = &base;
  else d.first = &base;
  d.last = &base;
  ++d.queued;
}

std::size_t drain_domain(Domain& d, std::size_t budget) noexcept {
  d.owner();
  std::size_t done = 0;
  while (done != budget && d.first) {
    auto* record = d.first;
    d.first = record->retirement_next;
    if (!d.first) d.last = nullptr;
    record->retirement_next = nullptr;
    --d.queued;
    record->destroy(record);
    ++done;
  }
  return done;
}

} // namespace orbital::object::detail
