#include <sixdb/sim/runtime.hpp>

#include <algorithm>
#include <charconv>
#include <deque>
#include <limits>
#include <map>
#include <ostream>
#include <istream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace sixdb::sim {
namespace {
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
Time add(Time a, Time b) {
  if (b > maximum - a) throw std::overflow_error("modeled time overflow");
  return a + b;
}
std::uint64_t hash(std::string_view bytes, std::uint64_t h = 14695981039346656037ULL) {
  for (unsigned char byte : bytes) h = (h ^ byte) * 1099511628211ULL;
  return h;
}
std::uint64_t mix(std::uint64_t v) {
  v += 0x9e3779b97f4a7c15ULL;
  v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
  v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
  return v ^ (v >> 31);
}
std::string fingerprint(const Event& e) {
  std::string out = std::to_string(static_cast<int>(e.kind)) + ":" +
      std::to_string(static_cast<int>(e.operation)) + ":" +
      std::to_string(static_cast<int>(e.status)) + ":" + std::to_string(e.id) + ":" +
      std::to_string(e.from) + ":" + std::to_string(e.tag) + ":" +
      std::to_string(e.bytes.size()) + ":" + e.bytes;
  for (const auto& key : e.keys) out += ":" + std::to_string(key.size()) + ":" + key;
  return out + ":" + e.next;
}
void quote(std::ostream& out, std::string_view value) {
  constexpr char hex[] = "0123456789abcdef";
  out << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
    else if (c < 32 || c >= 127) out << "\\u00" << hex[c >> 4] << hex[c & 15];
    else out << static_cast<char>(c);
  }
  out << '"';
}
void validate(Service service) {
  if (!service.bytes_per_second || !service.queue_bytes)
    throw std::invalid_argument("service rate and queue capacity must be positive");
}
} // namespace

struct Context::Access {
  Simulation::Impl* impl;
  ActorId actor;
  std::uint64_t incarnation;
};

struct Simulation::Impl {
  struct Resource { Service config; Time ready{}; std::uint64_t queued{}; };
  struct CpuJob { ActorId actor; std::uint64_t generation; OpId id; std::uint64_t tag; Time duration, started{}; std::uint64_t event{}; };
  struct Machine {
    Host config;
    Usage usage;
    Resource cpu, tx, rx, disk;
    std::uint64_t device_generation{1};
    std::map<std::pair<ActorId, std::string>, Bytes> storage;
    std::map<std::pair<ActorId, std::string>, std::uint64_t> origins;
    std::deque<std::shared_ptr<CpuJob>> cpu_jobs;
  };
  struct Process { HostId host; bool alive{}; std::uint64_t generation{}; };
  struct Role { ProcessId process; Factory factory; std::unique_ptr<Actor> actor; std::uint64_t last{}; };
  struct Edge { Link config; Resource resource; bool available{true}; };
  struct Memory { HostId host; ActorId actor; std::uint64_t generation; Bytes bytes; bool owned{true}; std::uint64_t borrowers{}; };
  using Key = std::tuple<Time, std::uint64_t, std::uint64_t>;
  struct Scheduled { std::uint64_t id, cause, fingerprint; std::function<void()> action; };
  struct DiskJob { HostId host; std::uint64_t generation; std::function<void()> cancel; bool active{true}; std::uint64_t event{}; };
  struct Delivery { ProcessId process; std::uint64_t generation; std::function<void()> cleanup; bool active{true}; std::uint64_t event{}; };

  explicit Impl(Options opts) : options(opts) {}
  Options options;
  Simulation* owner{};
  Time time{};
  std::uint64_t serial{}, operation{}, buffer_serial{}, record_serial{}, cause{}, service_serial{};
  std::uint64_t digest{14695981039346656037ULL};
  bool started{}, delivering{}, notifying{}, paused{}, running{};
  std::map<HostId, Machine> hosts;
  std::map<ProcessId, Process> processes;
  std::map<ActorId, Role> actors;
  std::map<std::pair<HostId, HostId>, Edge> links;
  std::map<Buffer, Memory> buffers;
  std::map<Key, Scheduled> queue;
  std::map<std::uint64_t, Key> event_keys;
  std::map<OpId, std::shared_ptr<DiskJob>> disk_jobs;
  std::map<std::uint64_t, std::shared_ptr<DiskJob>> service_jobs;
  std::map<std::uint64_t, std::shared_ptr<Delivery>> deliveries;
  std::map<std::uint64_t, std::shared_ptr<Delivery>> timers;
  std::vector<std::function<void(const Record&)>> observers;

