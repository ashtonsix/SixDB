// Finite Group selection, preparation and reclamation transactions.

#include "internal.h"
#include <algorithm>
#include <cstring>
#include <new>
#include <sys/mman.h>
#include <utility>

namespace orbital::allocation::detail {

void group_trim_run(Job& job) noexcept {
  auto& group = *static_cast<Group*>(job.context);
  if (group.retiring) {
    group.retired = group.geometry->retire().has_value();
    if (group.retired) group.geometry.reset();
    worker::post(*group.uses.domain.pool->lane, group.trimmed);
    return;
  }
  const auto membership = std::span<const std::uint64_t>(group.geometry->trim_membership());
  auto success = group.geometry->trim_success();
  std::size_t cursor = 0;
  PageSpan span;
  while (next_trim_span(membership, cursor, span)) {
    auto* address = group.geometry->page_address(span.first);
    const auto amount = group.geometry->native_page() * span.count;
    // No bound or pending Home overlaps a reserved trim page. Even failed protection
    // changes cannot invalidate readable contents: this was idle capacity.
    if (::madvise(address, amount, MADV_DONTNEED) == 0 &&
        ::mprotect(address, amount, PROT_NONE) == 0)
      set_trim_span(success, span, true);
  }
  worker::post(*group.uses.domain.pool->lane, group.trimmed);
}

void group_trim_handle(worker::Message& message, worker::Lane&) noexcept {
  auto& group = *static_cast<Group*>(message.context);
  auto& pool = *group.uses.domain.pool;
  if (group.retiring) {
    if (group.retired) {
      pool.internal->counts.trim_pages += group.retire_mapped / page_size();
      refund(pool, group.retire_charge);
      group.retire_mapped = group.retire_reserved = group.metadata_charge = 0;
    }
    else pool.internal->trim_failed = true;
    return;
  }
  std::uint64_t amount = 0;
  auto membership = group.geometry->trim_membership();
  const auto success = group.geometry->trim_success();
  std::size_t cursor = 0;
  PageSpan span;
  while (next_trim_span(membership, cursor, span)) {
    // The helper records success for a whole run, so its first bit suffices.
    if (!(success[span.first / 64] & (std::uint64_t{1} << (span.first % 64)))) {
      // A failed range operation may have changed some pages. Keep the entire
      // range unavailable until a retry establishes one known protection state.
      pool.internal->trim_failed = true;
    } else {
      verify(group.geometry->finish_trim(span, true));
      amount += group.geometry->native_page() * span.count;
      pool.internal->counts.trim_pages += span.count;
      set_trim_span(membership, span, false);
    }
  }
  refund(pool, amount);
}

void group_trim_release(worker::Message& message) noexcept {
  auto& group = *static_cast<Group*>(message.context);
  auto retained = std::move(group.trim_lifetime);
  if (group.retiring && group.retired) {
    auto& pool = *group.uses.domain.pool;
    erase_owner(pool.internal->groups, group);
    pulse(pool.capacity_changed);
    return;
  }
  group.trim_pending = false;
  pulse(group.uses.domain.pool->capacity_changed);
}

const worker::MessageOps group_trim_ops{no_retain, group_trim_handle, group_trim_release};

Result<Group*> make_group(Pool& pool, unsigned cls, bool deferred = false) {
  auto geometry = FiniteGroup::create(cls);
  if (!geometry) return std::unexpected(geometry.error());
  try {
    auto group = std::make_unique<Group>();
    group->geometry = std::move(*geometry);
    group->live.resize(group->geometry->live_words());
    // sizeof(Group) includes its fixed identity records and hints.
    // Account concrete prepared bookkeeping, separately from sparse VA/RSS.
    group->metadata_charge = group->geometry->control_bytes() + sizeof(FiniteGroup) + sizeof(Group) +
        group->live.capacity() * sizeof(std::uint64_t);
    // A fresh Group needs at least one payload page. Under pressure, do not
    // strand the remaining budget in metadata when an existing Group can map
    // that page. This is an admission probe, not a reservation across helper
    // work; actual page claims still arbitrate concurrent Arena users.
    const auto minimum = group->metadata_charge + group->geometry->native_page();
    const auto charged = pool.arena->charged_bytes.load(std::memory_order_relaxed);
    if (minimum > pool.arena->byte_limit || charged > pool.arena->byte_limit - minimum) {
      if (!claim(pool, minimum)) return std::unexpected(Error::capacity);
      refund(pool, minimum);
    }
    if (!claim(pool, group->metadata_charge)) return std::unexpected(Error::capacity);
    initialize(group->uses.domain, pool, group.get(), true);
    group->job = {nullptr, group.get(), group_trim_run};
    group->trimmed.context = group.get();
    group->trimmed.ops = &group_trim_ops;
    for (std::size_t i = 0; i < finite_homes; ++i)
      group->uses[i].initialize_identity(static_cast<std::uint8_t>(i));
    auto* result = group.get();
    result->owner_index = pool.internal->groups.size();
    try { pool.internal->groups.push_back(std::move(group)); }
    catch (...) { refund(pool, result->metadata_charge); throw; }
    if (deferred) {
      auto& preparing = pool.internal->preparing_groups[cls];
      assert(!preparing);
      preparing = result;
      result->control_preparing = true;
      return result;
    }
    if (auto prepared = result->geometry->prepare(); !prepared) {
      // The published Group owns every partially prepared mapping. No live
      // identity exists; the ordinary asynchronous retirement path retries it.
      result->retiring = true;
      result->retire_charge = result->metadata_charge;
      result->retire_reserved = result->geometry->reserved_bytes();
      return std::unexpected(prepared.error());
    }
    add_available(*result);
    return result;
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

// Map the Home's absent pages plus up to map_ahead following absent pages of
// the same Group (no more than it already maps) with one mprotect per run. Every page belongs to this class,
// so the run cannot strand another class's pages. Extra pages are charged idle
// capacity that trim returns; a refused claim maps only the Home itself.
constexpr std::size_t map_ahead = 15;
Result<void> map_home(Group& group, std::size_t home) noexcept {
  auto& pool = *group.uses.domain.pool;
  auto& geometry = *group.geometry;
  const auto page = geometry.native_page();
  const auto span = geometry.pages(home);
  std::size_t missing = 0;
  for (auto p = span.first; p < span.first + span.count; ++p) missing += !geometry.mapped(p);
  if (!missing) return {};
  const auto end = span.first + span.count;
  // Runs double with the Group's mapped inventory, so a sparsely used class
  // does not map 16 pages for its first Home.
  auto ahead = geometry.absent_run(end, std::min(map_ahead, geometry.mapped_pages()));
  if (!ahead || !claim(pool, (missing + ahead) * page)) {
    ahead = 0;
    if (!claim(pool, missing * page)) return std::unexpected(Error::capacity);
  }
  auto remaining = missing + ahead;
  for (auto p = span.first; p < end + ahead;) {
    if (geometry.mapped(p)) { ++p; continue; }
    auto q = p;
    while (q < end + ahead && !geometry.mapped(q)) ++q;
    auto* address = geometry.page_address(p);
    if (::mprotect(address, (q - p) * page, PROT_READ | PROT_WRITE) != 0) {
      refund(pool, remaining * page);
      return std::unexpected(Error::io);
    }
#ifdef ORBITAL_FINITE_POPULATE
    (void)::madvise(address, (q - p) * page, MADV_POPULATE_WRITE); // Advisory prefault.
#endif
    verify(geometry.mark_mapped({p, q - p}));
    remaining -= q - p;
    p = q;
  }
  return {};
}

void bind_home(Pool& pool, Group& group, std::size_t home, const AllocationRequest& request,
    Allocation& result) noexcept {
  if (!group.geometry->has_free()) remove_available(group);
  result.storage = &group.uses[home];
  result.address = group.geometry->address(home);
  result.size = request.bytes;
  result.readable_padding = request.readable_padding;
  (void)pool;
}

// Ready Homes in any Group of this class, then unmapped Homes in existing
// Groups, then a fresh Group. The inline fast path has already tried the head.
Result<void> finite_allocate(Pool& pool, const AllocationRequest& request, unsigned cls,
    Allocation& result) {
  auto& state = *pool.internal;
  for (auto* group = state.available[cls]; group; group = group->next_available) {
    if (!group->geometry->has_ready()) continue;
    const auto home = group->geometry->take_ready();
    ++state.counts.reuses;
    bind_home(pool, *group, home, request, result);
    return {};
  }
  for (auto* group = state.available[cls]; group; group = group->next_available) {
    const auto home = group->geometry->first_unready();
    if (home == finite_homes) continue;
    auto mapped = map_home(*group, home);
    if (!mapped) {
      if (mapped.error() != Error::capacity) return std::unexpected(mapped.error());
      continue;
    }
    group->geometry->claim(home);
    bind_home(pool, *group, home, request, result);
    return {};
  }
  auto made = make_group(pool, cls);
  if (!made) return std::unexpected(made.error());
  auto& group = **made;
  const auto home = group.geometry->first_unready();
  assert(home == 0);
  auto mapped = map_home(group, home);
  if (!mapped) return std::unexpected(mapped.error());
  group.geometry->claim(home);
  bind_home(pool, group, home, request, result);
  return {};
}

struct FinitePreparation {
  enum class Phase { control, material, cleanup };
  Preparation* request{};
  Pool* pool{};
  Group* group{};
  worker::Lane* preparer{};
  worker::Message message;
  Ref<Arena> lifetime;
  std::size_t home{finite_homes};
  std::array<PageSpan, 2> spans;
  std::array<bool, 2> discarded{};
  std::size_t span_count{};
  Phase phase{Phase::material};
  Error error{Error::io};
  bool control_ok{}, vm_ready{}, initialized{}, reused{};
};

void finite_prepare_post(FinitePreparation&) noexcept;

Result<void> reserve_preparation_home(FinitePreparation& record, std::size_t home) noexcept {
  auto& geometry = *record.group->geometry;
  const auto span = geometry.pages(home);
  if (span.count > record.spans.size()) return std::unexpected(Error::unavailable);
  std::size_t missing = 0;
  for (auto p = span.first; p < span.first + span.count; ++p) missing += !geometry.mapped(p);
  const auto charge = missing * geometry.native_page();
  if (charge && !claim(*record.pool, charge)) return std::unexpected(Error::capacity);
  const auto reserved = geometry.begin_prepare(home, record.spans);
  if (!reserved) { refund(*record.pool, charge); return std::unexpected(reserved.error()); }
  record.home = home; record.span_count = *reserved;
  record.reused = missing == 0;
  if (!geometry.has_free()) remove_available(*record.group);
  return {};
}

// Lowest free Home that is ready (or needs pages), without binding it.
std::size_t candidate_home(FiniteGroup& geometry, bool ready) noexcept {
  if (!ready) return geometry.first_unready();
  for (std::size_t home = 0; home < finite_homes; ++home)
    if (geometry.is_free(home)) {
      const auto span = geometry.pages(home);
      bool mapped = true;
      for (auto p = span.first; mapped && p < span.first + span.count; ++p) mapped = geometry.mapped(p);
      if (mapped) return home;
    }
  return finite_homes;
}

void finite_prepare_handle(worker::Message& message, worker::Lane&) noexcept {
  auto& record = *static_cast<FinitePreparation*>(message.context);
  auto& geometry = *record.group->geometry;
  if (record.phase == FinitePreparation::Phase::control) {
    const auto prepared = geometry.prepare();
    record.control_ok = prepared.has_value();
    if (!prepared) record.error = prepared.error();
    return;
  }
  const auto page = geometry.native_page();
  if (record.phase == FinitePreparation::Phase::cleanup) {
    for (std::size_t i = 0; i < record.span_count; ++i) {
      const auto span = record.spans[i];
      auto* address = geometry.page_address(span.first);
      record.discarded[i] = ::madvise(address, span.count * page, MADV_DONTNEED) == 0 &&
          ::mprotect(address, span.count * page, PROT_NONE) == 0;
    }
    return;
  }
  record.vm_ready = false; record.initialized = false;
  for (std::size_t i = 0; i < record.span_count; ++i) {
    const auto span = record.spans[i];
    auto* address = geometry.page_address(span.first);
    if (::mprotect(address, span.count * page, PROT_READ | PROT_WRITE) != 0) return;
    for (std::size_t offset = 0; offset < span.count * page; offset += page)
      *static_cast<volatile std::byte*>(address + offset) = std::byte{0};
  }
  record.vm_ready = true;
  const auto& request = record.request->request;
  auto* address = geometry.address(record.home);
  // Legal RW pages may still be physically absent after an untouched direct
  // uninitialized allocation. Touch only this exclusive Home, never neighbours.
  if (request.initialization == Initialization::uninitialized) {
    const auto bytes = request.bytes + request.readable_padding;
    for (std::size_t offset = 0; offset < bytes;) {
      *static_cast<volatile std::byte*>(address + offset) = std::byte{0};
      const auto to_boundary = page - reinterpret_cast<std::uintptr_t>(address + offset) % page;
      offset += std::min(to_boundary, bytes - offset);
    }
  }
  if (request.initialization == Initialization::zero)
    std::memset(address, 0, request.bytes + request.readable_padding);
  else if (request.readable_padding)
    std::memset(address + request.bytes, 0, request.readable_padding);
  record.initialized = true;
  if (auto constructor = record.request->construct) {
    const auto result = constructor({{address, request.bytes}, request.readable_padding},
        record.request->context);
    record.initialized = result.has_value();
    if (!result) record.error = result.error();
  }
}

void finite_prepare_reply_handle(worker::Message&, worker::Lane&) noexcept {}

void finite_prepare_finish(FinitePreparation& record, Result<void> status) noexcept {
  auto& request = *record.request;
  auto& pool = *record.pool;
  auto lifetime = std::move(record.lifetime);
  assert(pool.internal->preparations && record.group->preparations);
  --pool.internal->preparations; --record.group->preparations;
  request.backend = nullptr; request.preparer = nullptr;
  if (!status) {
    request.error = status.error();
    request.state = request.discard || pool.internal->close_requested
        ? Preparation::State::cancelled : Preparation::State::failed;
    request.owner = nullptr;
  } else request.state = Preparation::State::ready;
  pool.internal->preparation_metadata -= sizeof(FinitePreparation);
  delete &record;
  refund(pool, sizeof(FinitePreparation));
  pulse(pool.capacity_changed);
  complete(request.changed, status);
}

Result<void> select_preparation_home(FinitePreparation& record, bool ready) noexcept {
  const auto home = candidate_home(*record.group->geometry, ready);
  if (home == finite_homes) return std::unexpected(Error::capacity);
  return reserve_preparation_home(record, home);
}

void finite_prepare_reply_release(worker::Message& message) noexcept {
  auto& record = *static_cast<FinitePreparation*>(message.context);
  auto& pool = *record.pool;
  auto& group = *record.group;
  auto& request = *record.request;
  const bool cancelled = request.discard || pool.internal->close_requested;
  if (record.phase == FinitePreparation::Phase::control) {
    auto& preparing = pool.internal->preparing_groups[group.geometry->cls()];
    assert(preparing == &group);
    preparing = nullptr;
    group.control_preparing = false;
    if (!record.control_ok || cancelled) {
      group.retiring = true;
      group.retire_charge = group.metadata_charge;
      group.retire_reserved = group.geometry->reserved_bytes();
      finite_prepare_finish(record, std::unexpected(record.control_ok ? Error::rejected : record.error));
      return;
    }
    add_available(group);
    const auto selected = select_preparation_home(record, false);
    if (!selected) { finite_prepare_finish(record, std::unexpected(selected.error())); return; }
    record.phase = FinitePreparation::Phase::material;
    finite_prepare_post(record);
    return;
  }
  if (record.phase == FinitePreparation::Phase::cleanup) {
    const auto page = group.geometry->native_page();
    std::size_t remaining = 0;
    for (std::size_t i = 0; i < record.span_count; ++i) {
      const auto span = record.spans[i];
      if (record.discarded[i]) {
        verify(group.geometry->discard_preparation(record.home, std::span(&span, 1)));
        pool.internal->preparation_uncertain -= span.count * page;
        refund(pool, span.count * page);
      } else record.spans[remaining++] = span;
    }
    record.span_count = remaining;
    if (remaining) {
      request.state = Preparation::State::repair_wait;
      request.error = Error::io;
      ++pool.internal->preparation_debt;
      pulse(request.changed);
      pulse(pool.capacity_changed);
      return; // Explicit retry only; service stays useful under persistent failure.
    }
    group.geometry->end_prepare(record.home);
    add_available(group);
    finite_prepare_finish(record, std::unexpected(record.error));
    return;
  }
  const auto spans = std::span(record.spans).first(record.span_count);
  if (!record.vm_ready) {
    verify(group.geometry->finish_prepare(record.home, spans, false));
    record.phase = FinitePreparation::Phase::cleanup;
    record.error = Error::io;
    // At most two native payload pages: reclaim independently so one failed
    // syscall does not retain another page's confirmed discard indefinitely.
    const auto captured = record.spans;
    const auto count = record.span_count;
    record.span_count = 0;
    for (std::size_t i = 0; i < count; ++i)
      for (auto p = captured[i].first; p < captured[i].first + captured[i].count; ++p) {
        record.spans[record.span_count++] = {p, 1};
        pool.internal->preparation_uncertain += group.geometry->native_page();
      }
    finite_prepare_post(record); // One initial cleanup attempt; failed attempt parks.
    return;
  }
  verify(group.geometry->finish_prepare(record.home, spans, true));
  if (cancelled || !record.initialized) {
    group.geometry->end_prepare(record.home);
    add_available(group);
    finite_prepare_finish(record, std::unexpected(cancelled ? Error::rejected : record.error));
    return;
  }
  group.geometry->bind_prepared(record.home);
  auto& use = group.uses[record.home];
  activate(use);
  request.value.storage = &use; request.value.address = group.geometry->address(record.home);
  request.value.size = request.request.bytes; request.value.readable_padding = request.request.readable_padding;
  request.reused = record.reused;
  ++pool.internal->counts.allocations;
  if (record.reused) ++pool.internal->counts.reuses;
  finite_prepare_finish(record, {});
}

const worker::MessageOps finite_prepare_reply_ops{
    no_retain, finite_prepare_reply_handle, finite_prepare_reply_release};

void finite_prepare_send_reply(worker::Message& message) noexcept {
  auto& record = *static_cast<FinitePreparation*>(message.context);
  message.ops = &finite_prepare_reply_ops;
  worker::post(*record.pool->lane, message);
}

const worker::MessageOps finite_prepare_work_ops{
    no_retain, finite_prepare_handle, finite_prepare_send_reply};

void finite_prepare_post(FinitePreparation& record) noexcept {
  record.message.ops = &finite_prepare_work_ops;
  worker::post(*record.preparer, record.message);
}

Result<void> prepare_finite(Pool& pool, Preparation& request,
    const AllocationRequest& allocation, unsigned cls, worker::Lane& preparer) {
  auto owned = std::unique_ptr<FinitePreparation>(new (std::nothrow) FinitePreparation);
  if (!owned || !claim(pool, sizeof(FinitePreparation))) return std::unexpected(Error::capacity);
  auto& record = *owned;
  record.request = &request; record.pool = &pool; record.preparer = &preparer;
  request.request = allocation;
  Error refusal = Error::capacity;
  // Same order as direct allocation: mapped Homes, then new pages in existing
  // Groups, then one fresh control preparation per class.
  for (unsigned pass = 0; pass < 3 && !record.group && refusal == Error::capacity; ++pass) {
    if (pass == 2) {
      // Pending helper work is not budget pressure; wait for that Group.
      if (pool.internal->preparing_groups[cls]) {
        refund(pool, sizeof(FinitePreparation));
        return std::unexpected(Error::unavailable);
      }
      auto made = make_group(pool, cls, true);
      if (made) { record.group = *made; record.phase = FinitePreparation::Phase::control; break; }
      refusal = made.error();
      break;
    }
    for (auto* group = pool.internal->available[cls]; group; group = group->next_available) {
      record.group = group;
      auto selected = select_preparation_home(record, pass == 0);
      if (selected) break;
      record.group = nullptr;
      refusal = selected.error();
      if (refusal != Error::capacity) break;
    }
  }
  if (!record.group) { refund(pool, sizeof(FinitePreparation)); return std::unexpected(refusal); }
  ++record.group->preparations; ++pool.internal->preparations;
  pool.internal->preparation_metadata += sizeof(FinitePreparation);
  record.lifetime = Ref<Arena>::retain(*pool.arena);
  record.message.context = &record;
  request.owner = &pool; request.preparer = &preparer; request.backend = &record;
  request.changed.lane = pool.lane; request.changed.state = Readiness::pending;
  request.state = Preparation::State::pending;
  finite_prepare_post(record);
  owned.release();
  return {};
}

} // namespace orbital::allocation::detail

namespace orbital::allocation {
using namespace detail;

Result<void> retry_cleanup(Preparation& request) {
  if (request.state != Preparation::State::repair_wait || !request.backend)
    return std::unexpected(Error::invalid_argument);
  if (!request.owner || !worker::on_lane(*request.owner->lane))
    return std::unexpected(Error::conflict);
  auto& record = *static_cast<FinitePreparation*>(request.backend);
  assert(record.phase == FinitePreparation::Phase::cleanup && request.owner->internal->preparation_debt);
  --request.owner->internal->preparation_debt;
  request.state = Preparation::State::pending;
  finite_prepare_post(record);
  return {};
}

} // namespace orbital::allocation
