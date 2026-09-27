#include "orbital.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <compare>
#include <deque>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace sixdb::sim::orbital {
namespace {
constexpr std::uint64_t wire_tag = 100, tick_tag = 101, fold_tag = 102, compute_tag = 103;
constexpr std::uint64_t root_read = 200, root_write = 201, recover_list = 202, recover_read = 203, save_tag = 204;
constexpr std::uint64_t max_record = 1 << 20;

void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
std::uint64_t fingerprint(std::string_view bytes) {
  std::uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : bytes) { h ^= c; h *= 1099511628211ULL; }
  return h;
}
std::string hex(std::string_view value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out; out.reserve(value.size() * 2);
  for (unsigned char c : value) { out += digits[c >> 4]; out += digits[c & 15]; }
  return out;
}
std::string unhex(std::string_view value) {
  require(value.size() % 2 == 0, "odd encoded bytes");
  std::string out; out.reserve(value.size() / 2);
  auto digit = [](char c) -> unsigned { if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10; throw std::runtime_error("invalid hex"); };
  for (std::size_t i = 0; i < value.size(); i += 2) out += char(digit(value[i]) * 16 + digit(value[i + 1]));
  return out;
}
Bytes noted_bytes(std::string_view detail) {
  auto from = detail.find("\"bytes\":\"");
  require(from != std::string_view::npos, "model record has no bytes"); from += 9;
  auto until = detail.find('"', from); require(until != std::string_view::npos, "unterminated model bytes");
  return unhex(detail.substr(from, until - from));
}
std::string quote(std::string_view value) {
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c >= 32 && c < 127) result += char(c);
    else { std::ostringstream s; s << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c); result += s.str(); }
  }
  return result + '"';
}
struct Encoder {
  Bytes bytes;
  void number(std::uint64_t value) { for (int i = 0; i < 8; ++i) bytes += char(value >> (i * 8)); }
  void text(std::string_view value) { number(value.size()); bytes += value; }
};
struct Decoder {
  std::string_view bytes;
  std::size_t offset{};
  std::uint64_t number() { require(bytes.size() - offset >= 8, "truncated model number");
    std::uint64_t value{}; for (int i = 0; i < 8; ++i) value |= std::uint64_t(static_cast<unsigned char>(bytes[offset++])) << (i * 8); return value; }
  std::string text() { auto size = number(); require(size <= max_record && size <= bytes.size() - offset, "invalid model field length");
    auto out = std::string(bytes.substr(offset, size)); offset += size; return out; }
  std::size_t count() { auto count = number(); require(count <= 4096, "unbounded model collection"); return count; }
  void end() { require(offset == bytes.size(), "trailing model bytes"); }
};
struct Position {
  std::uint64_t round{}, tx{};
  auto operator<=>(const Position&) const = default;
};
void put(Encoder& e, Position p) { e.number(p.round); e.number(p.tx); }
Position position(Decoder& d) { return {d.number(), d.number()}; }
std::string json(Position p) { return '[' + std::to_string(p.round) + ',' + std::to_string(p.tx) + ']'; }
using Scopes = std::vector<Scope>;
void put(Encoder& e, const Scopes& values) { e.number(values.size()); for (auto v : values) e.number(v); }
Scopes scopes(Decoder& d) { Scopes values; auto count = d.count(); while (count--) values.push_back(static_cast<Scope>(d.number())); return values; }
std::uint32_t shard_of(Scope scope) { return scope / 1000; }
bool intersects(const Scopes& a, const Scopes& b) { return std::ranges::any_of(a, [&](Scope k) { return std::ranges::find(b, k) != b.end(); }); }

// Application-owned canonical physical representation. The ordering machinery
// copies payload bytes; only this fixture interprets them or chooses effects.
namespace fixture {
Bytes value(std::int64_t v) { Encoder e; e.number(static_cast<std::uint64_t>(v)); return e.bytes; }
std::int64_t value(std::string_view v) { Decoder d{v}; auto n = static_cast<std::int64_t>(d.number()); d.end(); return n; }
}
using Values = std::map<Scope, Bytes>;
void put(Encoder& e, const Values& values) { e.number(values.size()); for (const auto& [k, v] : values) { e.number(k); e.text(v); } }
Values values(Decoder& d) { Values out; auto count = d.count(); while (count--) { auto k = static_cast<Scope>(d.number()); require(out.emplace(k, d.text()).second, "duplicate value scope"); } return out; }
std::string json(const Values& values) {
  std::string out = "["; bool comma{};
  for (const auto& [k, v] : values) { if (comma) out += ','; comma = true;
    out += "{\"scope\":" + std::to_string(k) + ",\"value\":" + std::to_string(fixture::value(v)) + '}'; }
  return out + ']';
}
void put(Encoder& e, const Transaction& p) {
  e.number(p.id); e.number(p.at); e.number(p.origin); e.number(static_cast<unsigned>(p.program));
  put(e, p.reads); put(e, p.writes); e.number(static_cast<std::uint64_t>(p.value)); e.text(p.cohort);
}
Transaction plan(Decoder& d) {
  Transaction p; p.id = d.number(); p.at = d.number(); p.origin = static_cast<std::uint32_t>(d.number());
  auto program = d.number(); require(program <= static_cast<unsigned>(Program::transfer), "unknown fixture program"); p.program = static_cast<Program>(program);
  p.reads = scopes(d); p.writes = scopes(d); p.value = static_cast<std::int64_t>(d.number()); p.cohort = d.text(); return p;
}
Values initial() {
  Values out;
  for (std::uint32_t s = 0; s < 2; ++s) { for (std::uint32_t k = 0; k < 4; ++k) out[cell(s, k)] = fixture::value(100 + k); out[cell(s, 99)] = fixture::value(0); }
  return out;
}
Values execute(const Transaction& p, const Values& observed) {
  Values out;
  if (p.program == Program::put) for (auto key : p.writes) out[key] = fixture::value(p.value);
  else if (p.program == Program::checked_sum) {
    std::int64_t sum{}; for (auto key : p.reads) { auto value = fixture::value(observed.at(key)); require(!__builtin_add_overflow(sum, value, &sum), "checked sum overflow"); } out[p.writes.at(0)] = fixture::value(sum);
  } else {
    require(p.reads.size() == 2 && p.writes == p.reads, "transfer fixture needs matching two scopes");
    auto a = fixture::value(observed.at(p.reads[0])), b = fixture::value(observed.at(p.reads[1]));
    if (a > 0) { out[p.writes[0]] = fixture::value(a - 1); require(b < std::numeric_limits<std::int64_t>::max(), "transfer overflow"); out[p.writes[1]] = fixture::value(b + 1); }
  }
  return out;
}
struct ReadContext {
  Tx tx{}; Position cut; Scopes reads, writes;
  auto operator<=>(const ReadContext&) const = default;
};
void put(Encoder& e, const ReadContext& c) { e.number(c.tx); put(e, c.cut); put(e, c.reads); put(e, c.writes); e.text("cell-source-v1"); e.text("sum-code-v1"); e.text("canonical-fixture-v1"); }
ReadContext context(Decoder& d) { ReadContext c; c.tx = d.number(); c.cut = position(d); c.reads = scopes(d); c.writes = scopes(d);
  require(d.text() == "cell-source-v1" && d.text() == "sum-code-v1" && d.text() == "canonical-fixture-v1", "unknown source/code/profile"); return c; }
enum class Command { acquire, floor, announce, fix, read, register_context, resolve };
std::string_view name(Command c) { constexpr std::array names{"acquire", "floor", "announce", "fix", "read", "context", "resolve"}; return names.at(static_cast<unsigned>(c)); }
struct Request {
  Tx tx{}; Command command{}; std::uint32_t shard{}; ActorId reply{};
  Scopes reads, writes; Position cut; Values effects; ReadContext context;
};
std::uint64_t request_id(const Request& r) { return r.tx * 16 + static_cast<unsigned>(r.command); }
void put(Encoder& e, const Request& r) { e.number(r.tx); e.number(static_cast<unsigned>(r.command)); e.number(r.shard); e.number(r.reply);
  put(e, r.reads); put(e, r.writes); put(e, r.cut); put(e, r.effects); put(e, r.context); }
Request request(Decoder& d) { Request r; r.tx = d.number(); auto kind = d.number(); require(kind <= 6, "unknown metadata command"); r.command = static_cast<Command>(kind);
  r.shard = static_cast<std::uint32_t>(d.number()); r.reply = static_cast<ActorId>(d.number()); r.reads = scopes(d); r.writes = scopes(d); r.cut = position(d); r.effects = values(d); r.context = context(d); return r; }
struct Response { Request request; Position minimum; Values observed; };
void put(Encoder& e, const Response& r) { put(e, r.request); put(e, r.minimum); put(e, r.observed); }
Response response(Decoder& d) { auto r = request(d); auto minimum = position(d); auto v = values(d); return {std::move(r), minimum, std::move(v)}; }
struct Batch { std::uint64_t index{}, previous{}; Request request; };
void put(Encoder& e, const Batch& b) { e.number(b.index); e.number(b.previous); put(e, b.request); }
Batch batch(Decoder& d) { auto index = d.number(), previous = d.number(); return {index, previous, request(d)}; }
Bytes encoded(const Batch& b) { Encoder e; put(e, b); return e.bytes; }
std::uint64_t identity(const Batch& b) { return fingerprint(encoded(b)); }
struct Epoch { Batch batch; std::uint32_t voters{}; };
void put(Encoder& e, const Epoch& v) { put(e, v.batch); e.number(v.voters); }
Epoch epoch(Decoder& d) { auto b = batch(d); return {std::move(b), static_cast<std::uint32_t>(d.number())}; }
bool quorum(std::uint32_t voters) { return (voters & 1) && (voters & 6); }
struct Report { ReadContext context; Values observed, effects; };
void put(Encoder& e, const Report& r) { put(e, r.context); put(e, r.observed); put(e, r.effects); }
Report report(Decoder& d) { auto c = context(d); auto v = values(d); auto effects = values(d); return {std::move(c), std::move(v), std::move(effects)}; }
struct Verification { ReadContext context; std::map<ActorId, Report> reports; bool match{}; };
void put(Encoder& e, const Verification& v) { put(e, v.context); e.number(v.reports.size()); for (const auto& [actor, r] : v.reports) { e.number(actor); put(e, r); } e.number(v.match); }
Verification verification(Decoder& d) { Verification v; v.context = context(d); auto n = d.count(); while (n--) { auto actor = static_cast<ActorId>(d.number()); v.reports.emplace(actor, report(d)); } v.match = d.number(); return v; }
struct Decision { Tx tx{}; Position cut; bool commit{}, checked{}; Values observed, effects; };
void put(Encoder& e, const Decision& v) { e.number(v.tx); put(e, v.cut); e.number(v.commit); e.number(v.checked); put(e, v.observed); put(e, v.effects); }
Decision decision(Decoder& d) { Decision v; v.tx = d.number(); v.cut = position(d); v.commit = d.number(); v.checked = d.number(); v.observed = values(d); v.effects = values(d); return v; }
template<class T> Bytes encoded(const T& value) { Encoder e; put(e, value); return e.bytes; }