  Process& process(ActorId actor) { return processes.at(actors.at(actor).process); }
  Machine& machine(ActorId actor) { return hosts.at(process(actor).host); }
  std::uint64_t emit(std::string kind, ActorId actor = 0, std::uint64_t op = 0,
      std::uint64_t tag = 0, std::uint64_t size = 0, std::string detail = {},
      ActorId peer = 0, std::vector<std::uint64_t> parents = {}, std::optional<std::uint64_t> origin = {}) {
    Record r;
    r.id = ++record_serial; r.cause = cause; r.time = time; r.kind = std::move(kind);
    r.actor = actor; r.operation = op; r.tag = tag; r.size = size; r.detail = std::move(detail);
    r.peer = peer; r.parents = std::move(parents);
    if (actor) {
      r.process = actors.at(actor).process;
      const auto& p = processes.at(r.process);
      r.host = p.host; r.incarnation = origin.value_or(p.generation);
    }
    // Hash compact fields even with no trace sink. It is an accidental-divergence
    // fingerprint, not a cryptographic verification contract.
    digest = hash(r.kind, digest);
    for (auto n : {r.id,r.cause,r.time,std::uint64_t(r.actor),std::uint64_t(r.peer),
                  std::uint64_t(r.host),std::uint64_t(r.process),r.incarnation,r.operation,r.tag,r.size}) digest = mix(digest ^ n);
    digest = hash(r.detail, digest);
    for (auto p : r.parents) digest = mix(digest ^ p);
    notifying = true;
    try { for (auto& observer : observers) observer(r); }
    catch (...) { notifying = false; throw; }
    notifying = false;
    return r.id;
  }
  void transcript(char kind, Time at, std::uint64_t id, std::uint64_t fp) {
    if (options.decisions_in) {
      char actual{}; Time t{}; std::uint64_t i{}, f{};
      if (!(*options.decisions_in >> actual >> t >> i >> f) ||
          actual != kind || t != at || i != id || f != fp)
        throw std::runtime_error("exact replay diverged at " + std::to_string(id));
    }
    if (options.decisions_out) {
      *options.decisions_out << kind << ' ' << at << ' ' << id << ' ' << fp << '\n';
      if (!*options.decisions_out) throw std::runtime_error("failed to write choice transcript");
    }
  }
  std::uint64_t schedule(Time at, std::string label, std::function<void()> action) {
    if (at < time) throw std::invalid_argument("cannot schedule in the past");
    auto id = ++serial;
    auto fp = hash(label);
    transcript('S', at, id, fp);
    auto priority = options.ordering == Ordering::fifo ? id : mix(options.seed ^ id);
    Key key{at, priority, id};
    queue.emplace(key, Scheduled{id, cause, fp, std::move(action)});
    event_keys.emplace(id, key);
    return id;
  }
  void cancel_event(std::uint64_t id) {
    auto key = event_keys.find(id);
    if (key == event_keys.end()) return;
    auto event = queue.find(key->second);
    transcript('C',time,id,event->second.fingerprint);
    queue.erase(event); event_keys.erase(key);
    emit("event.cancel",0,id);
  }
  bool hold(HostId host, std::uint64_t bytes) {
    auto& m = hosts.at(host);
    if (bytes > m.config.memory_bytes - m.usage.memory) return false;
    m.usage.memory += bytes;
    m.usage.memory_peak = std::max(m.usage.memory_peak, m.usage.memory);
    return true;
  }
  void unhold(HostId host, std::uint64_t bytes) {
    auto& u = hosts.at(host).usage;
    if (bytes > u.memory) throw std::logic_error("memory conservation failure");
    u.memory -= bytes;
  }
  std::optional<Buffer> allocate(ActorId actor, Bytes bytes, bool count_refusal = true) {
    auto& p = process(actor);
    if (!hold(p.host, bytes.size())) { if (count_refusal) ++hosts.at(p.host).usage.refused; return {}; }
    auto id = ++buffer_serial;
    auto size = bytes.size();
    buffers.emplace(id, Memory{p.host, actor, p.generation, std::move(bytes), true, 0});
    emit("buffer.allocate", actor, id, 0, size);
    return id;
  }
  Memory& owned(ActorId actor, Buffer id) {
    auto& b = buffers.at(id);
    if (!b.owned || b.actor != actor || b.generation != process(actor).generation)
      throw std::logic_error("buffer is not owned by this actor incarnation");
    return b;
  }
  void retire(Buffer id) {
    auto it = buffers.find(id);
    if (it == buffers.end()) throw std::logic_error("buffer retired twice");
    auto& b = it->second;
    if (!b.owned && !b.borrowers) {
      unhold(b.host, b.bytes.size());
      emit("buffer.retire", b.actor, id, 0, b.bytes.size(), {}, 0, {}, b.generation);
      buffers.erase(it);
    }
  }
  void release(ActorId actor, Buffer id) { owned(actor, id).owned = false; retire(id); }
  void borrow(ActorId actor, Buffer id) { ++owned(actor, id).borrowers; }
  void return_borrow(Buffer id) {
    auto& b = buffers.at(id);
    if (!b.borrowers) throw std::logic_error("buffer borrow underflow");
    --b.borrowers; retire(id);
  }
  std::optional<Time> reserve(Resource& r, std::uint64_t bytes, std::optional<Time> duration = {}) {
    bytes = std::max<std::uint64_t>(1, bytes);
    if (bytes > r.config.queue_bytes - r.queued) return {};
    Time service = duration.value_or(0);
    if (!duration) {
      // Split quotient/remainder to avoid overflow in bytes * 1e9.
      auto whole = bytes / r.config.bytes_per_second;
      auto remainder = bytes % r.config.bytes_per_second;
      if (whole > maximum / 1'000'000'000 || remainder > maximum / 1'000'000'000)
        throw std::overflow_error("service time overflow");
      service = add(r.config.latency_ns, add(whole * 1'000'000'000,
          (remainder * 1'000'000'000) / r.config.bytes_per_second +
          ((remainder * 1'000'000'000) % r.config.bytes_per_second != 0)));
    }
    r.queued += bytes;
    r.ready = add(std::max(time, r.ready), service);
    return r.ready;
  }
  void unqueue(Resource& r, std::uint64_t bytes) {
    bytes = std::max<std::uint64_t>(1, bytes);
    if (r.queued < bytes) throw std::logic_error("resource queue conservation failure");
    r.queued -= bytes;
  }
  void deliver(ActorId actor, std::uint64_t generation, Event event,
               std::function<void()> cleanup = {}) {
    // Once a message reaches the local endpoint its resident buffer belongs to
    // that incarnation, even if dispatch is interrupted by a crash.
    if (!generation) generation = process(actor).generation;
    auto delivery = std::make_shared<Delivery>(Delivery{actors.at(actor).process,generation,std::move(cleanup),true});
    auto delivery_id = ++service_serial; deliveries.emplace(delivery_id, delivery);
    auto label = "deliver:" + std::to_string(actor) + ":" + std::to_string(generation) + ":" + fingerprint(event);
    delivery->event = schedule(time, std::move(label), [this, actor, generation, e = std::move(event), delivery, delivery_id] {
      if (!delivery->active) return;
      delivery->active = false; deliveries.erase(delivery_id);
      auto cleanup = std::move(delivery->cleanup);
      auto& role = actors.at(actor); auto& p = process(actor);
      if (!p.alive || (generation && generation != p.generation)) {
        emit("callback.fenced", actor, e.id, e.tag, e.bytes.size(), {}, 0, {}, generation);
        if (cleanup) cleanup();
        return;
      }
      auto id = emit("actor.receive", actor, e.id, e.tag, e.bytes.size(),
          std::to_string(static_cast<int>(e.kind)), e.from, role.last ? std::vector{role.last} : std::vector<std::uint64_t>{});
      role.last = id;
      auto previous = cause; cause = id;
      Context::Access access{this, actor, p.generation};
      Context context(&access);
      delivering = true;
      try { role.actor->receive(context, e); }
      catch (...) { delivering = false; cause = previous; if (cleanup) cleanup(); throw; }
      delivering = false; cause = previous;
      if (cleanup) cleanup();
    });
  }
  void completion(ActorId actor, std::uint64_t generation, OpId id, Operation operation,
      std::uint64_t tag, Status status, Bytes bytes = {}, std::vector<std::string> keys = {},
      std::string next = {}, std::function<void()> cleanup = {}) {
    Event e; e.kind = EventKind::completion; e.operation = operation; e.status = status;
    e.id = id; e.tag = tag; e.bytes = std::move(bytes); e.keys = std::move(keys); e.next = std::move(next);
    deliver(actor, generation, std::move(e), std::move(cleanup));
  }
  OpId refuse(ActorId actor, Operation op, std::uint64_t tag, Status status) {
    auto id = ++operation;
    ++machine(actor).usage.refused;
    emit("operation.refused", actor, id, tag, 0, std::string(name(status)));
    completion(actor, process(actor).generation, id, op, tag, status);
    return id;
  }
  OpId submit(ActorId actor, Operation op, std::uint64_t tag, std::uint64_t size) {
    auto id = ++operation;
    emit("operation.submit", actor, id, tag, size, std::string(name(op)));
    return id;
  }
  void pump_cpu(HostId host) {
    auto& m = hosts.at(host);
    if (m.cpu_jobs.empty() || m.cpu_jobs.front()->event) return;
    auto job = m.cpu_jobs.front(); job->started = time;
    job->event = schedule(add(time,job->duration), "compute:" + std::to_string(job->id), [this,host,job] {
      auto& machine = hosts.at(host);
      if (machine.cpu_jobs.empty() || machine.cpu_jobs.front() != job) throw std::logic_error("CPU queue order diverged");
      machine.cpu_jobs.pop_front(); unqueue(machine.cpu,1);
      emit("compute.complete",job->actor,job->id,job->tag,job->duration,{},0,{},job->generation);
      completion(job->actor,job->generation,job->id,Operation::compute,job->tag,Status::ok);
      pump_cpu(host);
    });
  }
  void cancel_cpu(ProcessId process_id) {
    auto host = processes.at(process_id).host;
    auto& m = hosts.at(host);
    for (auto it = m.cpu_jobs.begin(); it != m.cpu_jobs.end();) {
      auto job = *it;
      if (actors.at(job->actor).process != process_id) { ++it; continue; }
      if (job->event) cancel_event(job->event);
      auto elapsed = job->event ? time - job->started : 0;
      emit("compute.cancelled",job->actor,job->id,job->tag,elapsed,{},0,{},job->generation);
      unqueue(m.cpu,1); it = m.cpu_jobs.erase(it);
    }
    pump_cpu(host);
  }
  void arrive(ActorId from, ActorId to, std::uint64_t tag, Bytes bytes, OpId id) {
    auto& m = machine(to); auto host = m.config.id;
    auto size = bytes.size();
    if (!hold(host, size)) { ++m.usage.dropped; emit("network.drop", from, id, tag, size, "receive memory", to); return; }
    auto end = reserve(m.rx, size);
    if (!end) { unhold(host, size); ++m.usage.dropped; emit("network.drop", from, id, tag, size, "receive queue", to); return; }
    auto job = std::make_shared<DiskJob>(); job->host = host;
    job->cancel = [this, host, size, to, id, tag] {
      unhold(host, size); ++hosts.at(host).usage.dropped;
      emit("network.drop", to, id, tag, size, "receive reset");
    };
    auto service_id = ++service_serial; service_jobs.emplace(service_id, job);
    auto label = "receive:" + std::to_string(id) + ":" + bytes;
    job->event = schedule(*end, std::move(label), [this, from, to, tag, bytes = std::move(bytes), id, size, host, job, service_id] {
      if (!job->active) return;
      job->active = false; service_jobs.erase(service_id);
      auto& target = hosts.at(host); unqueue(target.rx, size); target.usage.received += size;
      auto prior = cause; cause = emit("network.arrive", to, id, tag, size, {}, from);
      Event e; e.kind = EventKind::message; e.from = from; e.id = id; e.tag = tag; e.bytes = bytes;
      deliver(to, 0, std::move(e), [this, host, size] { unhold(host, size); });
      cause = prior;
    });
  }
  OpId send(ActorId actor, ActorId to, std::uint64_t tag, Buffer buffer) {
    auto& b = owned(actor, buffer);
    if (!actors.contains(to)) return refuse(actor, Operation::send, tag, Status::unreachable);
    auto source = process(actor).host, target = process(to).host;
    if (source != target && !links.contains({source,target})) return refuse(actor, Operation::send, tag, Status::unreachable);
    auto size = b.bytes.size(); auto end = reserve(hosts.at(source).tx, size);
    if (!end) return refuse(actor, Operation::send, tag, Status::capacity);
    borrow(actor, buffer);
    auto generation = process(actor).generation;
    auto id = submit(actor, Operation::send, tag, size);
    auto job = std::make_shared<DiskJob>(); job->host = source;
    job->cancel = [this, actor, generation, id, tag, size, source, buffer] {
      return_borrow(buffer); ++hosts.at(source).usage.dropped;
      emit("network.drop", actor, id, tag, size, "transmit reset");
      completion(actor, generation, id, Operation::send, tag, Status::cancelled);
    };
    auto service_id = ++service_serial; service_jobs.emplace(service_id, job);
    job->event = schedule(*end, "send:" + std::to_string(actor) + ":" + std::to_string(to) + ":" + b.bytes,
        [this, actor, to, tag, buffer, size, source, target, id, generation, job, service_id] {
      if (!job->active) return;
      job->active = false; service_jobs.erase(service_id);
      auto bytes = buffers.at(buffer).bytes;
      unqueue(hosts.at(source).tx, size); hosts.at(source).usage.transmitted += size;
      auto previous = cause; cause = emit("network.depart", actor, id, tag, size, {}, to, {}, generation);
      return_borrow(buffer);
      // This completion acknowledges local departure, never remote delivery.
      completion(actor, generation, id, Operation::send, tag, Status::ok);
      if (source == target) arrive(actor, to, tag, std::move(bytes), id);
      else {
        auto& edge = links.at({source,target});
        auto end_link = edge.available ? reserve(edge.resource, size) : std::optional<Time>{};
        if (!end_link) {
          ++hosts.at(source).usage.dropped;
          emit("network.drop", actor, id, tag, size, edge.available ? "link queue" : "partition", to);
        } else {
          auto label = "link:" + std::to_string(id) + ":" + bytes;
          schedule(*end_link, std::move(label),
              [this, actor, to, tag, id, size, source, target, bytes = std::move(bytes)] {
            auto& e = links.at({source,target}); unqueue(e.resource, size);
            if (!e.available) { ++hosts.at(source).usage.dropped; emit("network.drop", actor, id, tag, size, "partition", to); return; }
            schedule(add(time, e.config.propagation_ns), "flight:" + std::to_string(id) + ":" + bytes,
                [this, actor, to, tag, id, source, target, bytes] {
              if (!links.at({source,target}).available) { ++hosts.at(source).usage.dropped; emit("network.drop", actor, id, tag, bytes.size(), "partition", to); return; }
              arrive(actor, to, tag, bytes, id);
            });
          });
        }
      }
      cause = previous;
    });
    return id;
  }
  OpId disk(ActorId actor, Operation op, std::string key, std::string after, std::uint32_t limit,
      std::uint64_t max_bytes, std::optional<Buffer> buffer, std::uint64_t tag) {
    auto& m = machine(actor); auto host = m.config.id; auto generation = process(actor).generation;
    auto size = buffer ? owned(actor, *buffer).bytes.size() : max_bytes;
    auto reserved = op == Operation::write ? size : 0;
    if (reserved > m.config.storage_bytes - m.usage.durable - m.usage.reserved_storage)
      return refuse(actor, op, tag, Status::capacity);
    auto resident = (op == Operation::read || op == Operation::list) ? max_bytes : 0;
    if (!hold(host, resident)) return refuse(actor, op, tag, Status::capacity);
    auto charge = add(size, key.size());
    auto end = reserve(m.disk, charge);
    if (!end) { unhold(host, resident); return refuse(actor, op, tag, Status::capacity); }
    if (buffer) borrow(actor, *buffer);
    m.usage.reserved_storage += reserved;
    auto id = submit(actor, op, tag, charge);
    auto job = std::make_shared<DiskJob>(); job->host = host; job->generation = m.device_generation;
    job->cancel = [this, host, actor, generation, id, op, tag, buffer, resident, reserved] {
      hosts.at(host).usage.reserved_storage -= reserved;
      if (buffer) return_borrow(*buffer);
      unhold(host, resident);
      emit("storage.cancelled", actor, id, tag, 0, {}, 0, {}, generation);
      completion(actor, generation, id, op, tag, Status::cancelled);
    };
    disk_jobs.emplace(id, job);
    auto data_fingerprint = buffer ? owned(actor, *buffer).bytes : std::string{};
    auto label = "disk:" + std::to_string(id) + ":" + key + ":" + after + ":" +
        std::to_string(limit) + ":" + std::to_string(max_bytes) + ":" + data_fingerprint;
    job->event = schedule(*end, std::move(label),
        [this, actor, generation, host, id, op, tag, key = std::move(key), after = std::move(after),
         limit, max_bytes, buffer, resident, reserved, charge, job] {
      if (!job->active) return;
      job->active = false; disk_jobs.erase(id);
      auto& machine = hosts.at(host); unqueue(machine.disk, charge);
      machine.usage.reserved_storage -= reserved;
      auto index = std::pair{actor,key};
      Status status = Status::ok; Bytes bytes; std::vector<std::string> keys; std::string next;
      std::vector<std::uint64_t> evidence;
      if (machine.origins.contains(index)) evidence.push_back(machine.origins.at(index));
      if (op == Operation::write) {
        auto& value = machine.storage[index]; machine.usage.durable -= value.size();
        value = buffers.at(*buffer).bytes; machine.usage.durable += value.size(); machine.usage.written += value.size();
      } else if (op == Operation::read) {
        auto it = machine.storage.find(index);
        if (it == machine.storage.end()) status = Status::missing;
        else if (it->second.size() > max_bytes) status = Status::capacity;
        else { bytes = it->second; machine.usage.read += bytes.size(); }
      } else if (op == Operation::erase) {
        auto it = machine.storage.find(index);
        if (it == machine.storage.end()) status = Status::missing;
        else { machine.usage.durable -= it->second.size(); machine.storage.erase(it); machine.origins.erase(index); }
      } else if (op == Operation::list) {
        std::uint64_t used{};
        auto it = after.empty() ? machine.storage.lower_bound({actor,key}) : machine.storage.upper_bound({actor,after});
        for (; it != machine.storage.end() && it->first.first == actor; ++it) {
          const auto& found = it->first.second;
          if (!found.starts_with(key)) break;
          if (keys.size() == limit || found.size() > max_bytes - used) {
            if (keys.empty()) status = Status::capacity;
            else next = keys.back();
            break;
          }
          keys.push_back(found); used += found.size();
          if (machine.origins.contains(it->first)) evidence.push_back(machine.origins.at(it->first));
        }
        machine.usage.read += used;
      }
      if (buffer) return_borrow(*buffer);
      auto previous = cause; cause = emit("storage." + std::string(name(op)), actor, id, tag, reserved ? reserved : bytes.size(), key, 0, std::move(evidence), generation);
      if (op == Operation::write) machine.origins[index] = cause;
      completion(actor, generation, id, op, tag, status, std::move(bytes), std::move(keys), std::move(next),
          [this, host, resident] { unhold(host, resident); });
      cause = previous;
    });
    return id;
  }
};

ActorId Context::self() const { return access_->actor; }
Time Context::now() const { return access_->impl->time; }
std::uint64_t Context::incarnation() const { return access_->incarnation; }
std::uint64_t Context::random(std::string_view domain, std::uint64_t key) const {
  return mix(hash(domain) ^ mix(key) ^ mix(access_->actor) ^ access_->impl->options.seed);
}
OpId Context::timer(Time delay, std::uint64_t tag, Bytes bytes) {
  auto* p = access_->impl; auto actor = self(); auto generation = incarnation();
  auto host = p->process(actor).host; auto size = bytes.size();
  if (!p->hold(host,size)) return p->refuse(actor,Operation::none,tag,Status::capacity);
  Event e; e.kind = EventKind::timer; e.id = ++p->operation; e.tag = tag; e.bytes = std::move(bytes);
  auto id = e.id;
  auto timer = std::make_shared<Simulation::Impl::Delivery>(Simulation::Impl::Delivery{
      p->actors.at(actor).process, generation, [p,host,size] { p->unhold(host,size); }, true});
  p->timers.emplace(id,timer);
  auto label = "timer:" + std::to_string(actor) + ":" + fingerprint(e);
  timer->event = p->schedule(add(p->time, delay), std::move(label),
      [p, actor, generation, id, timer, e = std::move(e)] {
    if (!timer->active) return;
    timer->active = false; p->timers.erase(id);
    p->deliver(actor, generation, e, std::move(timer->cleanup));
  });
  return id;
}
OpId Context::send(ActorId to, std::uint64_t tag, Bytes bytes) {
  auto* p = access_->impl;
  auto buffer = p->allocate(self(), std::move(bytes), false);
  if (!buffer) return p->refuse(self(), Operation::send, tag, Status::capacity);
  auto id = p->send(self(), to, tag, *buffer); p->release(self(), *buffer); return id;
}
OpId Context::send(ActorId to, std::uint64_t tag, Buffer buffer) {
  return access_->impl->send(self(), to, tag, buffer);
}
OpId Context::compute(Time duration, std::uint64_t tag) {
  auto* p = access_->impl; auto actor = self(); auto generation = incarnation();
  auto& machine = p->machine(actor);
  if (machine.cpu.queued == machine.cpu.config.queue_bytes) return p->refuse(actor, Operation::compute, tag, Status::capacity);
  ++machine.cpu.queued;
  auto id = p->submit(actor, Operation::compute, tag, 1);
  auto host = machine.config.id;
  machine.cpu_jobs.push_back(std::make_shared<Simulation::Impl::CpuJob>(Simulation::Impl::CpuJob{actor,generation,id,tag,duration}));
  p->pump_cpu(host);
  return id;
}
OpId Context::write(std::string key, Bytes bytes, std::uint64_t tag) {
  auto* p = access_->impl;
  auto buffer = p->allocate(self(), std::move(bytes), false);
  if (!buffer) return p->refuse(self(), Operation::write, tag, Status::capacity);
  auto id = p->disk(self(), Operation::write, std::move(key), {}, 0, 0, *buffer, tag);
  p->release(self(), *buffer); return id;
}
OpId Context::write(std::string key, Buffer buffer, std::uint64_t tag) {
  return access_->impl->disk(self(), Operation::write, std::move(key), {}, 0, 0, buffer, tag);
}
OpId Context::read(std::string key, std::uint64_t max_bytes, std::uint64_t tag) {
  return access_->impl->disk(self(), Operation::read, std::move(key), {}, 0, max_bytes, {}, tag);
}
OpId Context::erase(std::string key, std::uint64_t tag) {
  return access_->impl->disk(self(), Operation::erase, std::move(key), {}, 0, 0, {}, tag);
}
OpId Context::list(std::string prefix, std::string after, std::uint32_t limit,
    std::uint64_t max_bytes, std::uint64_t tag) {
  if (!limit || !max_bytes || (!after.empty() && !after.starts_with(prefix)))
    throw std::invalid_argument("invalid bounded enumeration request");
  return access_->impl->disk(self(), Operation::list, std::move(prefix), std::move(after), limit, max_bytes, {}, tag);
}
std::optional<Buffer> Context::allocate(Bytes bytes) { return access_->impl->allocate(self(), std::move(bytes)); }
std::string_view Context::view(Buffer buffer) const { return access_->impl->owned(self(), buffer).bytes; }
void Context::release(Buffer buffer) { access_->impl->release(self(), buffer); }
void Context::note(std::string kind, std::string detail, std::uint64_t tag) {
  access_->impl->emit(std::move(kind), self(), 0, tag, 0, std::move(detail));
}

Simulation::Simulation(Options options) : impl_(std::make_unique<Impl>(options)) { impl_->owner = this; }
Simulation::~Simulation() = default;
Simulation::Simulation(Simulation&& other) noexcept : impl_(std::move(other.impl_)) { if (impl_) impl_->owner = this; }
Simulation& Simulation::operator=(Simulation&& other) noexcept {
  impl_ = std::move(other.impl_); if (impl_) impl_->owner = this; return *this;
}
void Simulation::add_host(Host host) {
  if (impl_->started || !host.id || impl_->hosts.contains(host.id)) throw std::invalid_argument("duplicate/late/zero host");
  validate(host.cpu); validate(host.transmit); validate(host.receive); validate(host.disk);
  if (!host.memory_bytes || !host.storage_bytes) throw std::invalid_argument("host capacities must be positive");
  Impl::Machine m; m.config = host;
  m.cpu.config = host.cpu; m.tx.config = host.transmit; m.rx.config = host.receive; m.disk.config = host.disk;
  impl_->hosts.emplace(host.id, std::move(m));
}
void Simulation::add_link(Link link) {
  if (impl_->started || link.from == link.to || !impl_->hosts.contains(link.from) || !impl_->hosts.contains(link.to) ||
      impl_->links.contains({link.from,link.to})) throw std::invalid_argument("invalid/duplicate/late link");
  validate(link.service);
  impl_->links.emplace(std::pair{link.from,link.to}, Impl::Edge{link, {link.service,0,0},true});
}
void Simulation::add_process(ProcessId process, HostId host) {
  if (impl_->started || !process || !impl_->hosts.contains(host) || impl_->processes.contains(process))
    throw std::invalid_argument("invalid/duplicate/late process");
  impl_->processes.emplace(process, Impl::Process{host,false,0});
}
void Simulation::add_actor(ActorId actor, ProcessId process, Factory factory) {
  if (impl_->started || !actor || !impl_->processes.contains(process) || impl_->actors.contains(actor) || !factory)
    throw std::invalid_argument("invalid/duplicate/late actor");
  impl_->actors.emplace(actor, Impl::Role{process,std::move(factory),{},0});
}
void Simulation::observe(std::function<void(const Record&)> observer) {
  if (!observer || impl_->notifying) throw std::invalid_argument("invalid or recursive observer registration");
  impl_->observers.push_back(std::move(observer));
}
void Simulation::start() {
  if (impl_->started) throw std::logic_error("simulation already started");
  impl_->started = true;
  for (auto& [id,p] : impl_->processes) restart(id);
}
void Simulation::inject(Time at, ActorId to, std::uint64_t tag, Bytes bytes) {
  if (!impl_->actors.contains(to)) throw std::invalid_argument("input target unknown");
  auto* p = impl_.get();
  auto label = "input:" + std::to_string(to) + ":" + std::to_string(tag) + ":" + bytes;
  p->schedule(at, std::move(label),
      [p, to, tag, bytes = std::move(bytes)] {
    auto previous = p->cause;
    p->cause = p->emit("input.offer", to, 0, tag, bytes.size(), bytes);
    Event e; e.kind = EventKind::message; e.tag = tag; e.bytes = bytes;
    p->deliver(to, 0, std::move(e)); p->cause = previous;
  });
}
void Simulation::at(Time at, std::string name, std::function<void(Simulation&)> action) {
  if (!action) throw std::invalid_argument("empty incident");
  auto* p = impl_.get();
  auto label = "incident:" + name;
  p->schedule(at, std::move(label), [p, name = std::move(name), action = std::move(action)] {
    p->cause = p->emit("incident", 0, 0, 0, 0, name); action(*p->owner);
  });
}
void Simulation::crash(ProcessId process) {
  auto* p = impl_.get();
  if (p->delivering || p->notifying) throw std::logic_error("enqueue process fault after the current atomic event");
  auto& proc = p->processes.at(process);
  if (!proc.alive) return;
  proc.alive = false;
  p->cancel_cpu(process);
  std::vector<std::uint64_t> callbacks;
  for (auto& [id,d] : p->deliveries) if (d->process == process && d->generation == proc.generation && d->active) {
    d->active = false;
    p->cancel_event(d->event);
    if (d->cleanup) d->cleanup();
    callbacks.push_back(id);
  }
  for (auto id : callbacks) p->deliveries.erase(id);
  callbacks.clear();
  for (auto& [id,t] : p->timers) if (t->process == process && t->generation == proc.generation && t->active) {
    t->active = false; p->cancel_event(t->event); if (t->cleanup) t->cleanup(); callbacks.push_back(id);
  }
  for (auto id : callbacks) p->timers.erase(id);
  for (auto& [id,role] : p->actors) if (role.process == process) {
    p->emit("process.crash", id); role.actor.reset();
    std::vector<Buffer> owned;
    for (const auto& [buffer,b] : p->buffers) if (b.actor == id && b.owned) owned.push_back(buffer);
    for (auto buffer : owned) { p->buffers.at(buffer).owned = false; p->retire(buffer); }
  }
}
void Simulation::restart(ProcessId process) {
  auto* p = impl_.get();
  if (p->delivering || p->notifying) throw std::logic_error("enqueue restart after the current atomic event");
  auto& proc = p->processes.at(process);
  if (proc.alive) throw std::logic_error("restart requires a stopped process");
  proc.alive = true; ++proc.generation;
  for (auto& [id,role] : p->actors) if (role.process == process) {
    role.actor = role.factory();
    if (!role.actor) throw std::invalid_argument("actor factory returned null");
    role.last = 0;
    auto previous = p->cause; p->cause = p->emit("process.restart", id);
    p->deliver(id, proc.generation, Event{}); p->cause = previous;
  }
}
void Simulation::power_loss(HostId host, bool destroy_storage) {
  auto* p = impl_.get();
  if (p->delivering || p->notifying) throw std::logic_error("enqueue power loss after the current atomic event");
  auto& m = p->hosts.at(host);
  for (auto& [id,proc] : p->processes) if (proc.host == host && proc.alive) crash(id);
  ++m.device_generation;
  std::vector<OpId> cancelled;
  for (auto& [id,job] : p->disk_jobs) if (job->host == host && job->active) {
    job->active = false; p->cancel_event(job->event); job->cancel(); cancelled.push_back(id);
  }
  for (auto id : cancelled) p->disk_jobs.erase(id);
  m.disk.ready = p->time; m.disk.queued = 0;
  cancelled.clear();
  for (auto& [id,job] : p->service_jobs) if (job->host == host && job->active) {
    job->active = false; p->cancel_event(job->event); job->cancel(); cancelled.push_back(id);
  }
  for (auto id : cancelled) p->service_jobs.erase(id);
  for (auto* r : {&m.cpu,&m.tx,&m.rx}) { r->ready = p->time; r->queued = 0; }
  if (destroy_storage) { m.storage.clear(); m.origins.clear(); m.usage.durable = 0; }
  p->emit(destroy_storage ? "host.destroy" : "host.reset", 0, 0, 0, 0, std::to_string(host));
}
void Simulation::set_link(HostId from, HostId to, bool available) {
  if (impl_->delivering || impl_->notifying) throw std::logic_error("enqueue link change after the current atomic event");
  auto& edge = impl_->links.at({from,to}); edge.available = available;
  impl_->emit("link.state", 0, 0, 0, available, std::to_string(from) + ":" + std::to_string(to));
}
void Simulation::pause() { impl_->paused = true; }
Run Simulation::run(Time until, std::uint64_t max_events) {
  auto* p = impl_.get();
  if (!p->started || until < p->time || !max_events || p->running || p->notifying || p->delivering) throw std::invalid_argument("invalid or recursive run boundary");
  p->running = true;
  struct RunGuard { Impl* p; ~RunGuard() { p->running = false; } } guard{p};
  p->paused = false;
  Run result;
  while (!p->queue.empty() && std::get<0>(p->queue.begin()->first) <= until) {
    if (result.events == max_events) { result.budget_exhausted = true; break; }
    auto it = p->queue.begin();
    if (p->options.decisions_in) {
      // Peek the next streamed dispatch and select it among the earliest events.
      char kind{}; Time time{}; std::uint64_t id{}, fp{};
      if (!(*p->options.decisions_in >> kind >> time >> id >> fp) || kind != 'D')
        throw std::runtime_error("replay expected a dispatch");
      auto found = p->event_keys.find(id);
      if (found == p->event_keys.end() || std::get<0>(found->second) != std::get<0>(it->first) || time != std::get<0>(it->first))
        throw std::runtime_error("replay selected an event that is not enabled");
      it = p->queue.find(found->second);
      if (it->second.fingerprint != fp) throw std::runtime_error("replay dispatch payload diverged");
      if (p->options.decisions_out) {
        *p->options.decisions_out << "D " << time << ' ' << id << ' ' << fp << '\n';
        if (!*p->options.decisions_out) throw std::runtime_error("failed to write replay transcript");
      }
    } else p->transcript('D', std::get<0>(it->first), it->second.id, it->second.fingerprint);
    p->time = std::get<0>(it->first);
    auto event = std::move(it->second); p->event_keys.erase(event.id); p->queue.erase(it);
    p->cause = event.cause;
    p->cause = p->emit("dispatch", 0, event.id, 0, 0, std::to_string(event.fingerprint));
    event.action(); ++result.events;
    if (p->paused) { result.paused = true; break; }
  }
  if (!result.budget_exhausted && !result.paused) p->time = until;
  result.pending = !p->queue.empty(); p->cause = 0;
  return result;
}
void Simulation::finish_replay() {
  if (impl_->options.decisions_out) {
    impl_->options.decisions_out->flush();
    if (!*impl_->options.decisions_out) throw std::runtime_error("failed to flush choice transcript");
  }
  if (impl_->options.decisions_in) {
    *impl_->options.decisions_in >> std::ws;
    if (impl_->options.decisions_in->peek() != std::char_traits<char>::eof())
      throw std::runtime_error("exact replay left unused choices");
  }
}
Time Simulation::now() const { return impl_->time; }
Usage Simulation::usage(HostId host) const { return impl_->hosts.at(host).usage; }
std::uint64_t Simulation::trace_hash() const { return impl_->digest; }
std::uint64_t Simulation::records() const { return impl_->record_serial; }
std::string_view name(Status status) {
  switch (status) {
    case Status::ok: return "ok";
    case Status::missing: return "missing";
    case Status::capacity: return "capacity";
    case Status::unreachable: return "unreachable";
    case Status::cancelled: return "cancelled";
  }
  throw std::logic_error("unknown status");
}
std::string_view name(Operation operation) {
  switch (operation) {
    case Operation::none: return "none";
    case Operation::send: return "send";
    case Operation::compute: return "compute";
    case Operation::write: return "write";
    case Operation::read: return "read";
    case Operation::erase: return "erase";
    case Operation::list: return "list";
  }
  throw std::logic_error("unknown operation");
}
void write_json(std::ostream& out, const Record& r) {
  out << "{\"id\":" << r.id << ",\"cause\":" << r.cause << ",\"time\":" << r.time << ",\"kind\":"; quote(out,r.kind);
  out << ",\"actor\":" << r.actor << ",\"peer\":" << r.peer << ",\"process\":" << r.process << ",\"host\":" << r.host
      << ",\"incarnation\":" << r.incarnation << ",\"operation\":" << r.operation << ",\"tag\":" << r.tag << ",\"size\":" << r.size << ",\"detail\":";
  quote(out,r.detail); out << ",\"parents\":[";
  for (std::size_t i=0; i<r.parents.size(); ++i) { if (i) out << ','; out << r.parents[i]; }
  out << "]}\n";
  if (!out) throw std::runtime_error("failed to write causal trace");
}

} // namespace sixdb::sim
