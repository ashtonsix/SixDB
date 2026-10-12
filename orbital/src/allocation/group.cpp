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
    // No active home overlaps a reserved trim page. Even failed protection
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

Result<Group*> make_group(Pool& pool, Palette palette, bool deferred = false) {
  auto geometry = FiniteGroup::create({palette, group_identities});
  if (!geometry) return std::unexpected(geometry.error());
  try {
    auto group = std::make_unique<Group>();
    group->palette = palette;
    group->geometry = std::move(*geometry);
    const auto count = group->geometry->identity_count();
    assert(count == group->uses.size());
    group->live.resize(group->geometry->live_words());
    // sizeof(Group) includes its fixed identity records and hints.
    // Account concrete prepared bookkeeping, separately from sparse VA/RSS.
    group->metadata_charge = group->geometry->control_bytes() + sizeof(FiniteGroup) + sizeof(Group) +
        group->live.capacity() * sizeof(std::uint64_t);
    if (!claim(pool, group->metadata_charge)) return std::unexpected(Error::capacity);
    initialize(group->uses.domain, pool, group.get(), true);
    group->job = {nullptr, group.get(), group_trim_run};
    group->trimmed.context = group.get();
    group->trimmed.ops = &group_trim_ops;
    for (std::size_t i = 0; i < count; ++i) {
      group->uses[i].initialize_identity(static_cast<std::uint8_t>(i));
    }
    auto* result = group.get();
    result->owner_index = pool.internal->groups.size();
    try { pool.internal->groups.push_back(std::move(group)); }
    catch (...) { refund(pool, result->metadata_charge); throw; }
    if (deferred) { result->control_preparing = true; return result; }
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

struct FinitePreparation {
  enum class Phase { control, material, cleanup };
  Preparation* request{};
  Pool* pool{};
  Group* group{};
  worker::Lane* preparer{};
  worker::Message message;
  Ref<Arena> lifetime;
  Identity identity;
  Home home;
  std::array<PageSpan, 2> spans;
  std::array<bool, 2> discarded{};
  std::size_t span_count{};
  Phase phase{Phase::material};
  Error error{Error::io};
  bool control_ok{}, vm_ready{}, initialized{}, reused{};
};

void finite_prepare_post(FinitePreparation&) noexcept;
// Owner-only candidate traversal shared by direct and prepared allocation.
// A class-blocked grid cannot hide legal Homes in later grids.
Result<std::pair<Identity, Home>> claim_finite_candidate(Group& group,
    std::size_t size, std::size_t alignment, std::uint64_t& excluded) noexcept {
  const auto cls = group.geometry->class_for(size);
  if (!cls) return std::unexpected(cls.error());
  for (;;) {
    auto identity = group.geometry->claim_identity(excluded);
    if (!identity) return std::unexpected(identity.error());
    auto home = group.geometry->claim_home_class(*identity, *cls, alignment);
    if (home) return std::pair{*identity, *home};
    excluded |= std::uint64_t{1} << (group.geometry->grid_of(*identity));
    verify(group.geometry->release_identity(*identity));
    if (home.error() != Error::capacity) return std::unexpected(home.error());
  }
}

void release_finite_candidate(Group& group, const std::pair<Identity, Home>& candidate,
    std::uint64_t& excluded) noexcept {
  verify(group.geometry->release_home(candidate.second));
  verify(group.geometry->release_identity(candidate.first));
  excluded |= std::uint64_t{1} << (group.geometry->grid_of(candidate.first));
}

std::size_t missing_pages(const Group& group, const Home& home) noexcept {
  std::size_t count = 0;
  for (auto p = home.pages.first; p < home.pages.first + home.pages.count; ++p)
    count += !group.geometry->mapped(p);
  return count;
}

struct FiniteCandidate {
  Group* group{};
  Identity identity;
  Home home;
};

// Positive selection hints only. Authority remains in the Group's identities,
// occupied Homes and physical page state; a stale hint cannot hide a fallback.
Result<std::optional<FiniteCandidate>> claim_ready_candidate(Pool& pool,
    std::size_t size, std::size_t alignment) noexcept {
  for (auto& cached : pool.internal->ready_candidates) {
    if (!cached.group || size < cached.minimum || size > cached.maximum ||
        alignment != cached.alignment) continue;
    auto& group = *cached.group;
    if (group.retiring || group.control_preparing || !group.previous_available) {
      cached.group = nullptr; continue;
    }
    auto identity = group.geometry->claim_identity(~(std::uint64_t{1} << cached.grid));
    if (!identity) {
      cached.group = nullptr;
      if (identity.error() != Error::capacity) return std::unexpected(identity.error());
      continue;
    }
    auto home = group.geometry->claim_home(*identity, size, alignment);
    if (!home) {
      verify(group.geometry->release_identity(*identity));
      cached.group = nullptr;
      if (home.error() != Error::capacity) return std::unexpected(home.error());
      continue;
    }
    if (!missing_pages(group, *home)) return FiniteCandidate{&group, *identity, *home};
    verify(group.geometry->release_home(*home));
    verify(group.geometry->release_identity(*identity));
    cached.group = nullptr;
  }
  return std::nullopt;
}

void remember_ready_candidate(Pool& pool, Group& group, const Home& home,
    std::size_t alignment) noexcept {
  if (!group.previous_available || group.retiring || group.control_preparing) return;
  const auto minimum = static_cast<std::uint16_t>(group.geometry->class_minimum(home.cls));
  const auto maximum = static_cast<std::uint16_t>(home.capacity);
  auto& hints = pool.internal->ready_candidates;
  for (auto& cached : hints) {
    if (cached.minimum == minimum && cached.maximum == maximum && cached.alignment == alignment) {
      cached = {&group, minimum, maximum, static_cast<std::uint16_t>(alignment), static_cast<std::uint8_t>(home.grid)};
      return;
    }
  }
  auto& next = pool.internal->next_candidate;
  hints[next] = {&group, minimum, maximum, static_cast<std::uint16_t>(alignment), static_cast<std::uint8_t>(home.grid)};
  next = static_cast<std::uint8_t>((next + 1) % hints.size());
}

Result<void> reserve_preparation_home(FinitePreparation& record,
    const std::pair<Identity, Home>& candidate) noexcept {
  auto& geometry = *record.group->geometry;
  const auto& home = candidate.second;
  if (home.pages.count > record.spans.size()) return std::unexpected(Error::unavailable);
  const auto missing = missing_pages(*record.group, home);
  const auto charge = missing * geometry.native_page();
  if (charge && !claim(*record.pool, charge)) return std::unexpected(Error::capacity);
  const auto reserved = geometry.begin_prepare(home, record.spans);
  if (!reserved) { refund(*record.pool, charge); return std::unexpected(reserved.error()); }
  record.identity = candidate.first; record.home = home; record.span_count = *reserved;
  record.reused = missing == 0;
  if (!geometry.has_identity()) remove_available(*record.group);
  return {};
}

Result<void> select_preparation_home(FinitePreparation& record, bool ready) noexcept {
  const auto& request = record.request->request;
  std::uint64_t excluded = 0;
  for (;;) {
    auto candidate = claim_finite_candidate(*record.group,
        std::max(request.bytes + request.readable_padding, request.reserved_bytes), request.alignment, excluded);
    if (!candidate) return std::unexpected(candidate.error());
    const auto release = [&] { release_finite_candidate(*record.group, *candidate, excluded); };
    auto& home = candidate->second;
    if ((missing_pages(*record.group, home) == 0) != ready) { release(); continue; }
    const auto reserved = reserve_preparation_home(record, *candidate);
    if (!reserved) {
      release();
      if (reserved.error() == Error::capacity) continue;
      return std::unexpected(reserved.error());
    }
    return {};
  }
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
  // Legal RW pages may still be physically absent after an untouched direct
  // uninitialized allocation. Touch only this exclusive Home, never neighbours.
  if (request.initialization == Initialization::uninitialized) {
    const auto bytes = request.bytes + request.readable_padding;
    for (std::size_t offset = 0; offset < bytes;) {
      auto* address = record.home.address + offset;
      *static_cast<volatile std::byte*>(address) = std::byte{0};
      const auto to_boundary = page - reinterpret_cast<std::uintptr_t>(address) % page;
      offset += std::min(to_boundary, bytes - offset);
    }
  }
  if (request.initialization == Initialization::zero)
    std::memset(record.home.address, 0, request.bytes + request.readable_padding);
  else if (request.readable_padding)
    std::memset(record.home.address + request.bytes, 0, request.readable_padding);
  record.initialized = true;
  if (auto constructor = record.request->construct) {
    const auto result = constructor({{record.home.address, request.bytes}, request.readable_padding},
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

void finite_prepare_reply_release(worker::Message& message) noexcept {
  auto& record = *static_cast<FinitePreparation*>(message.context);
  auto& pool = *record.pool;
  auto& group = *record.group;
  auto& request = *record.request;
  const bool cancelled = request.discard || pool.internal->close_requested;
  if (record.phase == FinitePreparation::Phase::control) {
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
    verify(group.geometry->end_prepare(record.home));
    verify(group.geometry->release_identity(record.identity));
    verify(group.geometry->release_home(record.home));
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
    verify(group.geometry->release_identity(record.identity));
    verify(group.geometry->release_home(record.home));
    add_available(group);
    finite_prepare_finish(record, std::unexpected(cancelled ? Error::rejected : record.error));
    return;
  }
  verify(group.geometry->bind(record.identity, record.home));
  remember_ready_candidate(pool, group, record.home, request.request.alignment);
  auto& use = group.uses[record.identity.index];
  activate(use);
  request.value.storage = &use; request.value.address = record.home.address;
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
    const AllocationRequest& allocation, worker::Lane& preparer) {
  auto owned = std::unique_ptr<FinitePreparation>(new (std::nothrow) FinitePreparation);
  if (!owned || !claim(pool, sizeof(FinitePreparation))) return std::unexpected(Error::capacity);
  auto& record = *owned;
  record.request = &request; record.pool = &pool; record.preparer = &preparer;
  request.request = allocation;
  Error refusal = Error::capacity;
  const auto size = std::max(allocation.bytes + allocation.readable_padding, allocation.reserved_bytes);
  auto cached = claim_ready_candidate(pool, size, allocation.alignment);
  if (!cached) refusal = cached.error();
  else if (*cached) {
    const auto& choice = **cached;
    record.group = choice.group;
    const std::pair candidate{choice.identity, choice.home};
    auto reserved = reserve_preparation_home(record, candidate);
    if (!reserved) {
      std::uint64_t excluded = 0;
      release_finite_candidate(*record.group, candidate, excluded);
      record.group = nullptr; refusal = reserved.error();
    }
  }
  // Try prepared backing before paying for new pages.
  for (unsigned pass = 0; pass < 2 && !record.group && refusal == Error::capacity; ++pass) {
    for (auto* group = pool.internal->available_groups; group; group = group->next_available) {
      record.group = group;
      auto selected = select_preparation_home(record, pass == 0);
      if (selected) break;
      record.group = nullptr;
      refusal = selected.error();
      if (refusal != Error::capacity) break;
    }
  }
  if (!record.group && refusal == Error::capacity) {
    const auto reserve = std::max(allocation.bytes + allocation.readable_padding, allocation.reserved_bytes);
    const auto palette = preferred_palette(page_size(), reserve, allocation.alignment);
    // One fresh control preparation per compatible palette. Capacity wakes the
    // caller after publication; prediction policy need not queue N empty Groups.
    const bool waiting = std::any_of(pool.internal->groups.begin(), pool.internal->groups.end(),
        [palette](const auto& group) { return group->control_preparing && group->palette == palette; });
    if (waiting) { refund(pool, sizeof(FinitePreparation)); return std::unexpected(Error::unavailable); }
    auto made = make_group(pool, palette, true);
    if (made) { record.group = *made; record.phase = FinitePreparation::Phase::control; }
    else refusal = made.error();
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

Result<void> map_home(Group& group, const Home& home) noexcept {
  auto& pool = *group.uses.domain.pool;
  const auto page = group.geometry->native_page();
  std::size_t remaining = 0;
  for (std::size_t p = home.pages.first; p < home.pages.first + home.pages.count; ++p)
    remaining += !group.geometry->mapped(p);
  // Claim the complete candidate before changing any page. Capacity refusal
  // leaves the budget available to another legal home or packing group.
  if (remaining && !claim(pool, remaining * page)) return std::unexpected(Error::capacity);
  for (std::size_t p = home.pages.first; p < home.pages.first + home.pages.count; ++p) {
    if (group.geometry->mapped(p)) continue;
    if (::mprotect(group.geometry->page_address(p), page, PROT_READ | PROT_WRITE) != 0) {
      refund(pool, remaining * page);
      return std::unexpected(Error::io);
    }
    verify(group.geometry->mark_mapped({p, 1}));
    --remaining;
  }
  return {};
}

Result<void> finite_allocate(Pool& pool, const AllocationRequest& request, std::size_t size,
    Allocation& result) {
  const auto alignment = request.alignment;
  Group* selected = nullptr;
  Identity identity;
  Home home;
  bool prepared = false;
  auto cached = claim_ready_candidate(pool, size, alignment);
  if (!cached) return std::unexpected(cached.error());
  if (*cached) {
    const auto& choice = **cached;
    selected = choice.group; identity = choice.identity; home = choice.home;
    prepared = true;
  }
  // Reuse prepared backing before paying for new pages.
  for (unsigned pass = 0; pass < 2 && !selected; ++pass) {
    for (auto* group = pool.internal->available_groups; group; group = group->next_available) {
      std::uint64_t excluded = 0;
      for (;;) {
        auto candidate = claim_finite_candidate(*group, size, alignment, excluded);
        if (!candidate) {
          if (candidate.error() != Error::capacity) return std::unexpected(candidate.error());
          break;
        }
        const bool ready = missing_pages(*group, candidate->second) == 0;
        if (ready != (pass == 0)) { release_finite_candidate(*group, *candidate, excluded); continue; }
        auto mapped = map_home(*group, candidate->second);
        if (!mapped) {
          release_finite_candidate(*group, *candidate, excluded);
          if (mapped.error() != Error::capacity) return std::unexpected(mapped.error());
          continue;
        }
        prepared = ready;
        selected = group; identity = candidate->first; home = candidate->second; break;
      }
      if (selected) break;
    }
  }
  if (!selected) {
    auto made = make_group(pool, preferred_palette(page_size(), size, alignment));
    if (!made) return std::unexpected(made.error());
    selected = *made;
    auto id = selected->geometry->claim_identity();
    assert(id); identity = *id;
    auto candidate = selected->geometry->claim_home(identity, size, alignment);
    if (!candidate) { verify(selected->geometry->release_identity(identity)); return std::unexpected(candidate.error()); }
    home = *candidate;
    if (auto mapped = map_home(*selected, home); !mapped) {
      verify(selected->geometry->release_home(home));
      verify(selected->geometry->release_identity(identity));
      return std::unexpected(mapped.error());
    }
  }
  if (prepared) ++pool.internal->counts.reuses;
  verify(selected->geometry->bind(identity, home));
  if (!selected->geometry->has_identity()) remove_available(*selected);
  remember_ready_candidate(pool, *selected, home, alignment);
  auto& use = selected->uses[identity.index];
  result.storage = &use;
  result.address = home.address;
  result.size = request.bytes;
  result.readable_padding = request.readable_padding;
  return {};
}

Result<bool> relocate_finite(Allocation& allocation, const GrowthRequest& request,
    std::size_t needed) {
  auto& use = *allocation.storage;
  auto& group = *static_cast<Group*>(route(use)->context);
  auto& pool = *route(use)->pool;
  const auto old = group.geometry->bound_home({use.identity()});
  assert(old);
  auto destination = group.geometry->claim_home({use.identity()}, needed, request.alignment);
  if (!destination && destination.error() != Error::capacity)
    return std::unexpected(destination.error());
  if (destination) {
    const auto mapped = map_home(group, *destination);
    if (mapped) {
      std::memcpy(destination->address, allocation.address,
          allocation.size + allocation.readable_padding);
      verify(group.geometry->bind({use.identity()}, *destination));
      verify(group.geometry->release_home(*old));
      allocation.address = destination->address;
      allocation.size = request.bytes; allocation.readable_padding = request.readable_padding;
      ++pool.internal->counts.relocations;
      ++pool.internal->counts.growths;
      return true;
    }
    verify(group.geometry->release_home(*destination));
    if (mapped.error() != Error::capacity) return std::unexpected(mapped.error());
  }
  return false;
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
