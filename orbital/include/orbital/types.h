#pragma once

// Shared coordinates, byte ranges, explicit results and retained references.
// Signals describe Lane-owned readiness; Progress carries retry observations.

#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace orbital::worker {
struct Lane;
struct Message;

/// post retains the record; its owner handles it, then releases the queue use.
/// release may destroy or repost the unlinked Message.
struct MessageOps {
  void (*retain)(Message&) noexcept;
  void (*handle)(Message&, Lane&) noexcept;
  void (*release)(Message&) noexcept;
};

struct Message {
  std::atomic<Message*> next{};
  void* context{};
  const MessageOps* ops{};
};
} // namespace orbital::worker

namespace orbital {

template<class Tag>
struct Id {
  std::uint64_t high;
  std::uint64_t low;
  auto operator<=>(const Id&) const = default;
};

using ShardId = Id<struct ShardTag>;
using ObjectId = Id<struct ObjectTag>;
using PartId = Id<struct PartTag>;
using ReplicaId = Id<struct ReplicaTag>;
using ProducerId = Id<struct ProducerTag>;
using StreamId = Id<struct StreamTag>;
using ProgramId = Id<struct ProgramTag>;
using TransactionId = Id<struct TransactionTag>;
using SourceId = Id<struct SourceTag>;

struct Epoch { std::uint64_t value; };

struct ProducerSequence { std::uint64_t value; };

struct SerialPosition { std::uint64_t value; };

struct BufferGeneration { std::uint64_t value; };

struct EventId {
  ProducerId producer;
  ProducerSequence sequence;
};

/// Byte offset and size relative to a caller-defined byte sequence.
struct ByteRange {
  std::uint64_t offset;
  std::uint64_t size;
};

using Bytes = std::span<const std::byte>;
using MutableBytes = std::span<std::byte>;

/// Owner-defined region. Each use validates its supported scheme; sharing this
/// shape implies no shared meaning. Encoded bytes live with the containing record.
struct Scope { ProgramId scheme{}; Bytes encoded; };

enum class Error {
  invalid_argument,
  absent,
  conflict,
  stale,
  unavailable,
  capacity,
  io,
  rejected
};

template<class T>
using Result = std::expected<T, Error>;

/// Intrusive lifetime reference with type-specific retain/release hooks.
/// It supplies no byte stability, access authority or mandatory control block.
template<class T>
class Ref {
public:
  constexpr Ref() noexcept = default;

  static Ref retain(T& value) noexcept {
    retain_ref(value);
    Ref result;
    result.pointer_ = &value;
    return result;
  }

  Ref(const Ref& other) noexcept : pointer_(other.pointer_) {
    if (pointer_) retain_ref(*pointer_);
  }

  Ref& operator=(const Ref& other) noexcept {
    Ref copy(other);
    swap(copy);
    return *this;
  }

  Ref(Ref&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}

  Ref& operator=(Ref&& other) noexcept {
    Ref moved(std::move(other));
    swap(moved);
    return *this;
  }

  // Keep the empty-reference check in callers; the release hook owns retirement.
  [[gnu::always_inline]] ~Ref() { if (pointer_) release_ref(*pointer_); }

  void reset() noexcept { Ref empty; swap(empty); }

  void swap(Ref& other) noexcept { std::swap(pointer_, other.pointer_); }

  T* get() const noexcept { return pointer_; }

  T& operator*() const noexcept { return *pointer_; }

  T* operator->() const noexcept { return pointer_; }

  explicit operator bool() const noexcept { return pointer_ != nullptr; }
private:
  T* pointer_{};
};

enum class Readiness { idle, pending, ready, failed, cancelled };

struct Signal;
/// Link and source stay live through enrollment/detach, on the waiter's same Lane.
/// Notification unlinks first and queues a later visit; it must not reenter a step.
struct WaitLink {
  Signal* source{};
  WaitLink* next{};
  void* waiter{};

  void (*notify)(void*) noexcept{};
};

/// State and wait links change only on the assigned thread. Foreign completion
/// posts a retained Message; it must not mutate a destroyable caller's Signal.
struct Signal {
  worker::Lane* lane{};
  Readiness state{Readiness::idle};
  Error failure{Error::unavailable};
  std::uint64_t generation{}; // No reset/wrap while an observation or waiter is live.
  WaitLink* waiters{};
};

/// On the assigned thread, detach and notify waiters after advancing generation.
/// State stays nonterminal; waking grants no resource. Recheck the condition.
void pulse(Signal&) noexcept;
/// Detach and notify current waiters; pulse's tail for owners that advance generation inline.
void notify_waiters(Signal&) noexcept;
/// Set a terminal result on the assigned thread, then advance generation and notify.
/// Use pulse for reusable capacity/publication changes; complete is permanent.
void complete(Signal&, Result<void>) noexcept;

/// Waiting carries the generation observed before checking a reusable predicate.
class Progress {
public:
  enum class Kind { done, waiting, yielded, failed };

  static Progress done() noexcept { return {nullptr, {.status = {Kind::done}}}; }

  /// Observe before checking the predicate; enrollment rechecks at the same Lane.
  /// Changed observations retry instead of parking on a lost notification.
  static Progress wait(Signal& source, std::uint64_t observed) noexcept {
    return {&source, {.generation = observed}};
  }

  /// Only for an uninterrupted Lane-local check followed by this snapshot.
  /// If an intervening call can pulse/complete the source, observe first instead.
  static Progress wait(Signal& source) noexcept { return wait(source, source.generation); }

  static Progress yield() noexcept { return {nullptr, {.status = {Kind::yielded}}}; }

  static Progress fail(Error error) noexcept { return {nullptr, {.status = {Kind::failed, error}}}; }

  Kind kind() const noexcept { return source_ ? Kind::waiting : detail_.status.kind; }
  Signal* signal() const noexcept { return source_; }
  Error failure() const noexcept { return source_ ? Error::unavailable : detail_.status.failure; }
  std::uint64_t observed_generation() const noexcept { return source_ ? detail_.generation : 0; }

private:
  // A wait's non-null source selects the full generation. Other results use
  // the same word for status; no pointer bits or generation bits are borrowed.
  union Detail {
    std::uint64_t generation;
    struct { Kind kind; Error failure{Error::unavailable}; } status;
  };
  Progress(Signal* source, Detail detail) noexcept : source_(source), detail_(detail) {}
  Signal* source_;
  Detail detail_;
};

// These native coordinates are not frozen wire layouts.
} // namespace orbital
