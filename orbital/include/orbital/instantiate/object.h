#pragma once
#include <orbital/object.h>
#include <orbital/detail/object/history_impl.h>

// Opt in from the material's explicit-instantiation TU. Ordinary callers include
// orbital/object.h; format-specific adapters and instantiations stay with them.
namespace orbital {
template<class Physical>
ObjectStore<Physical>::ObjectStore(std::pmr::memory_resource& memory)
    : core_(object_detail::make<Core>(memory, memory)) {}

template<class Physical>
auto ObjectStore<Physical>::Scope::bind_alternative(ObjectId id) const -> Result<Selection> {
  if (!core_) return std::unexpected(Error::invalid_argument);
  auto selected = core_->resolve(cut_, id, true);
  if (!selected) return std::unexpected(Error::absent);
  return core_->capture_selection(*this, Physical::get(selected));
}

template<class Physical>
auto ObjectStore<Physical>::Core::capture_selection(const Scope& scope, Value* value) -> Result<Selection> {
  try {
    auto [where, inserted] = captures.try_emplace(&scope); (void)inserted;
    auto& captured = where->second;
    for (auto* owner : captured) if (owner == value) return Physical::selection(value);
    captured.push_back(value);
    Physical::retain(value);
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
  return Physical::selection(value);
}

template<class Physical>
void ObjectStore<Physical>::Core::close(Scope& scope) noexcept {
  const auto before = floor();
  const bool removed_distinct = scope.cut_ < cut &&
      (!scope.previous_ || scope.previous_->cut_ != scope.cut_) &&
      (!scope.next_ || scope.next_->cut_ != scope.cut_);
  if (removed_distinct) ++reader_changes;
  if (step == Step::cuts && next_cut == &scope) {
    auto* next = scope.next_;
    next_cut = next && next->cut_ < building_cut ? next : nullptr;
  }
  if (scope.previous_) scope.previous_->next_ = scope.next_; else first_scope = scope.next_;
  if (scope.next_) scope.next_->previous_ = scope.previous_; else last_scope = scope.previous_;
  --scopes; if (floor() != before || removed_distinct) { dirty = true; ++maintenance_changes; }
  if (!captures.empty()) {
    auto at = captures.find(&scope);
    if (at != captures.end()) {
      auto owners = std::move(at->second); captures.erase(at);
      for (auto* owner : owners) Physical::release(owner);
    }
  }
  scope.core_ = nullptr; release();
}

template<class Physical>
void ObjectStore<Physical>::Core::abort(Prepared& prepared) noexcept {
  if (prepared.disposition_ != PublicationState::accepted) return;
  assert(pending); prepared.disposition_ = PublicationState::aborted;
  for (auto& change : prepared.changes_) {
    const auto current = head(change.entry);
    if (current && (entries.indebted(change.entry) || !primary(current))) add_debt(change.entry);
    Layout::abort(change); // Bookkeeping precedes reentrant owner disposal.
  }
  // Owner callbacks can drain or close the ObjectStore. They cannot prepare while
  // later mutations in this batch still own entry pins.
  pending = false;
}

template<class Physical>
std::size_t ObjectStore<Physical>::Core::drain(std::size_t limit) noexcept {
  if (!limit || !dirty || draining) return 0;
  draining = true;
  std::size_t work = 0;
  while (work < limit && dirty) {
    if (callback_restart) {
      ++work; restart(); continue;
    }
    if (!work && debt_entry && step != Step::finish) {
      const auto current = head(debt_entry);
      if (anchor && step != Step::seek && version_cut(anchor) != anchor_cut) {
        // An inline current tip moved into history. Find that sampled tip
        // through a bounded prefix walk; future publications only prepend.
        resume_step = step; anchor = older(current); step = Step::seek; ++head_refreshes;
      }
    }
    switch (step) {
    case Step::idle: {
      ++work; ++plan_work; building_cut = cut; pass_generation = maintenance_changes;
      cut_begin = 0;
      planned_floor = cut;
      if (plan_generation != reader_changes) {
        reader_cuts.clear(); next_cut = nullptr;
        if (first_scope && first_scope->cut_ < cut) {
          try {
            if (scopes > reader_cuts.max_size()) throw std::bad_alloc();
            reader_cuts.reserve(scopes);
          }
          catch (const std::bad_alloc&) { ++maintenance_refusals; goto finished; }
          next_cut = first_scope;
        }
        plan_generation = reader_changes;
        step = Step::cuts;
        if (next_cut) break;
      }

      last_sweep_bytes = reader_cuts.capacity() * sizeof(std::uint64_t);
      last_sweep_cuts = reader_cuts.size(); begin_entries(); break;
    }
    case Step::cuts: {
      if (!next_cut) {
        last_sweep_bytes = reader_cuts.capacity() * sizeof(std::uint64_t);
        last_sweep_cuts = reader_cuts.size(); begin_entries(); break;
      }
      ++work; ++cut_visits;
      assert(next_cut->cut_ < building_cut);
      const auto value = next_cut->cut_;
      if (reader_cuts.empty() || reader_cuts.back() != value) reader_cuts.push_back(value);
      auto* next = next_cut->next_;
      next_cut = next && next->cut_ < building_cut ? next : nullptr;
      if (!next_cut) {
        last_sweep_bytes = reader_cuts.capacity() * sizeof(std::uint64_t);
        last_sweep_cuts = reader_cuts.size(); begin_entries();
      }
      break;
    }
    case Step::entry:
      if (!remaining_entries || !entries.front_debt()) {
        ++completed_sweeps; step = Step::idle;
        dirty = maintenance_changes != pass_generation; break;
      }
      ++work; ++entry_visits;
      debt_entry = entries.front_debt(); anchor = head(debt_entry);
      anchor_cut = anchor ? version_cut(anchor) : 0;
      crossed_floor = !anchor || version_cut(anchor) <= planned_floor;
      remaining_cuts = reader_cuts.size(); cut_begin = 0;
      step = !anchor ? Step::finish : Step::consume;
      break;
    case Step::older: {
      ++work; ++version_visits;
      const auto old = older(anchor);
      if (!old) { step = Step::finish; break; }
      const auto old_cut = version_cut(old);
      // Scopes added after this plan can select the suffix above its
      // boundary or its first predecessor. Keep that range this sweep.
      const bool newer = !crossed_floor;
      if (newer) crossed_floor = old_cut <= planned_floor;
      const bool keep = newer || (remaining_cuts && reader_cuts[remaining_cuts - 1] >= old_cut);
      if (keep) { anchor = old; anchor_cut = old_cut; cut_begin = 0; step = Step::consume; }
      else {
        link_older(anchor, older(old)); --versions; ++reclaimed_versions;
        const auto before_callback = maintenance_changes;
        drop(old); // No directory/node mutation follows the owner callback.
        callback_restart = maintenance_changes != before_callback;
      }
      break;
    }
    case Step::consume:
      if (!remaining_cuts) { step = Step::older; break; }
      ++work; ++cut_visits;
      if (reader_cuts[remaining_cuts - 1] >= version_cut(anchor)) {
        --remaining_cuts;
        cut_begin = remaining_cuts ? remaining_cuts - 1 : 0;
        step = remaining_cuts ? Step::skip_cuts : Step::older;
      }
      else step = Step::older;
      break;
    case Step::skip_cuts: {
      ++work; ++cut_visits;
      // Many cuts can select one unchanged tip. Gallop through that interval;
      // a single selected cut still takes the ordinary two end comparisons.
      if (reader_cuts[cut_begin] >= version_cut(anchor)) {
        const auto skipped = remaining_cuts - cut_begin;
        remaining_cuts = cut_begin;
        if (!remaining_cuts) { cut_begin = 0; step = Step::older; }
        else cut_begin = skipped <= remaining_cuts / 2 ? remaining_cuts - skipped * 2 : 0;
      } else {
        ++cut_begin;
        if (cut_begin == remaining_cuts) { cut_begin = 0; step = Step::older; }
        else step = Step::find_cut;
      }
      break;
    }
    case Step::find_cut: {
      ++work; ++cut_visits;
      const auto middle = cut_begin + (remaining_cuts - cut_begin) / 2;
      if (reader_cuts[middle] >= version_cut(anchor)) remaining_cuts = middle;
      else cut_begin = middle + 1;
      if (cut_begin == remaining_cuts) { cut_begin = 0; step = Step::older; }
      break;
    }
    case Step::finish: {
      ++work; ++debt_visits;
      const auto entry = std::exchange(debt_entry, ObjectHandle{});
      anchor = {}; cut_begin = 0; --remaining_entries; step = Step::entry;
      const auto current = head(entry);
      // Pins preserve the reserved debt slot across accepted work. Install
      // transfers the tip without allocating or disposing any user owner.
      if ((!current || !older(current)) && !entries.pinned(entry)) {
        if (!current || primary(current)) remove_debt(entry);
        else {
          remove_debt(entry); ++reclaimed_versions; erase_head(entry);
        }
      } else entries.rotate_debt();
      break;
    }
    case Step::seek:
      ++work; ++version_visits;
      assert(anchor);
      // The sampled cut is unchanged, so interval-search bounds survive a tip
      // moving into history while this drain was suspended.
      if (version_cut(anchor) == anchor_cut) step = resume_step;
      else { assert(version_cut(anchor) > anchor_cut); anchor = older(anchor); }
      break;
    }
  }
finished:
  if (step == Step::entry && !remaining_entries && maintenance_changes == pass_generation) {
    ++completed_sweeps; step = Step::idle; dirty = false;
  }
  draining = false; reclamation_work += work; return work;
}

template<class Physical>
auto ObjectStore<Physical>::capture(const RepresentationSpec& spec, std::span<const PartSource> parts) const -> Result<Owner> {
  if (!core_) return std::unexpected(Error::invalid_argument);
  Pin pin(core_);
  auto captured = Physical::capture(pin.core->memory, spec, parts);
  if (!captured) return std::unexpected(captured.error());
  const auto footprint = Physical::footprint(*captured);
  pin.core->copied_metadata_bytes += footprint.copied_metadata;
  pin.core->captured_descriptor_bytes += footprint.requested_bytes;
  return Owner::take(*captured);
}

template<class Physical>
Result<void> ObjectStore<Physical>::prepare(const Scope& basis, std::span<const Change> changes, Prepared& prepared) {
  if (!core_ || basis.core_ != core_ || prepared.core_ != core_) return std::unexpected(Error::conflict);
  auto& c = *prepared.core_; // The caller-owned packet already pins this Core.
  if (basis.cut_ != c.cut) return std::unexpected(Error::stale);
  if (c.pending || changes.empty() || prepared.disposition_ != PublicationState::empty)
    return std::unexpected(Error::unavailable);
  if (c.cut == std::numeric_limits<std::uint64_t>::max()) return std::unexpected(Error::capacity);
  const bool small = changes.size() <= 64;
  for (std::size_t i = 0; i < changes.size(); ++i) {
    const auto& change = changes[i];
    auto* rep = change.representation.get();
    if (rep && !Physical::accepts(rep, change.object, c.cut + 1))
      return std::unexpected(Error::conflict);
    if constexpr (requires { Physical::accepts_resource(rep, c.memory); }) {
      if (rep && !Physical::accepts_resource(rep, c.memory)) return std::unexpected(Error::conflict);
    }
    if (small)
      for (std::size_t j = 0; j < i; ++j) if (changes[j].object == change.object) return std::unexpected(Error::conflict);
  }
  assert(prepared.changes_.empty());
  try {
    if (!small) {
      // A scratch refusal can precede duplicate detection; no catalog or Owner
      // state changes until preflight succeeds. Ordinary packets need no scratch.
      std::pmr::vector<ObjectId> ids(&c.memory);
      ids.reserve(changes.size());
      for (const auto& change : changes) ids.push_back(change.object);
      if (!object_detail::unique_object_ids(ids)) return std::unexpected(Error::conflict);
    }
    c.reserve(changes);
    c.Layout::prepare(changes, prepared.changes_);
  } catch (const std::bad_alloc&) {
    prepared.changes_.clear(); // Layout rolled back owners/slots; retry must not revisit them.
    return std::unexpected(Error::capacity);
  } catch (...) {
    prepared.changes_.clear(); throw;
  }
  prepared.disposition_ = PublicationState::accepted; c.pending = true; return {};
}

template<class Physical>
Result<void> ObjectStore<Physical>::install(Prepared& prepared) noexcept {
  if (!core_ || prepared.core_ != core_) return std::unexpected(Error::conflict);
  auto& c = *core_;
  if (prepared.disposition_ != PublicationState::accepted || !c.pending) return std::unexpected(Error::rejected);
  // One accepted packet excludes every other publication. Its predecessor
  // therefore cannot change before install or completion of abort cleanup.
  for (auto& change : prepared.changes_)
    if (c.Layout::install(change, c.cut + 1)) c.add_debt(change.entry);
  if (c.last_scope && c.last_scope->cut_ == c.cut) ++c.reader_changes;
  ++c.cut; prepared.disposition_ = PublicationState::installed; c.pending = false; return {};
}

template<class Physical>
auto ObjectStore<Physical>::conversion(const Scope& basis, ObjectId id) const noexcept -> Result<ConversionTarget> {
  if (!core_ || basis.core_ != core_) return std::unexpected(Error::conflict);
  auto h = core_->locate(basis.cut_, id);
  if (!h || !core_->primary(h)) return std::unexpected(Error::absent);
  return ConversionTarget(*core_, core_->primary(h), id, core_->version_cut(h));
}

template<class Physical>
auto ObjectStore<Physical>::bound(const Scope& basis, ObjectId id) const noexcept -> Result<BoundSelection> {
  if (!core_ || basis.core_ != core_) return std::unexpected(Error::conflict);
  auto h = core_->locate(basis.cut_, id);
  if (!h || !core_->primary(h)) return std::unexpected(Error::absent);
  return BoundSelection{Physical::selection(core_->primary(h)), ObjectBinding{id, core_->version_cut(h)}};
}

template<class Physical>
auto ObjectStore<Physical>::bound_alternative(const Scope& basis, ObjectId id) const noexcept -> Result<BoundSelection> {
  if (!core_ || basis.core_ != core_) return std::unexpected(Error::conflict);
  auto h = core_->locate(basis.cut_, id);
  auto* value = h ? core_->alternate(h) : nullptr;
  if (!value) return std::unexpected(Error::absent);
  const auto selected = core_->capture_selection(basis, value);
  if (!selected) return std::unexpected(selected.error());
  return BoundSelection{*selected, ObjectBinding{id, core_->version_cut(h)}};
}

template<class Physical>
Result<void> ObjectStore<Physical>::adopt(const ConversionTarget& target, const Owner& replacement) {
  if (!core_ || !replacement) return std::unexpected(Error::invalid_argument);
  if (target.core_ != core_ || !target.source_) return std::unexpected(Error::conflict);
  Pin pin(core_); auto& c = *pin.core;
  const auto id = target.binding_.object;
  const auto cut = target.binding_.version;
  const auto h = c.locate(cut, id);
  if (!h || c.version_cut(h) != cut || !c.primary(h)) return std::unexpected(Error::absent);
  auto* next = replacement.get();
  if constexpr (requires { Physical::accepts_resource(next, c.memory); }) {
    if (!Physical::accepts_resource(next, c.memory)) return std::unexpected(Error::conflict);
  }
  if (!Physical::compatible(c.primary(h), next)) return std::unexpected(Error::conflict);
  return c.Layout::adopt(h, next);
}

template<class Physical>
std::size_t ObjectStore<Physical>::drain() noexcept {
  if (!core_) return 0;
  Pin pin(core_);
  const auto before = pin.core->maintenance_refusals;
  const auto work = pin.core->drain(std::numeric_limits<std::size_t>::max());
  // Preserve the old convenience loop's stop-on-refusal behavior. The
  // bounded overload and counters still account for the refused plan attempt.
  return pin.core->maintenance_refusals == before ? work : 0;
}

template<class Physical>
std::size_t ObjectStore<Physical>::drain(std::size_t work_limit) noexcept {
  if (!core_) return 0;
  Pin pin(core_); return pin.core->drain(work_limit);
}

template<class Physical>
auto ObjectStore<Physical>::statistics() const noexcept -> Statistics {
  if (!core_) return {};
  const auto alternative_count = core_->alternative_count();
  const auto directory_capacity_bytes = core_->descriptor_capacity_bytes();
  return {core_->entries.size(), core_->versions, core_->scopes,
      sizeof(Core), sizeof(Entry), Layout::node_bytes, alternative_count,
      core_->history_capacity_bytes(), directory_capacity_bytes, core_->copied_metadata_bytes,
      core_->captured_descriptor_bytes, core_->reclamation_work, core_->last_sweep_bytes,
      core_->last_sweep_cuts, core_->maintenance_refusals,
      core_->entries.capacity_bytes(), core_->entries.debt_capacity_bytes(),
      core_->reader_cuts.capacity() * sizeof(std::uint64_t), core_->entries.debt_count(), core_->remaining_entries,
      core_->entry_visits, core_->version_visits, core_->cut_visits, core_->debt_visits, core_->plan_work,
      core_->maintenance_restarts, core_->entries.debt_reservation_work(), core_->reclaimed_versions,
      core_->completed_sweeps, core_->head_refreshes, core_->dirty};
}
} // namespace orbital
