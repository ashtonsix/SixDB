#pragma once

// Background mapping, first touch and optional format construction.
// Ready native access needs no Preparation record.

#include <orbital/allocation.h>
#include <cassert>
namespace orbital::allocation {
struct PreparedBytes { MutableBytes body; std::size_t readable_padding{}; };

/// Keep this record, its constructor context and service Lane stable through
/// physical settlement and owner delivery, including repair_wait cleanup.
struct Preparation {
  Preparation() = default;
  Preparation(const Preparation&) = delete;
  Preparation& operator=(const Preparation&) = delete;
  Preparation(Preparation&&) = delete;
  Preparation& operator=(Preparation&&) = delete;
  enum class State { idle, pending, repair_wait, ready, taken, failed, cancelled };
  Pool* owner{};
  void* backend{};
  worker::Lane* preparer{}; // Caller-owned route outlives accepted outbound delivery.
  Signal changed;
  Allocation value;
  State state{State::idle};
  Error error{Error::unavailable};
  bool discard{}, reused{};
  AllocationRequest request;
  struct Outcome {
    std::byte* mapping{};
    std::byte* address{};
    std::size_t prepared{};
    bool success{};
    Error error{Error::io};
  } outcome; // Private helper result, observed only after queued completion.

  /// Trusted bounded initializer on the preparer Lane, over exclusive body bytes
  /// and initialized padding. Keep effects in its private context until owner delivery.
  Result<void> (*construct)(PreparedBytes, void*) noexcept{};
  void* context{};

  ~Preparation() { assert(state != State::pending && state != State::repair_wait && !backend); }
};

/// Pool's Lane reserves storage; preparer maps, touches and optionally constructs.
/// Owner delivery publishes Allocation. Capacity refusal can require a later retry.
Result<void> prepare(Pool&, Preparation&, const AllocationRequest&, worker::Lane& preparer);
/// Schedule one retry of parked finite cleanup; never rerun the byte constructor.
Result<void> retry_cleanup(Preparation&);
/// End demand on the Pool's Lane; pending physical work must still settle.
void cancel(Preparation&) noexcept;
/// Move a ready Allocation out on its Pool's Lane; never wait or restart work.
Result<Allocation> take(Preparation&);
}
