#include "embedded.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fcntl.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace embedded;
std::size_t checks{}, histories{};
void check(bool ok, int line) {
  ++checks; if (!ok) { std::fprintf(stderr, "embedded check failed at line %d\n", line); std::abort(); }
}
#define CHECK(x) check(static_cast<bool>(x), __LINE__)
template<class T> T get(Result<T> r) { CHECK(r); return std::move(*r); }
template<class T> T get(views::Result<T> r) { CHECK(r); return std::move(*r); }
void done(Result<void> r) { CHECK(r); }
void done(views::Result<void> r) { CHECK(r); }
const std::vector<std::size_t> first{0};
Bytes initial(std::uint8_t seed, std::size_t n = 1) {
  Bytes b(n * views::page_size());
  for (std::size_t i = 0; i < b.size(); ++i) b[i] = static_cast<std::uint8_t>((i * 37 + seed) % 251);
  return b;
}
void fill(views::Draft& draft, const Bytes& bytes) {
  done(draft.grow(bytes.size()));
  std::vector<std::size_t> pages((bytes.size() + views::page_size() - 1) / views::page_size());
  for (std::size_t i = 0; i < pages.size(); ++i) pages[i] = i;
  auto writer = get(draft.access(pages)); std::memcpy(writer.writable(), bytes.data(), bytes.size());
}
std::shared_ptr<Ticket> stage_all(Mutation& mutation, Id part, std::size_t size) {
  const std::vector<views::Range> ranges{{0, size}}; return get(mutation.stage(part, ranges));
}
using Complete = std::function<void(const std::shared_ptr<Ticket>&)>;
void settle(Mutation& mutation, Complete complete) {
  done(mutation.prepare()); CHECK(!mutation.commit()); auto ticket = mutation.decision(); CHECK(ticket);
  complete(ticket); done(mutation.commit());
}
std::unique_ptr<Mutation> start(History& history, Complete complete, Id object, Kind kind) {
  auto mutation = get(history.intend(object, kind)); CHECK(!mutation->start());
  complete(mutation->intent()); done(mutation->start()); return mutation;
}
void same(History& history, Id object, Epoch epoch, Id part, const Bytes& expected) {
  std::vector<std::size_t> pages((expected.size() + views::page_size() - 1) / views::page_size());
  for (std::size_t i = 0; i < pages.size(); ++i) pages[i] = i;
  auto held = get(history.open(object, epoch, part, pages)); CHECK(held.size() == expected.size());
  CHECK(std::memcmp(held.data(), expected.data(), expected.size()) == 0);
}

class MemoryPort final : public Port {
 public:
  Bytes storage;
  std::vector<std::shared_ptr<Ticket>> pending;
  std::shared_ptr<Ticket> append(Bytes bytes) override {
    auto ticket = std::make_shared<Ticket>(); ticket->bytes = std::move(bytes); pending.push_back(ticket); return ticket;
  }
  Bytes read() override { return storage; }
  void truncate(std::size_t n) override { storage.resize(n); }
  void complete(const std::shared_ptr<Ticket>& ticket) {
    CHECK(ticket->status == Ticket::Status::pending);
    storage.insert(storage.end(), ticket->bytes.begin(), ticket->bytes.end()); ticket->status = Ticket::Status::complete;
  }
};

