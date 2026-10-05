#include "prototype.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace views;
namespace {
std::size_t checks{};
void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "failed check %zu\n", checks); std::abort(); } }
template<class T> T get(Result<T> r) { check(r.has_value()); return std::move(*r); }
void done(Result<void> r) { check(r.has_value()); }
Bytes original(std::size_t pages = 3) {
  Bytes b(pages * page_size()); for (std::size_t i = 0; i < b.size(); ++i) b[i] = static_cast<std::uint8_t>((i * 37 + 19) % 251); return b;
}
const std::vector<std::size_t> all{0, 1, 2}, first{0};
bool traps(const std::uint8_t* at, bool write) {
  const auto pid = ::fork(); check(pid >= 0);
  if (!pid) {
    const rlimit limit{0, 0}; ::setrlimit(RLIMIT_CORE, &limit); ::alarm(2);
    if (write) *const_cast<volatile std::uint8_t*>(at) = 81;
    else { const auto v = *static_cast<const volatile std::uint8_t*>(at); (void)v; }
    ::_exit(0);
  }
  int status{}; check(::waitpid(pid, &status, 0) == pid);
  return WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
}
void versions() {
  const auto bytes = original(); Part part(bytes, 10);
  done(part.advance(19));
  check(get(part.interval(15)).first == 10);
  auto old = get(part.open(15, all));
  auto draft = get(part.begin(Method::cow, first));
  auto writer = get(draft.access(first)); writer.writable()[41] = 99; writer.close();
  const std::vector<Range> changed{{41, 1}};
  auto delivery = get(draft.stage(changed)); delivery->complete = true; done(draft.publish(20));
  check(get(part.interval(15)) == std::pair<Epoch, Epoch>{10, 20});
  check(get(part.interval(20)).first == 20);
  auto now = get(part.open(20, all));
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    check(old.data()[i] == bytes[i]); check(now.data()[i] == (i == 41 ? 99 : bytes[i]));
  }
  check(!part.open(21, first)); check(!part.open(9, first));
}
void methods(Method method, bool hardware) {
  const auto bytes = original(); Part part(bytes, 1, hardware);
  { auto warm = get(part.open(1, all, Method::copy)); }
  auto old = get(part.open(1, first)); auto physical_use = old; old.close();
  check(!part.can_take()); check(!part.begin(Method::take, first)); physical_use.close();
  check(part.can_take());
  auto draft = get(part.begin(method, all));
  check(!part.begin(Method::cow, first));
  auto a = get(draft.access(first)), b = get(draft.access(first));
  // The caller coordinates disjoint writers; Orbital does not provide a row lock here.
  std::thread x([&] { a.writable()[9] = 71; }), y([&] { b.writable()[10] = 72; }); x.join(); y.join();
  check(!draft.abort()); check(!draft.publish(2)); a.close(); b.close();
  const std::vector<Range> ranges{{9, 2}};
  auto sent = get(draft.stage(ranges));
  auto resumed = get(draft.access(first)); resumed.writable()[9] = 83; resumed.close();
  auto sent_later = get(draft.stage(ranges));
  sent_later->complete = true; check(!draft.publish(2));
  check(sent->patches[0].second == Bytes({71, 72}));
  sent->complete = true; done(draft.publish(2));
  auto latest = get(part.open(2, all)), late_old = get(part.open(1, all));
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    check(late_old.data()[i] == bytes[i]);
    check(latest.data()[i] == (i == 9 ? 83 : i == 10 ? 72 : bytes[i]));
  }
  part.evict(); check(late_old.data()[9] == bytes[9]); check(latest.data()[9] == 83);
}
void abort_and_sparse() {
  Part part(original()); auto cached = get(part.open(1, first, Method::copy));
  check(part.counts().mapped == page_size());
  check(traps(cached.data() + page_size(), false)); cached.close();
  auto draft = get(part.begin(Method::take, first));
  auto writer = get(draft.access(first)); writer.writable()[0] = 241; writer.close();
  const std::vector<Range> r{{0, 1}}, absent{{page_size(), 1}};
  check(!draft.stage(absent)); auto pending = get(draft.stage(r));
  auto during = get(part.open(1, first)); check(during.data()[0] == original()[0]);
  done(draft.abort()); check(pending->patches[0].second[0] == 241);
  auto restored = get(part.open(1, first)); check(restored.data()[0] == original()[0]);
  check(!part.open(1, std::vector<std::size_t>{3}));
  check(!part.begin(Method::copy, std::vector<std::size_t>{3}));
  // A vanished draft leaves its outstanding user alive, but cannot publish it.
  auto abandoned = get(part.begin(Method::cow, first)); auto pinned = get(abandoned.access(first));
  { auto last_owner = std::move(abandoned); }
  pinned.writable()[0] = 212; check(get(part.open(1, first)).data()[0] == original()[0]);
}
void masks() {
  const auto bytes = original(); Part part(bytes);
  auto input = get(part.open(1, all));
  const std::vector<Range> read{{11, 3}, {page_size(), page_size()}}, write{{30, 2}};
  Worker worker(input, read, write);
  check(worker.copied() == 3);
  for (std::size_t i = 0; i < page_size(); ++i) check(worker.data()[i] == (i >= 11 && i < 14 ? bytes[i] : 0));
  check(traps(worker.data() + page_size(), true));
  check(traps(worker.data() + 2 * page_size(), false));
  std::memset(worker.writable(), 222, page_size());
  auto draft = get(part.begin(Method::cow, first)); auto destination = get(draft.access(first));
  done(worker.apply(destination)); destination.close();
  auto staged = get(draft.stage(write)); staged->complete = true; done(draft.publish(2));
  auto latest = get(part.open(2, all));
  for (std::size_t i = 0; i < bytes.size(); ++i) check(latest.data()[i] == (i == 30 || i == 31 ? 222 : bytes[i]));
  // Read and write masks need not match: write-only input starts at zero.
  const std::vector<Range> none; Worker output(input, none, write);
  check(output.data()[30] == 0 && output.data()[31] == 0);
  auto changed_draft = get(part.begin(Method::cow, first)); auto changed = get(changed_draft.access(first));
  check(worker.apply(changed).error() == Error::version); changed.close(); done(changed_draft.abort());
}
void stale_worker() {
  Part part(original()); const std::vector<Range> r{{0, 1}};
  auto draft = get(part.begin(Method::cow, first)); auto source = get(draft.access(first));
  Worker worker(source, r, r); worker.writable()[0] = 242; source.close(); done(draft.abort());
  auto retry = get(part.begin(Method::cow, first)); auto destination = get(retry.access(first));
  check(worker.apply(destination).error() == Error::version);
  check(destination.data()[0] == original()[0]); destination.close(); done(retry.abort());
  // A worker from the current draft may return once the caller stops other stores.
  auto current = get(part.begin(Method::cow, first)); auto native = get(current.access(first));
  Worker valid(native, r, r); valid.writable()[0] = 221;
  done(valid.apply(native)); check(native.data()[0] == 221); native.close(); done(current.abort());
}
void promotion() {
  Part part(original()); { auto warm = get(part.open(1, first, Method::copy)); }
  auto draft = get(part.begin(Method::take, first)); auto native = get(draft.access(first));
  const auto* address = native.data(); native.writable()[33] = 141; native.close();
  check(draft.publish(2).error() == Error::unstaged);
  auto delivery = get(draft.stage(std::vector<Range>{{33, 1}})); delivery->complete = true;
  done(draft.publish(2)); auto latest = get(part.open(2, first));
  check(latest.data() == address && latest.data()[33] == 141); latest.close();
  auto next = get(part.begin(Method::take, first)); auto taken = get(next.access(first));
  check(taken.data() == address && taken.data()[33] == 141); taken.close(); done(next.abort());
}
void histories() {
  const auto base = original(1); Part part(base); Bytes expected = base;
  std::uint32_t random = 1537;
  for (Epoch epoch = 2; epoch <= 121; ++epoch) {
    { auto warm = get(part.open(part.latest(), first, Method::copy)); }
    const auto method = static_cast<Method>(epoch % 3);
    auto draft = get(part.begin(method, first)); auto native = get(draft.access(first));
    random = random * 1664525u + 1013904223u;
    const auto offset = random % page_size(); const auto value = static_cast<std::uint8_t>(random >> 24);
    native.writable()[offset] = value; native.close();
    if (epoch % 7 == 0) done(draft.abort());
    else {
      auto staged = get(draft.stage(std::vector<Range>{{offset, 1}})); staged->complete = true;
      done(draft.publish(epoch)); expected[offset] = value;
    }
    auto observed = get(part.open(part.latest(), first));
    check(std::equal(expected.begin(), expected.end(), observed.data()));
  }
}
}
int main() {
  versions(); abort_and_sparse(); masks(); stale_worker(); promotion(); histories();
  for (auto m : {Method::copy, Method::cow, Method::take}) for (bool h : {false, true}) methods(m, h);
  std::printf("{\"result\":\"pass\",\"checks\":%zu,\"histories\":120,\"page_bytes\":%zu}\n", checks, page_size());
}
