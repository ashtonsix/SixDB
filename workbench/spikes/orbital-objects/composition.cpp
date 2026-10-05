#include "composition.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/falloc.h>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace object_views {
namespace {
bool overlaps(Range a, Range b) {
  return a.offset < b.offset + b.size && b.offset < a.offset + a.size;
}
void validate(Range r, std::size_t size) {
  if (r.size == 0 || r.offset > size || r.size > size - r.offset)
    throw std::invalid_argument("range outside admitted image");
}
void exact_io(int fd, std::size_t offset, std::uint8_t* data,
              std::size_t size, bool write) {
  while (size) {
    const auto n = write
        ? ::pwrite(fd, data, size, static_cast<off_t>(offset))
        : ::pread(fd, data, size, static_cast<off_t>(offset));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) throw std::runtime_error("fixture I/O incomplete");
    offset += static_cast<std::size_t>(n);
    data += n;
    size -= static_cast<std::size_t>(n);
  }
}
std::atomic<std::uint64_t> next_incarnation{1};
}  // namespace

const char* name(Strategy s) {
  switch (s) {
    case Strategy::copy: return "private";
    case Strategy::share: return "shared";
    case Strategy::reuse: return "reuse";
  }
  std::terminate();
}

struct Pool::State {
  std::mutex mutex;
  int fd{-1};
  std::size_t page{}, limit{}, reserve{}, next_offset{};
  std::uint64_t next_home{1};
  std::vector<std::size_t> free_offsets;
  struct Cached { std::uint8_t* data; std::size_t offset; };
  std::vector<Cached> cache;
  std::size_t cache_limit{};
  Counts counts;
  ~State() {
    for (const auto& entry : cache) ::munmap(entry.data, page);
    if (fd >= 0) ::close(fd);
  }
  template<class F> void count(F&& f) {
    std::lock_guard guard(mutex);
    f(counts);
  }
};

struct Pool::Home {
  struct Window { std::uint64_t id; Range range; };
  std::shared_ptr<Pool::State> pool;
  std::uint8_t* data{};
  std::size_t offset{};
  std::uint64_t id{}, next_window{1};
  std::mutex mutex;
  std::vector<Window> users, writers;
  ~Home() {
    if (!pool) return;
    {
      std::lock_guard guard(pool->mutex);
      if (pool->cache.size() < pool->cache_limit) {
        pool->cache.push_back({data, offset});
        pool->counts.cached += pool->page;
        return;
      }
    }
    ::munmap(data, pool->page);
    const bool discarded = ::fallocate(pool->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                                       static_cast<off_t>(offset),
                                       static_cast<off_t>(pool->page)) == 0;
    std::lock_guard guard(pool->mutex);
    if (discarded) {
      pool->counts.charged -= pool->page;
      pool->counts.discarded += pool->page;
      pool->free_offsets.push_back(offset);
    } else {
      // Failed physical release stays charged; never report reclaimed capacity.
      pool->counts.unreleased += pool->page;
    }
    ++pool->counts.unmaps;
  }
  bool busy() {
    std::lock_guard guard(mutex);
    return !writers.empty();
  }
  bool claim(std::uint64_t writer, std::span<const Range> ranges) {
    std::lock_guard guard(mutex);
    for (auto r : ranges) {
      for (const auto& u : users) if (overlaps(r, u.range)) return false;
      for (const auto& w : writers) if (overlaps(r, w.range)) return false;
    }
    writers.reserve(writers.size() + ranges.size());
    for (auto r : ranges) writers.push_back({writer, r});
    return true;
  }
  void release_writer(std::uint64_t writer) {
    std::lock_guard guard(mutex);
    std::erase_if(writers, [=](const auto& w) { return w.id == writer; });
  }
};

