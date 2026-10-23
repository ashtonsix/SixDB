#include <ikea/tuplepack.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <memory>
#include <span>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// One versioned caller-owned replay law, not an Orbital journal/unit format.
namespace {
namespace tp = ikea::tuplepack;
using Bytes = std::vector<std::uint8_t>;
using U64 = std::uint64_t;
void check(bool ok) { if (!ok) std::abort(); }
template<class T, class E> T get(std::expected<T, E> r) { check(bool(r)); return std::move(*r); }
void put(Bytes& b, U64 n) { for (unsigned i = 0; i < 8; ++i) b.push_back(std::uint8_t(n >> (i * 8))); }
U64 at(std::span<const std::uint8_t> b, std::size_t p) {
  if (p + 8 > b.size()) throw std::runtime_error("truncated owner input");
  U64 n{}; for (unsigned i = 0; i < 8; ++i) n |= U64(b[p + i]) << (i * 8); return n;
}
U64 hash(std::span<const std::uint8_t> b) {
  U64 n = 1469598103934665603ULL; for (auto x : b) { n ^= x; n *= 1099511628211ULL; } return n;
}
enum class Op : U64 { create, update, layout, erase };
struct Command {
  U64 id, source, object = 42, unit = 11, profile = 1;
  Op op; U64 a{}, b{};
};
Bytes encode(const Command& c) {
  Bytes b; for (auto n : {c.id, c.source, c.object, c.unit, c.profile, U64(c.op), c.a, c.b}) put(b, n); return b;
}
Command decode(std::span<const std::uint8_t> b) {
  if (b.size() != 64 || at(b, 16) != 42 || at(b, 24) != 11 || at(b, 32) != 1 || at(b, 40) > 3)
    throw std::runtime_error("unsupported owner/profile/input");
  return {at(b, 0), at(b, 8), at(b, 16), at(b, 24), at(b, 32), Op(at(b, 40)), at(b, 48), at(b, 56)};
}
Bytes frame(const Command& c) {
  auto body = encode(c); Bytes b; put(b, 0x31444d43); put(b, body.size()); put(b, hash(body));
  b.insert(b.end(), body.begin(), body.end()); return b;
}
Bytes read_file(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC); check(fd >= 0);
  Bytes b; std::array<std::uint8_t, 4096> block;
  for (;;) { const auto n = ::read(fd, block.data(), block.size()); if (n < 0 && errno == EINTR) continue;
    check(n >= 0); if (!n) break; b.insert(b.end(), block.begin(), block.begin() + n); }
  ::close(fd); return b;
}
void persist(const std::string& path, std::span<const std::uint8_t> b, bool append = false) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC), 0600); check(fd >= 0);
  std::size_t p{}; while (p < b.size()) { const auto n = ::write(fd, b.data() + p, b.size() - p);
    if (n < 0 && errno == EINTR) continue; check(n > 0); p += std::size_t(n); }
  check(::fdatasync(fd) == 0); ::close(fd);
}
void sync_directory(const std::string& path) {
  const int dir = ::open(std::filesystem::path(path).parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  check(dir >= 0 && ::fsync(dir) == 0); ::close(dir);
}
struct Unit { U64 id, source; unsigned style; Bytes bytes; };
auto format() {
  const std::array<tp::code, 1> codes{{{0, 1, 3}}}; return get(tp::layout::make(1, codes));
}
unsigned value(const Unit& unit) {
  const std::array<std::uint8_t, 1> ranks{0}; const auto f = format();
  const auto reader = get(tp::read_plan<8>::make(f, ranks));
  auto view = get(tp::const_view::attach(f, std::as_bytes(std::span{unit.bytes}), 2, 3, unit.style ? 11 : 17));
  return unsigned(get(get(tp::bind_reader(reader, view)).get(0)));
}
std::shared_ptr<const Unit> unit(U64 id, U64 source, unsigned style, unsigned x) {
  auto u = std::make_shared<Unit>(Unit{id, source, style, Bytes(31, 0xa1)});
  const std::array<std::uint8_t, 1> ranks{0}; const auto f = format();
  const auto writer = get(tp::write_plan<8>::make(f, ranks));
  auto view = get(tp::view::attach(f, std::as_writable_bytes(std::span{u->bytes}), 2, 3, style ? 11 : 17));
  std::array<ikea::journal::entry, 1> slots; ikea::journal issued{slots};
  check(bool(get(tp::bind_writer(writer, view)).set(0, x, issued)) && issued.used == 1);
  check(issued.entries()[0].source == &view);
  const auto offset = style ? 11U : 17U;
  for (unsigned i = 0; i < u->bytes.size(); ++i)
    check((u->bytes[i] & (i == offset ? ~14U : ~0U)) == (0xa1U & (i == offset ? ~14U : ~0U)));
  return u;
}
struct Head {
  U64 epoch{}; bool present{}, no_effect{}; std::array<std::shared_ptr<const Unit>, 2> parts;
  U64 sum{}, count{};
};
void verify(const Head& h, U64 cut) {
  check(h.epoch == cut);
  // Independent scalar oracle for the fixed authored sequence, not replay code.
  if (!cut || cut >= 4) { check(!h.present && h.count == 0 && h.sum == 0 && !h.parts[0] && !h.parts[1]); return; }
  check(h.present && h.count == 2 && h.parts[0]->id == 11 && h.parts[1]->id == 22);
  const unsigned a = cut == 1 ? 2 : 6, b = cut == 1 ? 3 : 5;
  check(value(*h.parts[0]) == a && value(*h.parts[1]) == b && h.sum == a + b);
  check(h.parts[0]->source == (cut == 3 ? 3 : cut) && h.parts[1]->source == (cut == 1 ? 1 : 2));
}
struct Seed {
  U64 cut{}, floor{}; std::shared_ptr<const Head> head = std::make_shared<Head>();
  std::shared_ptr<const Head> retained;
};
Bytes checkpoint(const Seed& s) {
  Bytes b; put(b, 1); put(b, s.cut); put(b, s.floor); put(b, s.head->present);
  for (const auto& p : s.head->parts) { put(b, p ? value(*p) : 0); put(b, p ? p->source : 0); put(b, p ? p->style : 0); }
  put(b, s.head->sum); put(b, s.head->count); put(b, hash(b));
  if (s.retained) { auto old = checkpoint({s.retained->epoch, 0, s.retained, {}}); b.insert(b.end(), old.begin(), old.end()); }
  return b;
}
Seed restore(std::span<const std::uint8_t> b) {
  if (b.size() == 208) { auto s = restore(b.first(104)); s.retained = restore(b.subspan(104)).head; return s; }
  if (b.size() != 104 || at(b, 0) != 1 || hash(b.first(96)) != at(b, 96)) throw std::runtime_error("invalid owner checkpoint");
  auto h = std::make_shared<Head>(); h->epoch = at(b, 8); h->present = at(b, 24);
  if (h->present) for (unsigned i = 0; i < 2; ++i)
    h->parts[i] = unit(i ? 22 : 11, at(b, 40 + i * 24), unsigned(at(b, 48 + i * 24)), unsigned(at(b, 32 + i * 24)));
  h->sum = at(b, 80); h->count = at(b, 88); verify(*h, h->epoch); return {h->epoch, at(b, 16), h, {}};
}
struct Record { U64 epoch; Command command; };
// Both backends feed the same caller and consumer. Only FileLog promises restart durability.
class Log {
 public:
  Seed seed; std::vector<Record> records;
  virtual ~Log() = default;
  virtual void durable(std::span<const std::uint8_t>) = 0;
  U64 latest() const { return seed.cut + records.size(); }
  U64 admit(const Command& c) {
    auto bytes = encode(c); (void)decode(bytes);
    if (c.id <= seed.floor) throw std::runtime_error("retired identity: do not execute again; prior result not retained");
    for (const auto& r : records) if (r.command.id == c.id) {
      if (encode(r.command) != bytes) throw std::runtime_error("conflicting retry identity"); return r.epoch;
    }
    if (c.id != seed.floor + records.size() + 1 || c.source != latest()) throw std::runtime_error("wrong input cut/stream order");
    durable(frame(c)); records.push_back({latest() + 1, c}); return latest();
  }
};
class MemoryLog final : public Log { void durable(std::span<const std::uint8_t>) override {} };
class FileLog final : public Log {
  std::string base_, active_;
  void durable(std::span<const std::uint8_t> b) override { persist(active_, b, true); }
 public:
  std::size_t scanned{};
  explicit FileLog(std::string base) : base_(std::move(base)), active_(base_) {
    if (std::filesystem::exists(base_ + ".root")) {
      auto root = read_file(base_ + ".root"); check(root == Bytes{1});
      seed = restore(read_file(base_ + ".checkpoint")); active_ = base_ + ".tail";
    }
    if (!std::filesystem::exists(active_)) { persist(active_, {}); sync_directory(active_); }
    auto b = read_file(active_); std::size_t p{};
    while (b.size() - p >= 24) {
      if (at(b, p) != 0x31444d43 || at(b, p + 8) != 64) throw std::runtime_error("invalid complete frame metadata");
      if (b.size() - p < 88) break;
      auto body = std::span(b).subspan(p + 24, 64);
      if (hash(body) != at(b, p + 16)) throw std::runtime_error("invalid complete frame input");
      auto c = decode(body); if (c.source != latest() || c.id != seed.floor + records.size() + 1) throw std::runtime_error("invalid stream cut");
      records.push_back({latest() + 1, c}); p += 88; ++scanned;
    }
    if (p != b.size()) persist(active_, std::span(b).first(p));
  }
  void partial(const Command& c) { auto b = frame(c); persist(active_, std::span(b).first(39), true); }
  void save(const Seed& s, bool adopt) {
    check(s.cut == latest() && s.floor == latest() && s.head->epoch == s.cut && !std::filesystem::exists(base_ + ".root"));
    persist(base_ + ".checkpoint", checkpoint(s)); persist(base_ + ".tail", {}); sync_directory(base_);
    if (!adopt) return; // Old journal remains authoritative after this interruption.
    const Bytes root{1}; persist(base_ + ".root-new", root);
    check(::rename((base_ + ".root-new").c_str(), (base_ + ".root").c_str()) == 0);
    sync_directory(base_);
    seed = s; records.clear(); active_ = base_ + ".tail";
  }
};
class Consumer {
  unsigned style_; std::atomic<std::shared_ptr<const Head>> visible_;
 public:
  explicit Consumer(const Seed& seed = {}, unsigned style = 0) : style_(style), visible_(seed.head) {}
  auto head() const { return visible_.load(std::memory_order_acquire); }
  bool apply(const Record& r, bool capacity = true, bool crash_middle = false) {
    auto old = head(); check(r.command.source == old->epoch && r.epoch == old->epoch + 1);
    if (!capacity) return false; // Retry the same admitted obligation; never invent no-effect.
    auto h = std::make_shared<Head>(*old); h->epoch = r.epoch; h->no_effect = false;
    const auto& c = r.command;
    if (c.op == Op::create && !old->present) {
      h->present = true; h->parts[0] = unit(11, r.epoch, style_, unsigned(c.a & 7));
      h->parts[1] = unit(22, r.epoch, style_, unsigned(c.b & 7));
    } else if (c.op == Op::update && old->present) {
      const auto a = value(*old->parts[0]), b = value(*old->parts[1]);
      h->parts[0] = unit(11, r.epoch, style_, unsigned((a + b + c.a) & 7));
      if (crash_middle) { check(head() == old && old->epoch == c.source); ::_exit(0); }
      h->parts[1] = unit(22, r.epoch, style_, unsigned((b + c.b) & 7));
    } else if (c.op == Op::layout && old->present) {
      h->parts[0] = unit(11, r.epoch, 1 - old->parts[0]->style, value(*old->parts[0]));
      check(h->parts[1] == old->parts[1]);
    } else if (c.op == Op::erase && old->present) { h->present = false; h->parts = {}; }
    else h->no_effect = true;
    h->count = h->present ? 2 : 0; h->sum = h->present ? value(*h->parts[0]) + value(*h->parts[1]) : 0;
    visible_.store(std::move(h), std::memory_order_release); return true;
  }
  void replay(const Log& log) { for (const auto& r : log.records) check(apply(r)); }
  // The caller verifies immutable admitted membership/content before entry.
  bool fold_epoch(U64 epoch, std::span<const Record> events, U64& producer_cut, std::size_t capacity) {
    check(!events.empty()); auto old = head();
    if (epoch <= old->epoch) { check(events.back().epoch <= producer_cut); return true; }
    check(epoch == old->epoch + 1 && events.front().command.source == producer_cut);
    // File cases map one event to one epoch. Here Record.epoch is a producer
    // position; the scratch consumer folds those positions privately.
    auto source = std::make_shared<Head>(*old); source->epoch = producer_cut;
    Consumer work(Seed{producer_cut, producer_cut, source, {}}, style_);
    for (std::size_t i = 0; i < events.size(); ++i) {
      if (i == capacity) { check(head() == old); return false; }
      check(work.apply(events[i]) && head() == old);
    }
    auto complete = std::make_shared<Head>(*work.head()); complete->epoch = epoch;
    visible_.store(std::move(complete), std::memory_order_release);
    producer_cut = events.back().epoch; return true;
  }
};
class DerivedPort {
  std::string destination_;
  std::vector<std::shared_ptr<const Head>> requests_;
 public:
  explicit DerivedPort(std::string destination) : destination_(std::move(destination)) {}
  void submit(std::shared_ptr<const Head> h) { requests_.push_back(std::move(h)); }
  std::size_t pending() const { return requests_.size(); }
  void complete() {
    check(!requests_.empty()); auto h = requests_.front();
    persist(destination_, checkpoint({h->epoch, h->epoch, h, {}})); requests_.erase(requests_.begin());
  }
};
Command command(unsigned n) {
  static constexpr std::array<Op, 5> ops{Op::create, Op::update, Op::layout, Op::erase, Op::update};
  Command c{n, n - 1, 42, 11, 1, ops.at(n - 1)};
  if (n == 1) { c.a = 2; c.b = 3; } if (n == 2) { c.a = 1; c.b = 2; } return c;
}
template<class F> void rejects(F&& f) { bool rejected = false; try { f(); } catch (const std::runtime_error&) { rejected = true; } check(rejected); }
void operations(Log& log) {
  Consumer consumer(log.seed); std::vector<std::shared_ptr<const Head>> retained;
  for (unsigned n = 1; n <= 5; ++n) {
    auto c = command(n); check(log.admit(c) == n && log.admit(c) == n);
    check(!consumer.apply(log.records.back(), false) && consumer.head()->epoch == n - 1);
    check(consumer.apply(log.records.back())); verify(*consumer.head(), n); retained.push_back(consumer.head());
    auto conflict = c; ++conflict.a; rejects([&] { log.admit(conflict); });
  }
  check(consumer.head()->no_effect); for (unsigned i = 0; i < retained.size(); ++i) verify(*retained[i], i + 1);
  Consumer different(log.seed, 1); different.replay(log); verify(*different.head(), 5);
}
void memory_epochs() {
  MemoryLog producer;
  for (unsigned n = 1; n <= 6; ++n) {
    auto c = command(n == 1 ? 1 : n - 1); c.id = n; c.source = n - 1;
    if (n == 2) c.a = c.b = 7; // Create on present object is no-effect, followed by real update.
    producer.records.push_back({n, decode(encode(c))});
  }
  // Retained event input and batch membership are separate. Authored batch
  // sizes exercise the boundary; they are not an adaptive batching policy.
  struct Batch { U64 epoch; std::vector<Record> events; };
  const std::array<Batch, 2> admitted{{
      {1, {producer.records.begin(), producer.records.begin() + 3}},
      {2, {producer.records.begin() + 3, producer.records.end()}}}};
  auto fold = [&](Consumer& target, U64& cursor, const Batch& submitted, std::size_t capacity) {
    const auto authentic = std::find_if(admitted.begin(), admitted.end(), [&](const auto& b) { return b.epoch == submitted.epoch; });
    if (authentic == admitted.end() || authentic->events.size() != submitted.events.size())
      throw std::runtime_error("unadmitted batch membership");
    for (std::size_t i = 0; i < submitted.events.size(); ++i)
      if (submitted.events[i].epoch != authentic->events[i].epoch ||
          encode(submitted.events[i].command) != encode(authentic->events[i].command))
        throw std::runtime_error("conflicting admitted batch retry");
    return target.fold_epoch(authentic->epoch, authentic->events, cursor, capacity);
  };
  Consumer consumer; U64 producer_cut{}; auto initial = consumer.head();
  const auto& first = admitted[0];
  check(!fold(consumer, producer_cut, first, 2));
  check(consumer.head() == initial && producer_cut == 0 && first.events.size() == 3 && producer.records.size() == 6);
  check(fold(consumer, producer_cut, first, 3));
  auto completed = consumer.head();
  check(completed->epoch == 1 && producer_cut == 3 && !completed->no_effect && completed->count == 2 && completed->sum == 11);
  check(value(*completed->parts[0]) == 6 && value(*completed->parts[1]) == 5);
  check(fold(consumer, producer_cut, first, 0) && consumer.head() == completed);
  auto conflicting = first; ++conflicting.events[1].command.a;
  rejects([&] { fold(consumer, producer_cut, conflicting, 0); });
  conflicting = first; ++conflicting.events[1].epoch;
  rejects([&] { fold(consumer, producer_cut, conflicting, 0); });
  check(consumer.head() == completed && producer_cut == 3);
  const auto& second = admitted[1];
  check(!fold(consumer, producer_cut, second, 2));
  check(consumer.head() == completed && producer_cut == 3 && second.events.size() == 3);
  check(fold(consumer, producer_cut, second, 3));
  auto latest = consumer.head();
  check(latest->epoch == 2 && producer_cut == 6 && latest->no_effect && !latest->present && latest->count == 0 && latest->sum == 0);
  check(fold(consumer, producer_cut, second, 0) && consumer.head() == latest);
  check(completed->sum == 11 && value(*completed->parts[0]) == 6 && value(*completed->parts[1]) == 5);
  Consumer replayed({}, 1); U64 replay_cut{};
  for (const auto& b : admitted) check(fold(replayed, replay_cut, b, b.events.size()));
  check(replayed.head()->epoch == 2 && replay_cut == 6 && replayed.head()->no_effect && !replayed.head()->present);
}
void fresh(const std::string& path, const std::string& mode) {
  const auto child = ::fork(); check(child >= 0);
  if (!child) { ::execl("/proc/self/exe", "intent_probe", mode.c_str(), path.c_str(), nullptr); ::_exit(123); }
  int status{}; check(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
}
int main(int argc, char** argv) {
  if (argc == 3) {
    FileLog log(argv[2]); Consumer consumer(log.seed, 1);
    const std::string mode = argv[1];
    if (mode == "recover") { consumer.replay(log); verify(*consumer.head(), log.latest()); return 0; }
    consumer.replay(log); check(log.admit(command(2)) == 2);
    if (mode == "admitted") ::_exit(0);
    check(consumer.apply(log.records.back(), true, mode == "middle")); verify(*consumer.head(), 2);
    DerivedPort sink(std::string(argv[2]) + ".derived"); sink.submit(consumer.head());
    check(sink.pending() == 1 && !std::filesystem::exists(std::string(argv[2]) + ".derived")); ::_exit(0);
  }
  MemoryLog memory; operations(memory); memory_epochs();
  auto unknown = command(5); unknown.id = 6; unknown.source = 5; unknown.profile = 2;
  rejects([&] { memory.admit(unknown); }); check(memory.latest() == 5);
  const auto home = std::filesystem::temp_directory_path() / ("sixdb-intent-" + std::to_string(::getpid()));
  std::filesystem::create_directory(home);
  { FileLog file((home / "ordinary").string()); operations(file); } fresh((home / "ordinary").string(), "recover");
  { auto b = read_file((home / "ordinary").string()); b[72] ^= 1; persist((home / "corrupt").string(), b);
    rejects([&] { FileLog corrupt((home / "corrupt").string()); }); }
  for (const auto* mode : {"admitted", "middle", "installed"}) {
    const auto path = (home / mode).string(); { FileLog log(path); log.admit(command(1)); }
    fresh(path, mode); fresh(path, "recover");
    FileLog resumed(path); check(resumed.admit(command(2)) == 2 && resumed.latest() == 2);
    resumed.partial(command(3));
    FileLog clean(path); check(clean.latest() == 2 && clean.admit(command(3)) == 3); fresh(path, "recover");
  }
  const auto path = (home / "checkpoint").string();
  {
    FileLog log(path); Consumer consumer; std::shared_ptr<const Head> promised;
    for (unsigned n = 1; n <= 3; ++n) {
      log.admit(command(n)); check(consumer.apply(log.records.back())); if (n == 1) promised = consumer.head();
    }
    auto old = consumer.head(); Seed seed{3, 3, old, promised};
    DerivedPort sink(path + ".derived"); sink.submit(old);
    log.save(seed, false); fresh(path, "recover"); // Unadopted complete material does not replace authority.
    log.save(seed, true); verify(*old, 3); verify(*promised, 1);
    // The durable owner checkpoint includes the still-promised old cut before prefix retirement.
    check(std::filesystem::remove(path)); sync_directory(path);
    rejects([&] { log.admit(command(2)); }); // Retired ID is not a fresh operation or promised cached result.
    log.admit(command(4)); check(consumer.apply(log.records.back())); verify(*consumer.head(), 4); verify(*old, 3); verify(*promised, 1);
    sink.complete(); check(sink.pending() == 0); verify(*restore(read_file(path + ".derived")).head, 3);
    verify(*consumer.head(), 4);
  }
  { FileLog log(path); check(log.seed.cut == 3 && log.scanned == 1 && log.seed.retained); verify(*log.seed.retained, 1);
    Consumer c(log.seed); c.replay(log); verify(*c.head(), 4); }
  fresh(path, "recover"); std::filesystem::remove_all(home);
  std::puts("{\"status\":\"passed\",\"checks\":\"durable-intent-before-native,versioned-exact-cut-replay,memory-file,same-id,private-crash,installed-stalled-sink,interpretation-summary,multi-part,layout,delete,no-effect,resource-retry,torn-tail,checkpoint-before-root,suffix-only-reopen,retired-id,memory-batch-cut,private-read-own-work,batch-capacity-retry,duplicate-batch\"}");
}
