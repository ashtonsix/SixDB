#pragma once

#include "versions.h"
#include "physical.h"
#include "object_policy.h"
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
struct Hash {
  std::size_t operator()(ObjectId id) const noexcept { return id.high ^ (id.low * 0x9e3779b97f4a7c15ULL); }
};
struct Empty {};

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
  using Handle = typename Nodes::Handle;
  using Entry = typename Objects::Entry;
  using ObjectHandle = typename Objects::Handle;
  struct Pending { ObjectHandle entry; Handle version; };

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
  using AlternativeMap = std::pmr::unordered_map<Handle, Descriptor*>;
  struct Core {
    Memory& memory;
    std::size_t references{1};
    Nodes nodes;
    [[no_unique_address]] std::conditional_t<Nodes::indexed_rep, Registry, Empty> directory;
    Objects entries;
    [[no_unique_address]] std::conditional_t<Nodes::inline_alternative, Empty, AlternativeMap> alternatives;
    std::pmr::unordered_map<const Scope*, std::pmr::vector<Descriptor*>> captures;
    Scope* first_scope{};
    Scope* last_scope{};
    std::uint64_t cut{}, occurrences{}, pending{};
    std::size_t versions{}, scopes{};
    std::size_t copied_metadata_bytes{}, captured_descriptor_bytes{}, reclamation_work{};
    std::size_t last_sweep_bytes{}, last_sweep_cuts{}, maintenance_refusals{};
    std::uint64_t maintenance_changes{};
    bool dirty{}, draining{};

    explicit Core(Memory& resource) : memory(resource), nodes(resource), directory([&] {
          if constexpr (Nodes::indexed_rep) return Registry(resource);
          else return Empty{};
        }()),
        entries(resource), alternatives([&] {
          if constexpr (Nodes::inline_alternative) return Empty{};
          else return AlternativeMap(&resource);
        }()), captures(&resource) {}
    ~Core() {
      assert(!first_scope && !pending);
      entries.for_each_live([&](ObjectHandle, Entry& entry) {
        auto h = entry.head;
        while (h) { auto older = nodes.get(h).older; drop(h); h = older; }
      });
    }
    void release() noexcept { if (!--references) memory.destroy(this); }
    std::uint64_t floor() const noexcept { return first_scope ? first_scope->cut_ : cut; }
    Descriptor* primary(Handle h) const noexcept {
      if constexpr (Nodes::indexed_rep) return static_cast<Descriptor*>(directory.get(nodes.get(h).primary));
      else return static_cast<Descriptor*>(nodes.get(h).primary);
    }
    Descriptor* alternate(Handle h) const noexcept {
      if constexpr (Nodes::inline_alternative) return static_cast<Descriptor*>(nodes.get(h).alternative);
      else { auto at = alternatives.find(h); return at == alternatives.end() ? nullptr : at->second; }
    }
    void initialize(Handle h, Descriptor* rep) {
      if constexpr (Nodes::indexed_rep) nodes.get(h).primary = rep ? directory.add(rep) : 0;
      else nodes.get(h).primary = rep;
      if (rep) Physical::retain(rep);
    }
    void drop(Handle h) noexcept {
      auto* descriptor = primary(h);
      Descriptor* alternative{};
      if constexpr (Nodes::indexed_rep) { if (nodes.get(h).primary) directory.erase(nodes.get(h).primary); }
      if constexpr (Nodes::inline_alternative) {
        alternative = static_cast<Descriptor*>(nodes.get(h).alternative);
      } else {
        auto at = alternatives.find(h);
        if (at != alternatives.end()) { alternative = at->second; alternatives.erase(at); }
      }
      nodes.erase(h);
      if (alternative) Physical::release(alternative);
      if (descriptor) Physical::release(descriptor);
    }
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
    Handle locate(std::uint64_t selected_cut, ObjectId id) noexcept {
      auto at = entries.find(id);
      if (!at) return {};
      auto h = entries.get(at).head;
      while (h && nodes.cut(h) > selected_cut) h = nodes.get(h).older;
      return h;
    }
    Selection resolve(std::uint64_t selected_cut, ObjectId id, bool alternative) noexcept {
      auto h = locate(selected_cut, id);
      if (!h || !primary(h)) return {};
      return Physical::selection(alternative ? alternate(h) : primary(h));
    }
    void abort(Prepared& prepared) noexcept {
      if (prepared.disposition_ != Disposition::accepted) return;
      assert(pending == prepared.occurrence_); pending = 0; prepared.disposition_ = Disposition::aborted;
      for (auto& change : prepared.changes_) {
        const auto handle = std::exchange(change.entry, ObjectHandle{});
        auto& entry = entries.get(handle);
        const auto version = std::exchange(change.version, Handle{});
        assert(entries.pinned(handle)); entries.unpin(handle);
        if (!entry.head) {
          remove_debt(handle); const bool erased = entries.erase(handle); assert(erased); (void)erased;
        } else if (!primary(entry.head)) add_debt(handle);
        drop(version); // No raw Entry/Version use remains after owner callbacks.
      }
    }
    template<class Plan> std::size_t drain_plan(const Plan& plan) noexcept {
      const auto started = maintenance_changes;
      std::size_t work = 0;
      const auto oldest = floor();
      entries.for_each_debt([&](ObjectHandle handle, Entry& entry) {
        auto anchor = entry.head;
        ++work;
        if (anchor) {
          auto cursor = plan.cursor(nodes.cut(anchor));
          while (nodes.get(anchor).older) {
            auto old = nodes.get(anchor).older; ++work;
            if (cursor.keep(nodes.cut(old))) anchor = old;
            else {
              nodes.get(anchor).older = nodes.get(old).older;
              --versions; drop(old);
              if (maintenance_changes != started) return false;
            }
          }
        }
        auto head = entry.head;
        if (!head || !nodes.get(head).older) {
          if (!head || primary(head)) remove_debt(handle);
          else if ((Retention::uses_distinct_cuts || nodes.cut(head) <= oldest) && !entries.pinned(handle)) {
            remove_debt(handle); --versions;
            const bool erased = entries.erase(handle); assert(erased); (void)erased;
            drop(head);
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
    std::size_t new_debts = 0;
    for (std::size_t i = 0; i < changes.size(); ++i) {
      const auto& change = changes[i];
      auto* rep = change.representation.get();
      if (rep && !Physical::accepts(rep, change.object, c.cut + 1))
        return std::unexpected(Error::conflict);
      if constexpr (requires { Physical::accepts_resource(rep, c.memory); }) {
        if (rep && !Physical::accepts_resource(rep, c.memory)) return std::unexpected(Error::conflict);
      }
      for (std::size_t j = 0; j < i; ++j) if (changes[j].object == change.object) return std::unexpected(Error::conflict);
      const auto entry = c.entries.find(change.object);
      if ((!entry || !c.entries.indebted(entry)) && ((entry && c.entries.get(entry).head) || !rep)) ++new_debts;
    }
    Prepared prepared(c.memory);
    std::pmr::vector<Handle> allocated(&c.memory);
    std::size_t initialized = 0;
    try {
      prepared.changes_.reserve(changes.size());
      if (c.entries.reserve_debt(new_debts)) ++c.maintenance_changes;
      allocated = c.nodes.batch(changes.size(), c.cut + 1);
      for (std::size_t i = 0; i < changes.size(); ++i) {
        c.initialize(allocated[i], changes[i].representation.get()); ++initialized;
        auto [where, inserted] = c.entries.ensure(changes[i].object); (void)inserted;
        prepared.changes_.push_back({where, allocated[i]});
      }
    } catch (const std::bad_alloc&) {
      for (std::size_t i = 0; i < allocated.size(); ++i) {
        if (i < initialized) c.drop(allocated[i]); else c.nodes.erase(allocated[i]);
      }
      for (const auto& change : changes) {
        auto at = c.entries.find(change.object);
        if (at && !c.entries.get(at).head && !c.entries.pinned(at)) {
          const bool erased = c.entries.erase(at); assert(erased); (void)erased;
        }
      }
      return std::unexpected(Error::capacity);
    }
    for (auto& change : prepared.changes_) {
      const bool pinned = c.entries.pin(change.entry); assert(pinned); (void)pinned;
    }
    ++c.references; prepared.core_ = &c; prepared.basis_ = c.cut;
    prepared.occurrence_ = ++c.occurrences; prepared.disposition_ = Disposition::accepted;
    c.pending = c.occurrences; return prepared;
  }
  Result<void> install(Prepared& prepared) noexcept {
    if (!core_ || prepared.core_ != core_) return std::unexpected(Error::conflict);
    auto& c = *core_;
    if (prepared.disposition_ != Disposition::accepted || c.pending != prepared.occurrence_) return std::unexpected(Error::rejected);
    if (prepared.basis_ != c.cut) return std::unexpected(Error::stale);
    for (auto& change : prepared.changes_) {
      auto& entry = c.entries.get(change.entry);
      c.nodes.get(change.version).older = entry.head;
      entry.head = change.version; c.entries.unpin(change.entry); ++c.versions;
      if (c.nodes.get(change.version).older || !c.primary(change.version)) c.add_debt(change.entry);
    }
    ++c.cut; c.pending = 0; prepared.disposition_ = Disposition::installed; return {};
  }
  Result<ConversionTarget> conversion(const Scope& basis, ObjectId id) const noexcept {
    if (!core_ || basis.core_ != core_) return std::unexpected(Error::conflict);
    auto h = core_->locate(basis.cut_, id);
    if (!h || !core_->primary(h)) return std::unexpected(Error::absent);
    return ConversionTarget(*core_, core_->primary(h), id, core_->nodes.cut(h));
  }
  Result<BoundSelection> bound(const Scope& basis, ObjectId id) const noexcept {
    if (!core_ || basis.core_ != core_) return std::unexpected(Error::conflict);
    auto h = core_->locate(basis.cut_, id);
    if (!h || !core_->primary(h)) return std::unexpected(Error::absent);
    return BoundSelection{Physical::selection(core_->primary(h)), Physical::binding(id, core_->nodes.cut(h))};
  }
  // The cold capability pins this exact Store incarnation and immutable source.
  // It does not keep obsolete history alive or authorize logical publication.
  Result<void> adopt(const ConversionTarget& target, const Owner& replacement) {
    if (!core_ || !replacement) return std::unexpected(Error::invalid_argument);
    if (target.core_ != core_ || !target.source_) return std::unexpected(Error::conflict);
    Pin pin(core_); auto& c = *pin.core;
    const auto id = Physical::bound_object(target.source_.get(), target.binding_);
    const auto cut = Physical::bound_cut(target.source_.get(), target.binding_);
    auto at = c.entries.find(id); auto h = at ? c.entries.get(at).head : Handle{};
    while (h && c.nodes.cut(h) > cut) h = c.nodes.get(h).older;
    if (!h || c.nodes.cut(h) != cut || !c.primary(h)) return std::unexpected(Error::absent);
    auto* next = replacement.get();
    if constexpr (requires { Physical::accepts_resource(next, c.memory); }) {
      if (!Physical::accepts_resource(next, c.memory)) return std::unexpected(Error::conflict);
    }
    if (!Physical::equivalent(c.primary(h), next)) return std::unexpected(Error::conflict);
    if constexpr (Nodes::inline_alternative) {
      Physical::retain(next);
      auto* old = static_cast<Descriptor*>(c.nodes.get(h).alternative); c.nodes.get(h).alternative = next;
      if (old) Physical::release(old);
    } else {
      try {
        auto [where, inserted] = c.alternatives.try_emplace(h, next); Physical::retain(next);
        if (!inserted) { auto* old = where->second; where->second = next; Physical::release(old); }
      } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
    }
    return {};
  }
  std::size_t drain() noexcept {
    if (!core_) return 0;
    Pin pin(core_); return pin.core->drain();
  }
  Statistics statistics() const noexcept {
    if (!core_) return {};
    std::size_t alternative_count = 0;
    std::size_t directory_capacity_bytes = 0;
    if constexpr (!Nodes::inline_alternative) alternative_count = core_->alternatives.size();
    if constexpr (Nodes::indexed_rep) directory_capacity_bytes = core_->directory.capacity_bytes();
    return {core_->memory.live, core_->memory.peak, core_->entries.size(), core_->versions, core_->scopes,
        sizeof(Core), sizeof(Entry), sizeof(typename Nodes::Node), alternative_count, core_->memory.calls,
        core_->nodes.capacity_bytes(), directory_capacity_bytes, core_->copied_metadata_bytes,
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