// The same caller exercises file and memory ports. Expected bytes are maintained
// independently of staged patches and recovery records.
void lifecycle(const std::shared_ptr<Port>& port, Complete complete) {
  ++histories; const auto page = views::page_size(); History history(port, 8 * page);
  auto first_bytes = initial(17), second_bytes = initial(88);
  auto create = start(history, complete, 42, Kind::create);
  auto first_draft = get(create->edit(1, views::Method::cow, {})); fill(*first_draft, first_bytes);
  auto s1 = stage_all(*create, 1, page);
  auto second_draft = get(create->add(2)); fill(*second_draft, second_bytes); auto s2 = stage_all(*create, 2, page);
  CHECK(!create->prepare()); CHECK(history.frontier() == 0); CHECK(!history.layout(42, 0));
  complete(s2); CHECK(!create->prepare()); complete(s1); done(create->prepare());
  CHECK(!create->commit()); const auto e1 = create->epoch(); CHECK(!history.layout(42, e1));
  complete(create->decision()); CHECK(history.frontier() == 0); done(create->commit()); create.reset();
  CHECK(get(history.layout(42, e1)) == std::vector<Id>({1, 2})); same(history, 42, e1, 1, first_bytes);
  auto old_first = get(history.open(42, e1, 1, first)); auto old_second = get(history.open(42, e1, 2, first));
  CHECK(!history.intend(42, Kind::create));

  auto update = start(history, complete, 42, Kind::update);
  auto d1 = get(update->edit(1, views::Method::cow, first)); auto d2 = get(update->edit(2, views::Method::copy, first));
  auto w1 = get(d1->access(first)), w2 = get(d2->access(first));
  w1.writable()[9] = 31; w2.writable()[10] = 99; w1.close(); w2.close();
  const std::vector<views::Range> r1{{9, 1}}, r2{{10, 1}};
  auto earlier = get(update->stage(1, r1)); auto other = get(update->stage(2, r2));
  auto resumed = get(d1->access(first)); resumed.writable()[9] = 73; resumed.close();
  auto later = get(update->stage(1, r1));
  complete(later); complete(other); CHECK(!update->prepare()); complete(earlier); done(update->prepare());
  CHECK(!update->commit()); CHECK(history.frontier() == e1);
  same(history, 42, e1, 1, first_bytes); same(history, 42, e1, 2, second_bytes);
  const auto e2 = update->epoch(); complete(update->decision()); done(update->commit()); update.reset();
  first_bytes[9] = 73; second_bytes[10] = 99;
  same(history, 42, e2, 1, first_bytes); same(history, 42, e2, 2, second_bytes);
  CHECK(old_first.data()[9] != 73); CHECK(old_second.data()[10] != 99);

  // A representation layout changes while its surviving part's source revision does not.
  auto layout = start(history, complete, 42, Kind::update); done(layout->remove(2));
  const auto e3 = layout->epoch(); settle(*layout, complete); layout.reset();
  CHECK(get(history.layout(42, e3)) == std::vector<Id>({1})); CHECK(get(history.source(42, e3, 1)) == e2);
  CHECK(!history.open(42, e3, 2, first)); same(history, 42, e3, 1, first_bytes);
  same(history, 42, e2, 2, second_bytes);

  auto unrelated = start(history, complete, 99, Kind::create); auto other_bytes = initial(134);
  fill(*get(unrelated->edit(1, views::Method::cow, {})), other_bytes);
  auto other_stage = stage_all(*unrelated, 1, page); complete(other_stage); const auto unrelated_epoch = unrelated->epoch();
  settle(*unrelated, complete); unrelated.reset();
  CHECK(get(history.source(42, unrelated_epoch, 1)) == e2); same(history, 42, unrelated_epoch, 1, first_bytes);
  same(history, 99, unrelated_epoch, 1, other_bytes);
  const auto at_unrelated_epoch = first_bytes;

  auto grow = start(history, complete, 42, Kind::update);
  auto growing = get(grow->edit(1, views::Method::cow, first)); auto before = get(growing->access(first));
  auto* address = before.writable(); done(growing->grow(3 * page)); CHECK(before.writable() == address);
  const std::vector<std::size_t> tail{2}; auto tail_writer = get(growing->access(tail)); tail_writer.writable()[2 * page + 7] = 111;
  before.close(); const std::vector<views::Range> unchanged_first{{0, page}};
  auto while_disjoint_writer = get(grow->stage(1, unchanged_first));
  tail_writer.close(); const std::vector<views::Range> changed_tail{{2 * page + 7, 1}};
  auto tail_stage = get(grow->stage(1, changed_tail)); complete(tail_stage); complete(while_disjoint_writer);
  const auto e4 = grow->epoch(); settle(*grow, complete); grow.reset();
  first_bytes.resize(3 * page); first_bytes[2 * page + 7] = 111; same(history, 42, e4, 1, first_bytes);
  CHECK(old_first.size() == page);

  auto erase = start(history, complete, 42, Kind::erase); const auto deleted = erase->epoch();
  settle(*erase, complete); erase.reset(); CHECK(!history.layout(42, deleted));
  CHECK(old_first.data()[9] != 73); same(history, 42, e4, 1, first_bytes);
  auto recreate = start(history, complete, 42, Kind::create); auto next = initial(203);
  fill(*get(recreate->edit(1, views::Method::cow, {})), next); auto replaced = stage_all(*recreate, 1, page); complete(replaced);
  const auto e5 = recreate->epoch(); settle(*recreate, complete); recreate.reset(); same(history, 42, e5, 1, next);
  CHECK(old_first.data()[9] != next[9]); CHECK(!history.layout(42, deleted));
  History reopened(port, 8 * page); CHECK(reopened.frontier() == e5);
  same(reopened, 42, e4, 1, first_bytes); same(reopened, 42, e2, 2, second_bytes);
  same(reopened, 42, e5, 1, next); CHECK(!reopened.layout(42, deleted));
  same(reopened, 42, unrelated_epoch, 1, at_unrelated_epoch);
  same(reopened, 99, e5, 1, other_bytes);
}

