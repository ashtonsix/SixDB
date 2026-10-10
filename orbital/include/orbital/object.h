#pragma once
#include <orbital/types.h>
#include <orbital/detail/object/binding.h>
#include <orbital/detail/object/history.h>
#include <expected>
#include <unordered_map>

namespace orbital {
enum class PublicationState { empty, accepted, installed, aborted };

/// Embedded single-owner Objects. Local cuts number completed publications;
/// they are not SerialPosition, Epoch, journal acceptance or durability proof.
/// The resource outlives this Store, its Scopes, every Prepared packet, physical
/// owners and escaped Parts. Calls, including owner destruction, stay on one owner.
/// Resource allocation/deallocation callbacks must not reenter this catalog.
/// This differs from physical-owner disposal callbacks supported during cleanup.
/// A material instantiates cold operations once using src/object/store_impl.h.
template<class Physical> class ObjectStore {
  friend struct object_detail::Access<Physical>;
  using Value = typename Physical::Value;
  using Selection = typename Physical::Selection;
  using RepresentationSpec = typename Physical::Specification;
  using PartSource = typename Physical::PartSource;
public:
  using Owner = object_detail::PhysicalOwner<Physical>;
  using Change = object_detail::ChangeOf<Owner>;
  // A catalog-issued association. A live issuing Scope protects this borrow;
  // export_part creates its separate escaping lifetime. Callers cannot relabel
  // one selected physical realization with another version's identity.
  class BoundSelection {
    typename Physical::Selection selection_;
    ObjectBinding binding_;
    friend class ObjectStore;
    BoundSelection(Selection selected, ObjectBinding binding) noexcept
        : selection_(selected), binding_(binding) {}
  public:
    Selection selection() const noexcept { return selection_; }
    ObjectBinding binding() const noexcept { return binding_; }
    auto export_part(std::size_t index, typename Physical::ExportPolicy policy = {}) const noexcept {
      return Physical::export_part(selection_, index, binding_, policy);
    }
  };
private:
  struct Core;
  Core* core_{};
  using Layout = object_detail::History<Physical>;
  using Entry = typename Layout::Entry;
  using ObjectHandle = typename Layout::ObjectHandle;
  using Handle = typename Layout::Ref;
  using Pending = typename Layout::Mutation;

public:
  // Embedded coherent read, enrolled at one local completed cut.
  // Nonmovable, caller-resident enrollment. A stable address avoids repairing
  // links on moves. One Core pin preserves later discovery after ObjectStore reset.
  class Scope {
    friend struct object_detail::Access<Physical>;
    Core* core_{};
    std::uint64_t cut_{};
    Scope* previous_{};
    Scope* next_{};
    friend class ObjectStore;
    friend struct Core;
  public:
    explicit Scope(const ObjectStore& store) noexcept : core_(store.core_) {
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
    Result<Selection> bind_alternative(ObjectId id) const;
  };

  struct Borrowed {
    Core* core{};
    std::uint64_t cut{};
    // Borrow ends before publication, alternative replacement, suspension or
    // final ObjectStore reset. Already-bound native bytes need no version lookup.
    Selection bind(ObjectId id) const noexcept {
      return core && cut == core->cut ? core->resolve(cut, id, false) : Selection{};
    }
  };

  class ConversionTarget {
    Core* core_{};
    Owner source_;
    ObjectBinding binding_;
    friend class ObjectStore;
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

  // Embedded finished-work publication, not durable admitted input.
  // Caller-owned, single-use publication storage. An empty packet can retry
  // refusal; acceptance ends that eligibility even when subsequently aborted.
  // Every packet constructed from a live Store pins Core, so its resource must
  // outlive even empty, refused, installed and aborted packets.
  class Prepared {
    Core* core_{};
    PublicationState disposition_{PublicationState::empty};
    std::pmr::vector<Pending> changes_;
    friend class ObjectStore;
    friend struct Core;
  public:
    explicit Prepared(const ObjectStore& store) noexcept : core_(store.core_),
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
    PublicationState disposition() const noexcept { return disposition_; }
    void abort() noexcept { if (core_) core_->abort(*this); }
  };

private:
  struct Statistics {
    std::size_t objects{}, versions{}, scopes{};
    std::size_t core_bytes{}, entry_bytes{}, node_bytes{}, alternative_count{};
    std::size_t history_capacity_bytes{}, descriptor_directory_capacity_bytes{};
    std::size_t copied_metadata_bytes{}, captured_descriptor_bytes{}, reclamation_work{};
    std::size_t last_sweep_bytes{}, last_sweep_cuts{}, maintenance_refusals{};
    std::size_t object_directory_capacity_bytes{}, debt_capacity_bytes{};
    std::size_t cut_capacity_bytes{}, debt_objects{}, maintenance_remaining_entries{};
    std::size_t entry_visits{}, version_visits{}, cut_visits{}, debt_visits{}, plan_work{}, maintenance_restarts{};
    std::size_t debt_reservation_work{}, reclaimed_versions{}, completed_sweeps{}, head_refreshes{};
    bool maintenance_pending{};
  };

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

    explicit Core(std::pmr::memory_resource& resource) : Layout(resource), captures(&resource), reader_cuts(&resource) {}
    ~Core() { assert(!first_scope && !pending); this->dispose(); }
    void release() noexcept { if (!--references) object_detail::destroy(memory, this); }
    std::uint64_t floor() const noexcept { return first_scope ? first_scope->cut_ : cut; }
    void add_debt(ObjectHandle entry) noexcept {
      entries.add_debt(entry);
      dirty = true; ++maintenance_changes;
    }
    void remove_debt(ObjectHandle entry) noexcept { entries.remove_debt(entry); }
    Result<Selection> capture_selection(const Scope&, Value*);
    void close(Scope& scope) noexcept;
    void abort(Prepared& prepared) noexcept;
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
    std::size_t drain(std::size_t limit) noexcept;
  };
  struct Pin {
    Core* core;
    explicit Pin(Core* c) noexcept : core(c) { ++core->references; }
    ~Pin() { core->release(); }
  };

public:
  explicit ObjectStore(std::pmr::memory_resource& memory);
  ObjectStore() = default;
  ObjectStore(ObjectStore&& other) noexcept : core_(std::exchange(other.core_, nullptr)) {}
  ObjectStore& operator=(ObjectStore other) noexcept { std::swap(core_, other.core_); return *this; }
  ObjectStore(const ObjectStore&) = delete;
  ~ObjectStore() {
    // Disposal can call back through this handle; it must see an empty Store.
    if (auto* core = std::exchange(core_, nullptr)) core->release();
  }
  Borrowed borrow() const & noexcept { return {core_, core_ ? core_->cut : 0}; }
  Borrowed borrow() const && = delete;
  Result<Owner> capture(const RepresentationSpec& spec, std::span<const PartSource> parts) const;
  Result<void> prepare(const Scope& basis, std::span<const Change> changes, Prepared& prepared);
  Result<void> install(Prepared& prepared) noexcept;
  Result<ConversionTarget> conversion(const Scope& basis, ObjectId id) const noexcept;
  Result<BoundSelection> bound(const Scope& basis, ObjectId id) const noexcept;
  // Captures the exact alternative now selected at this Scope's logical cut.
  // Later adoption cannot redirect its escaping Part to a different realization.
  Result<BoundSelection> bound_alternative(const Scope& basis, ObjectId id) const noexcept;
  // The cold capability pins this exact ObjectStore incarnation and immutable source.
  // It does not keep obsolete history alive or authorize logical publication.
  Result<void> adopt(const ConversionTarget& target, const Owner& replacement);

  std::size_t drain() noexcept;
  // A work limit caps metadata steps, including planning and restart attempts.
  // It cannot cap malloc, OS calls or arbitrary backing-owner destructor time.
  std::size_t drain(std::size_t work_limit) noexcept;
private:
  Statistics statistics() const noexcept;
public:
  static Selection select_private(const Scope& basis, std::span<const Change> changes, ObjectId id) noexcept {
    for (const auto& change : changes) if (change.object == id) return change.representation.selection();
    return basis.bind(id);
  }
};

} // namespace orbital
