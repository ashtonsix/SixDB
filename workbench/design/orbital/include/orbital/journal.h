#pragma once

// Provisional admission, retained history and State publication; declarations only.
// See ../../DESIGN.md for the combined Orbital/Loom recommendation.

#include <orbital/object.h>
#include <memory>
#include <optional>
#include <vector>

namespace orbital::journal {
struct Interpretation;
struct Authority;
struct Event;
struct Append;
struct Log;
struct Batch;
struct AdmissionRequest;
struct WitnessWrite;
struct Obligations;
struct State;
struct ReaderDomain;
struct Checkpoint;
struct CheckpointWrite;
// Future records supply their own retention and retirement protocols.
void retain_ref(Interpretation&) noexcept;
void release_ref(Interpretation&) noexcept;
void retain_ref(Authority&) noexcept;
void release_ref(Authority&) noexcept;
void retain_ref(Event&) noexcept;
void release_ref(Event&) noexcept;
void retain_ref(Append&) noexcept;
void release_ref(Append&) noexcept;
void retain_ref(Log&) noexcept;
void release_ref(Log&) noexcept;
void retain_ref(AdmissionRequest&) noexcept;
void release_ref(AdmissionRequest&) noexcept;
void retain_ref(WitnessWrite&) noexcept;
void release_ref(WitnessWrite&) noexcept;
void retain_ref(Obligations&) noexcept;
void release_ref(Obligations&) noexcept;
void retain_ref(ReaderDomain&) noexcept;
void release_ref(ReaderDomain&) noexcept;
void retain_ref(Checkpoint&) noexcept;
void release_ref(Checkpoint&) noexcept;
void retain_ref(CheckpointWrite&) noexcept;
void release_ref(CheckpointWrite&) noexcept;

enum class ReplayLaw { reproducible, recorded_observations };
enum class Retention { instance, crash_durable };
enum class Ordering { local, witnessed };
struct Interpretation {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  ProgramId id{}, profile{};
  std::uint64_t version{};
  ReplayLaw replay{};
  port::Source executable, decoder;
};
struct ReadBound { ShardId shard{}; SerialPosition minimum{}; Scope scope; };
struct Frontier { ProducerId producer{}; ProducerSequence through{}; };
struct CopyReceipt {
  ReplicaId holder{};
  std::uint64_t storage_incarnation{}, protection_generation{};
  SourceId package{};
};
struct RetentionEvidence {
  Retention guarantee{};
  std::uint64_t protection_generation{};
  std::span<const CopyReceipt> copies;
};
struct Authority {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  ShardId shard{};
  ReplicaId leader{};
  std::uint64_t configuration{}, generation{};
  Ordering ordering{};
  std::span<const ReplicaId> witnesses;
  std::uint32_t required_acceptances{};
};
struct Event {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  ShardId shard{};
  StreamId stream{};
  EventId id{};
  Ref<Interpretation> interpretation;
  port::Source input;
  std::span<const ReadBound> bounds;
};
struct Append;
struct CheckpointWrite;
struct Batch;
struct AdmissionRequest;
struct WitnessWrite;
struct Log;
struct LogOps {
  Result<RetentionEvidence> (*append_ready)(Log&, Ref<Event>);
  Progress (*append)(Log&, Ref<Append>);
  Progress (*checkpoint)(Log&, Ref<CheckpointWrite>);
  // Retain one complete immutable Journal-encoded record. Capture its Source,
  // backend and Request through actual I/O; retry preserves record identity.
  // Journal owns framing/recovery closure and semantic qualification.
  Progress (*retain_record)(Log&, port::Source, Ref<port::Request>);
  // Only confirmed storage evidence for this exact record/request. Storage evidence spans
  // live in retained Log/request storage; persistence alone is not admission.
  Result<RetentionEvidence> (*record_evidence)(Log&, SourceId, const port::Request&);
  void (*retire)(Log&) noexcept;
};
struct Log {
  std::atomic<std::uint32_t> references{};
  worker::Lane* lane{};
  const LogOps* ops{};
  void* backend{};
  Retention guarantee{};
};
struct MemoryLogIndex;
// A concrete retained-memory provider. log retains this whole pool record;
// native append bypasses LogOps. Reserve vector/index capacity before ready use.
// Duplicate and retired-position lookup uses the owned index, not a linear scan.
struct MemoryLog {
  allocation::Hold storage;
  Log log;
  std::vector<Ref<Event>> events;
  std::vector<Frontier> retired;
  std::unique_ptr<MemoryLogIndex> index;
  MemoryLog();
  ~MemoryLog();
  MemoryLog(const MemoryLog&) = delete;
  MemoryLog& operator=(const MemoryLog&) = delete;
};
// Owner-lane setup: reserve total event and producer capacity, including the
// private retry/retirement index. Failure preserves retained records/identities;
// ready append uses reserved capacity or fails before mutation.
[[nodiscard]] Result<void> reserve(MemoryLog&, std::size_t events, std::size_t producers);
struct Producer {
  worker::Lane* lane{};
  ShardId shard{};
  StreamId stream{};
  ProducerId id{};
  Ref<Log> log;
  Frontier retained;
};
struct Append {
  std::atomic<std::uint32_t> references{};
  void (*retire)(Append&) noexcept{};
  allocation::Hold storage;
  Ref<Log> log;
  Ref<Event> event;
  RetentionEvidence evidence;
  Signal ready;
  worker::Message completion;
};
// Stable event storage owns bounds/input/code. Matching retained retries reuse
// identity; conflicts are rejected. Below a retired cursor, return stale, not a
// new append or an invented cached result.
// No physical observer record for a provider that completes synchronously.
// Any storage-evidence spans live in retained event/log storage, never local scratch.
[[nodiscard]] Result<RetentionEvidence> append_ready(Producer&, Ref<Event>);
// Producer.log must name this exact log/lane. Validate identity/conflict and
// capacity before mutation; accepted input is retained by MemoryLog itself.
[[nodiscard]] Result<RetentionEvidence> append_ready(Producer&, MemoryLog&, Ref<Event>);
[[nodiscard]] Progress append(Producer&, Ref<Event>, Ref<Append>);
void finish(Ref<Append>, RetentionEvidence, Result<void>) noexcept;

struct Batch {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  Ref<Authority> authority;
  ShardId shard{};
  Epoch epoch{}, predecessor{};
  SourceId value{}, predecessor_value{};
  std::span<const Ref<Event>> ordered;
  std::span<const Frontier> frontiers;
  RetentionEvidence material;
};
struct Acceptance {
  ReplicaId witness{};
  std::uint64_t storage_incarnation{}, configuration{}, ballot{};
  Epoch epoch{};
  SourceId value{};
};
struct AuthorityHandoff {
  Ref<Authority> predecessor, successor;
  Epoch closed{};
  SourceId certified_prefix{};
  std::span<const Acceptance> certificate;
};
struct AdmissionRequest {
  std::atomic<std::uint32_t> references{};
  void (*retire)(AdmissionRequest&) noexcept{};
  allocation::Hold storage;
  Ref<Batch> input;
  Ref<Log> log;
  port::Source record;
  std::span<const Acceptance> learned;
  Ref<port::Request> retention;
  std::span<Ref<port::Request>> requests;
  Signal ready;
  worker::Message completion;
};
struct BatchBudget { std::uint64_t bytes{}, work{}, wait_ns{}; };
struct WitnessRoute { ReplicaId witness{}; port::Destination endpoint; };
struct AdmissionService {
  worker::Lane* lane{};
  Ref<Authority> authority;
  Ref<Log> log;
  std::span<const WitnessRoute> routes;
  Epoch learned{};
};
// Form from eligible retained input, using reserved Batch storage. Validate
// stream positions/material and freeze explicit canonical order before admission.
[[nodiscard]] Progress form_epoch(AdmissionService&, std::span<const Ref<Event>> eligible,
    BatchBudget, Batch& storage);
// Retained input is distinct from ordered admission. Epoch input/value remains
// frozen through every protocol use. Embedded admission freezes
// a batch locally; full admission learns the actual configured witness proof.
struct AdmittedBatch {
  const Ref<Batch>& input() const noexcept { return input_; }
private:
  Ref<Batch> input_;
  explicit AdmittedBatch(Ref<Batch>);
  friend Result<AdmittedBatch> admit_ready(AdmissionService&, Ref<Batch>);
  friend Result<AdmittedBatch> inspect(const AdmissionRequest&);
};
[[nodiscard]] Result<AdmittedBatch> admit_ready(AdmissionService&, Ref<Batch>);
[[nodiscard]] Progress admit(AdmissionService&, Ref<Batch>, Ref<AdmissionRequest>);
[[nodiscard]] Result<AdmittedBatch> inspect(const AdmissionRequest&);
void finish(Ref<AdmissionRequest>, std::span<const Acceptance>, Result<void>) noexcept;

struct Promise {
  std::uint64_t promised_ballot{}, accepted_ballot{};
  Epoch accepted_through{};
  std::span<const Ref<Batch>> accepted_tail;
};
struct Witness;
struct WitnessWrite {
  std::atomic<std::uint32_t> references{};
  void (*retire)(WitnessWrite&) noexcept{};
  allocation::Hold storage;
  Ref<Authority> authority;
  Ref<Log> log;
  Ref<Batch> input;
  port::Source record;
  std::uint64_t ballot{};
  Promise promise;
  Acceptance acceptance;
  Ref<port::Request> retention;
  Signal ready;
  worker::Message completion;
};
struct Witness {
  worker::Lane* lane{};
  ReplicaId replica{};
  std::uint64_t storage_incarnation{};
  std::vector<Ref<Batch>> accepted;
  // vote.accepted_tail borrows accepted or immutable retained Log storage.
  // Response snapshots are copied/retained separately before either changes.
  Promise vote;
  Ref<Authority> authority;
  Ref<Log> log;
};
[[nodiscard]] Progress promise(Witness&, std::uint64_t ballot, Ref<WitnessWrite>);
[[nodiscard]] Progress accept(Witness&, Ref<Batch>, std::uint64_t ballot, Ref<WitnessWrite>);
void finish(Ref<WitnessWrite>, Promise, Acceptance, Result<void>) noexcept;
// One Promise is local accepted history, never a chosen-prefix certificate.
// Voting rules are compiled Journal code; persistence uses the real Log backend.
// Capture backend/transport uses in retained request storage before returning;
// retiring a role facade cannot end those physical uses.

struct OutputScope { ShardId shard{}; Scope scope; };
struct Reservation { TransactionId transaction{}; ShardId participant{}; std::span<const OutputScope> outputs; };
struct PositionFix { TransactionId transaction{}; SerialPosition position{}; ShardId participant{}; };
struct SourceObligation {
  TransactionId transaction{};
  ObjectId object{};
  PartId part{};
  Epoch source_cut{};
  Ref<object::Material> fallback;
  bool future_output{};
};
struct Continuation {
  TransactionId transaction{};
  SerialPosition position{};
  Ref<Interpretation> interpretation;
  port::Source logical_state;
  std::span<const ReadBound> bounds;
  std::span<const SourceObligation> sources;
};
struct TransactionState {
  TransactionId transaction{};
  SerialPosition position{};
  std::span<const ShardId> participants, fixed;
  std::span<const OutputScope> outputs;
  std::span<const ReadBound> bounds;
};
enum class Decision { commit, abort, no_effect };
struct OutputResolution { TransactionId transaction{}; SerialPosition position{}; OutputScope output; Decision decision{}; };
enum class EventOutcome { applied, no_effect, rejected };
struct EventResult { EventId id{}; EventOutcome outcome{}; port::Source result; };
struct Delivery { EventId origin{}; std::uint64_t ordinal{}; StreamId target{}; Ref<Event> event; };
struct SourceProgress { StreamId source{}; port::Source checkpoint; };
struct AuditObligation { EventId origin{}; Ref<Interpretation> interpretation; port::Source interaction, outcome; };
struct Obligations {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  object::Directory<Reservation> reservations;
  object::Directory<TransactionState> transactions;
  object::Directory<OutputResolution> resolved_outputs;
  object::Directory<Continuation> continuations;
  object::Directory<SourceObligation> sources;
  object::Directory<SourceProgress> source_progress;
  object::Directory<Delivery> outbox;
  object::Directory<AuditObligation> audits;
};
struct State {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  ShardId shard{};
  Epoch completed{};
  SourceId admitted_value{};
  object::Directory<object::Object> objects;
  // Shared directory paths; optional obligations also serve embedded ELT/audits.
  object::Directory<Frontier> terminal;
  object::Directory<EventResult> results;
  Ref<Obligations> obligations;
};
struct ReadOptions {
  std::optional<Epoch> historical;
  std::span<const ReadBound> bounds;
  std::optional<SerialPosition> transaction_position;
};
struct Reader;
struct ReadContext {
  const State* selected{};
  Reader* protection{};
  Ref<State> retained;
  std::optional<SerialPosition> transaction_position;
  ReadContext() = default;
  ReadContext(ReadContext&&) noexcept;
  ReadContext& operator=(ReadContext&&) noexcept;
  ReadContext(const ReadContext&) = delete;
  ReadContext& operator=(const ReadContext&) = delete;
  ~ReadContext(); // Close any native reader protection on its caller lane.
};
// Capture the same State while protected, then release native reader protection.
// No state copy/allocation. Required before independent context lifetime/transfer.
// Closed context is invalid; already-retained context is unchanged.
void retain(ReadContext&) noexcept;
struct ReaderDomain;
struct Reader {
  std::atomic<std::uint32_t> references{};
  void (*retire)(Reader&) noexcept{};
  allocation::Hold storage;
  Ref<ReaderDomain> domain;
  worker::Lane* lane{};
  void* registration{};
  Signal changed;
  worker::Message notification;
  std::uint64_t observed_generation{};
  bool context_active{};
  Reader() = default;
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
};
struct Consumer {
  worker::Lane* lane{};
  ShardId shard{};
  Ref<State> completed;
  Ref<Authority> authority;
  Ref<ReaderDomain> readers;
  Signal changed;
};
// One native context per enrolled Reader. Its protection covers immutable State and
// borrowed hierarchy, without a shared State retain per point. Close or retain
// that context before another acquisition through the Reader. A point read uses its
// caller lane/buffer; it need not run at Consumer.lane.
// Fresh Reader setup: failure leaves it unenrolled, with no queued notification.
// Prepare metadata before publishing registration; no fallible work follows it.
[[nodiscard]] Result<void> enroll(Consumer&, worker::Lane&, Reader&);
// Caller-lane leave detaches registration/interest. Queued notifications keep
// record/domain storage alive and cannot revive a detached or reenrolled Reader.
// Precondition: no native context remains protected by this Reader.
void leave(Reader&) noexcept;
[[nodiscard]] Result<ReadContext> read(Consumer&, Reader&, ReadOptions);
// Latest-ready acquisition stays direct. Pending bounds/exact cuts subscribe
// and recheck domain generation, then wait on caller-lane reader.changed.
[[nodiscard]] Progress read(Consumer&, Reader&, ReadOptions, ReadContext& output);
// Result pointer borrows the read context. Assigned-thread observe/subscribe/recheck prevents
// lost wakeup. changed uses retry pulses, not permanent event completion.
[[nodiscard]] Progress observe(Consumer&, EventId, ReadContext& context, const EventResult*& out);
// Other caller lanes use the enrolled publication notification, not changed.
[[nodiscard]] Progress observe(Consumer&, Reader&, EventId, ReadContext& context,
    const EventResult*& out);
struct ObligationChanges {
  std::span<const Reservation> reservations;
  std::span<const PositionFix> fixes;
  std::span<const TransactionState> transactions;
  std::span<const Continuation> waiting;
  std::span<const SourceObligation> sources;
  std::span<const OutputResolution> outputs;
  std::span<const SourceProgress> source_progress;
  std::span<const Delivery> outbox;
  std::span<const AuditObligation> audits;
};
// On Consumer.lane: qualify admission and capture the complete predecessor in
// fresh Draft storage. Failure leaves it unbegun. Assign its execution lane,
// without acquiring that lane's resources; hand off before editing elsewhere.
[[nodiscard]] Result<void> begin(Consumer&, const AdmittedBatch&,
    worker::Lane& execution, object::Draft& storage);
// Frozen Draft and result/update storage stay immutable/alive across waits;
// preparation copies/retains the installed closure into caller-reserved next.
// Validate private admitted provenance and exact current predecessor. Empty
// object directories are valid; sealed and closure checks establish readiness.
// Cross-lane use requires synchronized handoff, not equality of lanes.
// Omitted obligation updates preserve existing obligations.
[[nodiscard]] Progress prepare(Consumer&, const object::Draft&,
    std::span<const EventResult>, Ref<State>& next, const ObligationChanges* = nullptr);
// Same uninterrupted lane visit from final preflight through installation; no allocation/I/O
// or failure after install begins. One State installs objects/cut/results/outbox.
// next is exactly the prepared State; Draft/current predecessor still match.
void publish(Consumer&, const object::Draft&, Ref<State> next) noexcept;

struct Checkpoint {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  Ref<State> completed;
  Ref<Interpretation> interpretation;
  port::Source encoded_state;
  std::span<const Frontier> retained, terminal;
  std::span<const Ref<Batch>> accepted_suffix;
};
struct CheckpointWrite {
  std::atomic<std::uint32_t> references{};
  void (*retire)(CheckpointWrite&) noexcept{};
  allocation::Hold storage;
  Ref<Log> log;
  Ref<Checkpoint> checkpoint;
  RetentionEvidence evidence;
  Signal ready;
  worker::Message completion;
};
struct RecoveryPrefix {
  Ref<Checkpoint> checkpoint;
  Ref<Authority> authority;
  std::span<const Ref<Batch>> learned_suffix;
  std::span<const Acceptance> barrier;
  Epoch through{};
};
[[nodiscard]] Progress checkpoint(Consumer&, Ref<Checkpoint>, Ref<CheckpointWrite>);
void finish(Ref<CheckpointWrite>, RetentionEvidence, Result<void>) noexcept;
[[nodiscard]] Result<void> restore(Consumer&, const RecoveryPrefix&, Ref<State> reconstructed);
// Adopt only after complete owner state/code/material/obligations are retained;
// preserve accepted suffix and old native/retained readers through their actual
// release. Replacing a head cannot invalidate reader protection or regress history.
[[nodiscard]] Result<void> retire_prefix(Producer&, const CheckpointWrite&);
[[nodiscard]] Result<void> retire_prefix(Producer&, MemoryLog&, const CheckpointWrite&);

enum class EffectStatus { acknowledged, unknown };
struct Processor {
  worker::Lane* lane{};
  Ref<State> selected;
  const object::IndexNode* cursor{};
  std::uint64_t offset{};
};
[[nodiscard]] Result<const Delivery*> next(Processor&);
[[nodiscard]] Result<Ref<Event>> handle(Processor&, const Delivery&, EffectStatus);
struct EarlyGrant {
  ProducerId driver{};
  std::uint64_t incarnation{};
  ProducerSequence first{}, last{};
};
// Loss-tolerant early delivery has separate durable disjoint ranges. Committed
// delivery keeps original origin/ordinal and source progress across replacement.

// Ready provider callbacks return unavailable before any effects. Only that
// result permits automatic async fallback; other failures require reconciliation.
// Before submission, ready.lane names the calling role lane; backend/log lane
// may differ. Every returned wait Signal is handled at that caller lane.
// finish captures immutable evidence in request-owned storage before posting its
// retained Message. Message handling validates operation/value/incarnation, then
// completes Signal; backend never writes task storage. Evidence spans must already
// belong to that retained storage. Retry of accepted/finished requests observes the
// same submission; capacity wait before acceptance preserves the idle request.
// All spans/Scope bytes live in their owning retained storage, not task scratch.
// Full epochs may carry logical continuations while independent work completes;
// embedded whole-batch completion does not impose a shard-wide transaction fence.
} // namespace orbital::journal
