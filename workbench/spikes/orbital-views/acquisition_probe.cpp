#include "acquisition.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <semaphore>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace views;
using namespace acquisition;
namespace {
std::size_t checks{};
void check(bool ok, const char* reason) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "failed: %s\n", reason); std::abort(); }
}
template<class T> T get(Result<T> result) {
  check(result.has_value(), "operation succeeds");
  return std::move(*result);
}
void done(Result<void> result) { check(result.has_value(), "operation succeeds"); }
Bytes seed() {
  Bytes bytes(3 * views::page_size());
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>((i * 29 + 53) % 251);
  return bytes;
}
const std::vector<std::size_t> selected{0, 2};
bool traps(const std::uint8_t* address) {
  const auto pid = ::fork(); check(pid >= 0, "fork succeeds");
  if (!pid) {
    const rlimit limit{0, 0}; ::setrlimit(RLIMIT_CORE, &limit); ::alarm(2);
    const auto value = *static_cast<const volatile std::uint8_t*>(address); (void)value;
    ::_exit(0);
  }
  int status{}; check(::waitpid(pid, &status, 0) == pid, "child completes");
  return WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS);
}
void matches(const Lease& lease, const Bytes& expected) {
  check(lease.size() == expected.size(), "whole-part address extent preserved");
  for (auto page : selected) {
    const auto offset = page * views::page_size();
    check(std::equal(expected.begin() + offset, expected.begin() + offset + views::page_size(),
                     lease.data() + offset), "selected source page matches independent bytes");
  }
  check(traps(lease.data() + views::page_size()), "unrequested page stays inaccessible");
}
void change(Part& part, Epoch epoch, Bytes& expected) {
  auto draft = get(part.begin(Method::cow, selected));
  auto writer = get(draft.access(selected));
  writer.writable()[17] = 192;
  writer.writable()[2 * views::page_size() + 31] = 193;
  writer.close();
  const std::vector<Range> damage{{17, 1}, {2 * views::page_size() + 31, 1}};
  auto staged = get(draft.stage(damage)); staged->complete = true;
  done(draft.publish(epoch));
  expected[17] = 192; expected[2 * views::page_size() + 31] = 193;
}
struct Ticket { std::shared_ptr<Fetch> fetch; Completion complete; };
class QueuedPort final : public Port {
 public:
  void read(std::shared_ptr<Fetch> fetch, Completion complete) override {
    tickets.push_back({std::move(fetch), std::move(complete)});
  }
  Ticket take(std::size_t index) { return std::move(tickets.at(index)); }
  void finish(std::size_t index, bool mapping) {
    auto ticket = take(index);
    ticket.complete(mapping ? read_mapping(*ticket.fetch) : read_file(*ticket.fetch));
  }
  std::vector<Ticket> tickets;
};
void substitution() {
  const auto expected = seed();
  FilePort file; MappingPort mapping;
  for (Port* port : std::vector<Port*>{&file, &mapping}) {
    Part part(expected);
    Acquisition acquisition;
    done(acquisition.start(part, 1, selected, *port));
    check(acquisition.status() == Status::ready, "either port prepares the same view contract");
    auto lease = get(acquisition.lease()); matches(lease, expected);
    acquisition.cancel();
    check(!acquisition.lease(), "closing acquisition prevents new leases");
    matches(lease, expected);
  }
}
void failure_and_readiness() {
  Part part(seed()); QueuedPort port; Acquisition acquisition;
  done(acquisition.start(part, 1, selected, port));
  check(acquisition.status() == Status::pending, "request remains pending before completion");
  check(acquisition.lease().error() == Error::pending, "no native lease before completion");
  {
    auto ticket = port.take(0);
    auto first = ticket.fetch->destination(0);
    check(::pread(ticket.fetch->source_fd(), first.data(), first.size(), 0) == static_cast<ssize_t>(first.size()),
          "first page actually transferred");
    auto last = ticket.fetch->destination(2);
    const auto half = last.size() / 2;
    check(::pread(ticket.fetch->source_fd(), last.data(), half, static_cast<off_t>(2 * views::page_size())) ==
          static_cast<ssize_t>(half), "second page partially transferred");
    check(!acquisition.lease(), "partially populated native pages remain unavailable");
    ticket.complete(false);
  }
  check(acquisition.status() == Status::failed, "short transfer fails the acquisition");
  check(acquisition.lease().error() == Error::unavailable, "short transfer exposes no lease");
  done(acquisition.start(part, 1, selected, port));
  { auto ticket = port.take(1); ticket.complete(false); }
  check(acquisition.status() == Status::failed && !acquisition.lease(), "source failure exposes no lease");
  done(acquisition.start(part, 1, selected, port)); port.finish(2, false);
  matches(get(acquisition.lease()), seed());
}
void replaced_request() {
  const auto original = seed(); auto expected = original;
  Part part(original); QueuedPort port; Acquisition acquisition;
  done(acquisition.start(part, 1, selected, port));
  change(part, 2, expected);
  done(acquisition.start(part, 2, selected, port));
  {
    auto old = port.take(0);
    check(read_mapping(*old.fetch), "late old source transfer succeeds");
    check(old.fetch->destination(0)[17] == original[17], "old request captured original revision");
    old.complete(true);
  }
  check(acquisition.status() == Status::pending && !acquisition.lease(),
        "old completion cannot install into replacement request");
  port.finish(1, false); matches(get(acquisition.lease()), expected);
  auto latest = get(part.open(2, selected)); matches(latest, expected);
}
void captured_old_revision() {
  const auto original = seed(); auto expected = original;
  Part part(original);
  auto captured = get(part.fetch(1, selected));
  change(part, 2, expected);
  check(read_file(*captured), "captured old material remains readable after publication");
  auto old = get(captured->finish(true)); matches(old, original);
  check(part.can_take(), "old fetch lease does not replace or hold the latest cache head");
  auto latest = get(part.open(2, selected)); matches(latest, expected);
}
void cancelled_backend(bool destroy_owner) {
  auto pool = std::make_shared<recycling::Pool>();
  Part part(seed(), 1, false, 3 * views::page_size(), pool);
  QueuedPort port;
  auto acquisition = std::make_unique<Acquisition>();
  done(acquisition->start(part, 1, selected, port));
  auto ticket = port.take(0);
  std::weak_ptr<Fetch> retained = ticket.fetch;
  auto* destination = ticket.fetch->destination(0).data();
  check(pool->counts().held == 1, "pending backend holds actual pooled allocation");
  std::binary_semaphore entered{0}, resume{0};
  std::thread backend([ticket = std::move(ticket), &entered, &resume]() mutable {
    entered.release(); resume.acquire();
    const bool complete = read_mapping(*ticket.fetch);
    ticket.complete(complete);
    ticket.complete = {}; ticket.fetch.reset();
  });
  entered.acquire();
  acquisition->cancel();
  check(acquisition->status() == Status::cancelled && !acquisition->lease(),
        "cancellation revokes acquisition before backend completion");
  if (destroy_owner) acquisition.reset();
  check(!retained.expired() && pool->counts().held == 1,
        "cancelled or destroyed owner cannot release backend destination");
  {
    auto other = pool->acquire(recycling::Temperature::cold, ::sched_getcpu(), 3 * views::page_size());
    check(reinterpret_cast<std::uint8_t*>(other.data()) != destination,
          "pending target cannot be reassigned to another allocation");
  }
  resume.release(); backend.join();
  check(retained.expired() && pool->counts().held == 0,
        "actual completion ends the backend hold exactly once");
  if (acquisition) check(acquisition->status() == Status::cancelled && !acquisition->lease(),
                         "late successful transfer cannot revive cancelled acquisition");
}
}
int main() {
  substitution(); failure_and_readiness(); replaced_request(); captured_old_revision();
  cancelled_backend(false); cancelled_backend(true);
  std::printf("{\"result\":\"pass\",\"checks\":%zu,\"scenarios\":7,\"page_bytes\":%zu}\n", checks, views::page_size());
}
