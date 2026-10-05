#include "acquisition.h"
#include "embedded.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sched.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
std::size_t checks{};
void check(bool ok, const char* reason) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "failed: %s\n", reason); std::abort(); }
}
template<class T> auto get(T result) {
  check(result.has_value(), "operation succeeds"); return std::move(*result);
}
template<class T> void done(T result) { check(result.has_value(), "operation succeeds"); }
const std::vector<std::size_t> selected{0, 2}, all{0, 1, 2};
using views::Bytes;
using views::Lease;
using embedded::Kind;
std::unique_ptr<embedded::Mutation> begin(embedded::History& history, embedded::FilePort& file, Kind kind) {
  auto mutation = get(history.intend(77, kind));
  check(!mutation->start(), "intent awaits persistence"); file.complete(mutation->intent());
  done(mutation->start()); return mutation;
}
void commit(embedded::Mutation& mutation, embedded::FilePort& file) {
  done(mutation.prepare()); check(!mutation.commit(), "decision awaits persistence");
  file.complete(mutation.decision()); done(mutation.commit());
}
bool traps(const std::uint8_t* address) {
  const auto pid = ::fork(); check(pid >= 0, "fork succeeds");
  if (!pid) {
    const rlimit limit{0, 0}; ::setrlimit(RLIMIT_CORE, &limit); ::alarm(2);
    const auto value = *static_cast<const volatile std::uint8_t*>(address); (void)value; ::_exit(0);
  }
  int status{}; check(::waitpid(pid, &status, 0) == pid, "child completes");
  return WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
}
void matches(const Lease& lease, const Bytes& expected) {
  const auto page = views::page_size();
  check(lease.size() == expected.size(), "exact source extent survives replay and transfer");
  for (auto index : selected) {
    const auto offset = index * page, length = std::min(page, expected.size() - offset);
    check(std::equal(expected.begin() + offset, expected.begin() + offset + length, lease.data() + offset),
          "prepared bytes match independently maintained version");
  }
  for (auto i = expected.size(); i < 3 * page; ++i) check(lease.data()[i] == 0, "partial final-page padding is zero");
  check(traps(lease.data() + page), "unrequested middle page is inaccessible");
}
struct DelayedPort final : acquisition::Port {
  struct Request { std::shared_ptr<views::Fetch> fetch; acquisition::Completion complete; };
  std::vector<Request> requests;
  void read(std::shared_ptr<views::Fetch> fetch, acquisition::Completion complete) override {
    requests.push_back({std::move(fetch), std::move(complete)});
  }
  void settle(std::size_t index, bool mapping) {
    auto request = std::move(requests.at(index));
    request.complete(mapping ? acquisition::read_mapping(*request.fetch) : acquisition::read_file(*request.fetch));
  }
  void short_transfer(std::size_t index) {
    auto request = std::move(requests.at(index));
    auto destination = request.fetch->destination(0);
    check(::pread(request.fetch->source_fd(), destination.data(), destination.size(), 0) ==
          static_cast<ssize_t>(destination.size()), "one selected page really transfers before failure");
    destination = request.fetch->destination(2);
    check(::pread(request.fetch->source_fd(), destination.data(), 23, static_cast<off_t>(2 * views::page_size())) == 23,
          "last selected page transfers only partially");
    request.complete(false);
  }
};
void request(DelayedPort& port, std::shared_ptr<views::Fetch> fetch, std::optional<Lease>& result, bool& failed) {
  port.read(fetch, [fetch, &result, &failed](bool complete) {
    auto ready = fetch->finish(complete);
    if (ready) result.emplace(std::move(*ready)); else failed = true;
  });
}
void history_to_native_bytes() {
  const auto page = views::page_size(); char path[] = "/tmp/orbital-embedded-acquisition-XXXXXX";
  const int fd = ::mkstemp(path); check(fd >= 0, "temporary file created"); ::close(fd);
  auto pool = std::make_shared<recycling::Pool>();
  auto file = std::make_shared<embedded::FilePort>(path);
  Bytes original(2 * page + 47);
  for (std::size_t i = 0; i < original.size(); ++i) original[i] = static_cast<std::uint8_t>((i * 43 + 71) % 251);
  views::Epoch first_epoch{};
  {
    embedded::History history(file, 4 * page, pool);
    auto create = begin(history, *file, Kind::create);
    auto draft = get(create->edit(1, views::Method::cow, {})); done(draft->grow(original.size()));
    auto write = get(draft->access(all)); std::memcpy(write.writable(), original.data(), original.size()); write.close();
    auto staged = get(create->stage(1, std::vector<views::Range>{{0, original.size()}})); file->complete(staged);
    first_epoch = create->epoch(); commit(*create, *file);
  }
  auto history = std::make_unique<embedded::History>(file, 4 * page, pool);
  check(history->frontier() == first_epoch, "actual file replay selects the saved object head");
  DelayedPort port; std::optional<Lease> old, latest, partial; bool old_failed{}, latest_failed{}, partial_failed{};
  auto fetch = get(history->fetch(77, first_epoch, 1, selected));
  auto* old_target = fetch->destination(0).data(); std::weak_ptr<views::Fetch> old_request = fetch;
  request(port, fetch, old, old_failed); fetch.reset();
  auto expected = original;
  auto update = begin(*history, *file, Kind::update);
  auto draft = get(update->edit(1, views::Method::cow, selected)); auto write = get(draft->access(selected));
  write.writable()[17] = 231; write.writable()[2 * page + 13] = 232; write.close();
  auto staged = get(update->stage(1, std::vector<views::Range>{{17, 1}, {2 * page + 13, 1}})); file->complete(staged);
  const auto changed_epoch = update->epoch(); commit(*update, *file); update.reset();
  expected[17] = 231; expected[2 * page + 13] = 232;
  auto layout = begin(*history, *file, Kind::update); done(get(layout->add(2))->grow(1));
  commit(*layout, *file); const auto layout_epoch = layout->epoch(); layout.reset();
  check(get(history->source(77, layout_epoch, 1)) == changed_epoch, "layout epoch preserves selected part source");
  fetch = get(history->fetch(77, layout_epoch, 1, selected));
  auto* latest_target = fetch->destination(0).data(); request(port, fetch, latest, latest_failed); fetch.reset();
  request(port, get(history->fetch(77, first_epoch, 1, selected)), partial, partial_failed);
  check(!old && !latest && !partial, "queued transfers expose no CPU lease"); port.short_transfer(2);
  check(partial_failed && !partial, "partial source cannot expose prepared native bytes");
  auto erase = begin(*history, *file, Kind::erase); commit(*erase, *file);
  check(!history->fetch(77, erase->epoch(), 1, selected), "deleted head cannot open a part"); erase.reset();
  history.reset(); file.reset();
  check(!old_request.expired(), "backend retains captured source and target beyond history destruction");
  { auto other = pool->acquire(recycling::Temperature::hot, ::sched_getcpu(), 4 * page);
    check(reinterpret_cast<std::uint8_t*>(other.data()) != old_target &&
          reinterpret_cast<std::uint8_t*>(other.data()) != latest_target, "pending transfer homes cannot be reassigned"); }
  port.settle(1, false); port.settle(0, true);
  check(!old_failed && !latest_failed && old && latest, "both delayed source adapters complete successfully");
  check(old_request.expired(), "backend capture ends on actual completion"); matches(*old, original); matches(*latest, expected);
  old.reset(); latest.reset(); check(pool->counts().held == 0, "source and CPU borrowers release their actual homes");
  check(::unlink(path) == 0, "temporary file removed");
}
}
int main() {
  history_to_native_bytes();
  std::printf("{\"result\":\"pass\",\"checks\":%zu,\"history\":\"file-replay-to-sparse-delayed-native-view\"}\n", checks);
}
