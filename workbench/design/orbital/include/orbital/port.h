#pragma once

// Provisional retained endpoints and byte-transfer requests; declarations only.
// See ../../DESIGN.md for the combined Orbital/Loom recommendation.

#include <orbital/allocation.h>
#include <variant>

namespace orbital::port {
struct Backend;
struct BackendOps;
struct Resource;
struct Request;
void retain_ref(Backend&) noexcept;
void release_ref(Backend&) noexcept;
void retain_ref(Resource&) noexcept;
void release_ref(Resource&) noexcept;
void retain_ref(Request&) noexcept;
void release_ref(Request&) noexcept;
enum class Persistence { none, retained, durable };
enum class Outcome { complete, partial, failed, cancelled };

struct Backend {
  std::atomic<std::uint32_t> references{};
  const BackendOps* ops{};
  void* context{};
};
struct Resource {
  std::atomic<std::uint32_t> references{};
  Ref<Backend> backend;
  void* native{};
  void (*retire)(Resource&) noexcept{};
};
// Supplied by an independent pool/storage owner, not a destroyable Task frame.
// The memory backend retains this concrete record through Resource references.
struct MemoryResource {
  allocation::Hold record_storage;
  Resource resource;
  allocation::Hold bytes;
};
struct Source {
  Ref<Resource> resource;
  SourceId identity{};
  ByteRange extent{};
};
struct Destination {
  Ref<Resource> resource;
  ByteRange extent{};
};
// Endpoint offsets start at its captured extent; memory offsets start at
// Hold.address and are bounded by Hold.footprint.size. Storage belongs to Request.
struct TransferRange { std::uint64_t source_offset, destination_offset, size; };
struct Completion {
  Outcome outcome{Outcome::failed};
  Persistence persistence{Persistence::none};
  Error failure{Error::unavailable};
  // Known contiguous prefix length in bytes per frozen TransferRange; equality
  // with its size proves full coverage, independently of persistence.
  std::span<const std::uint64_t> transferred;
};
struct Request {
  std::atomic<std::uint32_t> references{};
  void (*retire)(Request&) noexcept{};
  allocation::Hold record_storage;
  worker::Message message;
  Signal finished;
  std::variant<Source, Destination> endpoint;
  allocation::Hold memory;
  Persistence required{Persistence::none};
  std::span<TransferRange> range_storage;
  std::span<std::uint64_t> result_storage;
  std::size_t range_count{};
  Completion completion;
  bool prepared{};
  std::atomic<bool> cancel_requested{};
};
// Memory/file/network/blob services implement one transfer table. Opening files,
// blobs or connections uses their typed backend APIs, not a Bytes locator codec.
struct BackendOps {
  // done = accepted; waiting/failed = not accepted. Waiting Signal has the
  // Request caller lane, even when pressure originates in a shared backend.
  Progress (*submit)(Ref<Request>);
  void (*cancel)(Request&) noexcept;
  void (*retire)(Backend&) noexcept;
};

// Uses a closed reserved record and its supplied retirement callback. Captures
// already immutable bytes; shares the built-in memory backend without allocation.
[[nodiscard]] Result<Source> retain_memory(MemoryResource&, SourceId,
    allocation::Hold immutable_bytes);
// Built-in memory only; no I/O or retention. Caller keeps Source alive. The
// returned Hold belongs to MemoryResource; Source.extent selects bytes within it.
[[nodiscard]] Result<const allocation::Hold*> borrow_memory(const Source&);
[[nodiscard]] Progress read(Request&, worker::Lane& caller, Source, allocation::Hold destination,
    std::span<const TransferRange>);
[[nodiscard]] Progress write(Request&, worker::Lane& caller, Destination, allocation::Hold stable_source,
    std::span<const TransferRange>, Persistence);
// finished stays idle through preparation/refusal; acceptance changes it to
// pending unless inline completion already made it terminal. Dispatch once
// through Resource.backend; pending/terminal retries never resubmit I/O.
// Cancelled idle work never submits; retire any retained pressure notification.
[[nodiscard]] Progress advance(Request&);
void cancel_interest(Request&) noexcept;
// Backend calls only after all physical accesses stop. Inline caller-thread
// completion can handle directly; off-thread completion retains Message to lane.
// Define every used result slot, even on failure/cancel: zero means no known prefix.
// The owner examines exact request and coverage, then binds.
void finish(Ref<Request>, Outcome, Persistence, Error) noexcept;
[[nodiscard]] Result<const Completion*> inspect(const Request&);

// Range/result arrays and record_storage stay valid through physical completion
// AND Message handling. Caller storage cannot be destroyed/reused on cancel.
// Source is immutable: a raw Hold does not freeze aliases; its owner establishes
// immutability before retain_memory. Write buffers need stability through finish.
// Inspect status/ranges together with the frozen Request/endpoint. Buffer
// qualification and backend wire-operation IDs belong to their respective owners.
// Request cannot be retargeted/reused before retirement; finish occurs once only
// after actual physical use ends. Partial writes may have external effects.
// Physical completion never publishes an epoch.
} // namespace orbital::port