Pool::Pool(std::size_t limit, std::size_t service_reserve, std::size_t cache_pages)
    : state_(std::make_shared<State>()) {
  state_->page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
  state_->limit = limit;
  state_->reserve = service_reserve;
  state_->cache_limit = cache_pages;
  state_->cache.reserve(cache_pages);
  if (service_reserve > limit) throw std::invalid_argument("reserve exceeds budget");
  state_->fd = ::memfd_create("object-view-backing", MFD_CLOEXEC);
  if (state_->fd < 0) throw std::runtime_error("memfd unavailable");
}
std::size_t Pool::page() const { return state_->page; }
Counts Pool::counts() const {
  std::lock_guard guard(state_->mutex);
  return state_->counts;
}
void Pool::reset_counts() {
  std::lock_guard guard(state_->mutex);
  const auto charged = state_->counts.charged;
  const auto active = state_->counts.active;
  const auto aliases = state_->counts.aliases;
  const auto undo = state_->counts.undo;
  const auto unreleased = state_->counts.unreleased;
  const auto cached = state_->counts.cached;
  state_->counts = {};
  state_->counts.charged = charged;
  state_->counts.peak = charged + undo;
  state_->counts.undo = undo;
  state_->counts.unreleased = unreleased;
  state_->counts.cached = cached;
  state_->counts.peak_home = charged - cached - unreleased;
  state_->counts.active = state_->counts.peak_active = active;
  state_->counts.aliases = aliases;
}
std::shared_ptr<Pool::Home> Pool::allocate(bool service) {
  std::lock_guard guard(state_->mutex);
  const auto ceiling = state_->limit - (service ? 0 : state_->reserve);
  const auto active = state_->counts.charged - state_->counts.cached + state_->counts.undo;
  const auto total = state_->counts.charged + state_->counts.undo;
  if (active > ceiling || state_->page > ceiling - active ||
      (state_->cache.empty() && (total > state_->limit || state_->page > state_->limit - total))) {
    ++state_->counts.denied;
    return {};
  }
  auto h = std::make_shared<Home>();
  h->id = state_->next_home++;
  if (!state_->cache.empty()) {
    const auto entry = state_->cache.back();
    state_->cache.pop_back();
    h->data = entry.data;
    h->offset = entry.offset;
    h->pool = state_;
    state_->counts.cached -= state_->page;
    state_->counts.peak_home = std::max(state_->counts.peak_home,
        state_->counts.charged - state_->counts.cached - state_->counts.unreleased);
    std::memset(h->data, 0, state_->page);
    state_->counts.zeroed += state_->page;
    return h;
  }
  state_->free_offsets.reserve(state_->next_offset / state_->page + 1);
  if (state_->free_offsets.empty()) {
    h->offset = state_->next_offset;
    if (::ftruncate(state_->fd, static_cast<off_t>(h->offset + state_->page)))
      throw std::runtime_error("backing extent failed");
    state_->next_offset += state_->page;
  } else {
    h->offset = state_->free_offsets.back();
    state_->free_offsets.pop_back();
  }
  auto* p = ::mmap(nullptr, state_->page, PROT_READ | PROT_WRITE, MAP_SHARED,
                   state_->fd, static_cast<off_t>(h->offset));
  if (p == MAP_FAILED) {
    state_->free_offsets.push_back(h->offset);
    throw std::runtime_error("backing mapping failed");
  }
  h->pool = state_;
  h->data = static_cast<std::uint8_t*>(p);
  std::memset(h->data, 0, state_->page);
  state_->counts.charged += state_->page;
  state_->counts.peak = std::max(state_->counts.peak, state_->counts.charged + state_->counts.undo);
  state_->counts.peak_home = std::max(state_->counts.peak_home,
      state_->counts.charged - state_->counts.cached - state_->counts.unreleased);
  state_->counts.zeroed += state_->page;
  ++state_->counts.maps;
  return h;
}

struct Projection::Binding {
  std::shared_ptr<Pool::Home> home;
  std::shared_ptr<Pool::State> pool;
  std::uint8_t* data{};
  bool alias{};
  std::shared_ptr<ByteUse::Hold> exposure;
  ~Binding() {
    if (alias) {
      ::munmap(data, pool->page);
      pool->count([](auto& c) { ++c.unmaps; --c.aliases; });
    }
  }
};
struct ByteUse::Hold {
  std::shared_ptr<Pool::Home> home;
  std::shared_ptr<Projection::Binding> binding;
  Range range;
  std::uint64_t window{};
  std::shared_ptr<Hold> physical;
  ~Hold() {
    if (!window) return;
    {
      std::lock_guard guard(home->mutex);
      std::erase_if(home->users, [&](const auto& u) { return u.id == window; });
    }
    home->pool->count([](auto& c) { --c.active; });
  }
};
namespace {
std::shared_ptr<ByteUse::Hold> hold(std::shared_ptr<Pool::Home> home, Range range,
                                  std::shared_ptr<Projection::Binding> binding = {}) {
  validate(range, home->pool->page);
  auto h = std::make_shared<ByteUse::Hold>();
  h->home = std::move(home);
  h->range = range;
  h->binding = std::move(binding);
  {
    std::lock_guard guard(h->home->mutex);
    for (const auto& w : h->home->writers)
      if (overlaps(range, w.range)) throw std::runtime_error("write acquisition closed");
    h->home->users.push_back({h->home->next_window, range});
    h->window = h->home->next_window++;
  }
  h->home->pool->count([](auto& c) {
    ++c.grants;
    ++c.active;
    c.peak_active = std::max(c.peak_active, c.active);
  });
  return h;
}
}  // namespace
const std::uint8_t* ByteUse::data() const {
  return (hold_->binding ? hold_->binding->data : hold_->home->data) +
         hold_->range.offset;
}
std::size_t ByteUse::size() const { return hold_->range.size; }
std::uint64_t ByteUse::home() const { return hold_->home->id; }

