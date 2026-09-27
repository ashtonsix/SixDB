#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sixdb::sim {

using Time = std::uint64_t; // Integer modeled nanoseconds, not measured time.
using HostId = std::uint32_t;
using ProcessId = std::uint32_t;
using ActorId = std::uint32_t;
using OpId = std::uint64_t;
using Buffer = std::uint64_t;
using Bytes = std::string; // Opaque bytes; no shared pointers cross a port.

enum class Status { ok, missing, capacity, unreachable, cancelled };
enum class EventKind { boot, message, timer, completion };
enum class Operation { none, send, compute, write, read, erase, list };

struct Event {
  EventKind kind{EventKind::boot};
  Operation operation{Operation::none};
  Status status{Status::ok};
  OpId id{};
  ActorId from{};
  std::uint64_t tag{};
  Bytes bytes;
  std::vector<std::string> keys;
  std::string next; // Last returned key when another enumeration page remains.
};

struct Service {
  Time latency_ns{100};
  std::uint64_t bytes_per_second{1'000'000'000};
  std::uint64_t queue_bytes{1 << 20};
};
struct Host {
  HostId id{};
  std::uint64_t memory_bytes{16 << 20};
  std::uint64_t storage_bytes{64 << 20};
  Service cpu{0, 1'000'000'000, 1 << 20};
  Service transmit{}, receive{}, disk{};
};
struct Link {
  HostId from{}, to{};
  Time propagation_ns{1'000};
  Service service{0, 1'000'000'000, 1 << 20};
};

/// Emitted synchronously to harness observers, never delivered to actors.
/// IDs and causes form a streaming causal history; observers may retain selectively.
struct Record {
  std::uint64_t id{}, cause{};
  Time time{};
  std::string kind;
  ActorId actor{}, peer{};
  ProcessId process{};
  HostId host{};
  std::uint64_t incarnation{}, operation{}, tag{}, size{};
  std::string detail;
  std::vector<std::uint64_t> parents;
};

class Context;
class Actor {
 public:
  virtual ~Actor() = default;
  virtual void receive(Context&, const Event&) = 0;
};
using Factory = std::function<std::unique_ptr<Actor>()>;

/// The only environment access available to a protocol actor. Time is an ideal
/// local monotonic clock in this first physical model. Actor compute must be
/// charged explicitly; C++ execution wall time never advances simulation time.
class Context {
 public:
  [[nodiscard]] ActorId self() const;
  [[nodiscard]] Time now() const;
  [[nodiscard]] std::uint64_t incarnation() const;
  [[nodiscard]] std::uint64_t random(std::string_view domain, std::uint64_t key) const;
  /// Every asynchronous request returns an identity and eventually a local
  /// completion unless its process dies. Refusals are asynchronous too; a
  /// timer-payload refusal has Operation::none and Status::capacity.
  OpId timer(Time delay, std::uint64_t tag, Bytes bytes = {});
  OpId send(ActorId to, std::uint64_t tag, Bytes bytes);
  OpId send(ActorId to, std::uint64_t tag, Buffer buffer);
  OpId compute(Time duration, std::uint64_t tag);
  OpId write(std::string key, Bytes bytes, std::uint64_t tag);
  OpId write(std::string key, Buffer buffer, std::uint64_t tag);
  OpId read(std::string key, std::uint64_t max_bytes, std::uint64_t tag);
  OpId erase(std::string key, std::uint64_t tag);
  /// Enumeration is lexicographic and bounded, not a snapshot across calls.
  /// Actors must recover a durable root before interpreting its child records.
  OpId list(std::string prefix, std::string after, std::uint32_t limit,
            std::uint64_t max_bytes, std::uint64_t tag);
  /// Buffers are immutable, host-charged, and owned by one actor incarnation.
  /// Dropping ownership does not retire a submitted backend borrow.
  [[nodiscard]] std::optional<Buffer> allocate(Bytes bytes);
  [[nodiscard]] std::string_view view(Buffer buffer) const;
  void release(Buffer buffer);
  void note(std::string kind, std::string detail = {}, std::uint64_t tag = 0);

 private:
  friend class Simulation;
  struct Access;
  explicit Context(Access* access) : access_(access) {}
  Access* access_;
};

enum class Ordering { fifo, seeded };
struct Options {
  std::uint64_t seed{1};
  Ordering ordering{Ordering::seeded};
  /// Optional streaming choice transcript. Input validates scheduled-event and
  /// dispatch fingerprints, including payloads, and fails closed on divergence.
  std::ostream* decisions_out{};
  std::istream* decisions_in{};
};
struct Run {
  std::uint64_t events{};
  bool budget_exhausted{};
  bool pending{};
  bool paused{};
};
struct Usage {
  std::uint64_t memory{}, memory_peak{}, durable{}, reserved_storage{};
  std::uint64_t transmitted{}, received{}, written{}, read{}, refused{}, dropped{};
};

/// Harness/environment only. Models receive Context, never Simulation.
/// A process may contain many actors; hosts may contain many processes.
class Simulation {
 public:
  explicit Simulation(Options options = {});
  ~Simulation();
  Simulation(Simulation&&) noexcept;
  Simulation& operator=(Simulation&&) noexcept;
  Simulation(const Simulation&) = delete;
  Simulation& operator=(const Simulation&) = delete;
  void add_host(Host);
  void add_link(Link);
  void add_process(ProcessId, HostId);
  void add_actor(ActorId, ProcessId, Factory);
  /// Observers and incidents belong to the harness. Keep policy decisions out.
  void observe(std::function<void(const Record&)> observer);
  void start();
  void inject(Time at, ActorId to, std::uint64_t tag, Bytes bytes = {});
  void at(Time at, std::string name, std::function<void(Simulation&)> action);
  /// Process death loses volatile actors/ownership and remaining CPU work,
  /// not submitted device work. Observers enqueue faults through at(), never
  /// invoke lifecycle mutation or run recursively during notification.
  void crash(ProcessId);
  void restart(ProcessId);
  /// Power loss kills host processes and uncompleted device work; completed
  /// durable records survive. Destruction also removes durable bytes.
  void power_loss(HostId, bool destroy_storage = false);
  void set_link(HostId from, HostId to, bool available);
  /// May be requested by an observer; returns from run after the current atomic
  /// event, before another dispatch. Resuming run clears the pause request.
  void pause();
  Run run(Time until, std::uint64_t max_events = 1'000'000);
  /// Explicit end of exact replay; staged runs may stop with choices remaining.
  void finish_replay();
  [[nodiscard]] Time now() const;
  [[nodiscard]] Usage usage(HostId) const;
  [[nodiscard]] std::uint64_t trace_hash() const;
  [[nodiscard]] std::uint64_t records() const;

 private:
  friend class Context;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::string_view name(Status);
std::string_view name(Operation);
void write_json(std::ostream&, const Record&);

} // namespace sixdb::sim
