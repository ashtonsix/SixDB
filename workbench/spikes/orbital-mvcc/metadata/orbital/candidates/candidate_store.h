#pragma once

#include "history.h"
#include "inline_history.h"
#include "../retention/retention.h"
#include <expected>
#include <unordered_map>

// Experimental catalog adapter for real immutable encoded owners. This surface
// is a comparison fixture, not a promoted Orbital API. The supplied Memory
// resource must outlive the Store, scopes, descriptor owners and Part exports.
namespace compact_history {

using compact_version_probe::Memory;
using compact_version_probe::Registry;
using compact_version_probe::Slab;
using compact_version_probe::Version32;
using compact_version_probe::Version16;
using compact_version_probe::Chunks8;
using compact_descriptor::Descriptor;
using compact_descriptor::Selection;
using compact_descriptor::RepresentationSpec;
using compact_descriptor::PartSource;
using orbital::ObjectId;
using orbital::Error;
using orbital::Result;

enum class Disposition { empty, accepted, installed, aborted };

template<class Nodes, class Retention = compact_retention::FloorRetention,
    class Physical = DescriptorPhysical, class Objects = HashObjects> class Store {
  using Descriptor = typename Physical::Value;
  using Selection = typename Physical::Selection;
  using RepresentationSpec = typename Physical::Specification;
  using PartSource = typename Physical::PartSource;
public:
  using Owner = PhysicalOwner<Physical>;
  using Change = ChangeOf<Owner>;
  // A catalog-issued association. A live issuing Scope protects this borrow;
  // export_part creates its separate escaping lifetime. Callers cannot relabel
  // one selected physical realization with another version's identity.
  class BoundSelection {
    typename Physical::Selection selection_;
    [[no_unique_address]] typename Physical::Binding binding_;
    friend class Store;
    BoundSelection(Selection selected, typename Physical::Binding binding) noexcept
        : selection_(selected), binding_(binding) {}
  public:
    Selection selection() const noexcept { return selection_; }
    typename Physical::Binding binding() const noexcept { return binding_; }
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
    [[no_unique_address]] typename Physical::Binding binding_;
    friend class Store;
    ConversionTarget(Core& core, Descriptor* source, ObjectId id, std::uint64_t cut) noexcept
        : core_(&core), source_(Owner::retain(source)), binding_(Physical::binding(id, cut)) {
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

  class Prepared {
    Core* core_{};
    std::uint64_t basis_{}, occurrence_{};
    Disposition disposition_{Disposition::empty};
    std::pmr::vector<Pending> changes_;
    friend class Store;
    friend struct Core;
    explicit Prepared(Memory& memory) : changes_(&memory) {}
  public:
    Prepared(Prepared&& other) noexcept : core_(std::exchange(other.core_, nullptr)),
        basis_(other.basis_), occurrence_(other.occurrence_), disposition_(other.disposition_),
        changes_(std::move(other.changes_)) {}
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
    std::pmr::unordered_map<const Scope*, std::pmr::vector<Descriptor*>> captures;
    Scope* first_scope{};
    Scope* last_scope{};
    std::uint64_t cut{}, occurrences{}, pending{};
    std::size_t scopes{};
    std::size_t copied_metadata_bytes{}, captured_descriptor_bytes{}, reclamation_work{};
    std::size_t last_sweep_bytes{}, last_sweep_cuts{}, maintenance_refusals{};
    std::uint64_t maintenance_changes{};
    bool dirty{}, draining{};

    explicit Core(Memory& resource) : Layout(resource), captures(&resource) {}
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
      assert(pending == prepared.occurrence_); prepared.disposition_ = Disposition::aborted;
      for (auto& change : prepared.changes_) {
        const auto current = head(change.entry);
        if (current && (entries.indebted(change.entry) || !primary(current))) add_debt(change.entry);
        Layout::abort(change); // Bookkeeping precedes reentrant owner disposal.
      }
      // Owner callbacks can drain or close the Store. They cannot prepare while
      // later mutations in this batch still own entry pins.
      pending = 0;
    }
    template<class Plan> std::size_t drain_plan(const Plan& plan) noexcept {
      const auto started = maintenance_changes;
      std::size_t work = 0;
      const auto oldest = floor();
      entries.for_each_debt([&](ObjectHandle handle, Entry&) {
        auto anchor = head(handle);
        ++work;
        if (anchor) {
          auto cursor = plan.cursor(version_cut(anchor));
          while (older(anchor)) {
            auto old = older(anchor); ++work;
            if (cursor.keep(version_cut(old))) anchor = old;
            else {
              link_older(anchor, older(old));
              --versions; drop(old);
              if (maintenance_changes != started) return false;
            }
          }
        }
        const auto current = head(handle);
        if (!current || !older(current)) {
          // Accepted work can replace this tip after the sweep. Keep its debt
          // slot pinned too, so install need not allocate a fresh enrollment.
          if (!current || primary(current)) { if (!entries.pinned(handle)) remove_debt(handle); }
          else if ((Retention::uses_distinct_cuts || version_cut(current) <= oldest) && !entries.pinned(handle)) {
            remove_debt(handle); erase_head(handle);
            if (maintenance_changes != started) return false;
          }
        }
        return true;
      });
      dirty = maintenance_changes != started; draining = false; reclamation_work += work; return work;
    }
    std::size_t drain() noexcept {
      if (!dirty || draining) return 0;
      try {
        typename Retention::Plan plan(memory, scopes, floor(), [&](auto visit) {
          for (auto* scope = first_scope; scope; scope = scope->next_) visit(scope->cut_);
        });
        last_sweep_bytes = plan.requested_bytes(); last_sweep_cuts = plan.cut_count();
        draining = true; return drain_plan(plan);
      } catch (const std::bad_alloc&) {
        ++maintenance_refusals; dirty = true; return 0;
      }
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
  Result<Prepared> prepare(const Scope& basis, std::span<const Change> changes) {
    if (!core_ || basis.core_ != core_) return std::unexpected(Error::conflict);
    Pin pin(core_); auto& c = *pin.core;
    if (basis.cut_ != c.cut) return std::unexpected(Error::stale);
    if (c.pending || changes.empty()) return std::unexpected(Error::unavailable);
    if (c.cut == std::numeric_limits<std::uint64_t>::max() ||
        c.occurrences == std::numeric_limits<std::uint64_t>::max()) return std::unexpected(Error::capacity);
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
    Prepared prepared(c.memory);
    try {
      if (c.reserve(changes)) ++c.maintenance_changes;
      c.Layout::prepare(changes, c.cut + 1, prepared.changes_);
    } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
    ++c.references; prepared.core_ = &c; prepared.basis_ = c.cut;
    prepared.occurrence_ = ++c.occurrences; prepared.disposition_ = Disposition::accepted;
    c.pending = c.occurrences; return prepared;
  }
  Result<void> install(Prepared& prepared) noexcept {
    if (!core_ || prepared.core_ != core_) return std::unexpected(Error::conflict);
    auto& c = *core_;
    if (prepared.disposition_ != Disposition::accepted || c.pending != prepared.occurrence_) return std::unexpected(Error::rejected);
    if (prepared.basis_ != c.cut) return std::unexpected(Error::stale);
    for (auto& change : prepared.changes_)
      if (c.Layout::install(change, c.cut + 1)) c.add_debt(change.entry);
    ++c.cut; c.pending = 0; prepared.disposition_ = Disposition::installed; return {};
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
    return BoundSelection{Physical::selection(core_->primary(h)), Physical::binding(id, core_->version_cut(h))};
  }
  // The cold capability pins this exact Store incarnation and immutable source.
  // It does not keep obsolete history alive or authorize logical publication.
  Result<void> adopt(const ConversionTarget& target, const Owner& replacement) {
    if (!core_ || !replacement) return std::unexpected(Error::invalid_argument);
    if (target.core_ != core_ || !target.source_) return std::unexpected(Error::conflict);
    Pin pin(core_); auto& c = *pin.core;
    const auto id = Physical::bound_object(target.source_.get(), target.binding_);
    const auto cut = Physical::bound_cut(target.source_.get(), target.binding_);
    const auto h = c.locate(cut, id);
    if (!h || c.version_cut(h) != cut || !c.primary(h)) return std::unexpected(Error::absent);
    auto* next = replacement.get();
    if constexpr (requires { Physical::accepts_resource(next, c.memory); }) {
      if (!Physical::accepts_resource(next, c.memory)) return std::unexpected(Error::conflict);
    }
    if (!Physical::equivalent(c.primary(h), next)) return std::unexpected(Error::conflict);
    return c.Layout::adopt(h, next);
  }

  std::size_t drain() noexcept {
    if (!core_) return 0;
    Pin pin(core_); return pin.core->drain();
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
        core_->entries.capacity_bytes(), core_->entries.debt_capacity_bytes()};
  }
  static Selection select_private(const Scope& basis, std::span<const Change> changes, ObjectId id) noexcept {
    for (const auto& change : changes) if (change.object == id) return change.representation.selection();
    return basis.bind(id);
  }
};

using Store32 = Store<Slab<Version32>>;
using Store16 = Store<Slab<Version16>>;
using Store8 = Store<Chunks8>;
static_assert(sizeof(Store32::Scope) == 32 && sizeof(Store16::Scope) == 32 && sizeof(Store8::Scope) == 32);
static_assert(sizeof(Store32) == 8 && sizeof(Store16) == 8 && sizeof(Store8) == 8);
static_assert(sizeof(Store32::Borrowed) == 16 && sizeof(Store16::Borrowed) == 16);

} // namespace compact_history