enum class Wire { submit, request, response, append, accepted, receipt, fetch, repeat, invoke, query, query_result, report, done };
Bytes message(Wire kind, std::string_view bytes) { Encoder e; e.number(static_cast<unsigned>(kind)); e.bytes += bytes; return e.bytes; }
void send(Context& ctx, ActorId to, Wire kind, Bytes bytes) { ctx.send(to, wire_tag, message(kind, bytes)); }
void note(Context& ctx, std::string kind, Bytes bytes, std::string fields = {}, std::uint64_t tag = 0) {
  ctx.note(std::move(kind), "{\"bytes\":\"" + hex(bytes) + '"' + fields + '}', tag);
}
std::string key(std::string_view prefix, std::uint64_t id) { std::ostringstream s; s << prefix << '/' << std::setw(20) << std::setfill('0') << id; return s.str(); }
std::uint64_t suffix(std::string_view k) { auto at = k.rfind('/'); require(at != std::string_view::npos, "record key missing suffix"); std::uint64_t n{}; auto result = std::from_chars(k.data() + at + 1, k.data() + k.size(), n); require(result.ec == std::errc{}, "invalid record key"); return n; }

/// A recovery/storage helper, not a protocol superclass. Child records are read
/// one at a time after validating a durable root. Immutable serialized records
/// and a base allowance are charged; STL bookkeeping costs are approximations.
struct Records {
  struct Saved { std::string key; Bytes bytes; bool ok{}; };
  struct Pending { std::string key; Bytes bytes; Buffer buffer{}; };
  std::map<std::string, Bytes> data;
  std::map<std::string, Buffer> buffers;
  std::map<OpId, Pending> pending;
  std::deque<std::string> page;
  std::string cursor, expected_root;
  std::optional<Buffer> base;
  bool ready{}, recovering{}, listing{};
  Time retry;
  std::optional<Saved> saved;
  explicit Records(Time retry_ns) : retry(retry_ns) {}
  void boot(Context& ctx) {
    if (!base) base = ctx.allocate(Bytes(2048, '\0'));
    if (!base) { ctx.timer(retry, tick_tag); return; }
    expected_root = "orbital-reference-v1/" + std::to_string(ctx.self()); recovering = true;
    ctx.read("root", 256, root_read);
  }
  void next(Context& ctx) {
    if (!page.empty()) ctx.read(page.front(), 16 << 10, recover_read);
    else if (!cursor.empty() || !listing) { listing = true; ctx.list("", cursor, 16, 4096, recover_list); }
    else { recovering = false; ready = true; ctx.note("orbital.recovered", "{\"records\":" + std::to_string(data.size()) + '}'); }
  }
  bool consume(Context& ctx, const Event& e) {
    saved.reset();
    if (e.kind == EventKind::boot) { boot(ctx); return true; }
    if (e.kind == EventKind::timer && e.tag == tick_tag && !ready && !recovering) { boot(ctx); return true; }
    if (e.kind != EventKind::completion) return false;
    if (e.tag == root_read) {
      if (e.status == Status::missing) ctx.write("root", expected_root, root_write);
      else if (e.status == Status::ok) { require(e.bytes == expected_root, "durable root version/identity mismatch"); cursor.clear(); listing = false; next(ctx); }
      else { recovering = false; ctx.timer(retry, tick_tag); }
      return true;
    }
    if (e.tag == root_write) {
      if (e.status == Status::ok) { cursor.clear(); listing = false; next(ctx); }
      else { recovering = false; ctx.timer(retry, tick_tag); }
      return true;
    }
    if (e.tag == recover_list) {
      if (e.status != Status::ok) { recovering = false; ctx.timer(retry, tick_tag); return true; }
      cursor = e.next; for (const auto& k : e.keys) if (k != "root" && !data.contains(k)) page.push_back(k); next(ctx); return true;
    }
    if (e.tag == recover_read) {
      if (e.status != Status::ok) { recovering = false; ctx.timer(retry, tick_tag); return true; }
      auto buffer = ctx.allocate(e.bytes);
      if (!buffer) { recovering = false; ctx.timer(retry, tick_tag); return true; }
      auto k = page.front(); page.pop_front(); data[k] = e.bytes; buffers[k] = *buffer; next(ctx); return true;
    }
    if (e.tag == save_tag) {
      auto it = pending.find(e.id); require(it != pending.end(), "unowned model write completion"); auto item = std::move(it->second); pending.erase(it);
      auto ok = e.status == Status::ok;
      if (ok) { require(!data.contains(item.key) || data.at(item.key) == item.bytes, "immutable record changed");
        if (buffers.contains(item.key)) ctx.release(buffers.at(item.key)); data[item.key] = item.bytes; buffers[item.key] = item.buffer; }
      else ctx.release(item.buffer);
      saved = Saved{std::move(item.key), std::move(item.bytes), ok}; return true;
    }
    return false;
  }
  bool save(Context& ctx, std::string k, Bytes bytes, std::string record_kind, std::string fields = {}) {
    require(!data.contains(k), "attempted duplicate immutable storage write");
    if (std::ranges::any_of(pending, [&](const auto& p) { return p.second.key == k; })) return true;
    auto buffer = ctx.allocate(bytes);
    if (!buffer) { ctx.note("orbital.pressure", "{\"phase\":\"retained-record\",\"key\":" + quote(k) + '}'); return false; }
    auto operation = ctx.write(k, *buffer, save_tag);
    note(ctx, "orbital.write", bytes, ",\"record\":" + quote(record_kind) + ",\"key\":" + quote(k) + fields, operation);
    pending.emplace(operation, Pending{std::move(k), std::move(bytes), *buffer}); return true;
  }
};

struct Ticket { Scopes writes; std::optional<Position> minimum, cut; bool released{}, resolved{}; };
struct Fold {
  std::uint32_t shard; bool skip_pending; QueuePolicy queue_policy;
  std::map<Tx, Ticket> tickets;
  std::map<Scope, Position> bounds;
  std::map<Scope, std::vector<std::pair<Position, Bytes>>> versions;
  std::map<std::uint64_t, Request> requests;
  std::map<std::uint64_t, Response> replies;
  std::vector<std::uint64_t> waiting, reads, live_order;
  std::uint64_t policy_probes{}, holder_visits{}, waiter_visits{}, barrier_visits{}, settle_calls{}, max_waiting{}, max_live{}, max_holders{};
  std::map<Tx, ReadContext> contexts;
  explicit Fold(std::uint32_t s, bool skip, QueuePolicy policy) : shard(s), skip_pending(skip), queue_policy(policy) {
    for (const auto& [scope, value] : initial()) if (shard_of(scope) == shard) versions[scope].push_back({{}, value});
  }
  Position floor(const Scopes& scopes) const {
    Position out;
    for (auto k : scopes) { require(versions.contains(k), "scope outside fixture object");
      for (const auto& [p, value] : versions.at(k)) out = std::max(out, p); }
    for (const auto& [tx, t] : tickets) if (intersects(scopes, t.writes)) { if (t.cut) out = std::max(out, *t.cut); else if (t.minimum) out = std::max(out, *t.minimum); }
    return out;
  }
  std::optional<Values> observe(Tx tx, Position cut, const Scopes& scopes) const {
    for (const auto& [other, t] : tickets) {
      if (other == tx || t.resolved || !t.minimum || !intersects(t.writes, scopes)) continue;
      if (t.cut.value_or(*t.minimum) <= cut && !skip_pending) return std::nullopt;
    }
    Values out;
    for (auto k : scopes) { const auto& history = versions.at(k); std::optional<std::pair<Position, Bytes>> chosen;
      for (const auto& v : history) if (v.first <= cut && (!chosen || chosen->first < v.first)) chosen = v;
      require(chosen.has_value(), "no retained value at cut"); out[k] = chosen->second; }
    return out;
  }
  bool conflict(const Scopes& a, const Scopes& b) { ++policy_probes; return intersects(a, b); }
  // Queued requests retain original age while held; alternate policies are
  // experimental controls. No policy change is supported within a lineage.
  // Only a durable local fix removes them; no holder is revoked.
  bool barrier(std::uint64_t waiter) {
    if (queue_policy == QueuePolicy::no_overtaking) return true;
    if (queue_policy == QueuePolicy::eligible_first) return false;
    if (queue_policy == QueuePolicy::oldest_live) return !live_order.empty() && live_order.front() == waiter;
    const auto& coverage = requests.at(waiter).writes;
    for (auto older : live_order) {
      ++barrier_visits;
      if (older == waiter) return true;
      if (conflict(requests.at(older).writes, coverage)) return false;
    }
    throw std::logic_error("waiting request missing from live order");
  }
  void settle() {
    ++settle_calls; max_waiting = std::max<std::uint64_t>(max_waiting, waiting.size());
    max_live = std::max<std::uint64_t>(max_live, live_order.size());
    std::vector<std::uint64_t> still;
    // Granting adds a holder; it cannot enable an earlier blocked waiter.
    // This forward scan therefore closes grants before the next agreed record.
    for (auto id : waiting) {
      const auto& r = requests.at(id); bool blocked = false;
      for (const auto& [tx, t] : tickets) {
        ++holder_visits;
        if (tx != r.tx && !t.released && !t.resolved && conflict(t.writes, r.writes)) { blocked = true; break; }
      }
      if (!blocked && queue_policy != QueuePolicy::eligible_first) for (auto older : still) {
        ++waiter_visits;
        if (conflict(requests.at(older).writes, r.writes) && barrier(older)) { blocked = true; break; }
      }
      if (blocked) still.push_back(id);
      else { tickets[r.tx] = Ticket{r.writes, {}, {}, false, false}; replies[id] = Response{r, {}, {}}; }
    }
    waiting = std::move(still);
    std::uint64_t holders{}; for (const auto& [tx, ticket] : tickets) if (!ticket.released && !ticket.resolved) ++holders;
    max_holders = std::max(max_holders, holders);
    for (auto id : reads) if (!replies.contains(id)) { const auto& r = requests.at(id); auto v = observe(r.tx, r.cut, r.reads); if (v) replies[id] = Response{r, {}, std::move(*v)}; }
  }
  std::vector<Response> apply(const Request& r) {
    auto id = request_id(r);
    if (requests.contains(id)) { require(encoded(requests.at(id)) == encoded(r), "metadata identity reused"); return replies.contains(id) ? std::vector<Response>{replies.at(id)} : std::vector<Response>{}; }
    std::set<std::uint64_t> previous; for (const auto& [key, value] : replies) previous.insert(key);
    requests[id] = r;
    Response out{r, {}, {}};
    switch (r.command) {
      case Command::acquire: waiting.push_back(id); live_order.push_back(id); break;
      case Command::floor: out.minimum = floor(r.reads); replies[id] = out; break;
      case Command::announce: {
        auto& t = tickets.at(r.tx); auto after = floor(t.writes);
        for (auto k : t.writes) after = std::max(after, bounds[k]);
        t.minimum = Position{after.round + 1, 0}; out.minimum = *t.minimum; replies[id] = out; break;
      }
      case Command::fix: { auto& t = tickets.at(r.tx); require(t.minimum && r.cut >= *t.minimum, "position below announced minimum"); t.cut = r.cut; t.released = true; std::erase_if(live_order, [&](auto acquire) { return requests.at(acquire).tx == r.tx; }); replies[id] = out; break; }
      case Command::read: for (auto k : r.reads) bounds[k] = std::max(bounds[k], r.cut); reads.push_back(id); break;
      case Command::register_context: {
        require(tickets.at(r.tx).cut == r.context.cut, "context before fixed position");
        contexts[r.tx] = r.context; for (auto k : r.context.reads) { require(shard_of(k) == shard, "checked native context must be shard local"); bounds[k] = std::max(bounds[k], r.context.cut); } replies[id] = out; break;
      }
      case Command::resolve: {
        auto& t = tickets.at(r.tx); require(t.cut.has_value(), "resolve before fixed position");
        for (const auto& [k, value] : r.effects) { require(std::ranges::find(t.writes, k) != t.writes.end(), "effect escaped coverage"); versions[k].push_back({*t.cut, value}); std::ranges::sort(versions[k]); }
        t.resolved = true; replies[id] = out; break;
      }
    }
    settle(); std::vector<Response> enabled;
    for (const auto& [key, value] : replies) if (!previous.contains(key)) enabled.push_back(value);
    return enabled;
  }
  Bytes state() const {
    Encoder e; e.number(shard);
    e.number(tickets.size()); for (const auto& [tx, t] : tickets) { e.number(tx); put(e, t.writes); e.number(t.minimum.has_value()); if (t.minimum) put(e, *t.minimum); e.number(t.cut.has_value()); if (t.cut) put(e, *t.cut); e.number(t.released); e.number(t.resolved); }
    e.number(bounds.size()); for (const auto& [k, p] : bounds) { e.number(k); put(e, p); }
    e.number(versions.size()); for (const auto& [k, vs] : versions) { e.number(k); e.number(vs.size()); for (const auto& [p, value] : vs) { put(e, p); e.text(value); } }
    e.number(requests.size()); for (const auto& [id, r] : requests) { e.number(id); put(e, r); }
    e.number(replies.size()); for (const auto& [id, r] : replies) { e.number(id); put(e, r); }
    e.number(waiting.size()); for (auto id : waiting) e.number(id);
    e.number(reads.size()); for (auto id : reads) e.number(id);
    e.number(live_order.size()); for (auto id : live_order) e.number(id);
    e.number(contexts.size()); for (const auto& [tx, c] : contexts) { e.number(tx); put(e, c); }
    return e.bytes;
  }
};

