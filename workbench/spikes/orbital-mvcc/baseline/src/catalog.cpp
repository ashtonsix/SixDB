#include "internal.h"

namespace orbital::object::detail {

void retain_ref(Version& v) noexcept { retain_base(v); }
void release_ref(Version& v) noexcept { release_base(v); }
void retain_ref(Edit& v) noexcept { retain_base(v); }
void release_ref(Edit& v) noexcept {
  v.domain->owner();
  assert(v.references);
  if (!--v.references) v.destroy(&v);
}
void retain_ref(Acceptance& v) noexcept { retain_base(v); }

void retain_ref(Catalog& c) noexcept { retain_base(c); }

Catalog::~Catalog() {
  assert(!first_snapshot && !pending);
  for (auto& [id, entry] : entries) {
    (void)id;
    while (entry.head) {
      auto* record = entry.head;
      entry.head = record->older;
      record->older = nullptr;
      record->published = false;
      record->representation.reset();
      record->alternative.reset();
      release_ref(*record);
    }
  }
}

void release_ref(Catalog& c) noexcept {
  auto& d = *c.domain;
  d.owner();
  assert(c.references);
  if (--c.references) return;
  // The collector is going away. Keep its resource live through complete local
  // cleanup; subsequently escaping immutable owners release synchronously.
  retain_domain(d);
  c.destroy(&c);
  drain_domain(d, std::numeric_limits<std::size_t>::max());
  d.detached = true;
  release_domain(d);
}

std::uint64_t Catalog::floor() const noexcept {
  return first_snapshot ? first_snapshot->cut : cut;
}

static void dirty(Catalog& c) noexcept {
  c.maintenance_done = false;
  c.pruning = nullptr;
  c.seeking = c.anchor = nullptr;
}

static void add_debt(Catalog& c, Entry& e) noexcept {
  if (!e.indebted) {
    e.previous = c.last_entry;
    if (c.last_entry) c.last_entry->next = &e;
    else c.first_entry = &e;
    c.last_entry = &e;
    e.indebted = true;
  }
  dirty(c);
}

static void remove_debt(Catalog& c, Entry& e) noexcept {
  if (!e.indebted) return;
  if (e.previous) e.previous->next = e.next;
  else c.first_entry = e.next;
  if (e.next) e.next->previous = e.previous;
  else c.last_entry = e.previous;
  e.previous = e.next = nullptr;
  e.indebted = false;
}

Snapshot::Snapshot(Domain& d, Ref<Catalog> c)
    : Base(d), catalog(std::move(c)), captured(&d), cut(catalog->cut) {
  auto& owner = *catalog;
  previous = owner.last_snapshot;
  if (previous) previous->next = this;
  else owner.first_snapshot = this;
  owner.last_snapshot = this;
  ++owner.contexts;
}

void retain_ref(Snapshot& s) noexcept { retain_base(s); }

void release_ref(Snapshot& s) noexcept {
  s.domain->owner();
  assert(s.references);
  if (s.references == 1) {
    auto& c = *s.catalog;
    const auto old_floor = c.floor();
    if (s.previous) s.previous->next = s.next;
    else c.first_snapshot = s.next;
    if (s.next) s.next->previous = s.previous;
    else c.last_snapshot = s.previous;
    s.previous = s.next = nullptr;
    --c.contexts;
    if (c.floor() != old_floor) dirty(c);
    // Destroy this small enrollment immediately: queuing its Catalog reference
    // could otherwise keep the collector alive forever after Store destruction.
    --s.references;
    s.destroy(&s);
    return;
  }
  --s.references;
}

Result<Selection> resolve(Catalog& c, std::uint64_t cut, ObjectId id, bool alternate) noexcept {
  c.domain->owner();
  const auto found = c.entries.find(id);
  if (found == c.entries.end()) return std::unexpected(Error::absent);
  auto* v = found->second.head;
  while (v && v->version > cut) v = v->older;
  if (!v || !v->present) return std::unexpected(Error::absent);
  auto* rep = alternate ? v->alternative.get() : v->representation.get();
  if (!rep) return std::unexpected(Error::absent);
  return Api::selection(*v, *rep);
}

static Entry& entry(Catalog& c, ObjectId id) {
  auto [where, inserted] = c.entries.try_emplace(id);
  auto& result = where->second;
  if (inserted) {
    result.object = id;
  }
  return result;
}

static void remove_empty(Catalog& c, Entry& e) noexcept {
  if (e.head) return;
  remove_debt(c, e);
  c.entries.erase(e.object);
}

void abort(Acceptance& a) noexcept {
  auto& c = *a.catalog;
  c.domain->owner();
  if (a.disposition != Disposition::accepted) return;
  assert(c.pending == &a);
  c.pending = nullptr;
  a.disposition = Disposition::aborted;
  ++c.aborted;
  for (auto& change : a.changes) {
    if (change.entry) {
      assert(change.entry->prepared_users);
      --change.entry->prepared_users;
      if (change.entry->head && !change.entry->head->present)
        add_debt(c, *change.entry);
      else remove_empty(c, *change.entry);
    }
    change.entry = nullptr;
  }
}

void release_ref(Acceptance& a) noexcept {
  a.domain->owner();
  // Every public Prepared release resolves its accepted episode first.
  assert(a.disposition != Disposition::accepted || a.references > 1);
  assert(a.references);
  if (!--a.references) a.destroy(&a);
}

static bool prune_one(Catalog& c) noexcept {
  if (c.maintenance_done) return false;
  const auto floor = c.floor();
  if (!c.pruning || c.pruning_floor != floor) {
    c.pruning = c.first_entry;
    c.seeking = c.pruning ? c.pruning->head : nullptr;
    c.anchor = nullptr;
    c.pruning_floor = floor;
  }
  if (!c.pruning) { c.maintenance_done = true; return false; }
  if (c.seeking) {
    if (c.seeking->version > floor) c.seeking = c.seeking->older;
    else { c.anchor = c.seeking; c.seeking = nullptr; }
    return true;
  }
  if (c.anchor && c.anchor->older) {
    auto* old = c.anchor->older;
    c.anchor->older = old->older;
    old->older = nullptr;
    old->published = false;
    old->representation.reset();
    old->alternative.reset();
    --c.versions;
    release_ref(*old);
    return true;
  }
  auto* next = c.pruning->next;
  auto* head = c.pruning->head;
  if (!head || !head->older) {
    auto* done = c.pruning;
    if (!head || head->present || (head->version <= floor && !done->prepared_users))
      remove_debt(c, *done);
    if (head && !head->present && head->version <= floor && !done->prepared_users) {
      done->head = nullptr;
      head->published = false;
      --c.versions;
      release_ref(*head);
      c.entries.erase(done->object);
    }
  }
  c.pruning = next;
  c.seeking = c.pruning ? c.pruning->head : nullptr;
  c.anchor = nullptr;
  if (!c.pruning) c.maintenance_done = true;
  return true;
}

} // namespace orbital::object::detail

