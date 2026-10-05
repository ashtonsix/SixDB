#include "composition_fixture.h"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <semaphore>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>

using namespace object_fixture;
namespace {
std::size_t checks{};
void require(bool ok, const char* why) {
  ++checks;
  if (!ok) {
    std::fprintf(stderr, "failed check %zu: %s\n", checks, why);
    throw std::runtime_error(why);
  }
}
template<class F> void rejects(F&& fn, const char* why) {
  bool failed{};
  try { fn(); } catch (const std::exception&) { failed = true; }
  require(failed, why);
}
std::unique_ptr<Working> write(Context& c, Strategy s, Range r, std::uint8_t byte) {
  auto w = c.begin(s, std::span(&r, 1));
  require(static_cast<bool>(w), "prepare native write");
  std::memset(w->data(0), byte, r.size);
  return w;
}
class DelayedSend {
 public:
  explicit DelayedSend(ByteUse bytes) : size_(bytes.size()) {
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fd_))
      throw std::runtime_error("socketpair");
    thread_ = std::thread([this, use = std::move(bytes)]() mutable {
      ready_.release(); go_.acquire();
      std::size_t sent{};
      while (sent < use.size()) {
        const auto n = ::send(fd_[0], use.data() + sent, use.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { failed_ = true; break; }
        sent += static_cast<std::size_t>(n);
      }
      ::shutdown(fd_[0], SHUT_WR);
      use.complete();
    });
    ready_.acquire();
  }
  ~DelayedSend() {
    if (thread_.joinable()) {
      go_.release(); ::shutdown(fd_[1], SHUT_RDWR); thread_.join();
    }
    ::close(fd_[0]); ::close(fd_[1]);
  }
  Bytes finish() {
    Bytes out(size_);
    go_.release();
    std::size_t received{};
    while (received < size_) {
      const auto n = ::recv(fd_[1], out.data() + received, size_ - received, 0);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) throw std::runtime_error("short backend completion");
      received += static_cast<std::size_t>(n);
    }
    thread_.join();
    require(!failed_, "actual sender completed");
    return out;
  }
 private:
  int fd_[2]{-1, -1};
  std::size_t size_{};
  std::binary_semaphore ready_{0}, go_{0};
  std::thread thread_;
  std::atomic<bool> failed_{false};
};

