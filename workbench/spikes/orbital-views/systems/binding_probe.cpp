#include "../recycling.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <semaphore>
#include <span>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace {
using Bytes = std::vector<std::uint8_t>;
void check(bool ok) { if (!ok) std::abort(); }
std::size_t page() { static const auto n = std::size_t(::sysconf(_SC_PAGESIZE)); return n; }
std::size_t rounded(std::size_t n) { return (n + page() - 1) / page() * page(); }
struct Arena { std::atomic<std::size_t> reserved{}; };
struct Allocation {
  std::shared_ptr<Arena> arena;
  std::uint8_t* data;
  std::size_t capacity, size;
  std::atomic<unsigned> uses{};
  recycling::Hint hint;
  Allocation(std::shared_ptr<Arena> a, std::size_t n, std::size_t reserve)
      : arena(std::move(a)), capacity(rounded(std::max(n, reserve))), size(n) {
    data = static_cast<std::uint8_t*>(::mmap(nullptr, capacity, PROT_READ | PROT_WRITE,
                                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    check(data != MAP_FAILED); arena->reserved += capacity;
  }
  ~Allocation() { check(::munmap(data, capacity) == 0); arena->reserved -= capacity; }
  bool grow(std::size_t n) {
    if (n < size) return false;
    if (n > capacity) {
      if (uses.load()) return false;
      const auto next = rounded(n);
      auto* p = static_cast<std::uint8_t*>(::mmap(nullptr, next, PROT_READ | PROT_WRITE,
                                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
      check(p != MAP_FAILED); std::memcpy(p, data, size);
      check(::munmap(data, capacity) == 0); arena->reserved += next - capacity;
      data = p; capacity = next;
    }
    size = n; return true;
  }
};
struct Use {
  std::shared_ptr<Allocation> allocation;
  explicit Use(std::shared_ptr<Allocation> a) : allocation(std::move(a)) { ++allocation->uses; }
  ~Use() { --allocation->uses; }
};
struct Group {
  std::shared_ptr<Arena> arena;
  std::vector<std::shared_ptr<Allocation>> returned;
  std::shared_ptr<Allocation> acquire(std::size_t n, std::size_t reserve) {
    for (auto i = returned.begin(); i != returned.end(); ++i)
      if (i->use_count() == 1 && (*i)->capacity >= std::max(n, reserve)) {
        auto result = std::move(*i); returned.erase(i); result->size = n; return result;
      }
    return std::make_shared<Allocation>(arena, n, reserve);
  }
  void release(std::shared_ptr<Allocation> a, recycling::Hint h = {}) {
    a->hint = h; returned.push_back(std::move(a));
  }
};
struct Source {
  std::uint64_t identity, revision;
  std::size_t offset, size;
  const void* adapter;
  std::shared_ptr<const void> retained;
};
struct Transfer {
  Source source;
  std::shared_ptr<Use> target;
  std::vector<std::size_t> pages;
  std::uint64_t generation;
};
struct Receipt { std::shared_ptr<Transfer> request; bool complete; };
struct Inbox {
  std::mutex mutex;
  std::deque<Receipt> receipts;
  void post(Receipt r) { std::lock_guard lock(mutex); receipts.push_back(std::move(r)); }
};
struct Backend {
  virtual ~Backend() = default;
  virtual Source capture(const Bytes&, std::uint64_t revision) const = 0;
  virtual bool fill(const Transfer&, bool fail) const = 0;
};
struct MemoryBackend final : Backend {
  Source capture(const Bytes& bytes, std::uint64_t revision) const override {
    return {41, revision, 128, bytes.size() - 128, this, std::make_shared<const Bytes>(bytes)};
  }
  bool fill(const Transfer& t, bool fail) const override {
    check(t.source.adapter == this);
    const auto& source = *static_cast<const Bytes*>(t.source.retained.get());
    for (auto p : t.pages) {
      std::memcpy(t.target->allocation->data + p * page(), source.data() + t.source.offset + p * page(), page());
      if (fail) return false;
    }
    return true;
  }
};
struct File { int fd; ~File() { check(::close(fd) == 0); } };
struct FileBackend final : Backend {
  Source capture(const Bytes& bytes, std::uint64_t revision) const override {
    auto file = std::make_shared<File>();
    file->fd = int(::syscall(SYS_memfd_create, "binding-source", MFD_CLOEXEC | MFD_ALLOW_SEALING));
    check(file->fd >= 0 && ::ftruncate(file->fd, bytes.size()) == 0);
    check(::pwrite(file->fd, bytes.data(), bytes.size(), 0) == ssize_t(bytes.size()));
    check(::fcntl(file->fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) == 0);
    return {41, revision, 128, bytes.size() - 128, this, std::move(file)};
  }
  bool fill(const Transfer& t, bool fail) const override {
    check(t.source.adapter == this);
    const auto& file = *static_cast<const File*>(t.source.retained.get());
    for (auto p : t.pages) {
      std::size_t done{};
      while (done != page()) {
        const auto n = ::pread(file.fd, t.target->allocation->data + p * page() + done,
                               page() - done, t.source.offset + p * page() + done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += std::size_t(n);
      }
      if (fail) return false;
    }
    return true;
  }
};
struct Window {
  std::shared_ptr<Use> use;
  Source source;
  std::vector<std::size_t> ready;
  std::span<const std::uint8_t> span(std::size_t p) const {
    if (std::ranges::find(ready, p) == ready.end()) return {};
    return {use->allocation->data + p * page(), page()};
  }
};
struct Owner {
  Group& group;
  Inbox& inbox;
  const std::thread::id thread = std::this_thread::get_id();
  std::uint64_t generation{};
  std::shared_ptr<Allocation> active;
  std::shared_ptr<Window> ready;
  bool pending{}, failed{};
  Owner(Group& g, Inbox& i) : group(g), inbox(i) {}
  void cancel() {
    check(std::this_thread::get_id() == thread); ++generation; ready.reset(); pending = failed = false;
    if (active) group.release(std::move(active));
  }
  std::shared_ptr<Transfer> start(Source source) {
    check(source.size == 3 * page());
    cancel(); active = group.acquire(source.size, source.size); pending = true;
    return std::make_shared<Transfer>(Transfer{std::move(source), std::make_shared<Use>(active), {0, 2}, generation});
  }
  void settle() {
    check(std::this_thread::get_id() == thread);
    std::deque<Receipt> done;
    { std::lock_guard lock(inbox.mutex); done.swap(inbox.receipts); }
    for (auto& r : done) if (r.request->generation == generation) {
      pending = false; failed = !r.complete;
      if (r.complete) ready = std::make_shared<Window>(Window{r.request->target, r.request->source, r.request->pages});
    }
  }
  ~Owner() { cancel(); }
};
Bytes bytes(unsigned revision) {
  Bytes result(3 * page() + 128);
  for (std::size_t i = 0; i != result.size(); ++i) result[i] = std::uint8_t((i * 17 + revision * 31) % 251);
  return result;
}
void verify(const Window& w, unsigned revision) {
  check(w.source.identity == 41 && w.source.revision == revision && w.span(1).empty());
  for (auto p : {0u, 2u}) {
    auto span = w.span(p); check(span.size() == page());
    for (std::size_t i = 0; i != span.size(); ++i)
      check(span[i] == (std::uint8_t(((128 + p * page() + i) * 17 + revision * 31) % 251)));
  }
}
void run(Backend& backend, Group& group) {
  Inbox inbox; Owner owner{group, inbox};
  auto request = owner.start(backend.capture(bytes(7), 7));
  std::thread first([&] { inbox.post({request, backend.fill(*request, false)}); });
  first.join(); check(!owner.ready && owner.pending); request.reset(); owner.settle(); verify(*owner.ready, 7);
  auto window = owner.ready; auto* address = window->use->allocation->data;
  check(window->use->allocation->grow(3 * page()) && window->use->allocation->data == address);
  check(!window->use->allocation->grow(4 * page()));
  owner.cancel(); verify(*window, 7); window.reset();
  request = owner.start(backend.capture(bytes(7), 7));
  inbox.post({request, backend.fill(*request, true)}); request.reset(); owner.settle();
  check(!owner.ready && !owner.pending && owner.failed);

  auto source = backend.capture(bytes(7), 7);
  std::weak_ptr<const void> retained = source.retained;
  request = owner.start(std::move(source)); auto* old = request->target->allocation->data;
  check(!request->target->allocation->grow(4 * page()));
  std::binary_semaphore entered{0}, resume{0};
  std::thread delayed([r = std::move(request), &backend, &inbox, &entered, &resume] {
    entered.release(); resume.acquire(); inbox.post({r, backend.fill(*r, false)});
  });
  entered.acquire(); owner.cancel(); check(!retained.expired());
  request = owner.start(backend.capture(bytes(8), 8)); check(request->target->allocation->data != old);
  inbox.post({request, backend.fill(*request, false)}); request.reset(); owner.settle(); verify(*owner.ready, 8);
  resume.release(); delayed.join(); check(!retained.expired()); owner.settle();
  check(retained.expired()); verify(*owner.ready, 8);
  auto reused = group.acquire(3 * page(), 3 * page()); check(reused->data == old);
  group.release(std::move(reused));
}
}  // namespace
int main() {
  auto arena = std::make_shared<Arena>();
  {
    Group group{arena, {}};
    auto scratch = group.acquire(47, page()); std::memset(scratch->data, 23, 47);
    auto held = std::make_shared<Use>(scratch); auto* address = scratch->data;
    check(scratch->grow(64) && scratch->data == address && !scratch->grow(2 * page()));
    held.reset(); check(scratch->grow(2 * page()) && scratch->data != address);
    for (unsigned i = 0; i != 47; ++i) check(scratch->data[i] == 23);
    group.release(std::move(scratch), {recycling::Temperature::warm, 0, std::chrono::steady_clock::now()});
    auto reuse = group.acquire(47, page()); check(reuse->hint.temperature == recycling::Temperature::warm);
    group.release(std::move(reuse));
    MemoryBackend memory; FileBackend file; run(memory, group); run(file, group);
  }
  check(arena->reserved == 0);
  std::shared_ptr<Use> escape;
  std::weak_ptr<Arena> retained_arena;
  {
    auto private_arena = std::make_shared<Arena>(); retained_arena = private_arena;
    Group transient{private_arena, {}};
    auto raw = transient.acquire(64, page()); raw->data[0] = 91;
    escape = std::make_shared<Use>(std::move(raw));
  }
  check(!retained_arena.expired() && escape->allocation->data[0] == 91);
  escape.reset(); check(retained_arena.expired());
  std::puts("raw scratch reserve/grow/reuse; retained native windows; generic-source memory/file reads; owner-settled delayed, failed and cancelled transfers: ok");
}