namespace orbital::object {
using detail::Api;

Result<Store> Store::open(worker::Lane& lane, std::size_t limit) {
  if (!worker::on_lane(lane) || limit < sizeof(detail::Domain))
    return std::unexpected(Error::invalid_argument);
  detail::Domain* domain{};
  try {
    domain = new detail::Domain(lane, limit);
    auto c = detail::make<detail::Catalog>(*domain);
    detail::release_domain(*domain);
    return Api::store(std::move(c));
  } catch (const std::bad_alloc&) {
    if (domain) detail::release_domain(*domain);
    return std::unexpected(Error::capacity);
  }
}

BorrowedContext Store::borrow() const & noexcept {
  if (!catalog_) return {};
  catalog_->domain->owner();
  return Api::borrow(*catalog_);
}

Result<ReadContext> Store::read() const {
  if (!catalog_) return std::unexpected(Error::invalid_argument);
  try { return Api::context(detail::make<detail::Snapshot>(*catalog_->domain, catalog_)); }
  catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

Result<Selection> select(const BorrowedContext& context, ObjectId id, bool alternate) noexcept {
  auto* c = Api::catalog(context);
  if (!c) return std::unexpected(Error::invalid_argument);
  c->domain->owner();
  if (Api::cut(context) != c->cut) return std::unexpected(Error::stale);
  return detail::resolve(*c, Api::cut(context), id, alternate);
}

Result<Selection> select(const ReadContext& context, ObjectId id, bool alternate) noexcept {
  const auto& s = Api::snapshot(context);
  if (!s) return std::unexpected(Error::invalid_argument);
  auto result = detail::resolve(*s->catalog, s->cut, id, alternate);
  if (!result || !alternate) return result;
  // Default descriptors are immutable history anchors. Replaceable physical
  // alternatives need a context-owned capture so a later adoption cannot revoke
  // this borrowed descriptor. Charge enrollment only on this optional path.
  auto* rep = Api::rep(*result);
  for (const auto& captured : s->captured) if (captured.get() == rep) return result;
  try { s->captured.push_back(Ref<detail::Rep>::retain(*rep)); }
  catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
  return result;
}

Result<Draft> Store::edit() const {
  if (!catalog_ || catalog_->cut == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected(Error::capacity);
  try {
    auto context = detail::make<detail::Snapshot>(*catalog_->domain, catalog_);
    return Api::draft(detail::make<detail::Edit>(*catalog_->domain, std::move(context)));
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

std::uint64_t Draft::version() const noexcept {
  return edit_ && !edit_->consumed ? edit_->basis->cut + 1 : 0;
}

static Result<void> change(detail::Edit& edit, ObjectId id, Ref<detail::Rep> rep) {
  edit.domain->owner();
  if (edit.consumed) return std::unexpected(Error::rejected);
  try {
    auto v = detail::make<detail::Version>(*edit.domain, id, edit.basis->cut + 1, std::move(rep));
    for (auto& existing : edit.changes) {
      if (existing.object == id) { existing.version = std::move(v); return {}; }
    }
    edit.changes.push_back({id, std::move(v)});
    return {};
  } catch (const std::bad_alloc&) { return std::unexpected(Error::capacity); }
}

Result<void> Draft::put(Representation rep) {
  if (!edit_ || !rep) return std::unexpected(Error::invalid_argument);
  const auto& r = Api::rep(rep);
  if (r->domain != edit_->domain || rep.description().version != version())
    return std::unexpected(Error::conflict);
  return change(*edit_, rep.description().object, r);
}

Result<void> Draft::erase(ObjectId id) {
  if (!edit_) return std::unexpected(Error::invalid_argument);
  return change(*edit_, id, {});
}

Result<Selection> Draft::select(ObjectId id) const & noexcept {
  if (!edit_ || edit_->consumed) return std::unexpected(Error::rejected);
  edit_->domain->owner();
  for (const auto& c : edit_->changes) {
    if (c.object != id) continue;
    if (!c.version->present) return std::unexpected(Error::absent);
    return Api::selection(*c.version, *c.version->representation);
  }
  return detail::resolve(*edit_->basis->catalog, edit_->basis->cut, id, false);
}

Result<Prepared> Store::prepare(Draft& draft) {
  auto& edit = Api::edit(draft);
  if (!catalog_ || !edit || edit->consumed) return std::unexpected(Error::invalid_argument);
  auto& c = *catalog_;
  c.domain->owner();
  if (edit->domain != c.domain) return std::unexpected(Error::conflict);
  if (edit->basis->cut != c.cut) return std::unexpected(Error::stale);
  if (c.pending) return std::unexpected(Error::unavailable);
  if (edit->changes.empty()) return std::unexpected(Error::invalid_argument);
  if (c.occurrences == std::numeric_limits<std::uint64_t>::max()) return std::unexpected(Error::capacity);
  Ref<detail::Acceptance> prepared;
  try {
    prepared = detail::make<detail::Acceptance>(*c.domain, catalog_);
    prepared->changes.reserve(edit->changes.size());
    for (const auto& change : edit->changes) {
      // Allocate every placeholder before acceptance. No hash-table operation
      // occurs during logical installation; placeholder heads remain absent.
      auto& target = detail::entry(c, change.object);
      prepared->changes.push_back({&target, change.version});
    }
  } catch (const std::bad_alloc&) {
    if (prepared) {
      for (auto& change : prepared->changes) {
        detail::remove_empty(c, *change.entry);
        change.entry = nullptr;
      }
    }
    return std::unexpected(Error::capacity);
  }
  prepared->basis = c.cut;
  prepared->occurrence = ++c.occurrences;
  prepared->disposition = Disposition::accepted;
  for (auto& change : prepared->changes) ++change.entry->prepared_users;
  c.pending = prepared.get();
  edit->consumed = true;
  edit.reset();
  return Api::prepared(std::move(prepared));
}

Result<void> Store::install(Prepared& prepared) noexcept {
  auto& a = Api::acceptance(prepared);
  if (!catalog_ || !a) return std::unexpected(Error::invalid_argument);
  auto& c = *catalog_;
  c.domain->owner();
  if (a->catalog.get() != &c || a->domain != c.domain) return std::unexpected(Error::conflict);
  if (a->disposition != Disposition::accepted || c.pending != a.get()) return std::unexpected(Error::rejected);
  if (a->basis != c.cut || !a->occurrence || a->occurrence != c.occurrences)
    return std::unexpected(Error::stale);
  // Owner visit is non-reentrant. Every record and index slot already exists;
  // these links and counters cannot allocate, destroy backing or call user code.
  for (auto& change : a->changes) {
    auto& v = *change.version;
    v.older = change.entry->head;
    detail::retain_ref(v); // Catalog's history use; no predecessor ownership.
    v.published = true;
    change.entry->head = &v;
    assert(change.entry->prepared_users);
    --change.entry->prepared_users;
    ++c.versions;
    if (v.older || !v.present) detail::add_debt(c, *change.entry);
  }
  c.cut = a->basis + 1;
  c.pending = nullptr;
  ++c.installed;
  a->disposition = Disposition::installed;
  return {};
}

Prepared::Prepared(Prepared&& other) noexcept : acceptance_(std::move(other.acceptance_)) {}

Prepared& Prepared::operator=(Prepared&& other) noexcept {
  Prepared moved(std::move(other));
  acceptance_.swap(moved.acceptance_);
  return *this;
}

Prepared::~Prepared() { abort(); }

Disposition Prepared::disposition() const noexcept {
  return acceptance_ ? acceptance_->disposition : Disposition::empty;
}

void Prepared::abort() noexcept {
  if (acceptance_) detail::abort(*acceptance_);
}

void Prepared::retire() noexcept { abort(); acceptance_.reset(); }

Result<void> Store::adopt(Selection selected, Representation alternative) {
  auto* version = Api::version(selected);
  if (!catalog_ || !version || !alternative) return std::unexpected(Error::invalid_argument);
  auto& c = *catalog_;
  c.domain->owner();
  const auto& rep = Api::rep(alternative);
  if (version->domain != c.domain || rep->domain != c.domain || !version->published)
    return std::unexpected(Error::conflict);
  const auto& old = version->representation->info->description;
  const auto& next = alternative.description();
  if (old.object != next.object || old.version != next.version ||
      !std::ranges::equal(old.logical_metadata, next.logical_metadata)) return std::unexpected(Error::conflict);
  version->alternative = rep;
  return {};
}

Statistics Store::statistics() const noexcept {
  if (!catalog_) return {};
  const auto& c = *catalog_;
  c.domain->owner();
  return {c.domain->charged, c.domain->peak, c.domain->limit, c.entries.size(), c.versions,
      c.contexts, c.domain->queued, c.cut, c.floor(), c.occurrences, c.installed, c.aborted};
}

std::size_t Store::drain(std::size_t budget) noexcept {
  if (!catalog_) return 0;
  // Opaque owner destruction may reenter or release the public Store handle.
  // No reference to this wrapper is used after that callback boundary.
  auto retained = catalog_;
  auto& c = *retained;
  c.domain->owner();
  std::size_t done = 0;
  while (done != budget) {
    if (c.domain->first) { detail::drain_domain(*c.domain, 1); ++done; }
    else if (detail::prune_one(c)) ++done;
    else break;
  }
  return done;
}

} // namespace orbital::object