History::History(std::vector<Bytes> outcomes) {
  if (outcomes.empty() || outcomes.front().empty())
    throw std::invalid_argument("empty history");
  size_ = outcomes.front().size();
  positions_ = outcomes.size();
  for (const auto& b : outcomes)
    if (b.size() != size_) throw std::invalid_argument("different fixture geometries");
  char path[] = "/tmp/sixdb-object-history-XXXXXX";
  fd_ = ::mkstemp(path);
  if (fd_ < 0) throw std::runtime_error("history fixture open failed");
  ::unlink(path);
  try {
    for (std::size_t i = 0; i < outcomes.size(); ++i)
      exact_io(fd_, i * size_, outcomes[i].data(), size_, true);
    if (::fdatasync(fd_)) throw std::runtime_error("fixture prefix sync failed");
  } catch (...) {
    ::close(fd_);
    fd_ = -1;
    throw;
  }
}
History::~History() { if (fd_ >= 0) ::close(fd_); }
std::size_t History::size() const { return size_; }
std::size_t History::retained_bytes() const { return size_ * positions_; }
void History::read(std::size_t position, Range range, std::uint8_t* out) const {
  validate(range, size_);
  if (position >= positions_) throw std::invalid_argument("unknown outcome position");
  if (hook_) hook_(position, range);
  exact_io(fd_, position * size_ + range.offset, out, range.size, false);
}

namespace {
struct Image {
  std::size_t position{};
  std::vector<std::shared_ptr<Pool::Home>> pages;
};
std::vector<Range> local_ranges(std::span<const Range> ranges,
                               std::size_t page_index, std::size_t page) {
  std::vector<Range> out;
  for (auto r : ranges)
    if (r.offset / page == page_index) out.push_back({r.offset % page, r.size});
  return out;
}
}  // namespace
struct Context::State {
  std::mutex mutex;
  Pool pool;
  std::shared_ptr<History> history;
  std::shared_ptr<Image> image;
  bool closed{};
  std::uint64_t incarnation{next_incarnation++}, epoch{1}, next_writer{1};
  struct Writer { std::uint64_t id; std::vector<Range> ranges; };
  std::vector<Writer> writers;
  State(Pool p, std::shared_ptr<History> h, std::size_t position)
      : pool(std::move(p)), history(std::move(h)), image(std::make_shared<Image>()) {
    if (history->size() % pool.page()) throw std::invalid_argument("fixture page geometry");
    image->position = position;
    image->pages.resize(history->size() / pool.page());
  }
  std::shared_ptr<Pool::Home> load(std::size_t position, std::size_t page_index,
                                  bool reconstruction, bool service = false) {
    auto h = pool.allocate(service);
    if (!h) return {};
    history->read(position, {page_index * pool.page(), pool.page()}, h->data);
    h->pool->count([&](auto& c) {
      c.fetched += pool.page();
      if (reconstruction) c.reconstructed += pool.page();
    });
    return h;
  }
};
struct Working::State {
  std::shared_ptr<Context::State> owner;
  Strategy strategy{};
  std::uint64_t id{};
  std::size_t base{};
  std::vector<Range> writes;
  std::vector<std::shared_ptr<Pool::Home>> pages;
  std::vector<bool> reused;
  std::vector<Bytes> undo;
  std::uint64_t undo_reserved{};
  bool finished{true};
  ~State() {
    for (auto& p : pages) if (p) p->release_writer(id);
    release_undo();
  }
  void release_undo() {
    if (undo_reserved) {
      owner->pool.state_->count([&](auto& c) { c.undo -= undo_reserved; });
      undo_reserved = 0;
      undo.clear();
    }
  }
};