struct Temp {
  std::string directory, path;
  Temp() {
    char pattern[] = "/tmp/orbital-view-history-XXXXXX"; auto* d = ::mkdtemp(pattern); CHECK(d);
    directory = d; path = directory + "/journal";
  }
  ~Temp() { ::unlink(path.c_str()); ::rmdir(directory.c_str()); }
};
Epoch seed(const std::string& path) {
  auto port = std::make_shared<FilePort>(path); History history(port, 8 * views::page_size());
  auto complete = [&](auto& t) { port->complete(t); };
  auto mutation = start(history, complete, 7, Kind::create); const auto bytes = initial(29);
  fill(*get(mutation->edit(1, views::Method::cow, {})), bytes);
  auto ticket = stage_all(*mutation, 1, bytes.size()); complete(ticket); const auto epoch = mutation->epoch();
  settle(*mutation, complete); return epoch;
}
void restart(bool short_tail) {
  ++histories; Temp temp; const auto e1 = seed(temp.path); const auto child = ::fork(); CHECK(child >= 0);
  if (!child) {
    auto port = std::make_shared<FilePort>(temp.path); History history(port, 8 * views::page_size());
    auto complete = [&](auto& t) { port->complete(t); };
    auto mutation = start(history, complete, 7, Kind::update);
    auto draft = get(mutation->edit(1, views::Method::cow, first)); auto writer = get(draft->access(first));
    writer.writable()[55] = 181; writer.close(); const std::vector<views::Range> changed{{55, 1}};
    auto stage = get(mutation->stage(1, changed)); complete(stage); done(mutation->prepare()); CHECK(!mutation->commit());
    if (short_tail) port->short_write(mutation->decision(), mutation->decision()->bytes.size() - 5);
    else port->complete(mutation->decision());
    CHECK(history.frontier() == e1); ::_exit(0);
  }
  int status{}; CHECK(::waitpid(child, &status, 0) == child); CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  auto port = std::make_shared<FilePort>(temp.path); History reopened(port, 8 * views::page_size());
  auto expected = initial(29); if (!short_tail) expected[55] = 181;
  CHECK(reopened.frontier() == e1 + (short_tail ? 0 : 1)); same(reopened, 7, reopened.frontier(), 1, expected);
  const auto complete = [&](auto& t) { port->complete(t); };
  auto again = start(reopened, complete, 7, Kind::update); const auto epoch = again->epoch();
  settle(*again, complete); CHECK(reopened.frontier() == epoch);
  History next(port, 8 * views::page_size()); same(next, 7, epoch, 1, expected);
}
void ambiguous() {
  ++histories; Temp temp; const auto e1 = seed(temp.path); auto original = initial(29); auto expected = original;
  Epoch e2;
  {
    auto port = std::make_shared<FilePort>(temp.path); History history(port, 8 * views::page_size());
    auto complete = [&](auto& t) { port->complete(t); }; auto old = get(history.open(7, e1, 1, first));
    auto mutation = start(history, complete, 7, Kind::update); e2 = mutation->epoch();
    auto draft = get(mutation->edit(1, views::Method::cow, first)); auto writer = get(draft->access(first));
    writer.writable()[8] = 144; writer.close(); const std::vector<views::Range> range{{8, 1}};
    auto ticket = get(mutation->stage(1, range)); complete(ticket); done(mutation->prepare()); CHECK(!mutation->commit());
    port->lose_ack(mutation->decision()); CHECK(!mutation->commit()); CHECK(history.needs_recovery());
    CHECK(history.frontier() == e1); CHECK(old.data()[8] == original[8]); same(history, 7, e1, 1, original);
    mutation.reset(); CHECK(!history.intend(7, Kind::update));
  }
  expected[8] = 144; auto port = std::make_shared<FilePort>(temp.path); History recovered(port, 8 * views::page_size());
  CHECK(recovered.frontier() == e2); same(recovered, 7, e2, 1, expected);
}
void abandoned_decision() {
  ++histories; Temp temp; const auto e1 = seed(temp.path); auto original = initial(29); auto expected = original;
  auto port = std::make_shared<FilePort>(temp.path); History history(port, 8 * views::page_size());
  auto complete = [&](auto& t) { port->complete(t); };
  auto mutation = start(history, complete, 7, Kind::update); const auto e2 = mutation->epoch();
  auto draft = get(mutation->edit(1, views::Method::cow, first)); auto writer = get(draft->access(first));
  writer.writable()[74] = 201; writer.close(); const std::vector<views::Range> range{{74, 1}};
  auto staged = get(mutation->stage(1, range)); complete(staged); done(mutation->prepare()); CHECK(!mutation->commit());
  auto outstanding = mutation->decision(); mutation.reset();
  CHECK(history.needs_recovery()); CHECK(!history.intend(7, Kind::update));
  complete(outstanding); CHECK(history.frontier() == e1); same(history, 7, e1, 1, original);
  History recovered(port, 8 * views::page_size()); expected[74] = 201;
  CHECK(recovered.frontier() == e2); same(recovered, 7, e2, 1, expected);
}
void physical_hold() {
  ++histories; const auto page = views::page_size(); auto pool = std::make_shared<recycling::Pool>();
  auto storage = std::make_shared<MemoryPort>(); std::optional<views::Lease> held;
  auto bytes = initial(52); const std::uint8_t* address{};
  {
    History history(storage, 4 * page, pool); auto complete = [&](auto& t) { storage->complete(t); };
    auto mutation = start(history, complete, 8, Kind::create);
    fill(*get(mutation->edit(1, views::Method::cow, {})), bytes);
    auto staged = stage_all(*mutation, 1, page); complete(staged); const auto epoch = mutation->epoch();
    settle(*mutation, complete); mutation.reset(); held.emplace(get(history.open(8, epoch, 1, first)));
    address = held->data(); auto erase = start(history, complete, 8, Kind::erase); settle(*erase, complete);
  }
  CHECK(pool->counts().held == 1); CHECK(std::memcmp(held->data(), bytes.data(), page) == 0);
  {
    auto other_storage = std::make_shared<MemoryPort>(); History other(other_storage, 4 * page, pool);
    auto complete = [&](auto& t) { other_storage->complete(t); };
    auto create = start(other, complete, 19, Kind::create);
    fill(*get(create->edit(1, views::Method::cow, {})), initial(199));
    auto staged = stage_all(*create, 1, page); complete(staged); const auto epoch = create->epoch();
    settle(*create, complete); create.reset(); auto newer = get(other.open(19, epoch, 1, first));
    CHECK(newer.data() != address); CHECK(std::memcmp(held->data(), bytes.data(), page) == 0);
  }
  held->release({recycling::Temperature::warm, 0, std::chrono::steady_clock::now()}); held.reset();
  CHECK(pool->counts().held == 0); const auto before = pool->counts();
  {
    auto fresh_storage = std::make_shared<MemoryPort>(); History fresh(fresh_storage, 4 * page, pool);
    auto complete = [&](auto& t) { fresh_storage->complete(t); };
    auto create = start(fresh, complete, 21, Kind::create);
    fill(*get(create->edit(1, views::Method::cow, {})), initial(73));
    auto staged = stage_all(*create, 1, page); complete(staged); settle(*create, complete);
    CHECK(pool->counts().reused > before.reused); CHECK(pool->counts().maps == before.maps);
  }
  CHECK(pool->counts().held == 0);
}
void byte_extents() {
  ++histories; Temp temp; auto port = std::make_shared<FilePort>(temp.path);
  auto pool = std::make_shared<recycling::Pool>(); History history(port, views::page_size(), pool);
  auto complete = [&](auto& t) { port->complete(t); }; Bytes short_bytes(47);
  for (std::size_t i = 0; i < short_bytes.size(); ++i) short_bytes[i] = static_cast<std::uint8_t>(i + 1);
  auto create = start(history, complete, 3, Kind::create);
  fill(*get(create->edit(1, views::Method::cow, {})), short_bytes);
  auto staged = stage_all(*create, 1, short_bytes.size()); complete(staged); const auto e1 = create->epoch();
  settle(*create, complete); create.reset(); auto old = get(history.open(3, e1, 1, first));
  CHECK(old.size() == 47); CHECK(old.data()[47] == 0); CHECK(old.data()[views::page_size() - 1] == 0);
  auto growth = start(history, complete, 3, Kind::update); auto draft = get(growth->edit(1, views::Method::cow, first));
  Bytes larger(5003); std::copy(short_bytes.begin(), short_bytes.end(), larger.begin()); larger[5002] = 241;
  fill(*draft, larger); auto captured = stage_all(*growth, 1, larger.size()); complete(captured); const auto e2 = growth->epoch();
  settle(*growth, complete); growth.reset(); same(history, 3, e2, 1, larger);
  const std::vector<std::size_t> both{0, 1}; auto current = get(history.open(3, e2, 1, both));
  CHECK(current.size() == 5003); CHECK(current.data()[5003] == 0); CHECK(current.data()[8191] == 0);
  CHECK(old.size() == 47); CHECK(std::memcmp(old.data(), short_bytes.data(), 47) == 0);
  History reopened(port, views::page_size(), pool); same(reopened, 3, e1, 1, short_bytes); same(reopened, 3, e2, 1, larger);
}
void preflight() {
  ++histories; auto port = std::make_shared<MemoryPort>(); History history(port, 4 * views::page_size());
  auto complete = [&](auto& t) { port->complete(t); };
  {
    auto mutation = start(history, complete, 2, Kind::create);
    auto draft = get(mutation->edit(1, views::Method::cow, {})); fill(*draft, initial(1));
    auto s = stage_all(*mutation, 1, views::page_size()); complete(s);
    auto unstaged = get(mutation->add(2)); fill(*unstaged, initial(7)); CHECK(!mutation->prepare());
    CHECK(!mutation->prepare()); CHECK(!mutation->edit(1, views::Method::cow, first));
    CHECK(!mutation->decision()); CHECK(history.frontier() == 0);
  }
  CHECK(!history.layout(2, 0));
  auto retry = start(history, complete, 2, Kind::create); auto draft = get(retry->edit(1, views::Method::cow, {}));
  fill(*draft, initial(11)); auto s = stage_all(*retry, 1, views::page_size()); complete(s); settle(*retry, complete);
  History recovered(port, 4 * views::page_size()); same(recovered, 2, retry->epoch(), 1, initial(11));
}
void corruption() {
  ++histories; Temp temp; seed(temp.path); const int fd = ::open(temp.path.c_str(), O_RDWR | O_CLOEXEC); CHECK(fd >= 0);
  const std::uint8_t changed = 247; CHECK(::pwrite(fd, &changed, 1, 20) == 1); CHECK(::fdatasync(fd) == 0); ::close(fd);
  bool rejected{}; try { History history(std::make_shared<FilePort>(temp.path), 8 * views::page_size()); }
  catch (const std::runtime_error&) { rejected = true; } CHECK(rejected);
}
}
int main() {
  {
    auto memory = std::make_shared<MemoryPort>(); lifecycle(memory, [&](auto& t) { memory->complete(t); });
  }
  {
    Temp temp; auto file = std::make_shared<FilePort>(temp.path); lifecycle(file, [&](auto& t) { file->complete(t); });
  }
  restart(false); restart(true); ambiguous(); abandoned_decision(); physical_hold(); byte_extents(); preflight(); corruption();
  std::printf("{\"result\":\"pass\",\"histories\":%zu,\"checks\":%zu,\"page_bytes\":%zu}\n",
      histories, checks, views::page_size());
}
