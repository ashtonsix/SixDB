#pragma once

#include <orbital/mvcc_baseline.h>
#include <algorithm>
#include <cassert>
#include <limits>
#include <memory_resource>
#include <unordered_map>
#include <vector>

namespace orbital::object::detail {

struct Base;
struct Domain final : std::pmr::memory_resource {
  worker::Lane* lane;
  std::size_t references{1}, limit, charged{sizeof(Domain)}, peak{sizeof(Domain)};
  Base* first{};
  Base* last{};
  std::size_t queued{};
  bool detached{};
  Domain(worker::Lane& owner, std::size_t maximum) : lane(&owner), limit(maximum) {}
  void owner() const noexcept { assert(worker::on_lane(*lane)); }
private:
  void* do_allocate(std::size_t, std::size_t) override;
  void do_deallocate(void*, std::size_t, std::size_t) override;
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

struct Base {
  Domain* domain;
  std::size_t references{};
  Base* retirement_next{};
  void (*destroy)(Base*) noexcept{};
  explicit Base(Domain& d) : domain(&d) {}
};

void retain_domain(Domain&) noexcept;
void release_domain(Domain&) noexcept;
void retain_base(Base&) noexcept;
void release_base(Base&) noexcept;
std::size_t drain_domain(Domain&, std::size_t) noexcept;

template<class T, class... Args>
Ref<T> make(Domain& domain, Args&&... args) {
  auto* memory = domain.allocate(sizeof(T), alignof(T));
  retain_domain(domain);
  T* value;
  try { value = std::construct_at(static_cast<T*>(memory), domain, std::forward<Args>(args)...); }
  catch (...) {
    domain.deallocate(memory, sizeof(T), alignof(T));
    release_domain(domain);
    throw;
  }
  value->destroy = [](Base* base) noexcept {
    auto* typed = static_cast<T*>(base);
    auto& d = *typed->domain;
    std::destroy_at(typed);
    d.deallocate(typed, sizeof(T), alignof(T));
    release_domain(d);
  };
  return Ref<T>::retain(*value);
}

struct Info : Base {
  Description description;
  std::pmr::vector<std::byte> logical, interpretation;
  std::shared_ptr<const void> dependencies;
  Info(Domain&, const RepresentationSpec&);
};
void retain_ref(Info&) noexcept;
void release_ref(Info&) noexcept;

struct Part : Base {
  Leaf leaf;
  Ref<Info> info;
  std::pmr::vector<std::byte> interpretation;
  std::shared_ptr<const void> external;
  allocation::Allocation storage;
  Part(Domain&, Ref<Info>, PartId, Bytes);
};

struct Rep : Base {
  Ref<Info> info;
  std::pmr::vector<Ref<Part>> parts;
  std::pmr::vector<const Leaf*> leaves;
  std::pmr::vector<const Leaf*> lookup;
  Access access;
  Rep(Domain&, Ref<Info>);
  void finish() noexcept { access = {leaves, this}; }
};

struct Build : Base {
  Ref<Rep> rep;
  std::pmr::vector<bool> active;
  std::size_t writers{};
  bool revoked{}, frozen{};
  explicit Build(Domain& d) : Base(d), active(&d) {}
};

struct Version : Base {
  ObjectId object;
  std::uint64_t version;
  Ref<Rep> representation, alternative;
  Version* older{};
  bool present;
  bool published{};
  Version(Domain& d, ObjectId id, std::uint64_t cut, Ref<Rep> rep)
      : Base(d), object(id), version(cut), representation(std::move(rep)), present(bool(representation)) {}
};

struct Entry {
  ObjectId object{};
  Version* head{};
  Entry* previous{};
  Entry* next{};
  bool indebted{};
  std::size_t prepared_users{};
};
struct Hash {
  std::size_t operator()(ObjectId id) const noexcept {
    auto value = id.low ^ (id.high + 0x9e3779b97f4a7c15ULL + (id.low << 6) + (id.low >> 2));
    value ^= value >> 30; value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27; value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
  }
};

struct Catalog : Base {
  std::pmr::unordered_map<ObjectId, Entry, Hash> entries;
  Entry* first_entry{};
  Entry* last_entry{};
  Entry* pruning{};
  Version* seeking{};
  Version* anchor{};
  std::uint64_t pruning_floor{};
  bool maintenance_done{true};
  Snapshot* first_snapshot{};
  Snapshot* last_snapshot{};
  Acceptance* pending{};
  std::uint64_t cut{}, occurrences{}, installed{}, aborted{};
  std::size_t versions{}, contexts{};
  explicit Catalog(Domain& d) : Base(d), entries(&d) {}
  ~Catalog();
  std::uint64_t floor() const noexcept;
};

struct Snapshot : Base {
  Ref<Catalog> catalog;
  std::pmr::vector<Ref<Rep>> captured;
  Snapshot* previous{};
  Snapshot* next{};
  std::uint64_t cut;
  explicit Snapshot(Domain&, Ref<Catalog>);
};

struct Change {
  ObjectId object;
  Ref<Version> version;
};
struct Edit : Base {
  Ref<Snapshot> basis;
  std::pmr::vector<Change> changes;
  bool consumed{};
  Edit(Domain& d, Ref<Snapshot> context) : Base(d), basis(std::move(context)), changes(&d) {}
};

struct Pending { Entry* entry; Ref<Version> version; };
struct Acceptance : Base {
  Ref<Catalog> catalog;
  std::pmr::vector<Pending> changes;
  std::uint64_t basis{}, occurrence{};
  Disposition disposition{Disposition::empty};
  Acceptance(Domain& d, Ref<Catalog> c) : Base(d), catalog(std::move(c)), changes(&d) {}
};

struct Api {
  static Ref<Catalog>& catalog(Store& s) noexcept { return s.catalog_; }
  static const Ref<Catalog>& catalog(const Store& s) noexcept { return s.catalog_; }
  static Store store(Ref<Catalog> c) { Store s; s.catalog_ = std::move(c); return s; }
  static BorrowedContext borrow(Catalog& c) { BorrowedContext v; v.catalog_ = &c; v.cut_ = c.cut; return v; }
  static Catalog* catalog(const BorrowedContext& c) { return c.catalog_; }
  static std::uint64_t cut(const BorrowedContext& c) { return c.cut_; }
  static Ref<Snapshot>& snapshot(ReadContext& c) { return c.snapshot_; }
  static const Ref<Snapshot>& snapshot(const ReadContext& c) { return c.snapshot_; }
  static ReadContext context(Ref<Snapshot> s) { ReadContext c; c.snapshot_ = std::move(s); return c; }
  static Representation representation(Ref<Rep> r) { Representation v; v.rep_ = std::move(r); return v; }
  static const Ref<Rep>& rep(const Representation& r) { return r.rep_; }
  static Selection selection(Version& v, Rep& r) { Selection s; s.version_ = &v; s.access_ = &r.access; return s; }
  static Version* version(Selection s) { return s.version_; }
  static Rep* rep(Selection s) { return s.access_ ? s.access_->owner : nullptr; }
  static View view(const Leaf* l) { return View(l); }
  static Part* part(View v) { return v.leaf_ ? v.leaf_->owner : nullptr; }
  static RetainedView retained(Ref<Part> p) { RetainedView r; r.part_ = std::move(p); return r; }
  static RetainedSelection retained(Ref<Version> v, Ref<Rep> r) { RetainedSelection s; s.version_ = std::move(v); s.rep_ = std::move(r); return s; }
  static Ref<Build>& build(Builder& b) { return b.build_; }
  static Builder builder(Ref<Build> b) { Builder r; r.build_ = std::move(b); return r; }
  static Ref<Build>& build(WriteView& b) { return b.build_; }
  static WriteView writer(Ref<Build> b, std::size_t index) { WriteView v; v.build_ = std::move(b); v.index_ = index; return v; }
  static const Ref<Edit>& edit(const Draft& d) { return d.edit_; }
  static Ref<Edit>& edit(Draft& d) { return d.edit_; }
  static Draft draft(Ref<Edit> d) { Draft r; r.edit_ = std::move(d); return r; }
  static Ref<Acceptance>& acceptance(Prepared& p) { return p.acceptance_; }
  static Prepared prepared(Ref<Acceptance> p) { Prepared r; r.acceptance_ = std::move(p); return r; }
};

[[nodiscard]] Result<Selection> resolve(Catalog&, std::uint64_t, ObjectId, bool) noexcept;
void abort(Acceptance&) noexcept;

} // namespace orbital::object::detail
