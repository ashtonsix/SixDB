#include "prototype.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <system_error>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace views {
namespace {
void require(bool ok) {
  if (!ok) throw std::system_error(errno, std::generic_category());
}
bool valid(Range r, std::size_t size) { return r.offset <= size && r.size <= size - r.offset; }
std::size_t rounded(std::size_t size) {
  if (size > std::numeric_limits<std::size_t>::max() - (page_size() - 1)) throw std::length_error("part too large");
  return (size + page_size() - 1) / page_size() * page_size();
}
struct Material {
  int fd;
  std::size_t size;
  explicit Material(const Bytes& bytes) : fd(static_cast<int>(::syscall(SYS_memfd_create,
      "orbital-view-part", MFD_CLOEXEC | MFD_ALLOW_SEALING))), size(bytes.size()) {
    require(fd >= 0);
    try {
      require(::ftruncate(fd, static_cast<off_t>(rounded(size))) == 0);
      std::size_t done = 0;
      while (done < size) {
        const auto n = ::pwrite(fd, bytes.data() + done, size - done, static_cast<off_t>(done));
        if (n < 0 && errno == EINTR) continue;
        require(n > 0); done += static_cast<std::size_t>(n);
      }
      require(::fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) == 0);
    } catch (...) { ::close(fd); throw; }
  }
  ~Material() { ::close(fd); }
  void read(std::size_t offset, std::uint8_t* target, std::size_t length) const {
    std::size_t done = 0;
    while (done < length) {
      const auto n = ::pread(fd, target + done, length - done, static_cast<off_t>(offset + done));
      if (n < 0 && errno == EINTR) continue;
      require(n > 0); done += static_cast<std::size_t>(n);
    }
  }
};
}
std::size_t page_size() { static const auto p = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE)); return p; }
const char* name(Method m) { return m == Method::copy ? "copy" : m == Method::cow ? "cow" : "take"; }
struct Image {
  std::shared_ptr<Material> material;
  Epoch epoch;
  std::uint8_t* data;
  std::size_t size, capacity;
  std::optional<recycling::Lease> allocation;
  std::vector<std::weak_ptr<Borrow>> borrowers;
  std::set<std::size_t> ready;
  bool mutable_image{};
  bool hardware;
  Method method;
  Image(std::shared_ptr<Material> source, Epoch e, Method m, bool h, std::size_t reserved,
        const std::shared_ptr<recycling::Pool>& pool) : material(std::move(source)),
      epoch(e), size(material->size), capacity(reserved), hardware(h), method(m) {
    if (pool) {
      allocation.emplace(pool->acquire(recycling::Temperature::hot, ::sched_getcpu(), capacity));
      data = reinterpret_cast<std::uint8_t*>(allocation->data());
      allocation->mapping_changed();
      require(::mprotect(data, capacity, PROT_NONE) == 0);
    } else {
      data = static_cast<std::uint8_t*>(::mmap(nullptr, capacity, PROT_NONE,
          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)); require(data != MAP_FAILED);
    }
  }
  ~Image() { if (!allocation) ::munmap(data, capacity); }
  void prepare(std::size_t page, Counts& counts, const Image* resident = nullptr) {
    if (ready.contains(page)) return;
    auto* at = data + page * page_size();
    const int prot = PROT_READ | (!hardware || mutable_image ? PROT_WRITE : 0);
    // Insert first: allocation failure must precede changing the native mapping.
    ready.insert(page);
    try {
      if (method == Method::copy || page * page_size() >= material->size) {
        // A reused home may still alias another part's file. Replace the backing too.
        require(::mmap(at, page_size(), PROT_READ | PROT_WRITE,
            MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED);
        if (resident && resident->ready.contains(page)) std::memcpy(at, resident->data + page * page_size(), page_size());
        else if (page * page_size() < material->size) {
          const auto length = std::min(page_size(), material->size - page * page_size());
          material->read(page * page_size(), at, length); counts.source_bytes += length;
        }
        counts.copied += page_size();
        require(::mprotect(at, page_size(), prot) == 0);
      } else {
        require(::mmap(at, page_size(), prot, MAP_FIXED | MAP_PRIVATE,
            material->fd, static_cast<off_t>(page * page_size())) != MAP_FAILED);
      }
      counts.mapped += page_size();
    } catch (...) {
      ready.erase(page);
      ::mmap(at, page_size(), PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      throw;
    }
  }
  void protect(bool write) {
    if (hardware) for (auto page : ready)
      require(::mprotect(data + page * page_size(), page_size(), PROT_READ | (write ? PROT_WRITE : 0)) == 0);
    mutable_image = write;
  }
};
struct State {
  std::map<Epoch, std::shared_ptr<Material>> history;
  std::shared_ptr<Image> cache;
  Epoch latest;
  bool drafting{};
  bool hardware{};
  Counts counts;
  std::size_t capacity;
  std::shared_ptr<recycling::Pool> pool;
};
struct Borrow { std::vector<std::size_t> pages; bool write; };
struct Preparation {
  std::shared_ptr<State> state;
  std::shared_ptr<Image> image;
  Bytes bytes;
  std::map<Epoch, std::shared_ptr<Material>>::node_type revision;
};
namespace {
bool pages_valid(std::span<const std::size_t> pages, std::size_t size) {
  return std::ranges::all_of(pages, [&](auto p) { return p < size / page_size() + (size % page_size() != 0); });
}
bool ready_range(const Image& image, Range r) {
  if (!valid(r, image.size)) return false;
  if (!r.size) return true;
  for (auto p = r.offset / page_size(); p <= (r.offset + r.size - 1) / page_size(); ++p)
    if (!image.ready.contains(p)) return false;
  return true;
}
auto revision(const State& state, Epoch e) {
  auto it = state.history.upper_bound(e);
  return it == state.history.begin() ? state.history.end() : std::prev(it);
}
}
const std::uint8_t* Lease::data() const { assert(image_); return image_->data; }
std::uint8_t* Lease::writable() const { assert(image_ && write_ && image_->mutable_image); return image_->data; }
std::size_t Lease::size() const { assert(image_); return image_->size; }
Lease::Lease(std::shared_ptr<Image> image, bool write, std::span<const std::size_t> pages)
    : image_(std::move(image)), write_(write) {
  if (write) {
    std::erase_if(image_->borrowers, [](const auto& b) { return b.expired(); });
    borrow_ = std::make_shared<Borrow>(Borrow{{pages.begin(), pages.end()}, true});
    image_->borrowers.emplace_back(borrow_);
  }
}
void Lease::release(recycling::Hint hint) {
  assert(image_);
  if (image_->allocation) {
    // Record the CPU use now; the backing returns only when the Image's final use ends.
    auto use = *image_->allocation;
    use.release(hint);
  }
  close();
}
Part::Part(Bytes initial, Epoch epoch, bool hardware, std::size_t reserved, std::shared_ptr<recycling::Pool> pool)
    : state_(std::make_shared<State>()) {
  reserved = reserved ? reserved : std::max(page_size(), rounded(initial.size()));
  if (reserved % page_size() || reserved < initial.size() || epoch == std::numeric_limits<Epoch>::max())
    throw std::invalid_argument("page-aligned reservation and finite epoch required");
  state_->history.emplace(epoch, std::make_shared<Material>(initial)); state_->latest = epoch; state_->hardware = hardware;
  state_->capacity = reserved; state_->pool = std::move(pool);
}
Result<Lease> Part::open(Epoch epoch, std::span<const std::size_t> pages, Method method) {
  const auto it = revision(*state_, epoch);
  if (it == state_->history.end() || epoch > state_->latest) return std::unexpected(Error::version);
  if (!pages_valid(pages, it->second->size)) return std::unexpected(Error::range);
  if (method == Method::take) return std::unexpected(Error::unavailable);
  auto image = state_->cache;
  if (!image || image->epoch != it->first) image = std::make_shared<Image>(it->second, it->first, method,
      state_->hardware, state_->capacity, state_->pool);
  for (auto p : pages) image->prepare(p, state_->counts);
  if (it == std::prev(state_->history.end())) state_->cache = image;
  return Lease(std::move(image), false, pages);
}
Result<std::shared_ptr<Fetch>> Part::fetch(Epoch epoch, std::span<const std::size_t> pages) {
  const auto it = revision(*state_, epoch);
  if (it == state_->history.end() || epoch > state_->latest) return std::unexpected(Error::version);
  if (!pages_valid(pages, it->second->size)) return std::unexpected(Error::range);
  auto image = std::make_shared<Image>(it->second, it->first, Method::copy,
      state_->hardware, state_->capacity, state_->pool);
  return std::shared_ptr<Fetch>(new Fetch(state_, std::move(image), pages));
}
Fetch::Fetch(std::shared_ptr<State> state, std::shared_ptr<Image> image, std::span<const std::size_t> pages)
    : state_(std::move(state)), image_(std::move(image)), pages_(pages.begin(), pages.end()) {
  for (auto page : pages_) require(::mmap(image_->data + page * page_size(), page_size(),
      PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED);
}
int Fetch::source_fd() const { assert(image_ && !finished_); return image_->material->fd; }
std::span<std::uint8_t> Fetch::destination(std::size_t page) {
  assert(image_ && !finished_ && std::ranges::find(pages_, page) != pages_.end());
  return {image_->data + page * page_size(), page_size()};
}
Result<Lease> Fetch::finish(bool complete) {
  if (finished_) return std::unexpected(Error::unavailable);
  finished_ = true;
  if (!complete) return std::unexpected(Error::unavailable);
  for (auto page : pages_) image_->ready.insert(page);
  image_->protect(false);
  state_->counts.mapped += pages_.size() * page_size();
  state_->counts.source_bytes += pages_.size() * page_size();
  // Return the captured revision. Cache ownership is a separate decision.
  return Lease(image_, false, pages_);
}
bool Part::can_take() const {
  return !state_->drafting && state_->cache && state_->cache->epoch == state_->history.rbegin()->first && state_->cache.use_count() == 1;
}
Result<Draft> Part::begin(Method method, std::span<const std::size_t> pages) {
  if (state_->drafting) return std::unexpected(Error::busy);
  const auto material = state_->history.rbegin()->second;
  if (!pages_valid(pages, material->size)) return std::unexpected(Error::range);
  if (method == Method::take && !can_take()) return std::unexpected(Error::unavailable);
  auto image = method == Method::take ? std::move(state_->cache) :
      std::make_shared<Image>(material, state_->history.rbegin()->first, method, state_->hardware,
          state_->capacity, state_->pool);
  image->protect(true);
  for (auto p : pages) image->prepare(p, state_->counts, state_->cache.get());
  state_->drafting = true;
  return Draft(state_, std::move(image));
}
void Part::evict() { state_->cache.reset(); }
Counts Part::counts() const { return state_->counts; }
void Part::reset_counts() { state_->counts = {}; }
Epoch Part::latest() const { return state_->latest; }
std::size_t Part::size() const { return state_->history.rbegin()->second->size; }
std::size_t Part::capacity() const { return state_->capacity; }
Result<void> Part::advance(Epoch epoch) {
  if (state_->drafting) return std::unexpected(Error::busy);
  if (epoch <= state_->latest || epoch == std::numeric_limits<Epoch>::max()) return std::unexpected(Error::version);
  state_->latest = epoch; return {};
}
Result<std::pair<Epoch, Epoch>> Part::interval(Epoch epoch) const {
  const auto it = revision(*state_, epoch);
  if (it == state_->history.end() || epoch > state_->latest) return std::unexpected(Error::version);
  const auto next = std::next(it);
  return std::pair{it->first, next == state_->history.end() ? std::numeric_limits<Epoch>::max() : next->first};
}
Draft::Draft(std::shared_ptr<State> s, std::shared_ptr<Image> i) : state_(std::move(s)), image_(std::move(i)) {}
Draft::Draft(Draft&&) noexcept = default;
Draft::~Draft() { if (image_) { state_->drafting = false; } }
Result<Lease> Draft::access(std::span<const std::size_t> pages, bool write) {
  if (!image_) return std::unexpected(Error::unavailable);
  if (!pages_valid(pages, image_->size)) return std::unexpected(Error::range);
  for (auto p : pages) image_->prepare(p, state_->counts);
  return Lease(image_, write, pages);
}
Result<void> Draft::grow(std::size_t bytes) {
  if (!image_) return std::unexpected(Error::unavailable);
  if (bytes < image_->size) return std::unexpected(Error::range);
  if (bytes > image_->capacity) {
    if (image_.use_count() != 1) return std::unexpected(Error::busy);
    auto replacement = std::make_shared<Image>(image_->material, image_->epoch, Method::copy,
        image_->hardware, rounded(bytes), state_->pool);
    replacement->mutable_image = true;
    replacement->size = bytes;
    for (auto page : image_->ready) replacement->prepare(page, state_->counts, image_.get());
    image_ = std::move(replacement); state_->capacity = rounded(bytes);
  }
  image_->size = bytes; return {};
}
Result<std::shared_ptr<Delivery>> Draft::stage(std::span<const Range> ranges) {
  if (!image_) return std::unexpected(Error::unavailable);
  for (auto r : ranges) if (!ready_range(*image_, r)) return std::unexpected(Error::range);
  std::erase_if(image_->borrowers, [](const auto& b) { return b.expired(); });
  for (const auto& weak : image_->borrowers) if (auto b = weak.lock(); b && b->write)
    for (auto p : b->pages) for (auto r : ranges)
      if (r.size && r.offset < (p + 1) * page_size() && p * page_size() < r.offset + r.size)
        return std::unexpected(Error::busy);
  auto delivery = std::make_shared<Delivery>();
  for (auto r : ranges) if (r.size) {
    delivery->patches.emplace_back(r, Bytes(image_->data + r.offset, image_->data + r.offset + r.size));
    state_->counts.staged += r.size;
  }
  staged_.push_back(delivery);
  return delivery;
}
Result<Prepared> Draft::prepare(Epoch epoch) {
  if (!image_) return std::unexpected(Error::unavailable);
  if (image_.use_count() != 1) return std::unexpected(Error::busy);
  if (epoch <= state_->latest || epoch == std::numeric_limits<Epoch>::max()) return std::unexpected(Error::version);
  for (const auto& s : staged_) if (!s->complete) return std::unexpected(Error::pending);
  Bytes bytes(image_->size);
  image_->material->read(0, bytes.data(), image_->material->size); state_->counts.source_bytes += image_->material->size;
  for (const auto& s : staged_) for (const auto& [r, b] : s->patches) if (r.size)
    std::memcpy(bytes.data() + r.offset, b.data(), r.size);
  for (auto p : image_->ready) {
    const auto offset = p * page_size(), length = std::min(page_size(), bytes.size() - offset);
    if (std::memcmp(image_->data + offset, bytes.data() + offset, length) != 0) return std::unexpected(Error::unstaged);
    for (auto tail = length; tail < page_size(); ++tail)
      if (image_->data[offset + tail] != 0) return std::unexpected(Error::unstaged);
  }
  auto material = std::make_shared<Material>(bytes);
  state_->counts.material_bytes += bytes.size();
  std::map<Epoch, std::shared_ptr<Material>> slot;
  slot.emplace(epoch, std::move(material));
  auto prepared = std::make_unique<Preparation>(Preparation{state_, image_, std::move(bytes), slot.extract(epoch)});
  image_->protect(false);
  image_.reset(); staged_.clear();
  return Prepared(std::move(prepared));
}
Prepared::Prepared(std::unique_ptr<Preparation> p) : preparation_(std::move(p)) {}
Prepared::Prepared(Prepared&&) noexcept = default;
Prepared::~Prepared() { if (preparation_) preparation_->state->drafting = false; }
const Bytes& Prepared::bytes() const { assert(preparation_); return preparation_->bytes; }
Result<void> Prepared::install() {
  if (!preparation_) return std::unexpected(Error::unavailable);
  auto& p = *preparation_;
  const auto epoch = p.revision.key();
  p.image->material = p.revision.mapped(); p.image->epoch = epoch;
  auto inserted = p.state->history.insert(std::move(p.revision)); assert(inserted.inserted);
  p.state->cache = p.image; p.state->latest = epoch; p.state->drafting = false;
  preparation_.reset();
  return {};
}
Result<void> Draft::publish(Epoch epoch) {
  auto prepared = prepare(epoch); if (!prepared) return std::unexpected(prepared.error());
  return prepared->install();
}
Result<void> Draft::abort() {
  if (!image_) return std::unexpected(Error::unavailable);
  if (image_.use_count() != 1) return std::unexpected(Error::busy);
  image_.reset(); staged_.clear(); state_->drafting = false; return {};
}
Worker::Worker(const Lease& source, std::span<const Range> read, std::span<const Range> write)
    : size_(source.size()), writes_(write.begin(), write.end()), source_material_(source.image_->material),
      source_draft_(source.image_), from_draft_(source.image_->mutable_image) {
  for (auto r : read) if (!ready_range(*source.image_, r)) throw std::invalid_argument("unprepared input");
  for (auto r : write) if (!valid(r, size_)) throw std::invalid_argument("bad output mask");
  data_ = static_cast<std::uint8_t*>(::mmap(nullptr, size_, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)); require(data_ != MAP_FAILED);
  try {
    for (std::size_t p = 0; p < size_; p += page_size()) {
      auto overlaps = [&](Range r) { return r.size && r.offset < p + page_size() && p < r.offset + r.size; };
      const bool readable = std::ranges::any_of(read, overlaps);
      const bool writable = std::ranges::any_of(write, overlaps);
      const int prot = writable ? PROT_READ | PROT_WRITE : readable ? PROT_READ : PROT_NONE;
      const bool full_read = std::ranges::any_of(read, [&](Range r) { return r.offset <= p && r.offset + r.size >= p + page_size(); });
      if (full_read && !source.image_->mutable_image) {
        require(::mmap(data_ + p, page_size(), prot, MAP_FIXED | MAP_PRIVATE,
            source.image_->material->fd, static_cast<off_t>(p)) != MAP_FAILED);
      } else {
        for (auto r : read) if (overlaps(r)) {
          const auto start = std::max(p, r.offset), end = std::min(p + page_size(), r.offset + r.size);
          std::memcpy(data_ + start, source.data() + start, end - start); copied_ += end - start;
        }
        require(::mprotect(data_ + p, page_size(), prot) == 0);
      }
    }
  } catch (...) { ::munmap(data_, size_); throw; }
}
Worker::~Worker() { ::munmap(data_, size_); }
Result<void> Worker::apply(Lease& destination) const {
  if (!destination.image_ || !destination.write_) return std::unexpected(Error::unavailable);
  if (destination.size() != size_ || destination.image_->material != source_material_ ||
      (from_draft_ && source_draft_.lock() != destination.image_)) return std::unexpected(Error::version);
  for (auto r : writes_) if (!ready_range(*destination.image_, r)) return std::unexpected(Error::range);
  for (auto r : writes_) if (r.size) std::memcpy(destination.writable() + r.offset, data_ + r.offset, r.size);
  return {};
}
}