struct ActorOptions {
  Time retry_ns, checker_delay_ns;
  Negative negative;
  QueuePolicy queue_policy;
};

struct Witness final : Actor {
  std::uint32_t shard, copy;
  ActorOptions config;
  Records records;
  std::map<std::uint64_t, Batch> log;
  std::map<std::uint64_t, std::uint64_t> known;
  std::deque<std::pair<Request, Buffer>> queue;
  std::set<std::uint64_t> queued;
  std::optional<Batch> pending;
  std::array<std::uint64_t, 2> frontiers{}, sent{};
  bool writing{};
  Witness(std::uint32_t s, std::uint32_t c, ActorOptions cfg) : shard(s), copy(c), config(std::move(cfg)), records(config.retry_ns) {}
  void accept(const Batch& b) {
    require(b.index == log.size() + 1 && b.previous == (log.empty() ? 0 : identity(log.rbegin()->second)), "witness prefix gap/fork");
    require(b.request.shard == shard, "batch names another shard"); log[b.index] = b; known[request_id(b.request)] = b.index;
  }
  void receipt(Context& ctx, const Batch& b, std::optional<ActorId> target = {}) {
    Encoder e; put(e, b); e.number(copy);
    if (target) send(ctx, *target, Wire::receipt, e.bytes);
    else for (std::uint32_t i = 0; i < 3; ++i) send(ctx, consumer(shard, i), Wire::receipt, e.bytes);
    if (copy) { Encoder ack; ack.number(b.index); ack.number(identity(b)); send(ctx, leader(shard), Wire::accepted, ack.bytes); }
  }
  void pump(Context& ctx, bool retry = false) {
    if (copy == 0) {
      for (std::uint32_t i = 0; i < 2; ++i) {
        auto next = frontiers[i] + 1;
        if (log.contains(next) && (retry || sent[i] != next)) { send(ctx, follower(shard, i), Wire::append, encoded(log.at(next))); sent[i] = next; }
      }
      if (!pending && !queue.empty()) pending = Batch{log.size() + 1, log.empty() ? 0 : identity(log.rbegin()->second), queue.front().first};
    }
    if (pending && !writing) writing = records.save(ctx, key("log", pending->index), encoded(*pending), "witness",
        ",\"shard\":" + std::to_string(shard) + ",\"epoch\":" + std::to_string(pending->index));
  }
  void receive(Context& ctx, const Event& e) override {
    if (e.kind == EventKind::completion && e.operation == Operation::read && e.tag >= 10'000) { fetched(ctx, e); return; }
    bool ready = records.ready;
    if (records.consume(ctx, e)) {
      if (!ready && records.ready) {
        for (const auto& [k, value] : records.data) if (k.starts_with("log/")) { Decoder d{value}; accept(batch(d)); d.end(); }
        pump(ctx, true); ctx.timer(config.retry_ns, tick_tag);
      }
      if (records.saved) {
        writing = false;
        if (records.saved->ok) {
          Decoder d{records.saved->bytes}; auto b = batch(d); d.end(); accept(b);
          if (copy == 0) { require(!queue.empty(), "leader lost pending request"); ctx.release(queue.front().second); queued.erase(request_id(queue.front().first)); queue.pop_front(); }
          pending.reset(); receipt(ctx, b);
        }
        if (records.saved->ok) pump(ctx);
      }
      return;
    }
    if (!records.ready) return;
    if (e.kind == EventKind::timer && e.tag == tick_tag) { pump(ctx, true); ctx.timer(config.retry_ns, tick_tag); return; }
    if (e.kind != EventKind::message || e.tag != wire_tag) return;
    Decoder d{e.bytes}; auto kind = static_cast<Wire>(d.number());
    if (kind == Wire::request && copy == 0) {
      auto r = request(d); d.end(); require(r.reply == e.from, "request reply identity differs from authenticated sender"); auto rid = request_id(r);
      if (known.contains(rid)) { require(encoded(log.at(known.at(rid)).request) == encoded(r), "request identity changed"); send(ctx, consumer(shard, 0), Wire::repeat, encoded(r)); }
      else if (!queued.contains(rid)) {
        require(known.size() + queued.size() < 4096, "bounded reference input limit reached");
        auto memory = ctx.allocate(encoded(r)); if (!memory) { ctx.note("orbital.pressure", "{\"phase\":\"request-admission\"}"); return; }
        queued.insert(rid); queue.emplace_back(std::move(r), *memory); pump(ctx);
      }
    } else if (kind == Wire::append && copy && e.from == leader(shard)) {
      auto b = batch(d); d.end();
      if (log.contains(b.index)) { require(encoded(log.at(b.index)) == encoded(b), "prepared authority equivocated"); receipt(ctx, b); }
      else if (b.index == log.size() + 1) { if (pending) require(encoded(*pending) == encoded(b), "prepared slot changed"); else pending = b; pump(ctx); }
    } else if (kind == Wire::accepted && copy == 0) {
      auto index = d.number(), hash = d.number(); d.end();
      for (std::uint32_t i = 0; i < 2; ++i) if (e.from == follower(shard, i) && index > frontiers[i] && log.contains(index) && identity(log.at(index)) == hash) { frontiers[i] = index; pump(ctx); }
    } else if (kind == Wire::fetch) {
      auto index = d.number(); d.end();
      bool allowed = false; for (std::uint32_t i = 0; i < 3; ++i) allowed |= e.from == consumer(shard, i);
      if (allowed && log.contains(index)) {
        // Fetch reads the retained physical record instead of using the cache as
        // an uncharged disk/network oracle. Tag carries the bounded destination.
        ctx.read(key("log", index), 16 << 10, 10'000 + e.from);
      }
    }
  }
  // A retained-record fetch supplies fresh receipt evidence to this requester.
  void fetched(Context& ctx, const Event& e) {
    if (records.ready && e.status == Status::ok) { Decoder d{e.bytes}; auto b = batch(d); d.end(); receipt(ctx, b, static_cast<ActorId>(e.tag - 10'000)); }
  }
};

struct Consumer final : Actor {
  std::uint32_t shard, copy;
  ActorOptions config;
  Records records;
  Fold fold;
  std::map<std::uint64_t, Epoch> epochs;
  std::map<std::uint64_t, Batch> incoming;
  std::map<std::uint64_t, std::uint32_t> votes;
  std::map<std::uint64_t, Buffer> metadata;
  std::uint64_t applied{};
  bool writing{}, running{};
  Consumer(std::uint32_t s, std::uint32_t c, ActorOptions cfg) : shard(s), copy(c), config(std::move(cfg)), records(config.retry_ns), fold(s, config.negative == Negative::skip_pending, config.queue_policy) {}
  void fetch(Context& ctx) {
    auto index = epochs.empty() ? 1 : epochs.rbegin()->first + 1; Encoder e; e.number(index);
    send(ctx, leader(shard), Wire::fetch, e.bytes); for (std::uint32_t i = 0; i < 2; ++i) send(ctx, follower(shard, i), Wire::fetch, e.bytes);
  }
  void pump(Context& ctx) {
    if (!running && epochs.contains(applied + 1)) {
      if (!metadata.contains(applied + 1)) {
        auto memory = ctx.allocate(Bytes(encoded(epochs.at(applied + 1).batch).size() * 2 + 128, '\0'));
        if (!memory) return;
        metadata[applied + 1] = *memory;
      }
      running = true; ctx.compute(500 + 500 * copy, fold_tag);
    }
    auto next = epochs.empty() ? 1 : epochs.rbegin()->first + 1;
    if (!writing && incoming.contains(next) && quorum(votes[next])) {
      Epoch epoch{incoming.at(next), votes.at(next)};
      writing = records.save(ctx, key("epoch", next), encoded(epoch), "epoch",
                 ",\"shard\":" + std::to_string(shard) + ",\"epoch\":" + std::to_string(next));
    }
  }
  void apply(Context& ctx) {
    auto index = applied + 1; const auto& b = epochs.at(index).batch;
    require(b.previous == (index == 1 ? 0 : identity(epochs.at(index - 1).batch)), "consumer history gap/fork");
    auto before_probes = fold.policy_probes; auto before_settles = fold.settle_calls;
    auto before_holders=fold.holder_visits, before_waiters=fold.waiter_visits, before_barriers=fold.barrier_visits;
    auto outputs = fold.apply(b.request);
    ctx.note("orbital.policy-work", "{\"shard\":" + std::to_string(shard) + ",\"copy\":" + std::to_string(copy) + ",\"epoch\":" + std::to_string(index) + ",\"probes\":" + std::to_string(fold.policy_probes-before_probes) + ",\"settles\":" + std::to_string(fold.settle_calls-before_settles) + ",\"holder_visits\":" + std::to_string(fold.holder_visits-before_holders) + ",\"waiter_visits\":" + std::to_string(fold.waiter_visits-before_waiters) + ",\"barrier_visits\":" + std::to_string(fold.barrier_visits-before_barriers) + ",\"max_waiting\":" + std::to_string(fold.max_waiting) + ",\"max_live\":" + std::to_string(fold.max_live) + ",\"max_holders\":" + std::to_string(fold.max_holders) + '}');
    note(ctx, "orbital.applied", encoded(b.request), ",\"shard\":" + std::to_string(shard) + ",\"copy\":" + std::to_string(copy) + ",\"epoch\":" + std::to_string(index) + ",\"tx\":" + std::to_string(b.request.tx) + ",\"command\":" + quote(name(b.request.command)) + ",\"position\":" + json(b.request.cut) + ",\"effects\":" + json(b.request.effects));
    Encoder emitted;
    for (const auto& out : outputs) {
      put(emitted, out);
      note(ctx, "orbital.output", encoded(out), ",\"shard\":" + std::to_string(shard) + ",\"copy\":" + std::to_string(copy) + ",\"tx\":" + std::to_string(out.request.tx) + ",\"command\":" + quote(name(out.request.command)) + ",\"position\":" + json(out.request.cut) + ",\"minimum\":" + json(out.minimum) + ",\"values\":" + json(out.observed));
      if (copy == 0) send(ctx, out.request.reply, Wire::response, encoded(out));
    }
    Encoder complete; complete.number(shard); complete.number(copy); complete.number(index); complete.text(fold.state()); complete.text(emitted.bytes);
    note(ctx, "orbital.fixpoint", complete.bytes, ",\"shard\":" + std::to_string(shard) + ",\"copy\":" + std::to_string(copy) + ",\"epoch\":" + std::to_string(index) + ",\"state_hash\":" + std::to_string(fingerprint(fold.state())));
    applied = index; running = false; pump(ctx);
  }
  void receive(Context& ctx, const Event& e) override {
    bool ready = records.ready;
    if (records.consume(ctx, e)) {
      if (!ready && records.ready) {
        for (const auto& [k, v] : records.data) if (k.starts_with("epoch/")) { Decoder d{v}; auto epoch = ::sixdb::sim::orbital::epoch(d); d.end(); require(quorum(epoch.voters), "recovered epoch lacks quorum"); epochs[epoch.batch.index] = std::move(epoch); }
        pump(ctx); fetch(ctx); ctx.timer(config.retry_ns, tick_tag);
      }
      if (records.saved) {
        writing = false;
        if (records.saved->ok) { Decoder d{records.saved->bytes}; auto epoch = ::sixdb::sim::orbital::epoch(d); d.end(); auto index = epoch.batch.index; epochs[index] = std::move(epoch); incoming.erase(index); votes.erase(index); fetch(ctx); }
        if (records.saved->ok) pump(ctx);
      }
      return;
    }
    if (!records.ready) return;
    if (e.kind == EventKind::timer && e.tag == tick_tag) { pump(ctx); fetch(ctx); ctx.timer(config.retry_ns, tick_tag); return; }
    if (e.kind == EventKind::completion && e.operation == Operation::compute && e.tag == fold_tag) {
      if (e.status == Status::ok) apply(ctx); else running = false; return;
    }
    if (e.kind != EventKind::message || e.tag != wire_tag) return;
    Decoder d{e.bytes}; auto kind = static_cast<Wire>(d.number());
    if (kind == Wire::receipt) {
      auto b = batch(d); auto voter = d.number(); d.end(); require(voter <= 2, "invalid witness identity");
      ActorId expected = voter == 0 ? leader(shard) : follower(shard, static_cast<std::uint32_t>(voter - 1));
      require(e.from == expected && b.request.shard == shard, "receipt sender/shard mismatch");
      if (epochs.contains(b.index)) return;
      if (incoming.contains(b.index)) require(encoded(incoming.at(b.index)) == encoded(b), "receipt input mismatch");
      else {
        auto retained = ctx.allocate(Bytes(encoded(b).size() * 2 + 128, '\0'));
        if (!retained) { ctx.note("orbital.pressure", "{\"phase\":\"fold-metadata\"}"); return; }
        metadata[b.index] = *retained; incoming[b.index] = b;
      }
      votes[b.index] |= 1U << voter; pump(ctx);
    } else if (kind == Wire::repeat && e.from == leader(shard) && copy == 0) {
      auto r = request(d); d.end(); auto id = request_id(r);
      if (fold.replies.contains(id)) { require(encoded(fold.replies.at(id).request) == encoded(r), "repeat request changed"); send(ctx, r.reply, Wire::response, encoded(fold.replies.at(id))); }
    } else if (kind == Wire::query) {
      auto c = context(d); auto scope = static_cast<Scope>(d.number()); d.end();
      bool valid = (e.from == checker(0) || e.from == checker(1)) && fold.contexts.contains(c.tx) && fold.contexts.at(c.tx) == c && std::ranges::find(c.reads, scope) != c.reads.end();
      auto before = fingerprint(fold.state()); auto result = valid ? fold.observe(c.tx, c.cut, Scopes{scope}) : std::nullopt;
      Encoder answer; put(answer, c); answer.number(scope); answer.number(result.has_value()); if (result) put(answer, *result);
      Encoder observed; put(observed, c); observed.number(scope); observed.number(e.from); observed.number(result.has_value()); observed.number(before); observed.number(fingerprint(fold.state())); if (result) put(observed, *result);
      note(ctx, "orbital.private_read", observed.bytes, ",\"tx\":" + std::to_string(c.tx) + ",\"scope\":" + std::to_string(scope) + ",\"position\":" + json(c.cut) + ",\"ready\":" + (result ? "true" : "false") + ",\"values\":" + json(result.value_or(Values{})));
      send(ctx, e.from, Wire::query_result, answer.bytes);
    }
  }
};

std::map<std::uint32_t, Scopes> grouped(const Scopes& scopes) {
  std::map<std::uint32_t, Scopes> out; for (auto k : scopes) out[shard_of(k)].push_back(k); return out;
}
ReadContext read_context(const Transaction& p, Position cut) { return {p.id, cut, p.reads, p.writes}; }
bool verification_matches(const Verification& v) {
  if (v.reports.size() != 2 || !v.reports.contains(checker(0)) || !v.reports.contains(checker(1))) return false;
  const auto& a = v.reports.at(checker(0)); const auto& b = v.reports.at(checker(1));
  if (a.context != v.context || b.context != v.context || encoded(a) != encoded(b)) return false;
  Scopes read; for (const auto& [k, val] : a.observed) read.push_back(k);
  if (read != v.context.reads) return false;
  return std::ranges::all_of(a.effects, [&](const auto& kv) { return std::ranges::find(v.context.writes, kv.first) != v.context.writes.end(); });
}
struct Coordinator final : Actor {
  enum class Phase { input, floor, acquire, announce, position, fix, read, checked_context, verify, compute, verification, decision, install, done };
  struct State {
    Transaction plan; Phase phase{Phase::input}; Position cut;
    std::map<std::uint32_t, Scopes> read_scopes, write_scopes;
    std::map<std::uint32_t, Position> floors, minima;
    std::set<std::uint32_t> acquired, fixed, read, installed;
    Values observed;
    std::map<ActorId, Report> reports;
    std::vector<Buffer> retained;
    Decision outcome;
    bool pending{}, commit{true};
  };
  ActorOptions config;
  Records records;
  std::map<Tx, State> states;
  std::map<OpId, Tx> computations;
  explicit Coordinator(ActorOptions cfg) : config(std::move(cfg)), records(config.retry_ns) {}
  State make(const Transaction& p) { State s; s.plan = p; s.read_scopes = grouped(p.reads); s.write_scopes = grouped(p.writes); return s; }
  void ask(Context& ctx, State& s, std::uint32_t shard, Command command) {
    Request r; r.tx = s.plan.id; r.shard = shard; r.reply = ctx.self(); r.command = command; r.cut = s.cut;
    if (s.read_scopes.contains(shard)) r.reads = s.read_scopes.at(shard);
    if (s.write_scopes.contains(shard)) r.writes = s.write_scopes.at(shard);
    if (command == Command::register_context) r.context = read_context(s.plan, s.cut);
    if (command == Command::resolve) for (const auto& [k, v] : s.outcome.effects) if (shard_of(k) == shard) r.effects[k] = v;
    send(ctx, leader(shard), Wire::request, encoded(r));
  }
  void advance(Context& ctx, State& s) {
    const auto tx = s.plan.id;
    for (;;) {
      switch (s.phase) {
        case Phase::input:
          if (!s.pending) s.pending = records.save(ctx, key("input", tx), encoded(s.plan), "input", ",\"tx\":" + std::to_string(tx));
          return;
        case Phase::floor:
          if (s.floors.size() != s.read_scopes.size()) { for (const auto& [shard, scopes] : s.read_scopes) if (!s.floors.contains(shard)) ask(ctx, s, shard, Command::floor); return; }
          s.phase = Phase::acquire; break;
        case Phase::acquire:
          if (s.acquired.size() != s.write_scopes.size()) { for (const auto& [shard, scopes] : s.write_scopes) if (!s.acquired.contains(shard)) { ask(ctx, s, shard, Command::acquire); break; } return; }
          s.phase = Phase::announce; break;
        case Phase::announce:
          if (s.minima.size() != s.write_scopes.size()) { for (const auto& [shard, scopes] : s.write_scopes) if (!s.minima.contains(shard)) ask(ctx, s, shard, Command::announce); return; }
          s.cut = {1, tx};
          for (const auto& [shard, bound] : s.floors) s.cut.round = std::max(s.cut.round, bound.round + 1);
          for (const auto& [shard, bound] : s.minima) s.cut.round = std::max(s.cut.round, bound.round);
          s.phase = Phase::position; break;
        case Phase::position:
          if (!s.pending) s.pending = records.save(ctx, key("position", tx), encoded(s.cut), "position", ",\"tx\":" + std::to_string(tx) + ",\"position\":" + json(s.cut));
          return;
        case Phase::fix:
          if (s.fixed.size() != s.write_scopes.size()) { for (const auto& [shard, scopes] : s.write_scopes) if (!s.fixed.contains(shard)) ask(ctx, s, shard, Command::fix); return; }
          s.phase = s.plan.program == Program::checked_sum ? Phase::checked_context : Phase::read; break;
        case Phase::checked_context:
          require(s.write_scopes.size() == 1 && s.read_scopes.size() == 1 && s.write_scopes.begin()->first == s.read_scopes.begin()->first, "checked fixture is shard local");
          ask(ctx, s, s.write_scopes.begin()->first, Command::register_context); return;
        case Phase::read:
          if (s.read.size() != s.read_scopes.size()) { for (const auto& [shard, scopes] : s.read_scopes) if (!s.read.contains(shard)) ask(ctx, s, shard, Command::read); return; }
          s.phase = Phase::compute; break;
        case Phase::verify: {
          Encoder e; put(e, s.plan); put(e, read_context(s.plan, s.cut));
          for (std::uint32_t copy = 0; copy < 2; ++copy) if (!s.reports.contains(checker(copy))) send(ctx, checker(copy), Wire::invoke, e.bytes);
          return;
        }
        case Phase::compute:
          if (!s.pending) { auto op = ctx.compute(2'000, compute_tag); computations[op] = tx; s.pending = true; } return;
        case Phase::verification: {
          Verification v{read_context(s.plan, s.cut), s.reports, s.commit};
          if (!s.pending) s.pending = records.save(ctx, key("verification", tx), encoded(v), "verification", ",\"tx\":" + std::to_string(tx) + ",\"match\":" + (s.commit ? "true" : "false"));
          return;
        }
        case Phase::decision:
          if (!s.pending) s.pending = records.save(ctx, key("decision", tx), encoded(s.outcome), "decision",
            ",\"tx\":" + std::to_string(tx) + ",\"position\":" + json(s.cut) + ",\"commit\":" + (s.outcome.commit ? "true" : "false") + ",\"checked\":" + (s.outcome.checked ? "true" : "false") + ",\"observed\":" + json(s.outcome.observed) + ",\"effects\":" + json(s.outcome.effects));
          return;
        case Phase::install:
          if (s.installed.size() != s.write_scopes.size()) { for (const auto& [shard, scopes] : s.write_scopes) if (!s.installed.contains(shard)) ask(ctx, s, shard, Command::resolve); return; }
          s.phase = Phase::done; break;
        case Phase::done: send(ctx, client, Wire::done, encoded(s.outcome)); return;
      }
    }
  }
  void restore(Context& ctx) {
    for (const auto& [k, bytes] : records.data) if (k.starts_with("input/")) { Decoder d{bytes}; auto p = plan(d); d.end(); auto s = make(p); s.phase = Phase::floor; states[p.id] = std::move(s); }
    for (auto& [tx, s] : states) {
      if (records.data.contains(key("position", tx))) { Decoder d{records.data.at(key("position", tx))}; s.cut = position(d); d.end(); s.phase = Phase::fix; }
      if (records.data.contains(key("verification", tx)) && !records.data.contains(key("decision", tx))) {
        Decoder vd{records.data.at(key("verification", tx))}; auto v = verification(vd); vd.end();
        require(v.context == read_context(s.plan, s.cut), "recovered verification context differs");
        require(v.match == verification_matches(v), "recovered verification verdict differs");
        const auto& representative = v.reports.begin()->second;
        s.outcome = Decision{tx, s.cut, v.match, true, representative.observed, v.match ? representative.effects : Values{}};
        s.phase = Phase::decision;
      }
      if (records.data.contains(key("decision", tx))) {
        Decoder d{records.data.at(key("decision", tx))}; s.outcome = decision(d); d.end();
        require(s.outcome.cut == s.cut, "recovered decision position differs");
        if (s.outcome.checked) {
          require(records.data.contains(key("verification", tx)), "recovered checked decision has no evidence"); Decoder vd{records.data.at(key("verification", tx))}; auto v = verification(vd); vd.end();
          require(v.context == read_context(s.plan, s.cut), "recovered verification context differs");
          if (s.outcome.commit) require(verification_matches(v) && s.outcome.observed == v.reports.begin()->second.observed && s.outcome.effects == v.reports.begin()->second.effects, "recovered checked outcome lacks matching evidence");
        }
        s.phase = Phase::install;
      }
      advance(ctx, s);
    }
    ctx.timer(config.retry_ns, tick_tag);
  }
  void receive(Context& ctx, const Event& e) override {
    bool ready = records.ready;
    if (records.consume(ctx, e)) {
      if (!ready && records.ready) restore(ctx);
      if (records.saved) {
        auto tx = suffix(records.saved->key); auto& s = states.at(tx); s.pending = false;
        if (!records.saved->ok) return;
        if (records.saved->key.starts_with("input/")) { s.phase = Phase::floor; ctx.note("orbital.accepted", "{\"tx\":" + std::to_string(tx) + '}'); }
        else if (records.saved->key.starts_with("position/")) s.phase = Phase::fix;
        else if (records.saved->key.starts_with("verification/")) s.phase = Phase::decision;
        else if (records.saved->key.starts_with("decision/")) s.phase = Phase::install;
        advance(ctx, s);
      }
      return;
    }
    if (!records.ready) return;
    if (e.kind == EventKind::timer && e.tag == tick_tag) { for (auto& [tx, s] : states) if (s.phase != Phase::done) advance(ctx, s); ctx.timer(config.retry_ns, tick_tag); return; }
    if (e.kind == EventKind::completion && e.tag == compute_tag && e.operation == Operation::compute) {
      auto tx = computations.at(e.id); computations.erase(e.id); auto& s = states.at(tx); s.pending = false; if (e.status != Status::ok) return;
      s.outcome = Decision{tx, s.cut, true, false, s.observed, execute(s.plan, s.observed)}; s.phase = Phase::decision; advance(ctx, s); return;
    }
    if (e.kind != EventKind::message || e.tag != wire_tag) return;
    Decoder d{e.bytes}; auto kind = static_cast<Wire>(d.number());
    if (kind == Wire::submit && e.from == client) {
      auto p = plan(d); d.end();
      if (states.contains(p.id)) { require(encoded(states.at(p.id).plan) == encoded(p), "transaction input identity changed"); if (states.at(p.id).phase == Phase::done) advance(ctx, states.at(p.id)); return; }
      auto buffer = ctx.allocate(Bytes(encoded(p).size() + 256, '\0')); if (!buffer) { ctx.note("orbital.pressure", "{\"phase\":\"coordinator-admission\"}"); return; }
      auto s = make(p); s.retained.push_back(*buffer); states[p.id] = std::move(s); advance(ctx, states.at(p.id));
    } else if (kind == Wire::response) {
      auto r = response(d); d.end(); require(e.from == consumer(r.request.shard, 0), "response not from prepared reply consumer");
      if (!states.contains(r.request.tx)) return; auto& s = states.at(r.request.tx); auto shard = r.request.shard;
      switch (r.request.command) {
        case Command::floor: if (s.phase != Phase::floor) return; s.floors[shard] = r.minimum; break;
        case Command::acquire: if (s.phase != Phase::acquire) return; s.acquired.insert(shard); break;
        case Command::announce: if (s.phase != Phase::announce) return; s.minima[shard] = r.minimum; break;
        case Command::fix: if (s.phase != Phase::fix) return; s.fixed.insert(shard); break;
        case Command::register_context: if (s.phase != Phase::checked_context) return; s.phase = Phase::verify; break;
        case Command::read: if (s.phase != Phase::read) return; s.read.insert(shard); s.observed.insert(r.observed.begin(), r.observed.end()); break;
        case Command::resolve: if (s.phase != Phase::install) return; s.installed.insert(shard); break;
      }
      advance(ctx, s);
    } else if (kind == Wire::report && (e.from == checker(0) || e.from == checker(1))) {
      auto r = report(d); d.end(); if (!states.contains(r.context.tx)) return; auto& s = states.at(r.context.tx);
      if (s.phase != Phase::verify || r.context != read_context(s.plan, s.cut)) return;
      if (!s.reports.contains(e.from)) { auto buffer = ctx.allocate(encoded(r)); if (!buffer) return; s.retained.push_back(*buffer); s.reports[e.from] = r; }
      if (s.reports.size() == 2 || config.negative == Negative::skip_verification) {
        Verification v{read_context(s.plan, s.cut), s.reports, false}; s.commit = verification_matches(v);
        if (config.negative == Negative::skip_verification) s.commit = true;
        auto representative = s.reports.begin()->second;
        s.outcome = Decision{s.plan.id, s.cut, s.commit, true, representative.observed, s.commit ? representative.effects : Values{}};
        s.phase = Phase::verification; advance(ctx, s);
      }
    }
  }
};

struct Checker final : Actor {
  struct Job { Transaction plan; ReadContext context; ActorId reply{}; Values observed; std::vector<Buffer> projection; bool delayed{}, computed{}, writing{}; };
  std::uint32_t copy;
  ActorOptions config;
  Records records;
  std::map<Tx, Job> jobs;
  std::map<Tx, Report> reports;
  std::map<OpId, Tx> computations;
  Checker(std::uint32_t c, ActorOptions cfg) : copy(c), config(std::move(cfg)), records(config.retry_ns) {}
  void advance(Context& ctx, Tx tx) {
    auto& j = jobs.at(tx);
    if (reports.contains(tx)) { send(ctx, j.reply, Wire::report, encoded(reports.at(tx))); return; }
    if (!j.delayed) { j.delayed = true; auto op = ctx.compute(copy == 0 ? 10'000 : config.checker_delay_ns, compute_tag); computations[op] = tx; return; }
    if (!j.computed) return;
    if (j.observed.size() != j.context.reads.size()) {
      for (auto k : j.context.reads) if (!j.observed.contains(k)) { Encoder query; put(query, j.context); query.number(k); send(ctx, consumer(shard_of(k), copy), Wire::query, query.bytes); }
      return;
    }
    auto observed = j.observed;
    if (copy == 1 && config.negative == Negative::corrupt_checker && !observed.empty()) observed.begin()->second = fixture::value(fixture::value(observed.begin()->second) + 1);
    Report r{j.context, observed, execute(j.plan, observed)};
    if (!j.writing) j.writing = records.save(ctx, key("report", tx), encoded(r), "report", ",\"tx\":" + std::to_string(tx) + ",\"checker\":" + std::to_string(ctx.self()) + ",\"observed\":" + json(observed));
  }
  void receive(Context& ctx, const Event& e) override {
    bool ready = records.ready;
    if (records.consume(ctx, e)) {
      if (!ready && records.ready) {
        for (const auto& [k, bytes] : records.data) if (k.starts_with("report/")) { Decoder d{bytes}; auto r = report(d); d.end(); reports[r.context.tx] = r; }
        ctx.timer(config.retry_ns, tick_tag);
      }
      if (records.saved) {
        auto tx = suffix(records.saved->key); auto& j = jobs.at(tx); j.writing = false;
        if (records.saved->ok) { Decoder d{records.saved->bytes}; auto r = report(d); d.end(); reports[tx] = r;
          note(ctx, "orbital.checked_report", encoded(r), ",\"tx\":" + std::to_string(tx) + ",\"checker\":" + std::to_string(ctx.self()) + ",\"observed\":" + json(r.observed));
          for (auto b : j.projection) ctx.release(b); j.projection.clear(); advance(ctx, tx); }
      }
      return;
    }
    if (!records.ready) return;
    if (e.kind == EventKind::timer && e.tag == tick_tag) { for (const auto& [tx, j] : jobs) if (!reports.contains(tx)) advance(ctx, tx); ctx.timer(config.retry_ns, tick_tag); return; }
    if (e.kind == EventKind::completion && e.operation == Operation::compute && e.tag == compute_tag) {
      auto tx = computations.at(e.id); computations.erase(e.id); auto& j = jobs.at(tx);
      if (e.status != Status::ok) { j.delayed = false; return; } j.computed = true; advance(ctx, tx); return;
    }
    if (e.kind != EventKind::message || e.tag != wire_tag) return;
    Decoder d{e.bytes}; auto kind = static_cast<Wire>(d.number());
    if (kind == Wire::invoke) {
      auto p = plan(d); auto c = context(d); d.end(); require(c == read_context(p, c.cut), "invocation changed declared read context");
      if (reports.contains(p.id)) { require(reports.at(p.id).context == c, "completed invocation identity changed"); send(ctx, e.from, Wire::report, encoded(reports.at(p.id))); return; }
      if (!jobs.contains(p.id)) {
        auto projection = ctx.allocate(encoded(c)); if (!projection) return;
        jobs[p.id] = Job{p, c, e.from, {}, {*projection}, false, false, false};
      } else require(jobs.at(p.id).context == c && jobs.at(p.id).reply == e.from, "invocation sender/context changed");
      advance(ctx, p.id);
    } else if (kind == Wire::query_result) {
      auto c = context(d); auto scope = static_cast<Scope>(d.number()); bool ok = d.number(); auto read = ok ? values(d) : Values{}; d.end();
      require(e.from == consumer(shard_of(scope), copy), "private result from wrong source replica");
      if (!jobs.contains(c.tx) || jobs.at(c.tx).context != c || reports.contains(c.tx)) return;
      auto& j = jobs.at(c.tx);
      if (ok && !j.observed.contains(scope)) {
        require(read.size() == 1 && read.contains(scope), "private response coverage changed"); auto b = ctx.allocate(read.at(scope)); if (!b) return;
        j.projection.push_back(*b); j.observed[scope] = read.at(scope); advance(ctx, c.tx);
      }
    }
  }
};

struct Client final : Actor {
  ActorOptions config;
  std::map<Tx, Transaction> active;
  std::map<Tx, Buffer> memory;
  std::set<Tx> complete;
  explicit Client(ActorOptions cfg) : config(std::move(cfg)) {}
  void receive(Context& ctx, const Event& e) override {
    if (e.kind == EventKind::boot) { ctx.timer(config.retry_ns, tick_tag); return; }
    if (e.kind == EventKind::timer && e.tag == tick_tag) { for (const auto& [tx, p] : active) send(ctx, coordinator(p.origin), Wire::submit, encoded(p)); ctx.timer(config.retry_ns, tick_tag); return; }
    if (e.kind != EventKind::message || e.tag != wire_tag) return;
    Decoder d{e.bytes}; auto kind = static_cast<Wire>(d.number());
    if (kind == Wire::submit) {
      auto p = plan(d); d.end(); if (active.contains(p.id) || complete.contains(p.id)) return;
      note(ctx, "orbital.offer", encoded(p), ",\"tx\":" + std::to_string(p.id) + ",\"cohort\":" + quote(p.cohort));
      auto buffer = ctx.allocate(encoded(p)); if (!buffer) { ctx.note("orbital.pressure", "{\"phase\":\"client-offer\",\"tx\":" + std::to_string(p.id) + '}'); return; }
      memory[p.id] = *buffer; active[p.id] = p; send(ctx, coordinator(p.origin), Wire::submit, encoded(p));
    } else if (kind == Wire::done) {
      auto result = decision(d); d.end();
      if (complete.contains(result.tx)) return;
      require(active.contains(result.tx) && e.from == coordinator(active.at(result.tx).origin), "unowned client completion");
      note(ctx, "orbital.response", encoded(result), ",\"tx\":" + std::to_string(result.tx) + ",\"commit\":" + (result.commit ? "true" : "false") + ",\"position\":" + json(result.cut) + ",\"effects\":" + json(result.effects));
      complete.insert(result.tx); active.erase(result.tx); ctx.release(memory.at(result.tx)); memory.erase(result.tx);
    }
  }
};

std::uint64_t field_number(std::string_view json, std::string_view field) {
  auto start = json.find('"' + std::string(field) + "\":");
  require(start != std::string_view::npos, "missing numeric note field"); start += field.size() + 3;
  std::uint64_t n{}; auto parsed = std::from_chars(json.data() + start, json.data() + json.size(), n);
  require(parsed.ec == std::errc{}, "invalid numeric note field"); return n;
}
std::string field_string(std::string_view json, std::string_view field) {
  auto start = json.find('"' + std::string(field) + "\":\"");
  require(start != std::string_view::npos, "missing string note field"); start += field.size() + 4;
  auto end = json.find('"', start); require(end != std::string_view::npos, "unterminated string note field");
  return std::string(json.substr(start, end - start));
}
std::optional<std::pair<std::uint32_t, std::uint32_t>> consumer_role(ActorId actor) {
  for (std::uint32_t s = 0; s < 2; ++s) for (std::uint32_t c = 0; c < 3; ++c)
    if (actor == consumer(s, c)) return std::pair{s, c};
  return {};
}
} // namespace

// The observer deliberately reconstructs certificates, pending obligations and
// application values from emitted bytes + real durable commits. It never calls
// Fold, inspects actor objects, or supplies admission/publication evidence.
struct Experiment::State {
  struct Write { ActorId actor; std::string key; Bytes bytes; };
  struct Obligation { Scopes writes; std::optional<Position> minimum, fixed; bool resolved{}; };
  struct View {
    std::map<Tx, Obligation> obligations;
    std::map<Scope, std::map<Position, Bytes>> versions;
    std::map<Tx, ReadContext> contexts;
    View() { for (const auto& [scope, bytes] : initial()) versions[scope][{}] = bytes; }
  };
  Case config;
  std::map<Tx, Transaction> authored;
  std::vector<HostId> hosts;
  std::map<ActorId, HostId> placement;
  std::map<OpId, Write> writes;
  std::map<std::pair<ActorId, std::string>, Bytes> durable;
  std::map<ActorId, View> views;
  std::map<std::pair<std::uint32_t, std::uint64_t>, std::pair<Bytes, Bytes>> fixpoints;
  std::map<std::pair<ActorId, std::uint64_t>, std::uint64_t> prefixes;
  std::map<Tx, Decision> decisions, responses;
  std::map<std::pair<ActorId, Tx>, Values> projections;
  std::map<Tx, Time> completed;
  std::set<Tx> accepted, arrived;
  bool incident_scheduled{};
  std::set<std::pair<Tx, std::uint32_t>> installed;
  std::set<std::string> errors, triggered;
  std::uint64_t wire{};
  explicit State(Case c) : config(std::move(c)) { for (auto p : transactions(config)) authored.emplace(p.id, std::move(p)); }
  void check(bool condition, std::string error) { if (!condition) errors.insert(std::move(error)); }
  const Bytes* stored(ActorId actor, std::string_view prefix, std::uint64_t id) const {
    auto it = durable.find({actor, key(prefix, id)}); return it == durable.end() ? nullptr : &it->second;
  }
  void snapshot(ActorId actor, Tx tx, Position cut, const Values& observed) {
    auto& view = views[actor];
    for (const auto& [other, obligation] : view.obligations) {
      if (other == tx || obligation.resolved || !obligation.minimum) continue;
      const auto lower = obligation.fixed.value_or(*obligation.minimum);
      bool overlap = std::ranges::any_of(observed, [&](const auto& kv) { return std::ranges::find(obligation.writes, kv.first) != obligation.writes.end(); });
      check(!overlap || cut < lower, "read " + std::to_string(tx) + " skipped pending output " + std::to_string(other));
    }
    for (const auto& [scope, bytes] : observed) {
      auto& history = view.versions[scope]; auto at = history.upper_bound(cut);
      check(at != history.begin(), "read lacks retained initial value");
      if (at != history.begin()) { --at; check(at->second == bytes, "snapshot value differs for " + std::to_string(tx) + "/" + std::to_string(scope)); }
    }
  }
  void committed(ActorId actor, const std::string& k, const Bytes& bytes) {
    if (k.starts_with("input/")) {
      Decoder d{bytes}; auto p = plan(d); d.end(); accepted.insert(p.id);
      check(authored.contains(p.id) && encoded(authored.at(p.id)) == bytes, "durable input differs from authored offer");
      check(actor == coordinator(p.origin), "input persisted by wrong coordinator");
    } else if (k.starts_with("decision/")) {
      Decoder d{bytes}; auto outcome = decision(d); d.end(); auto tx = outcome.tx;
      if (!authored.contains(tx)) { check(false, "decision without authored input"); return; }
      const auto& p = authored.at(tx);
      check(actor == coordinator(p.origin) && stored(actor, "input", tx), "decision without durable owned input");
      const auto* pos = stored(actor, "position", tx);
      check(pos && *pos == encoded(outcome.cut), "decision differs from durable position");
      check(!decisions.contains(tx) || encoded(decisions.at(tx)) == bytes, "transaction has conflicting outcomes");
      check(outcome.checked == (p.program == Program::checked_sum), "decision changed verification requirement");
      if (outcome.checked) {
        const auto* evidence = stored(actor, "verification", tx);
        check(evidence != nullptr, "checked decision lacks durable verification");
        if (evidence) {
          Decoder vd{*evidence}; auto v = verification(vd); vd.end();
          check(v.context == read_context(p, outcome.cut), "verification changed invocation context");
          const bool complete = v.reports.size() == 2 && v.reports.contains(checker(0)) && v.reports.contains(checker(1));
          bool equal = complete && encoded(v.reports.at(checker(0))) == encoded(v.reports.at(checker(1)));
          check(complete, "checked decision missing required checker evidence");
          check(v.match == equal && outcome.commit == equal, "verification verdict differs from required comparison");
          for (const auto& [who, r] : v.reports) {
            auto* report_bytes = stored(who, "report", tx);
            check(report_bytes && *report_bytes == encoded(r), "verification report lacks actual checker persistence");
            check(r.context == v.context, "report context differs from verification");
          }
          if (outcome.commit && complete) {
            for (const auto& [who, report] : v.reports) {
              check(projections.contains({who, tx}) && projections.at({who, tx}) == report.observed,
                    "committed checker output differs from observed private projection");
            }
            check(outcome.observed == v.reports.at(checker(0)).observed && outcome.effects == v.reports.at(checker(0)).effects, "committed output differs from verified report");
          }
        }
      }
      if (!outcome.commit) check(outcome.effects.empty(), "aborted transaction contains effects");
      else {
        Scopes observed; for (const auto& [scope, val] : outcome.observed) observed.push_back(scope);
        check(observed == p.reads, "decision observation coverage differs from program");
        Values expected;
        if (p.program == Program::put) for (auto scope : p.writes) expected[scope] = fixture::value(p.value);
        else if (p.program == Program::checked_sum && observed == p.reads) {
          std::int64_t total{}; bool overflow{};
          for (const auto& [scope, val] : outcome.observed) overflow |= __builtin_add_overflow(total, fixture::value(val), &total);
          check(!overflow, "oracle sum overflow"); expected[p.writes.at(0)] = fixture::value(total);
        } else if (p.program == Program::transfer && observed == p.reads) {
          auto from = fixture::value(outcome.observed.at(p.reads.at(0))), to = fixture::value(outcome.observed.at(p.reads.at(1)));
          if (from > 0 && to < std::numeric_limits<std::int64_t>::max()) { expected[p.writes[0]] = fixture::value(from - 1); expected[p.writes[1]] = fixture::value(to + 1); }
        }
        check(expected == outcome.effects, "decision arithmetic differs from independent fixture oracle");
      }
      decisions[tx] = std::move(outcome);
    }
  }
  void observe(const Record& r) {
    if (r.kind == "network.depart") wire += r.size;
    if (r.kind == "host.destroy") {
      const auto host = static_cast<HostId>(std::stoul(r.detail));
      std::erase_if(durable, [&](const auto& entry) { return placement.at(entry.first.first) == host; });
    }
    if (r.kind == "orbital.offer") { arrived.insert(field_number(r.detail, "tx")); }
    if (r.kind == "orbital.write") {
      writes[r.tag] = Write{r.actor, field_string(r.detail, "key"), noted_bytes(r.detail)};
    } else if (r.kind == "storage.write" && writes.contains(r.operation)) {
      const auto& w = writes.at(r.operation);
      check(w.actor == r.actor && w.key == r.detail && w.bytes.size() == r.size, "physical write differs from submitted model record");
      const auto dk = std::pair{r.actor, w.key};
      check(!durable.contains(dk) || durable.at(dk) == w.bytes, "immutable durable record changed");
      durable[dk] = w.bytes; committed(r.actor, w.key, w.bytes);
    } else if (r.kind == "orbital.recovered" && consumer_role(r.actor)) {
      views[r.actor] = View{};
    } else if (r.kind == "orbital.applied") {
      auto b = noted_bytes(r.detail); Decoder d{b}; auto command = request(d); d.end();
      auto replica = consumer_role(r.actor); check(replica.has_value(), "metadata applied outside consumer"); if (!replica) return;
      const auto [shard, copy] = *replica; const auto index = field_number(r.detail, "epoch");
      auto* proof = stored(r.actor, "epoch", index); check(proof != nullptr, "fold without actual durable epoch");
      if (proof) {
        Decoder pd{*proof}; auto e = epoch(pd); pd.end();
        check(e.batch.index == index && e.batch.request.shard == shard && encoded(e.batch.request) == b, "fold differs from retained epoch");
        check((e.voters & ~7U) == 0 && (e.voters & 1U) == 1U && ((e.voters & 2U) != 0 || (e.voters & 4U) != 0), "epoch lacks prepared leader and follower quorum");
        for (std::uint32_t bit = 0; bit < 3; ++bit) if (e.voters & (1U << bit)) {
          auto witness = bit == 0 ? leader(shard) : follower(shard, bit - 1);
          auto* actual = stored(witness, "log", index);
          check(actual && *actual == encoded(e.batch), "epoch certificate lacks actual matching witness persistence");
        }
      }
      auto& v = views[r.actor]; auto& obligation = v.obligations[command.tx];
      if (command.command == Command::acquire) obligation.writes = command.writes;
      if (command.command == Command::fix) {
        auto* pos = stored(command.reply, "position", command.tx);
        check(pos && *pos == encoded(command.cut), "fix without matching durable position");
        check(obligation.minimum && command.cut >= *obligation.minimum, "fixed position below inclusive minimum");
        check(!obligation.fixed || *obligation.fixed == command.cut, "immutable position changed"); obligation.fixed = command.cut;
      } else if (command.command == Command::register_context) {
        check(obligation.fixed == command.context.cut, "context before fixed output obligation"); v.contexts[command.tx] = command.context;
      } else if (command.command == Command::resolve) {
        check(decisions.contains(command.tx), "resolution without durable outcome");
        if (decisions.contains(command.tx)) {
          const auto& outcome = decisions.at(command.tx); Values expected;
          for (const auto& [scope, value] : outcome.effects) if (shard_of(scope) == shard) expected[scope] = value;
          check(expected == command.effects && outcome.cut == command.cut && obligation.fixed == command.cut, "resolution differs from durable outcome");
        }
        for (const auto& [scope, value] : command.effects) v.versions[scope][command.cut] = value;
        obligation.resolved = true; if (copy == 0) installed.emplace(command.tx, shard);
      }
    } else if (r.kind == "orbital.output") {
      auto b = noted_bytes(r.detail); Decoder d{b}; auto out = response(d); d.end(); auto& v = views[r.actor];
      if (out.request.command == Command::announce) v.obligations[out.request.tx].minimum = out.minimum;
      if (out.request.command == Command::read) snapshot(r.actor, out.request.tx, out.request.cut, out.observed);
    } else if (r.kind == "orbital.private_read") {
      auto b = noted_bytes(r.detail); Decoder d{b}; auto c = context(d); auto scope = static_cast<Scope>(d.number());
      auto who = d.number(); bool ready = d.number(); auto before = d.number(), after = d.number(); auto observed = ready ? values(d) : Values{}; d.end();
      check(before == after, "private query mutated agreed logical state");
      if (ready) {
        check(views[r.actor].contexts.contains(c.tx) && views[r.actor].contexts.at(c.tx) == c, "private query lacks agreed context");
        check((who == checker(0) || who == checker(1)) && observed.size() == 1 && observed.contains(scope) && std::ranges::find(c.reads, scope) != c.reads.end(), "private query escaped context coverage");
        snapshot(r.actor, c.tx, c.cut, observed);
        projections[{static_cast<ActorId>(who), c.tx}].insert(observed.begin(), observed.end());
      }
    } else if (r.kind == "orbital.fixpoint") {
      auto b = noted_bytes(r.detail); Decoder d{b}; auto shard = static_cast<std::uint32_t>(d.number()); auto copy = d.number(); auto index = d.number(); auto logical = d.text(), output = d.text(); d.end();
      check(r.actor == consumer(shard, static_cast<std::uint32_t>(copy)), "fixpoint role mismatch");
      auto& prefix = prefixes[{r.actor, r.incarnation}]; check(index == prefix + 1, "consumer skipped logical prefix"); prefix = index;
      auto id = std::pair{shard, index};
      if (fixpoints.contains(id)) check(fixpoints.at(id) == std::pair{logical, output}, "same agreed prefix produced different full state or outputs");
      else fixpoints[id] = {std::move(logical), std::move(output)};
    } else if (r.kind == "orbital.response") {
      auto b = noted_bytes(r.detail); Decoder d{b}; auto outcome = decision(d); d.end();
      check(r.actor == client, "completion outside client");
      check(decisions.contains(outcome.tx) && encoded(decisions.at(outcome.tx)) == b, "client completion lacks matching durable outcome");
      if (authored.contains(outcome.tx)) for (auto scope : authored.at(outcome.tx).writes)
        check(installed.contains({outcome.tx, shard_of(scope)}), "client completion before all output shards resolved");
      else check(false, "client completed unknown offer");
      check(!responses.contains(outcome.tx), "client completed offer twice"); responses[outcome.tx] = outcome; completed[outcome.tx] = r.time;
    }
  }
};

std::vector<ActorId> roles() {
  std::vector<ActorId> ids{client, checker(0), checker(1)};
  for (std::uint32_t s = 0; s < 2; ++s) { ids.push_back(coordinator(s)); ids.push_back(leader(s));
    for (std::uint32_t c = 0; c < 2; ++c) ids.push_back(follower(s, c));
    for (std::uint32_t c = 0; c < 3; ++c) ids.push_back(consumer(s, c)); }
  std::ranges::sort(ids); return ids;
}
std::vector<Transaction> transactions(const Case& c) {
  if (!c.transactions.empty()) return c.transactions;
  std::vector<Transaction> out{{1, 100'000, 0, Program::checked_sum, {cell(0, 0), cell(0, 1), cell(0, 2), cell(0, 3)}, {cell(0, 99)}, 0, "checked"}};
  for (std::uint32_t i = 0; i < c.point_writes; ++i) { auto s = i % 2;
    out.push_back(Transaction{2 + i, c.point_start_ns + c.interval_ns * i, s, Program::put, {}, {cell(s, (i / 2) % 4)}, 1000 + i, "point"}); }
  return out;
}
std::string_view name(Incident i) { constexpr std::array names{"none", "one-follower", "quorum-pause", "consumer-reset", "coordinator-reset", "checker-reset"}; return names.at(static_cast<unsigned>(i)); }
std::string_view name(QueuePolicy p) { constexpr std::array names{"no-overtaking", "eligible-first", "older-conflicts-drain", "oldest-live"}; return names.at(static_cast<unsigned>(p)); }
std::string_view name(Negative n) { constexpr std::array names{"none", "skip-pending", "skip-verification", "corrupt-checker"}; return names.at(static_cast<unsigned>(n)); }
Experiment::Experiment(std::shared_ptr<State> state) : state_(std::move(state)) {}
Experiment::~Experiment() = default;
Experiment::Experiment(Experiment&&) noexcept = default;
Experiment& Experiment::operator=(Experiment&&) noexcept = default;

Experiment assemble(Simulation& simulation, const Case& config, std::ostream* trace) {
  require(config.queue_policy == QueuePolicy::no_overtaking || config.queue_policy == QueuePolicy::eligible_first || config.queue_policy == QueuePolicy::older_conflicts_drain || config.queue_policy == QueuePolicy::oldest_live, "unknown reservation queue policy");
  require(config.retry_ns > 0 && config.until_ns > 0, "positive retry and observation horizon required");
  auto ids = roles(); auto state = std::make_shared<Experiment::State>(config);
  std::set<ActorId> valid(ids.begin(), ids.end());
  for (const auto& [role, host] : config.placement) require(valid.contains(role), "unknown placement role");
  std::map<HostId, Host> hosts;
  if (config.hosts.empty()) {
    for (auto id : ids) { Host host; host.id = id; host.memory_bytes = config.memory_bytes; host.storage_bytes = config.storage_bytes;
      host.disk.latency_ns = config.disk_ns; hosts[id] = host; }
  } else for (const auto& host : config.hosts) require(hosts.emplace(host.id, host).second, "duplicate physical host");
  for (auto id : ids) { auto host = config.placement.contains(id) ? config.placement.at(id) : id; require(hosts.contains(host), "placement references unknown physical host"); state->placement[id] = host; }
  std::set<std::pair<HostId, HostId>> edges;
  std::vector<Link> links;
  if (config.links) links = *config.links;
  else for (const auto& [a, ah] : hosts) for (const auto& [b, bh] : hosts) if (a != b) { Link link; link.from = a; link.to = b; link.propagation_ns = config.link_ns; links.push_back(link); }
  for (const auto& link : links) { require(hosts.contains(link.from) && hosts.contains(link.to), "link references unknown physical host"); require(link.from != link.to, "same-host links are supplied by host services"); require(edges.emplace(link.from, link.to).second, "duplicate directed link"); }
  for (std::uint32_t s = 0; s < 2; ++s) {
    std::set<HostId> domains{state->placement.at(leader(s)), state->placement.at(follower(s, 0)), state->placement.at(follower(s, 1))};
    require(domains.size() == 3, "prepared witness replicas require distinct host failure domains");
  }
  std::set<Tx> txids;
  for (const auto& p : transactions(config)) {
    require(p.id > 0 && p.id < (1ULL << 48) && txids.insert(p.id).second, "duplicate/out-of-range transaction identity");
    require(p.origin < 2 && !p.writes.empty(), "invalid transaction origin, output coverage or arrival");
    require(p.reads.size() <= 8 && p.writes.size() <= 8 && p.cohort.size() <= 128, "fixture declaration exceeds bounded schema");
    require(p.value >= -1'000'000'000 && p.value <= 1'000'000'000, "fixture value exceeds arithmetic envelope");
    for (const auto* scopes : {&p.reads, &p.writes}) {
      require(std::ranges::is_sorted(*scopes) && std::adjacent_find(scopes->begin(), scopes->end()) == scopes->end(), "scope coverage must be sorted and unique");
      for (auto scope : *scopes) require(initial().contains(scope), "scope outside application fixture");
    }
    if (p.program == Program::put) require(p.reads.empty(), "put fixture does not read");
    if (p.program == Program::transfer) require(p.reads.size() == 2 && p.reads == p.writes, "transfer requires two matching scopes");
    if (p.program == Program::checked_sum) require(!p.reads.empty() && p.writes.size() == 1 && grouped(p.reads).size() == 1 && shard_of(p.writes[0]) == shard_of(p.reads[0]), "checked fixture must be shard local with one output");
  }
  for (const auto& [id, host] : hosts) { simulation.add_host(host); state->hosts.push_back(id); }
  for (const auto& link : links) simulation.add_link(link);
  for (auto id : ids) simulation.add_process(id, state->placement.at(id));
  const ActorOptions options{config.retry_ns, config.checker_delay_ns, config.negative, config.queue_policy};
  simulation.add_actor(client, client, [options] { return std::make_unique<Client>(options); });
  for (std::uint32_t s = 0; s < 2; ++s) {
    simulation.add_actor(coordinator(s), coordinator(s), [options] { return std::make_unique<Coordinator>(options); });
    simulation.add_actor(leader(s), leader(s), [options, s] { return std::make_unique<Witness>(s, 0, options); });
    for (std::uint32_t i = 0; i < 2; ++i) simulation.add_actor(follower(s, i), follower(s, i), [options, s, i] { return std::make_unique<Witness>(s, i + 1, options); });
    for (std::uint32_t i = 0; i < 3; ++i) simulation.add_actor(consumer(s, i), consumer(s, i), [options, s, i] { return std::make_unique<Consumer>(s, i, options); });
  }
  for (std::uint32_t i = 0; i < 2; ++i) simulation.add_actor(checker(i), checker(i), [options, i] { return std::make_unique<Checker>(i, options); });
  simulation.observe([state, trace](const Record& r) { state->observe(r); if (trace) sim::write_json(*trace, r); });
  for (const auto& p : transactions(config)) simulation.inject(p.at, client, wire_tag, message(Wire::submit, encoded(p)));
  const auto incident_name = std::string(name(config.incident));
  if (config.incident == Incident::one_follower || config.incident == Incident::quorum_pause) {
    const unsigned count = config.incident == Incident::one_follower ? 1 : 2;
    simulation.at(0, incident_name, [state, count, incident_name](Simulation& sim) {
      for (unsigned i = 0; i < count; ++i) sim.set_link(state->placement.at(leader(0)), state->placement.at(follower(0, i)), false);
      state->triggered.insert(incident_name);
    });
    if (config.incident == Incident::quorum_pause) simulation.at(600'000, "quorum-heal", [state](Simulation& sim) {
      for (unsigned i = 0; i < 2; ++i) sim.set_link(state->placement.at(leader(0)), state->placement.at(follower(0, i)), true);
    });
  } else if (config.incident != Incident::none) {
    // Semantic cut schedules a fault at the actual device persistence time.
    // FIFO cuts before the callback; seeded ties may deliver that callback first.
    // Observation and actual incident receipts exist without trace output.
    simulation.observe([state, &simulation, incident_name](const Record& r) {
      if (state->incident_scheduled) return;
      bool hit = false; ActorId victim{};
      if (state->config.incident == Incident::coordinator_reset) { victim = coordinator(0); hit = r.kind == "storage.write" && r.actor == victim && r.detail.starts_with("decision/"); }
      if (state->config.incident == Incident::consumer_reset) { victim = consumer(0, 0); hit = r.kind == "orbital.fixpoint" && r.actor == victim; }
      if (state->config.incident == Incident::checker_reset) { victim = checker(1); hit = r.kind == "orbital.private_read" && field_number(r.detail, "tx") == 1 && r.actor == consumer(0, 1); }
      if (!hit) return;
      state->incident_scheduled = true; const auto host = state->placement.at(victim);
      simulation.at(r.time, incident_name, [state, host, incident_name, destroy = state->config.incident == Incident::consumer_reset](Simulation& sim) { sim.power_loss(host, destroy); state->triggered.insert(incident_name); });
      simulation.at(r.time + 300'000, incident_name + "-return", [state, host](Simulation& sim) {
        for (const auto& [role, placement] : state->placement) if (placement == host) sim.restart(role);
      });
    });
  }
  return Experiment(state);
}

Result Experiment::result(const Simulation& simulation, Run execution) const {
  Result out; out.name = state_->config.name; out.queue_policy = state_->config.queue_policy; out.execution = execution; out.observed_until_ns = simulation.now();
  out.violations.assign(state_->errors.begin(), state_->errors.end()); out.triggered_incidents.assign(state_->triggered.begin(), state_->triggered.end());
  if (state_->config.incident != Incident::none && !state_->triggered.contains(std::string(name(state_->config.incident)))) out.missing_incidents.emplace_back(name(state_->config.incident));
  for (const auto& [tx, p] : state_->authored) {
    auto& cohort = out.cohorts[p.cohort]; ++cohort.offered;
    if (state_->arrived.contains(tx)) ++cohort.arrived; else ++cohort.not_yet_offered;
    if (state_->responses.contains(tx)) { const auto& outcome = state_->responses.at(tx); if (outcome.commit) ++cohort.completed; else ++cohort.failed;
      cohort.max_latency_ns = std::max(cohort.max_latency_ns, state_->completed.at(tx) - p.at); }
    else ++cohort.unfinished;
  }
  out.completed_at = state_->completed; out.accepted = state_->accepted.size();
  for (auto tx : state_->accepted) if (!state_->responses.contains(tx)) ++out.admitted_unfinished;
  std::map<Scope, std::pair<Position, Bytes>> last;
  for (const auto& [scope, value] : initial()) last[scope] = {{}, value};
  for (const auto& [tx, outcome] : state_->decisions) for (const auto& [scope, value] : outcome.effects)
    if (state_->installed.contains({tx, shard_of(scope)}) && last[scope].first < outcome.cut) last[scope] = {outcome.cut, value};
  for (const auto& [scope, positioned] : last) out.latest_values[scope] = fixture::value(positioned.second);
  for (auto host : state_->hosts) { auto usage = simulation.usage(host); out.durable_bytes += usage.durable; out.memory_peak += usage.memory_peak; }
  out.wire_bytes = state_->wire; out.records = simulation.records(); out.trace_hash = simulation.trace_hash(); return out;
}
Result run_case(const Case& config, Options options, std::ostream* trace) {
  Simulation sim(options); auto experiment = assemble(sim, config, trace); sim.start(); auto execution = sim.run(config.until_ns, config.max_events); sim.finish_replay(); return experiment.result(sim, execution);
}
void write_json(std::ostream& out, const Result& r) {
  auto strings = [&](const std::vector<std::string>& list) { out << '['; bool comma{}; for (const auto& s : list) { if (comma) out << ','; comma = true; out << quote(s); } out << ']'; };
  out << "{\"name\":" << quote(r.name) << ",\"queue_policy\":" << quote(name(r.queue_policy)) << ",\"cohorts\":{"; bool comma{};
  for (const auto& [name, c] : r.cohorts) { if (comma) out << ','; comma = true; out << quote(name) << ":{\"offered\":" << c.offered << ",\"arrived\":" << c.arrived << ",\"not_yet_offered\":" << c.not_yet_offered << ",\"completed\":" << c.completed << ",\"failed\":" << c.failed << ",\"unfinished\":" << c.unfinished << ",\"max_latency_ns\":" << c.max_latency_ns << '}'; }
  out << "},\"violations\":"; strings(r.violations); out << ",\"triggered_incidents\":"; strings(r.triggered_incidents); out << ",\"missing_incidents\":"; strings(r.missing_incidents);
  out << ",\"completed_at\":{"; comma = false; for (const auto& [tx, time] : r.completed_at) { if (comma) out << ','; comma = true; out << quote(std::to_string(tx)) << ':' << time; }
  out << "},\"latest_values\":{"; comma = false; for (const auto& [scope, value] : r.latest_values) { if (comma) out << ','; comma = true; out << quote(std::to_string(scope)) << ':' << value; }
  out << "},\"wire_bytes\":" << r.wire_bytes << ",\"durable_bytes\":" << r.durable_bytes << ",\"memory_peak\":" << r.memory_peak << ",\"records\":" << r.records << ",\"trace_hash\":" << r.trace_hash << ",\"accepted\":" << r.accepted << ",\"admitted_unfinished\":" << r.admitted_unfinished;
  out << ",\"observed_until_ns\":" << r.observed_until_ns;
  out << ",\"execution\":{\"events\":" << r.execution.events << ",\"budget_exhausted\":" << (r.execution.budget_exhausted ? "true" : "false") << ",\"pending\":" << (r.execution.pending ? "true" : "false") << ",\"paused\":" << (r.execution.paused ? "true" : "false") << "}}\n";
}
} // namespace sixdb::sim::orbital