Context::Context(Pool p, std::shared_ptr<History> h, std::size_t position)
    : state_(std::make_shared<State>(std::move(p), std::move(h), position)) {}
Context::~Context() { close(); }
RetainedState Context::retain(std::size_t position) const {
  std::lock_guard guard(state_->mutex);
  if (state_->closed) throw std::runtime_error("context closed");
  return {state_->history, position, {}};
}
View Context::open(const RetainedState& retained, std::span<const Range> ranges,
                   bool service) {
  auto owner = state_;
  auto& s = *owner;
  std::unique_lock guard(s.mutex);
  if (s.closed) throw std::runtime_error("context closed");
  if (retained.history != s.history) throw std::invalid_argument("foreign history");
  auto position = retained.position;
  if (retained.pending) {
    if (retained.pending->outcome == Pending::Outcome::unresolved)
      throw std::runtime_error("input predecessor unresolved");
    if (retained.pending->outcome == Pending::Outcome::effect)
      position = retained.pending->result;
  }
  View view{retained, {}};
  view.bytes.resize(ranges.size());
  const std::weak_ptr<Image> image = s.image;
  const auto image_position = s.image->position;
  std::vector<std::shared_ptr<Pool::Home>> prepared(s.image->pages.size());
  std::vector<bool> needed(s.image->pages.size());
  std::vector<bool> reconstruction(s.image->pages.size());
  // Capture every resident range before dropping the owner latch. A subsequent
  // in-place writer must see these windows, including native overread padding.
  for (std::size_t j = 0; j < ranges.size(); ++j) {
    const auto r = ranges[j];
    validate(r, s.history->size());
    if (r.offset / s.pool.page() != (r.offset + r.size - 1) / s.pool.page())
      throw std::invalid_argument("native range crosses binding page");
    const auto i = r.offset / s.pool.page();
    if (position == s.image->position) {
      auto& resident = s.image->pages[i];
      if (resident && !resident->busy()) {
        view.bytes[j] = ByteUse(hold(resident, {r.offset % s.pool.page(), r.size}));
        continue;
      }
    }
    needed[i] = true;
    reconstruction[i] = position != image_position || static_cast<bool>(s.image->pages[i]);
  }
  guard.unlock();
  for (std::size_t i = 0; i < needed.size(); ++i) if (needed[i]) {
    prepared[i] = s.load(position, i, reconstruction[i], service);
    if (!prepared[i]) throw std::runtime_error("preparation budget exhausted");
  }
  guard.lock();
  if (s.closed) throw std::runtime_error("context closed during preparation");
  for (std::size_t j = 0; j < ranges.size(); ++j) if (!view.bytes[j]) {
    const auto r = ranges[j];
    const auto i = r.offset / s.pool.page();
    // Completion can bind only into the same current image. Otherwise these
    // freshly fetched bytes remain a historical view of the requested state.
    const auto original = image.lock();
    if (original && s.image == original && position == image_position && !original->pages[i])
      original->pages[i] = prepared[i];
    view.bytes[j] = ByteUse(hold(prepared[i], {r.offset % s.pool.page(), r.size}));
  }
  return view;
}
Context::Ticket Context::resolve(Range range) {
  const auto retained = retain(position());
  auto view = open(retained, std::span(&range, 1));
  auto& s = *state_;
  std::lock_guard guard(s.mutex);
  if (s.closed) throw std::runtime_error("context closed");
  const auto i = range.offset / s.pool.page();
  auto& home = s.image->pages[i];
  const bool same = s.image->position == retained.position && home &&
                    home->id == view.bytes[0].home();
  return {s.incarnation, same ? s.epoch : 0, view.bytes[0].home(), retained.position, range};
}
ByteUse Context::acquire(const Ticket& t) {
  auto& s = *state_;
  std::lock_guard guard(s.mutex);
  const auto page = t.range.offset / s.pool.page();
  if (s.closed || !s.image || t.incarnation != s.incarnation || t.epoch != s.epoch ||
      t.position != s.image->position || page >= s.image->pages.size() ||
      !s.image->pages[page] || s.image->pages[page]->id != t.home ||
      s.image->pages[page]->busy()) {
    s.pool.state_->count([](auto& c) { ++c.stale; });
    return {};
  }
  return ByteUse(hold(s.image->pages[page], {t.range.offset % s.pool.page(), t.range.size}));
}
std::unique_ptr<Working> Context::begin(Strategy strategy, std::span<const Range> ranges) {
  auto s = state_;
  std::unique_lock guard(s->mutex);
  if (s->closed || ranges.empty()) return {};
  for (std::size_t i = 0; i < ranges.size(); ++i) {
    validate(ranges[i], s->history->size());
    if (ranges[i].offset / s->pool.page() !=
        (ranges[i].offset + ranges[i].size - 1) / s->pool.page())
      throw std::invalid_argument("write crosses binding page");
    for (std::size_t j = 0; j < i; ++j)
      if (overlaps(ranges[i], ranges[j])) throw std::invalid_argument("overlapping write ranges");
    for (const auto& w : s->writers)
      for (auto other : w.ranges) if (overlaps(ranges[i], other)) return {};
  }
  auto w = std::make_unique<Working::State>();
  w->owner = s;
  w->strategy = strategy;
  w->id = s->next_writer++;
  w->base = s->image->position;
  w->writes.assign(ranges.begin(), ranges.end());
  w->pages.resize(s->image->pages.size());
  w->reused.resize(w->pages.size());
  s->writers.reserve(s->writers.size() + 1);
  Context::State::Writer registration{w->id, w->writes};
  std::vector<std::shared_ptr<ByteUse::Hold>> sources(w->pages.size());
  std::vector<bool> needed(w->pages.size());
  // Reserve logical coverage, and capture/close physical acquisition before I/O.
  // No writable pointer escapes until every required destination is prepared.
  for (std::size_t i = 0; i < w->pages.size(); ++i) {
    const auto local = local_ranges(ranges, i, s->pool.page());
    if (strategy != Strategy::copy && local.empty()) continue;
    auto source = s->image->pages[i];
    if (strategy == Strategy::reuse && source && !local.empty() &&
        !source->busy() && source->claim(w->id, local)) {
      w->pages[i] = source;
      w->reused[i] = true;
      continue;
    }
    needed[i] = true;
    if (source && !source->busy()) sources[i] = hold(source, {0, s->pool.page()});
  }
  w->undo.resize(w->writes.size());
  std::size_t undo_bytes{};
  for (auto r : w->writes) if (w->reused[r.offset / s->pool.page()]) undo_bytes += r.size;
  if (undo_bytes) {
    auto pool = s->pool.state_;
    std::lock_guard budget(pool->mutex);
    const auto total = pool->counts.charged + pool->counts.undo;
    const auto active = total - pool->counts.cached;
    const auto ceiling = pool->limit - pool->reserve;
    if (active > ceiling || undo_bytes > ceiling - active ||
        total > pool->limit || undo_bytes > pool->limit - total) {
      ++pool->counts.denied;
      return {};
    }
    pool->counts.undo += undo_bytes;
    pool->counts.peak = std::max(pool->counts.peak, pool->counts.charged + pool->counts.undo);
    w->undo_reserved = undo_bytes;
  }
  for (std::size_t j = 0; j < w->writes.size(); ++j) {
    const auto r = w->writes[j];
    const auto i = r.offset / s->pool.page();
    if (w->reused[i]) {
      w->undo[j].resize(r.size);
      std::memcpy(w->undo[j].data(), w->pages[i]->data + r.offset % s->pool.page(), r.size);
      w->pages[i]->pool->count([&](auto& c) { c.copies += r.size; });
    }
  }
  auto result = std::unique_ptr<Working>(new Working(std::move(w)));
  auto* prepared = result->state_.get();
  s->writers.push_back(std::move(registration));
  prepared->finished = false;
  guard.unlock();
  for (std::size_t i = 0; i < prepared->pages.size(); ++i) {
    if (needed[i]) {
      auto fresh = s->pool.allocate();
      if (!fresh) return {};
      if (sources[i]) {
        std::memcpy(fresh->data, sources[i]->home->data, s->pool.page());
        fresh->pool->count([&](auto& c) { c.copies += s->pool.page(); });
        sources[i].reset();
      } else {
        s->history->read(prepared->base, {i * s->pool.page(), s->pool.page()}, fresh->data);
        fresh->pool->count([&](auto& c) { c.fetched += s->pool.page(); });
      }
      const auto local = local_ranges(prepared->writes, i, s->pool.page());
      if (!fresh->claim(prepared->id, local)) std::terminate();
      prepared->pages[i] = std::move(fresh);
    }
    if (prepared->pages[i]) prepared->pages[i]->pool->count([&](auto& c) {
      if (prepared->reused[i]) ++c.reused;
      else if (!local_ranges(prepared->writes, i, s->pool.page()).empty()) ++c.replacements;
    });
  }
  guard.lock();
  const bool closed = s->closed;
  guard.unlock();
  if (closed) return {};
  return result;
}
std::size_t Context::position() const {
  std::lock_guard guard(state_->mutex);
  if (state_->closed) throw std::runtime_error("context closed");
  return state_->image->position;
}
void Context::close() {
  std::lock_guard guard(state_->mutex);
  state_->closed = true;
  state_->image.reset();
}
Working::Working(std::unique_ptr<State> s) : state_(std::move(s)) {}
Working::~Working() { abort(); }
std::uint8_t* Working::data(std::size_t index) {
  if (state_->finished) throw std::runtime_error("working image finished");
  const auto r = state_->writes.at(index);
  return state_->pages[r.offset / state_->owner->pool.page()]->data +
         r.offset % state_->owner->pool.page();
}
bool Working::publish(std::size_t position) {
  auto& w = *state_;
  auto s = w.owner;
  std::unique_lock guard(s->mutex);
  if (w.finished || s->closed || position >= s->history->positions() ||
      position <= s->image->position) return false;
  auto next = std::make_shared<Image>();
  next->position = position;
  next->pages = s->image->pages;
  if (s->image->position == w.base) {
    for (std::size_t i = 0; i < w.pages.size(); ++i)
      if (w.pages[i]) next->pages[i] = w.pages[i];
  } else {
    // A later disjoint publication changed the base. Rebase actual staged stores,
    // never install or undo a whole stale page over that publication.
    const std::weak_ptr<Image> expected_image = s->image;
    const auto source_position = s->image->position;
    std::vector<std::shared_ptr<ByteUse::Hold>> sources(w.pages.size());
    std::vector<std::shared_ptr<Pool::Home>> replacements(w.pages.size());
    for (std::size_t i = 0; i < w.pages.size(); ++i) {
      const auto local = local_ranges(w.writes, i, s->pool.page());
      if (w.strategy != Strategy::copy && local.empty()) continue;
      const auto source = s->image->pages[i];
      if (source && !source->busy()) sources[i] = hold(source, {0, s->pool.page()});
    }
    next->pages.clear();
    guard.unlock();
    for (std::size_t i = 0; i < w.pages.size(); ++i) {
      const auto local = local_ranges(w.writes, i, s->pool.page());
      if (w.strategy != Strategy::copy && local.empty()) continue;
      auto fresh = s->pool.allocate();
      if (!fresh) return false;
      if (sources[i]) {
        std::memcpy(fresh->data, sources[i]->home->data, s->pool.page());
        fresh->pool->count([&](auto& c) { c.copies += s->pool.page(); });
        sources[i].reset();
      } else {
        s->history->read(source_position, {i * s->pool.page(), s->pool.page()}, fresh->data);
        fresh->pool->count([&](auto& c) { c.reconstructed += s->pool.page(); c.fetched += s->pool.page(); });
      }
      for (auto r : local) {
        std::memcpy(fresh->data + r.offset, w.pages[i]->data + r.offset, r.size);
        fresh->pool->count([&](auto& c) { c.copies += r.size; });
      }
      replacements[i] = std::move(fresh);
    }
    guard.lock();
    const auto original = expected_image.lock();
    if (s->closed || !original || s->image != original) return false;
    next->pages = s->image->pages;
    for (std::size_t i = 0; i < replacements.size(); ++i)
      if (replacements[i]) next->pages[i] = std::move(replacements[i]);
  }
  for (auto& p : w.pages) if (p) p->release_writer(w.id);
  std::erase_if(s->writers, [&](const auto& writer) { return writer.id == w.id; });
  s->image = std::move(next);
  ++s->epoch;
  w.finished = true;
  w.pages.clear();
  w.release_undo();
  return true;
}
void Working::abort() {
  if (!state_ || state_->finished) return;
  auto& w = *state_;
  auto s = w.owner;
  std::lock_guard guard(s->mutex);
  // Restore only this writer's issued coverage. Another writer may have committed
  // disjoint bytes into the same physical page while this context was alive.
  for (std::size_t j = 0; j < w.writes.size(); ++j) {
    const auto r = w.writes[j];
    const auto i = r.offset / s->pool.page();
    if (w.reused[i]) {
      std::memcpy(w.pages[i]->data + r.offset % s->pool.page(), w.undo[j].data(), r.size);
      w.pages[i]->pool->count([&](auto& c) { c.copies += r.size; });
    }
  }
  for (auto& p : w.pages) if (p) p->release_writer(w.id);
  std::erase_if(s->writers, [&](const auto& writer) { return writer.id == w.id; });
  w.finished = true;
  w.pages.clear();
  w.release_undo();
}

