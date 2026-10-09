#pragma once

#include "history.h"
#include "inline_history.h"
#include "../retention/retention.h"
#include <expected>
#include <unordered_map>

// Experimental catalog adapter for real immutable encoded owners. This surface
// is a comparison fixture, not a promoted Orbital API. The supplied Memory
// resource must outlive the Store, scopes, physical owners and Part exports.
namespace compact_history {

using compact_version_probe::Memory;
using compact_version_probe::Registry;
using compact_version_probe::Slab;
using compact_version_probe::Version32;
using compact_version_probe::Version16;
using compact_version_probe::Chunks8;
using orbital::ObjectId;
using orbital::Error;
using orbital::Result;

enum class Disposition { empty, accepted, installed, aborted };

// Catalog-issued identity snapshot; physical ownership alone grants no labels.
struct Binding { ObjectId object{}; std::uint64_t version{}; };

template<class Nodes, class Retention, class Physical, class Objects = HashObjects> class Store {
  using Value = typename Physical::Value;
  using Selection = typename Physical::Selection;
  using RepresentationSpec = typename Physical::Specification;
  using PartSource = typename Physical::PartSource;
public:
  static constexpr bool exact_retention = Retention::uses_distinct_cuts;
  using Owner = PhysicalOwner<Physical>;
  using Change = ChangeOf<Owner>;
  // A catalog-issued association. A live issuing Scope protects this borrow;
  // export_part creates its separate escaping lifetime. Callers cannot relabel
  // one selected physical realization with another version's identity.
  class BoundSelection {
    typename Physical::Selection selection_;
    Binding binding_;
    friend class Store;
    BoundSelection(Selection selected, Binding binding) noexcept
        : selection_(selected), binding_(binding) {}
  public:
    Selection selection() const noexcept { return selection_; }
    Binding binding() const noexcept { return binding_; }
    auto export_part(std::size_t index, typename Physical::ExportPolicy policy = {}) const noexcept {
      return Physical::export_part(selection_, index, binding_, policy);
    }
  };
private:
  struct Core;
  Core* core_{};
  static constexpr bool inline_tip = [] {
    if constexpr (requires { Objects::inline_current; }) return Objects::inline_current;
    else return false;
  }();
  using Layout = std::conditional_t<inline_tip, InlineHistory<Nodes, Physical, Objects>,
      NodeHistory<Nodes, Physical, Objects>>;
  using Entry = typename Layout::Entry;
  using ObjectHandle = typename Layout::ObjectHandle;
  using Handle = typename Layout::Ref;
  using Pending = typename Layout::Mutation;

public:
  // Nonmovable, caller-resident enrollment. A stable address avoids repairing
  // links on moves. One Core pin preserves later discovery after Store reset.
  class Scope {
    Core* core_{};
    std::uint64_t cut_{};
    Scope* previous_{};
    Scope* next_{};
    friend class Store;
    friend struct Core;
  public:
    explicit Scope(const Store& store) noexcept : core_(store.core_) {
      if (!core_) return;
      ++core_->references; cut_ = core_->cut;
      previous_ = core_->last_scope;
      if (previous_) previous_->next_ = this; else core_->first_scope = this;
      core_->last_scope = this; ++core_->scopes;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { if (core_) core_->close(*this); }
    std::uint64_t cut() const noexcept { return cut_; }
    Selection bind(ObjectId id) const noexcept { return core_ ? core_->resolve(cut_, id, false) : Selection{}; }
    Result<Selection> bind_alternative(ObjectId id) const {
      if (!core_) return std::unexpected(Error::invalid_argument);
      auto selected = core_->resolve(cut_, id, true);
      if (!selected) return std::unexpected(Error::absent);
      try {
        auto [where, inserted] = core_->captures.try_emplace(this); (void)inserted;
        auto& captured = where->second;
        for (auto* owner : captured) if (owner == Physical::get(selected)) return selected;
        captured.push_back(Physical::get(selected));
        Physical::retain(Physical::get(selected));
      } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
      return selected;
    }
  };

  struct Borrowed {
    Core* core{};
    std::uint64_t cut{};
    // Borrow ends before publication, alternative replacement, suspension or
    // final Store reset. Already-bound native bytes need no version lookup.
    Selection bind(ObjectId id) const noexcept {
      return core && cut == core->cut ? core->resolve(cut, id, false) : Selection{};
    }
  };

  class ConversionTarget {
    Core* core_{};
    Owner source_;
    Binding binding_;
    friend class Store;
    ConversionTarget(Core& core, Value* source, ObjectId id, std::uint64_t cut) noexcept
        : core_(&core), source_(Owner::retain(source)), binding_{id, cut} {
      ++core.references;
    }
  public:
    ConversionTarget(ConversionTarget&& other) noexcept
        : core_(std::exchange(other.core_, nullptr)), source_(std::move(other.source_)), binding_(other.binding_) {}
    ConversionTarget(const ConversionTarget&) = delete;
    ConversionTarget& operator=(const ConversionTarget&) = delete;
    ~ConversionTarget() {
      if (!core_) return;
      source_ = {};
      auto* owner = std::exchange(core_, nullptr); owner->release();
    }
    Selection source() const noexcept { return source_.selection(); }
  };

  // Caller-owned, single-use publication storage. An empty packet can retry
  // refusal; acceptance ends that eligibility even when subsequently aborted.
  class Prepared {
    Core* core_{};
    Disposition disposition_{Disposition::empty};
    std::pmr::vector<Pending> changes_;
    friend class Store;
    friend struct Core;
  public:
    explicit Prepared(const Store& store) noexcept : core_(store.core_),
        changes_(core_ ? &core_->memory : std::pmr::get_default_resource()) {
      if (core_) ++core_->references;
    }
    Prepared(const Prepared&) = delete;
    Prepared& operator=(const Prepared&) = delete;
    ~Prepared() {
      if (!core_) return;
      core_->abort(*this);
      auto* owner = core_; core_ = nullptr; owner->release();
    }
    Disposition disposition() const noexcept { return disposition_; }
    void abort() noexcept { if (core_) core_->abort(*this); }
  };

  struct Statistics {
    std::size_t requested_bytes{}, peak_bytes{}, objects{}, versions{}, scopes{};
    std::size_t core_bytes{}, entry_bytes{}, node_bytes{}, alternative_count{};
    std::size_t allocation_calls{};
    std::size_t history_capacity_bytes{}, descriptor_directory_capacity_bytes{};
    std::size_t copied_metadata_bytes{}, captured_descriptor_bytes{}, reclamation_work{};
    std::size_t last_sweep_bytes{}, last_sweep_cuts{}, maintenance_refusals{};
    std::size_t object_directory_capacity_bytes{}, debt_capacity_bytes{};
    std::size_t cut_capacity_bytes{}, debt_objects{}, maintenance_remaining_entries{};
    std::size_t entry_visits{}, version_visits{}, cut_visits{}, debt_visits{}, plan_work{}, maintenance_restarts{};
    std::size_t debt_reservation_work{}, reclaimed_versions{}, completed_sweeps{}, head_refreshes{};
    bool maintenance_pending{};
  };

private:
  struct Core : Layout {
    using Layout::memory;
    using Layout::entries;
    using Layout::versions;
    using Layout::primary;
    using Layout::locate;
    using Layout::resolve;
    using Layout::head;
    using Layout::older;
    using Layout::link_older;
    using Layout::version_cut;
    using Layout::drop;
    using Layout::erase_head;
    std::size_t references{1};
    std::pmr::unordered_map<const Scope*, std::pmr::vector<Value*>> captures;
    Scope* first_scope{};
    Scope* last_scope{};
    std::uint64_t cut{};
    std::size_t scopes{};
    std::size_t copied_metadata_bytes{}, captured_descriptor_bytes{}, reclamation_work{};
    std::size_t last_sweep_bytes{}, last_sweep_cuts{}, maintenance_refusals{};
    std::uint64_t maintenance_changes{};
    // A cut snapshot is conservative through scope release. New scopes start
    // at the current cut, so the unsampled suffix is protected through the
    // plan's boundary until the next sweep. Closing a builder's exact Scope
    // repairs its pointer before unlinking; unrelated writes cannot restart it.
    std::uint64_t reader_changes{}, plan_generation = UINT64_MAX;
    std::uint64_t pass_generation{}, planned_floor{}, building_cut{}, anchor_cut{};
    std::pmr::vector<std::uint64_t> reader_cuts;
    Scope* next_cut{};
    ObjectHandle debt_entry{};
    Handle anchor{};
    std::size_t remaining_entries{}, remaining_cuts{};
    std::size_t entry_visits{}, version_visits{}, cut_visits{}, debt_visits{}, plan_work{}, maintenance_restarts{};
    std::size_t reclaimed_versions{}, completed_sweeps{}, head_refreshes{};
    enum class Step { idle, cuts, entry, older, consume, finish, seek };
    Step step{Step::idle};
    Step resume_step{Step::idle};
    bool crossed_floor{};
    bool callback_restart{};
    bool dirty{}, draining{}, pending{};

    explicit Core(Memory& resource) : Layout(resource), captures(&resource), reader_cuts(&resource) {}
    ~Core() { assert(!first_scope && !pending); this->dispose(); }
    void release() noexcept { if (!--references) memory.destroy(this); }
    std::uint64_t floor() const noexcept { return first_scope ? first_scope->cut_ : cut; }
    void add_debt(ObjectHandle entry) noexcept {
      entries.add_debt(entry);
      dirty = true; ++maintenance_changes;
    }
    void remove_debt(ObjectHandle entry) noexcept { entries.remove_debt(entry); }
    void close(Scope& scope) noexcept {
      const auto before = floor();
      const bool removed_distinct = Retention::uses_distinct_cuts && scope.cut_ < cut &&
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
    void abort(Prepared& prepared) noexcept {
      if (prepared.disposition_ != Disposition::accepted) return;
      assert(pending); prepared.disposition_ = Disposition::aborted;
      for (auto& change : prepared.changes_) {
        const auto current = head(change.entry);
        if (current && (entries.indebted(change.entry) || !primary(current))) add_debt(change.entry);
        Layout::abort(change); // Bookkeeping precedes reentrant owner disposal.
      }
      // Owner callbacks can drain or close the Store. They cannot prepare while
      // later mutations in this batch still own entry pins.
      pending = false;
    }
    void begin_entries() noexcept {
      remaining_entries = entries.debt_count();
      step = Step::entry;
    }
    void restart() noexcept {
      // A callback may keep changing the front object. Rotation preserves the
      // progress of other objects even when this particular chain must restart.
      if (debt_entry) { assert(entries.front_debt() == debt_entry); entries.rotate_debt(); }
      if (step == Step::cuts) plan_generation = UINT64_MAX;
      debt_entry = {}; anchor = {}; next_cut = nullptr; step = Step::idle;
      callback_restart = false; dirty = true; ++maintenance_restarts;
    }
    std::size_t drain(std::size_t limit) noexcept {
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
          planned_floor = Retention::uses_distinct_cuts ? cut : floor();
          if constexpr (Retention::uses_distinct_cuts) {
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
          remaining_cuts = reader_cuts.size();
          step = !anchor ? Step::finish : Retention::uses_distinct_cuts ? Step::consume : Step::older;
          break;
        case Step::older: {
          ++work; ++version_visits;
          const auto old = older(anchor);
          if (!old) { step = Step::finish; break; }
          const auto old_cut = version_cut(old);
          bool keep;
          if constexpr (Retention::uses_distinct_cuts) {
            // Scopes added after this plan can select the suffix above its
            // boundary or its first predecessor. Keep that range this sweep.
            const bool newer = !crossed_floor;
            if (newer) crossed_floor = old_cut <= planned_floor;
            keep = newer || (remaining_cuts && reader_cuts[remaining_cuts - 1] >= old_cut);
          }
          else { keep = !crossed_floor; if (keep) crossed_floor = old_cut <= planned_floor; }
          if (keep) { anchor = old; anchor_cut = old_cut; if constexpr (Retention::uses_distinct_cuts) step = Step::consume; }
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
          if (reader_cuts[remaining_cuts - 1] >= version_cut(anchor)) --remaining_cuts;
          else step = Step::older;
          break;
        case Step::finish: {
          ++work; ++debt_visits;
          const auto entry = std::exchange(debt_entry, ObjectHandle{});
          anchor = {}; --remaining_entries; step = Step::entry;
          const auto current = head(entry);
          // Pins preserve the reserved debt slot across accepted work. Install
          // transfers the tip without allocating or disposing any user owner.
          if ((!current || !older(current)) && !entries.pinned(entry)) {
            if (!current || primary(current)) remove_debt(entry);
            else if (Retention::uses_distinct_cuts || version_cut(current) <= planned_floor) {
              remove_debt(entry); ++reclaimed_versions; erase_head(entry);
            } else entries.rotate_debt();
          } else entries.rotate_debt();
          break;
        }
        case Step::seek:
          ++work; ++version_visits;
          assert(anchor);
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
  };
  struct Pin {
    Core* core;
    explicit Pin(Core* c) noexcept : core(c) { ++core->references; }
    ~Pin() { core->release(); }
  };

public:
  explicit Store(Memory& memory) : core_(memory.make<Core>(memory)) {}
  Store() = default;
  Store(Store&& other) noexcept : core_(std::exchange(other.core_, nullptr)) {}
  Store& operator=(Store other) noexcept { std::swap(core_, other.core_); return *this; }
  Store(const Store&) = delete;
  ~Store() { if (core_) core_->release(); }
  Borrowed borrow() const & noexcept { return {core_, core_ ? core_->cut : 0}; }
  Borrowed borrow() const && = delete;
  Result<Owner> capture(const RepresentationSpec& spec, std::span<const PartSource> parts) const {
    if (!core_) return std::unexpected(Error::invalid_argument);
    Pin pin(core_);
    auto captured = Physical::capture(pin.core->memory, spec, parts);
    if (!captured) return std::unexpected(captured.error());
    const auto footprint = Physical::footprint(*captured);
    pin.core->copied_metadata_bytes += footprint.copied_metadata;
    pin.core->captured_descriptor_bytes += footprint.requested_bytes;
    return Owner::take(*captured);
  }
  Result<void> prepare(const Scope& basis, std::span<const Change> changes, Prepared& prepared) {
    if (!core_ || basis.core_ != core_ || prepared.core_ != core_) return std::unexpected(Error::conflict);
    auto& c = *prepared.core_; // The caller-owned packet already pins this Core.
    if (basis.cut_ != c.cut) return std::unexpected(Error::stale);
    if (c.pending || changes.empty() || prepared.disposition_ != Disposition::empty)
      return std::unexpected(Error::unavailable);
    if (c.cut == std::numeric_limits<std::uint64_t>::max()) return std::unexpected(Error::capacity);
    for (std::size_t i = 0; i < changes.size(); ++i) {
      const auto& change = changes[i];
      auto* rep = change.representation.get();
      if (rep && !Physical::accepts(rep, change.object, c.cut + 1))
        return std::unexpected(Error::conflict);
      if constexpr (requires { Physical::accepts_resource(rep, c.memory); }) {
        if (rep && !Physical::accepts_resource(rep, c.memory)) return std::unexpected(Error::conflict);
      }
      for (std::size_t j = 0; j < i; ++j) if (changes[j].object == change.object) return std::unexpected(Error::conflict);
    }
    assert(prepared.changes_.empty());
    try {
      c.reserve(changes);
      c.Layout::prepare(changes, c.cut + 1, prepared.changes_);
    } catch (const std::bad_alloc&) {
      prepared.changes_.clear(); // Layout rolled back owners/slots; retry must not revisit them.
      return std::unexpected(Error::capacity);
    } catch (...) {
      prepared.changes_.clear(); throw;
    }
    prepared.disposition_ = Disposition::accepted; c.pending = true; return {};
  }
  Result<void> install(Prepared& prepared) noexcept {
    if (!core_ || prepared.core_ != core_) return std::unexpected(Error::conflict);
    auto& c = *core_;
    if (prepared.disposition_ != Disposition::accepted || !c.pending) return std::unexpected(Error::rejected);
    // One accepted packet excludes every other publication. Its predecessor
    // therefore cannot change before install or completion of abort cleanup.
    for (auto& change : prepared.changes_)
      if (c.Layout::install(change, c.cut + 1)) c.add_debt(change.entry);
    if constexpr (Retention::uses_distinct_cuts)
      if (c.last_scope && c.last_scope->cut_ == c.cut) ++c.reader_changes;
    ++c.cut; prepared.disposition_ = Disposition::installed; c.pending = false; return {};
  }
  Result<ConversionTarget> conversion(const Scope& basis, ObjectId id) const noexcept {
    if (!core_ || basis.core_ != core_) return std::unexpected(Error::conflict);
    auto h = core_->locate(basis.cut_, id);
    if (!h || !core_->primary(h)) return std::unexpected(Error::absent);
    return ConversionTarget(*core_, core_->primary(h), id, core_->version_cut(h));
  }
  Result<BoundSelection> bound(const Scope& basis, ObjectId id) const noexcept {
    if (!core_ || basis.core_ != core_) return std::unexpected(Error::conflict);
    auto h = core_->locate(basis.cut_, id);
    if (!h || !core_->primary(h)) return std::unexpected(Error::absent);
    return BoundSelection{Physical::selection(core_->primary(h)), Binding{id, core_->version_cut(h)}};
  }
  // The cold capability pins this exact Store incarnation and immutable source.
  // It does not keep obsolete history alive or authorize logical publication.
  Result<void> adopt(const ConversionTarget& target, const Owner& replacement) {
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

  std::size_t drain() noexcept {
    if (!core_) return 0;
    Pin pin(core_);
    const auto before = pin.core->maintenance_refusals;
    const auto work = pin.core->drain(std::numeric_limits<std::size_t>::max());
    // Preserve the old convenience loop's stop-on-refusal behavior. The
    // bounded overload and counters still account for the refused plan attempt.
    return pin.core->maintenance_refusals == before ? work : 0;
  }
  // A work limit caps metadata steps, including planning and restart attempts.
  // It cannot cap malloc, OS calls or arbitrary backing-owner destructor time.
  std::size_t drain(std::size_t work_limit) noexcept {
    if (!core_) return 0;
    Pin pin(core_); return pin.core->drain(work_limit);
  }
  Statistics statistics() const noexcept {
    if (!core_) return {};
    const auto alternative_count = core_->alternative_count();
    const auto directory_capacity_bytes = core_->descriptor_capacity_bytes();
    return {core_->memory.live, core_->memory.peak, core_->entries.size(), core_->versions, core_->scopes,
        sizeof(Core), sizeof(Entry), Layout::node_bytes, alternative_count, core_->memory.calls,
        core_->history_capacity_bytes(), directory_capacity_bytes, core_->copied_metadata_bytes,
        core_->captured_descriptor_bytes, core_->reclamation_work, core_->last_sweep_bytes,
        core_->last_sweep_cuts, core_->maintenance_refusals,
        core_->entries.capacity_bytes(), core_->entries.debt_capacity_bytes(),
        core_->reader_cuts.capacity() * sizeof(std::uint64_t), core_->entries.debt_count(), core_->remaining_entries,
        core_->entry_visits, core_->version_visits, core_->cut_visits, core_->debt_visits, core_->plan_work,
        core_->maintenance_restarts, core_->entries.debt_reservation_work(), core_->reclaimed_versions,
        core_->completed_sweeps, core_->head_refreshes, core_->dirty};
  }
  static Selection select_private(const Scope& basis, std::span<const Change> changes, ObjectId id) noexcept {
    for (const auto& change : changes) if (change.object == id) return change.representation.selection();
    return basis.bind(id);
  }
};

} // namespace compact_history