void canonical(Strategy s) {
  Caller caller;
  Pool pool(32 * page());
  {
    Context c(pool, caller.history);
    require(caller.run(c, s) == caller.expected, "independent canonical filter result");
    require(read_all(c, 1, caller.after.size()) == caller.after, "output plus locator/summary publication");
    require(read_all(c, 0, caller.before.size()) == caller.before, "old state reconstructed");
  }
  require(pool.counts().charged == 0 && pool.counts().active == 0, "complete caller retirement");
}
void readers(Strategy s) {
  auto a = seed(), b = a;
  std::fill_n(b.begin() + page(), 64, 211);
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(32 * page());
  {
    Context c(p, h);
    auto raw = one(c, 0, {page(), 64});
    const auto old = raw.home();
    DelayedSend send(raw);
    auto w = write(c, s, {page(), 64}, 211);
    require(w->publish(1), "publication with raw and backend use");
    require(std::equal(raw.data(), raw.data() + 64, a.data() + page()), "old bytes survive replacement");
    require(one(c, 1, {page(), 64}).home() != old, "overlap uses different home");
    c.close();
    rejects([&] { one(c, 1, {page(), 64}); }, "close stops submissions");
    require(p.counts().charged >= page() && p.counts().active == 1,
            "context close leaves captured physical record");
    Context replacement(p, h, 1);
    require(read_all(replacement, 1, b.size()) == b, "replacement incarnation state");
    require(send.finish() == Bytes(a.begin() + page(), a.begin() + page() + 64),
            "delayed actual syscall reads captured old bytes");
    raw.complete(); raw.complete();
    require(p.counts().active == 0, "duplicate release idempotent");
  }
  require(p.counts().charged == 0, "old completion never releases replacement bytes");
}
void padding(Strategy s, std::size_t admitted) {
  auto a = seed(), b = a;
  std::fill_n(a.begin() + page(), 47, 71);
  std::fill_n(a.begin() + page() + 47, 17, 213);
  b = a;
  std::fill_n(b.begin() + page() + 47, 3, 89);
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(32 * page()); Context c(p, h);
  auto old = one(c, 0, {page(), admitted});
  auto w = write(c, s, {page() + 47, 3}, 89);
  require(w->publish(1), "append publishes");
  require(std::equal(old.data(), old.data() + admitted, a.data() + page()), "native window including padding stable");
  auto now = one(c, 1, {page(), 64});
  require((now.home() == old.home()) == (s == Strategy::reuse && admitted == 47),
          "reuse depends on issued writes versus admitted windows");
  auto late = one(c, 0, {page(), 64});
  require(std::equal(late.data(), late.data() + 64, a.data() + page()), "late wider old window reconstructs");
}
void writers(Strategy s, bool both, bool reverse) {
  auto a = seed();
  const auto x = page() + 16, y = page() + 64;
  a[x] = 11; a[y] = 22; a[page() + 100] = 165;
  auto b = a; b[reverse ? x : y] = reverse ? 111 : 222;
  auto d = b; if (both) d[reverse ? y : x] = reverse ? 222 : 111;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b, d});
  Pool p(64 * page()); Context c(p, h);
  require(read_all(c, 0, a.size()) == a, "initial shared-page state");
  auto wa = write(c, s, {x, 1}, 111);
  Range overlap{x, 1};
  require(!c.begin(s, std::span(&overlap, 1)), "conflicting writer rejected");
  auto wb = write(c, s, {y, 1}, 222);
  require(one(c, 0, {x, 1}).data()[0] == 11, "tentative reuse hidden from reader");
  auto* first = reverse ? wa.get() : wb.get();
  auto* last = reverse ? wb.get() : wa.get();
  require(first->publish(1), "first disjoint writer publication");
  if (both) require(last->publish(2), "rebase actual stores onto actual base");
  else last->abort();
  require(read_all(c, both ? 2 : 1, d.size()) == d, "commit/abort preserves other writer and guard");
  require(read_all(c, 0, a.size()) == a, "old shared page reconstructs");
}
void stale() {
  auto a = seed(), b = a; b[page()] = 212;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(32 * page()); Context c(p, h);
  auto ticket = c.resolve({page(), 1});
  std::binary_semaphore ready{0}, go{0};
  ByteUse result;
  std::thread reader([&] { ready.release(); go.acquire(); result = c.acquire(ticket); });
  ready.acquire();
  auto w = write(c, Strategy::share, {page(), 1}, 212);
  require(w->publish(1), "rebind paused locator");
  auto foreign = std::make_shared<History>(std::vector<Bytes>{Bytes(a.size(), 238)});
  Context other(p, foreign);
  auto alien = one(other, 0, {page(), 1});
  require(alien.home() != ticket.home, "reused slot has another physical identity");
  go.release(); reader.join();
  require(!result && p.counts().stale == 1, "stale ticket rejected");
  require(one(c, 0, {page(), 1}).data()[0] == a[page()], "retry keeps logical old state");
  require(c.acquire(c.resolve({page(), 1})).data()[0] == 212, "fresh acquisition");
}
template<class F> void traps(F&& fn, const char* why) {
  auto child = ::fork();
  if (child < 0) throw std::runtime_error("fork");
  if (!child) {
    const rlimit none{0, 0}; ::setrlimit(RLIMIT_CORE, &none); ::alarm(3);
    fn(); ::_exit(0);
  }
  int status{};
  require(::waitpid(child, &status, 0) == child, "protection child completes");
  require(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS), why);
}
void projection() {
  auto a = Bytes(3 * page(), 238);
  std::fill_n(a.begin(), page(), 17);
  std::fill_n(a.begin() + page() + 16, 47, 29);
  auto h = std::make_shared<History>(std::vector<Bytes>{a});
  Pool p(12 * page()); Context c(p, h);
  auto v = c.open(c.retain(0), whole(a.size()));
  std::array<Range, 2> allowed{{{0, page()}, {page() + 16, 47}}};
  Projection projected(v, allowed);
  require(p.counts().charged == 4 * page(), "only partial projection page adds backing");
  require(projected.page_data(0)[page() - 1] == 17, "whole permitted page shared");
  for (std::size_t i = 0; i < page(); ++i)
    require(projected.page_data(1)[i] == (i >= 16 && i < 63 ? 29 : 0), "masked page contains no secret");
  rejects([&] { projected.borrow(1, {0, 1}); }, "I/O borrow stays within permitted extent");
  rejects([&] { projected.borrow(2, {0, 1}); }, "I/O borrow excludes forbidden page");
  traps([&] { *const_cast<volatile std::uint8_t*>(projected.page_data(0)) = 99; }, "RO view traps store");
  traps([&] { volatile auto byte = projected.page_data(2)[0]; (void)byte; }, "excluded page traps read");
  auto captured = projected.borrow(1, {16, 47});
  DelayedSend send(captured);
  v.bytes.clear(); c.close(); captured.complete();
  require(send.finish() == Bytes(47, 29), "actual backend uses projected bytes");
}
void projection_lifetime() {
  auto a = Bytes(2 * page(), 17), b = a;
  std::fill_n(b.begin(), 64, 211);
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(12 * page()); Context c(p, h);
  auto source = c.open(c.retain(0), whole(a.size()));
  const Range allowed{0, page()};
  auto projection = std::make_unique<Projection>(source, std::span(&allowed, 1));
  source.bytes.clear();
  const auto active = p.counts().active;
  auto w = write(c, Strategy::reuse, {0, 64}, 211);
  require(w->publish(1), "publish while projected source view has closed");
  require(projection->page_data(0)[0] == 17, "projection itself preserves full shared page");
  auto borrowed = projection->borrow(0, {0, 64});
  require(p.counts().active == active, "derived backend borrow shares projection physical record");
  projection.reset(); c.close();
  require(borrowed.data()[0] == 17, "backend borrow preserves alias mapping after projection destruction");
  borrowed.complete();
  require(p.counts().charged == 0 && p.counts().aliases == 0, "projection and backing retire together");
}
void projection_failure() {
  auto a = seed(1);
  auto h = std::make_shared<History>(std::vector<Bytes>{a});
  Pool p(4 * page()); Context c(p, h);
  auto source = c.open(c.retain(0), whole(a.size()));
  const auto before = p.counts();
  const Range allowed{16, 47};
  const std::uint8_t* failed_alias{};
  rejects([&] {
    Projection projection(source, std::span(&allowed, 1), [&](const auto* alias) {
      failed_alias = alias;
      throw std::bad_alloc();
    });
  }, "metadata failure after mapping unwinds projection");
  unsigned char residency{};
  require(::mincore(const_cast<std::uint8_t*>(failed_alias), page(), &residency) == -1 && errno == ENOMEM,
          "failed native alias really unmapped");
  const auto after = p.counts();
  require(after.charged == before.charged && after.active == before.active && after.aliases == 0,
          "failed masked projection releases backing and grants without counter underflow");
  require(after.maps - before.maps == 2 && after.unmaps - before.unmaps == 2,
          "failed masked backing and alias mappings accounted symmetrically");
}
void projection_narrow_borrow() {
  auto a = seed(1), b = a; b[256] = 211;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(4 * page()); Context c(p, h);
  auto source = c.open(c.retain(0), whole(a.size()));
  const Range allowed{0, page()};
  auto projection = std::make_unique<Projection>(source, std::span(&allowed, 1));
  auto captured = projection->borrow(0, {0, 64});
  source.bytes.clear(); projection.reset();
  auto w = write(c, Strategy::reuse, {256, 1}, 211);
  require(w->publish(1), "write disjoint from narrowed backend coverage publishes");
  require(p.counts().reused == 0 && p.counts().replacements == 1,
          "shared projection record conservatively retains original full-page exposure");
  require(std::equal(captured.data(), captured.data() + 64, a.data()), "narrow backend bytes stable");
  captured.complete(); c.close();
  require(p.counts().charged == 0 && p.counts().active == 0, "narrow backend final completion retires exposure");
}
void source_progress() {
  auto a = seed(), b = a; b[2 * page()] = 231;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(16 * page()); Context c(p, h);
  one(c, 0, {0, 1}).complete();
  std::binary_semaphore entered{0}, finish{0};
  std::atomic<bool> first{true};
  h->source_hook([&](auto, Range r) {
    if (r.offset == 2 * page() && first.exchange(false)) {
      entered.release(); finish.acquire();
    }
  });
  ByteUse fetched;
  std::thread fetch([&] { fetched = one(c, 0, {2 * page(), 1}); });
  entered.acquire();
  for (unsigned i = 0; i < 32; ++i)
    require(one(c, 0, {0, 1}).data()[0] == a[0], "resident work progresses during blocked source");
  auto w = write(c, Strategy::share, {2 * page(), 1}, 231);
  require(w->publish(1), "new binding publishes while old fetch is in flight");
  finish.release(); fetch.join();
  require(fetched.data()[0] == a[2 * page()], "old fetch completion keeps intended state");
  require(one(c, 1, {2 * page(), 1}).data()[0] == 231, "old fetch cannot overwrite replacement binding");
  fetched.complete();
  first = true;
  h->source_hook([&](auto, Range r) {
    if (r.offset == 3 * page() && first.exchange(false)) {
      entered.release(); finish.acquire();
    }
  });
  std::atomic<bool> cancelled{false};
  std::thread closing_fetch([&] {
    try { one(c, 1, {3 * page(), 1}); }
    catch (const std::exception&) { cancelled = true; }
  });
  entered.acquire();
  c.close();
  require(p.counts().charged == page(), "close leaves actual pending backend buffer charged");
  finish.release(); closing_fetch.join();
  require(cancelled && p.counts().charged == 0, "closed fetch finishes and retires without installing");
}
void source_failure(Strategy strategy) {
  auto a = seed(), b = a; b[0] = 211; b[2 * page()] = 231;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(16 * page()); Context c(p, h);
  one(c, 0, {0, 1}).complete();
  h->source_hook([&](auto, Range r) {
    if (r.offset == 2 * page()) throw std::runtime_error("injected unavailable source");
  });
  Range cold{2 * page(), 1};
  rejects([&] { one(c, 0, cold); }, "failed read never substitutes zero bytes");
  std::array<Range, 2> writes{{{0, 1}, cold}};
  rejects([&] { c.begin(strategy, writes); }, "failed preparation rolls back acquired claims");
  require(one(c, 0, {0, 1}).data()[0] == a[0] && p.counts().charged == page(),
          "failed preparation leaves published bytes and budget unchanged");
  h->source_hook({});
  auto w = write(c, Strategy::reuse, {0, 1}, 211);
  require(!w->publish(99), "unknown supplied position rejected");
  h->source_hook([](auto, auto) { throw std::runtime_error("source failed after writable exposure"); });
  w->abort();
  require(one(c, 0, {0, 1}).data()[0] == a[0] && p.counts().undo == 0,
          "abort uses charged local undo slices even after source failure");
  h->source_hook({});
}
void rebase_progress() {
  auto a = seed(), b = a; b[page() + 64] = 222;
  auto d = b; d[page() + 16] = 111;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b, d});
  Pool p(32 * page()); Context c(p, h);
  require(read_all(c, 0, a.size()) == a, "rebase fixture prepared");
  auto wa = write(c, Strategy::share, {page() + 16, 1}, 111);
  auto wb = write(c, Strategy::share, {page() + 64, 1}, 222);
  require(wb->publish(1), "different writer changes rebase source");
  auto wc = write(c, Strategy::reuse, {page() + 128, 1}, 231);
  std::binary_semaphore entered{0}, finish{0};
  std::atomic<bool> first{true};
  h->source_hook([&](auto at, Range r) {
    if (at == 1 && r.offset == page() && first.exchange(false)) {
      entered.release(); finish.acquire();
    }
  });
  bool published{};
  std::thread publication([&] { published = wa->publish(2); });
  entered.acquire();
  for (unsigned i = 0; i < 32; ++i)
    require(one(c, 1, {0, 1}).data()[0] == a[0], "point work progresses during rebase source delay");
  finish.release(); publication.join();
  require(published, "rebase resumes");
  wc->abort(); h->source_hook({});
  require(read_all(c, 2, d.size()) == d, "rebase and later abort preserve committed bytes");
}
void rebase_replaced(Strategy strategy, bool retry) {
  auto a = seed(), b = a; b[page() + 64] = 222;
  auto d = b; d[page() + 128] = 231;
  auto e = d; e[page() + 16] = 111;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b, d, e});
  Pool p(64 * page()); Context c(p, h);
  require(read_all(c, 0, a.size()) == a, "replacement rebase fixture");
  auto wa = write(c, strategy, {page() + 16, 1}, 111);
  auto wb = write(c, Strategy::share, {page() + 64, 1}, 222);
  require(wb->publish(1), "first base replacement");
  auto wc = write(c, Strategy::reuse, {page() + 128, 1}, 231);
  std::binary_semaphore entered{0}, finish{0};
  std::atomic<bool> first{true};
  h->source_hook([&](auto at, Range r) {
    if (at == 1 && r.offset == page() && first.exchange(false)) {
      entered.release(); finish.acquire();
    }
  });
  bool installed{true};
  std::thread rebase([&] { installed = wa->publish(3); });
  entered.acquire();
  require(wc->publish(2), "second base replacement during blocked rebase");
  finish.release(); rebase.join(); h->source_hook({});
  require(!installed && c.position() == 2, "stale rebase cannot install into newer image");
  require(*wa->data(0) == 111, "rejected rebase retains actual staged store");
  if (retry) require(wa->publish(3), "staged rebase retries onto latest image");
  else wa->abort();
  require(read_all(c, retry ? 3 : 2, a.size()) == (retry ? e : d),
          "retry or abort preserves both intervening publications");
  c.close(); wa.reset(); wb.reset(); wc.reset();
  require(p.counts().charged == 0 && p.counts().active == 0 && p.counts().undo == 0,
          "failed rebase and later disposition retire every grant and beforeimage");
}
void rebase_failure(bool source_error) {
  auto a = seed(), b = a; b[page() + 64] = 222;
  auto d = b; d[page() + 16] = 111;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b, d});
  Pool p(12 * page(), page()); Context c(p, h);
  require(read_all(c, 0, a.size()) == a, "bounded rebase fixture");
  auto wa = write(c, Strategy::copy, {page() + 16, 1}, 111);
  auto wb = write(c, Strategy::share, {page() + 64, 1}, 222);
  require(wb->publish(1), "rebase failure published predecessor");
  auto wc = write(c, Strategy::reuse, {page() + 128, 1}, 231);
  const auto before = p.counts();
  if (source_error) {
    h->source_hook([&](auto at, Range r) {
      if (at == 1 && r.offset == page()) throw std::runtime_error("injected rebase source failure");
    });
    rejects([&] { wa->publish(2); }, "source failure midway through rebase");
  } else {
    require(!wa->publish(2) && p.counts().denied > before.denied,
            "capacity denial midway through rebase");
  }
  require(p.counts().maps > before.maps && p.counts().charged == before.charged &&
              p.counts().active == before.active && *wa->data(0) == 111,
          "partial rebase preparation unwinds while retaining staged work");
  wa->abort(); wc->abort(); h->source_hook({});
  require(read_all(c, 1, a.size()) == b, "failed rebase abort preserves predecessor and untouched bytes");
  auto retry = write(c, Strategy::share, {page() + 16, 1}, 111);
  require(retry->publish(2) && read_all(c, 2, a.size()) == d,
          "failed rebase releases writer coverage for a later caller");
  c.close(); wa.reset(); wb.reset(); wc.reset(); retry.reset();
  require(p.counts().charged == 0 && p.counts().active == 0 && p.counts().undo == 0,
          "rebase source and admission failures fully retire");
}
void prepared_cache() {
  auto a = seed();
  auto h = std::make_shared<History>(std::vector<Bytes>{a});
  Pool p(8 * page(), page(), 4);
  {
    Context c(p, h);
    require(read_all(c, 0, a.size()) == a, "cache seed");
  }
  require(p.counts().cached == 4 * page() && p.counts().charged == 4 * page(),
          "empty prepared cache stays charged");
  p.reset_counts();
  {
    Context c(p, h);
    require(read_all(c, 0, a.size()) == a && p.counts().maps == 0,
            "cached home reuse needs no mapping syscall");
  }
  require(p.counts().charged - p.counts().cached == 0, "cached is separate from active home capacity");
  for (bool spare : {false, true}) {
    Pool bounded((spare ? 9 : 8) * page(), page(), 8);
    {
      std::vector<std::shared_ptr<Pool::Home>> warm;
      for (unsigned i = 0; i < 8; ++i) warm.push_back(bounded.allocate(true));
      require(std::all_of(warm.begin(), warm.end(), [](const auto& home) { return static_cast<bool>(home); }),
              "full cache prewarm using service capacity");
    }
    Context current(bounded, h);
    require(read_all(current, 0, a.size()) == a, "active homes drawn from full charged cache");
    const Range r{0, 64};
    auto reused = current.begin(Strategy::reuse, std::span(&r, 1));
    require(static_cast<bool>(reused) == spare,
            "undo admission respects active foreground and total backing budgets separately");
    if (reused) reused->abort();
    auto copied = current.begin(Strategy::share, std::span(&r, 1));
    require(static_cast<bool>(copied), "cached replacement fits without new total backing charge");
    copied->abort(); current.close();
    require(bounded.counts().undo == 0 && bounded.counts().charged == 8 * page(),
            "rejected or aborted cache preparation leaves no hidden undo charge");
  }
}
void pressure(Strategy s) {
  auto a = seed(), b = a; b[page()] = 111;
  auto d = b; d[2 * page()] = 222;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b, d});
  Pool p(6 * page(), page()); Context c(p, h);
  require(read_all(c, 0, a.size()) == a, "pressure initial preparation");
  auto old = one(c, 0, {page(), 1});
  Range ar{page(), 1}, br{2 * page(), 1};
  if (s == Strategy::copy) {
    require(!c.begin(s, std::span(&ar, 1)), "whole copy denied by finite capacity");
    require(read_all(c, 0, a.size()) == a, "failed preparation changes no bytes");
    return;
  }
  auto w = write(c, s, ar, 111);
  require(w->publish(1), "first pressure publication");
  auto other = one(c, 1, br);
  require(p.counts().charged == 5 * page(), "current and old backing counted once");
  require(!c.begin(s, std::span(&br, 1)), "replacement denied before mutation");
  for (unsigned i = 0; i < 64; ++i)
    require(one(c, 1, {0, 1}).data()[0] == a[0], "unrelated point work progresses");
  auto service = one(c, 0, br, true);
  require(service.data()[0] == a[br.offset] && p.counts().charged == 6 * page(), "reserved service capacity works");
  service.complete(); old.complete(); other.complete();
  auto retry = write(c, s, br, 222);
  require(retry->publish(2), "retry after captured completion");
  require(read_all(c, 2, d.size()) == d && p.counts().peak <= 6 * page(), "budget and exact state preserved");
}
void reset(Strategy s) {
  auto a = seed(), b = a; b[page()] = 231;
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(32 * page()); RetainedState retained; std::unique_ptr<Working> work;
  {
    Context c(p, h);
    retained = c.retain(0); retained.pending = std::make_shared<Pending>();
    Range r{page(), 1};
    rejects([&] { c.open(retained, std::span(&r, 1)); }, "unresolved input cannot masquerade as final fallback");
    one(c, 0, r).complete();
    work = write(c, s, r, 231); c.close();
    require(!work->publish(1), "closed incarnation cannot publish");
  }
  work->abort(); work.reset();
  require(p.counts().charged == 0, "logical retention does not retain mappings");
  Context reopened(p, h, 0); Range r{page(), 1};
  retained.pending->outcome = Pending::Outcome::no_effect;
  require(reopened.open(retained, std::span(&r, 1)).bytes[0].data()[0] == a[page()], "pending no-effect resolves fallback");
  retained.pending->outcome = Pending::Outcome::effect; retained.pending->result = 1;
  require(reopened.open(retained, std::span(&r, 1)).bytes[0].data()[0] == 231, "pending effect resolves future result");
  Context installed(p, h, 1);
  require(read_all(installed, 1, b.size()) == b, "context reset uses supplied synced prefix");
}
void atomic_view(Strategy s) {
  auto a = seed(), b = a;
  std::array<Range, 3> ranges{{{16, 1}, {page() + 64, 1}, {2 * page() + 8, 1}}};
  std::array<std::uint8_t, 3> values{111, 222, 99};
  for (std::size_t i = 0; i < 3; ++i) b[ranges[i].offset] = values[i];
  auto h = std::make_shared<History>(std::vector<Bytes>{a, b});
  Pool p(64 * page()); Context c(p, h);
  auto w = c.begin(s, ranges); require(static_cast<bool>(w), "prepare multipart write");
  for (std::size_t i = 0; i < 3; ++i) *w->data(i) = values[i];
  std::atomic<bool> consistent{true}; std::binary_semaphore ready{0}, go{0};
  std::thread observer([&] {
    ready.release(); go.acquire();
    for (unsigned i = 0; i < 200; ++i) {
      const auto at = c.position();
      auto view = c.open(c.retain(at), ranges);
      const auto& expected = at == 0 ? a : b;
      for (std::size_t j = 0; j < 3; ++j)
        if (view.bytes[j].data()[0] != expected[ranges[j].offset]) consistent = false;
    }
  });
  ready.acquire(); go.release();
  require(w->publish(1), "multipart publication"); observer.join();
  require(consistent, "view never observes mixed locator/body/summary");
}
}  // namespace
int main() {
  ::alarm(25);
  try {
    for (auto s : {Strategy::copy, Strategy::share, Strategy::reuse}) {
      const auto first = checks;
      canonical(s); readers(s); padding(s, 47); padding(s, 64);
      for (bool both : {false, true}) for (bool reverse : {false, true}) writers(s, both, reverse);
      pressure(s); reset(s); atomic_view(s); source_failure(s);
      rebase_replaced(s, false); rebase_replaced(s, true);
      std::printf("{\"strategy\":\"%s\",\"checks\":%zu,\"result\":\"pass\"}\n", name(s), checks - first);
    }
    stale(); projection(); projection_lifetime(); projection_failure(); projection_narrow_borrow();
    source_progress(); rebase_progress(); rebase_failure(false); rebase_failure(true); prepared_cache();
    std::printf("{\"checks\":%zu,\"page_bytes\":%zu,\"result\":\"pass\","
                "\"scope\":\"native local composition; supplied history; context reset\"}\n", checks, page());
  } catch (const std::exception& e) {
    std::fprintf(stderr, "check %zu: %s\n", checks, e.what());
    return 1;
  }
}