Projection::Projection(const View& source, std::span<const Range> allowed,
                       std::function<void(const std::uint8_t*)> mapped_hook) {
  if (source.bytes.empty()) throw std::invalid_argument("empty projection");
  const auto page = source.bytes.front().hold_->home->pool->page;
  for (auto r : allowed) validate(r, page * source.bytes.size());
  for (std::size_t i = 0; i < source.bytes.size(); ++i) {
    const auto& use = source.bytes[i];
    if (use.size() != page || use.hold_->range.offset)
      throw std::invalid_argument("projection needs whole source pages");
    std::vector<Range> local;
    for (auto r : allowed) {
      const auto first = std::max(r.offset, i * page);
      const auto end = std::min(r.offset + r.size, (i + 1) * page);
      if (first < end) local.push_back({first - i * page, end - first});
    }
    std::sort(local.begin(), local.end(), [](auto a, auto b) { return a.offset < b.offset; });
    std::vector<Range> merged;
    for (auto r : local) {
      if (!merged.empty() && r.offset <= merged.back().offset + merged.back().size)
        merged.back().size = std::max(merged.back().offset + merged.back().size,
                                      r.offset + r.size) - merged.back().offset;
      else merged.push_back(r);
    }
    local = std::move(merged);
    auto binding = std::make_shared<Binding>();
    binding->pool = use.hold_->home->pool;
    binding->home = use.hold_->home;
    const bool full = local.size() == 1 && local[0].offset == 0 && local[0].size == page;
    if (local.empty()) binding->home.reset();
    if (!local.empty() && !full) {
      // Use the same backing/accounting substrate, not a hidden projection heap.
      Pool p(binding->pool);
      binding->home = p.allocate();
      if (!binding->home) throw std::runtime_error("projection budget exhausted");
      for (auto r : local) {
        std::memcpy(binding->home->data + r.offset, use.data() + r.offset, r.size);
        binding->pool->count([&](auto& c) { c.copies += r.size; });
      }
    }
    const int prot = local.empty() ? PROT_NONE : PROT_READ;
    void* ptr = local.empty()
        ? ::mmap(nullptr, page, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        : ::mmap(nullptr, page, prot, MAP_SHARED, binding->pool->fd,
                 static_cast<off_t>(binding->home->offset));
    if (ptr == MAP_FAILED) throw std::runtime_error("projection mapping failed");
    binding->data = static_cast<std::uint8_t*>(ptr);
    binding->alias = true;
    binding->pool->count([](auto& c) { ++c.maps; ++c.aliases; ++c.protections; });
    if (mapped_hook) mapped_hook(binding->data);
    if (!local.empty()) binding->exposure = full ? use.hold_ : hold(binding->home, {0, page});
    bindings_.push_back(std::move(binding));
    allowed_.push_back(std::move(local));
  }
}
const std::uint8_t* Projection::page_data(std::size_t i) const { return bindings_.at(i)->data; }
ByteUse Projection::borrow(std::size_t i, Range r) const {
  const auto& b = bindings_.at(i);
  validate(r, b->pool->page);
  const auto& ranges = allowed_.at(i);
  if (std::none_of(ranges.begin(), ranges.end(), [&](auto a) {
        return r.offset >= a.offset && r.offset + r.size <= a.offset + a.size;
      })) throw std::invalid_argument("projection borrow outside permission");
  auto use = std::make_shared<ByteUse::Hold>();
  use->home = b->home;
  use->range = r;
  use->binding = b;
  use->physical = b->exposure;
  return ByteUse(std::move(use));
}
}  // namespace object_views
