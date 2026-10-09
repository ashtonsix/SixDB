#pragma once

// Provisional Object/Representation/Part/View interfaces; declarations only.
// See ../../../DESIGN.md for the combined Orbital/Loom recommendation.

#include "port.h"
#include <memory>
#include <optional>
#include <variant>

namespace orbital::candidate::object { struct Draft; }
namespace orbital::candidate::journal {
struct State;
struct ReadContext;
struct Batch;
void retain_ref(State&) noexcept;
void release_ref(State&) noexcept;
void retain_ref(Batch&) noexcept;
void release_ref(Batch&) noexcept;
struct Consumer;
struct AdmittedBatch;
[[nodiscard]] Result<void> begin(Consumer&, const AdmittedBatch&, worker::Lane&, object::Draft&);
}
namespace orbital::candidate::object {
struct Object;
struct Representation;
struct Part;
struct ViewRequest;
struct Material;
struct Lock;
struct LockSet;
struct LockIndex;
struct IndexNode;
struct BufferIndex;
struct Buffer;
struct RetainedView;
void retain_ref(IndexNode&) noexcept;
void release_ref(IndexNode&) noexcept;
void retain_ref(LockSet&) noexcept;
void release_ref(LockSet&) noexcept;
void retain_ref(Lock&) noexcept;
void release_ref(Lock&) noexcept;
void retain_ref(Material&) noexcept;
void release_ref(Material&) noexcept;
void retain_ref(Buffer&) noexcept;
void release_ref(Buffer&) noexcept;
void retain_ref(RetainedView&) noexcept;
void release_ref(RetainedView&) noexcept;
// Typed value handle shares actual immutable index nodes. Nodes retain their
// complete row/metadata closure independently of State. Geometry stays open;
// no separately allocated/refcounted directory wrapper is required.
template<class Row> struct Directory { Ref<IndexNode> root; };
struct Cursor { const IndexNode* position{}; std::uint64_t offset{}; };

enum class Access { read, write };
enum class ViewMethod { populate, copy, copy_on_write, take };
struct Footprint {
  std::span<const ByteRange> reads, writes;
  std::uint64_t readable_padding{};
  std::size_t alignment{1}; // Required View base alignment; typed offsets remain caller-qualified.
};
using Populate = Progress (*)(const Material&, ViewRequest&, allocation::Pool&);
struct LockOps {
  bool (*conflicts)(Scope, Scope);
  Result<void> (*authorize)(const Lock&, const Part&, Footprint);
};
struct LockSet {
  std::atomic<std::uint32_t> references{};
  const LockOps* ops{};
  allocation::Hold storage;
  std::unique_ptr<LockIndex> index; // Owns shared metadata/synchronization, not native execution.
  LockSet();
  ~LockSet();
};
struct Lock {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  Ref<LockSet> set;
  Scope scope;
  Access access{};
  Epoch selected{};
  std::optional<SerialPosition> transaction_position;
  Draft* writer{};
  std::uint64_t generation{};
  bool open{};
};

// Owner/Ikea interpretation includes metadata and unchanged dependencies.
// This is an extensible retained recipe, not a universal byte-patch format.
struct Material {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  SourceId identity{};
  ProgramId format{};
  Bytes metadata;
  std::span<const port::Source> sources;
  std::span<const Ref<Material>> dependencies;
  Populate populate{};
};
struct Part {
  PartId id{};
  std::uint64_t size{};
  Ref<Material> material;
};
struct Representation {
  ReplicaId replica{};
  Bytes physical_state;
  Directory<Part> parts;
};
struct Object {
  ObjectId id{};
  Epoch version{};
  Bytes logical_state;
  Ref<LockSet> locks;
  Directory<Representation> representations;
};
struct Buffer {
  std::atomic<std::uint32_t> references{};
  allocation::Hold storage;
  worker::Lane* lane{};
  ObjectId object{};
  PartId part{};
  ReplicaId replica{};
  Ref<Material> material;
  // Owned mutable/reconstructed bytes or a compatible immutable native slice.
  std::variant<allocation::Allocation, port::Source> backing;
  BufferGeneration generation{};
  std::span<const ByteRange> prepared;
  struct View* users{};
  std::uint32_t preparations{};
  bool acquisition_closed{};
  Signal changed;
};
struct BufferCache {
  // Stable caller-owned context for one Worker Lane; Loom chooses cache policy.
  // Drain cached backing, preparations and Views before that Worker shuts down.
  worker::Lane* lane{};
  std::unique_ptr<BufferIndex> index; // Cache for immutable Material; records can be filled/evicted.
  BufferCache();
  ~BufferCache();
};
struct Selection {
  const Object* object{};
  const Representation* representation{};
  const Part* part{};
};
// Registration keeps Buffer backing stable. Native use borrows its Allocation
// or immutable Source; it needs no separate allocation capture.
struct View {
  Buffer* buffer{};
  Lock* authority{};
  Footprint footprint;
  View* next{};
  std::byte* base{};
  std::uint64_t logical_size{}, readable_extent{};
  View() = default;
  View(const View&) = delete;
  View& operator=(const View&) = delete;
  View(View&&) = delete;
  View& operator=(View&&) = delete;
  ~View(); // Retire any still-open native registration on Buffer lane.
};
// Only actual escape needs an independently retained record and owned ranges.
// The pool/record owner retires this RetainedView, independently of its former Task.
struct RetainedView {
  std::atomic<std::uint32_t> references{};
  // Final foreign release keeps this record alive through Buffer-lane teardown.
  void (*retire)(RetainedView&) noexcept{};
  allocation::Hold record_storage;
  Ref<Buffer> buffer;
  Ref<Lock> authority;
  std::span<ByteRange> footprint_storage;
  std::uint64_t authority_generation{};
  View view; // Destroy registration before retained Buffer/Lock/storage.
};
struct ViewRequest {
  Footprint footprint;
  std::uint64_t logical_size{};
  Epoch selected_epoch{};
  std::optional<SerialPosition> transaction_position;
  ViewMethod method{ViewMethod::copy};
  Ref<Material> material;
  Ref<Buffer> buffer;
  Ref<Lock> authority;
  BufferGeneration generation{}; // Captured storage generation; retains no addresses.
  std::uint64_t authority_generation{};
  std::span<Ref<port::Request>> requests;
  std::size_t submitted{};
  bool cancelled{}, registered{};
  ViewRequest() = default;
  ViewRequest(ViewRequest&&) noexcept;
  ViewRequest& operator=(ViewRequest&&) noexcept;
  ViewRequest(const ViewRequest&) = delete;
  ViewRequest& operator=(const ViewRequest&) = delete;
  ~ViewRequest();
};
struct Draft {
private:
  Ref<journal::Batch> input_;
  Ref<journal::State> predecessor_;
  friend Result<void> journal::begin(journal::Consumer&, const journal::AdmittedBatch&,
      worker::Lane&, Draft&);
public:
  worker::Lane* lane{};
  Directory<Object> frozen; // Empty is valid; sealed marks completed freezing.
  allocation::Hold storage;
  std::span<Object> candidates;
  std::span<Ref<Lock>> locks;
  std::unique_ptr<BufferIndex> buffers; // Draft buffers never alias read cache entries.
  std::uint64_t generation{};
  bool sealed{};
  Draft();
  ~Draft();
  Draft(const Draft&) = delete;
  Draft& operator=(const Draft&) = delete;
  Draft(Draft&&) = delete;
  Draft& operator=(Draft&&) = delete;
  const Ref<journal::Batch>& input() const noexcept { return input_; }
  const Ref<journal::State>& predecessor() const noexcept { return predecessor_; }
};

[[nodiscard]] Result<const Object*> select(const journal::ReadContext&, ObjectId);
[[nodiscard]] Result<const Representation*> representation(const Object&, ReplicaId);
[[nodiscard]] Result<const Part*> part(const Representation&, PartId);
[[nodiscard]] Result<const Object*> next(const journal::ReadContext&, Cursor&);
[[nodiscard]] Result<const Representation*> next(const Object&, Cursor&);
[[nodiscard]] Result<const Part*> next(const Representation&, Cursor&);
[[nodiscard]] Result<Ref<Lock>> acquire(LockSet&, Scope, const journal::ReadContext&, Lock&);
[[nodiscard]] Result<Ref<Lock>> acquire(LockSet&, Scope, Access, Draft&, Lock&);
// Setup/cold metadata growth, never an implicit part of ready try_view.
[[nodiscard]] Result<void> reserve(BufferCache&, std::size_t buffers);
// Owner-local victim inspection; borrows index storage until its next mutation.
[[nodiscard]] std::span<const Ref<Buffer>> resident(const BufferCache&) noexcept;
// Cache member only, with valid immutable reconstruction and no native users.
// False means ineligible. Success drops resident backing/coverage and advances
// placement generation; it does not prove any physical bytes were freed.
// With no pending preparations, also remove the cache entry and return its
// metadata slot. Otherwise retain indexed identity through preparation handoff.
[[nodiscard]] Result<bool> evict(BufferCache&, Buffer&) noexcept;
// Ready-only on a closed View. False/error leave it closed, with no registered
// use, queued work or implicit allocation.
[[nodiscard]] Result<bool> try_view(const journal::ReadContext&, Selection,
    BufferCache&, Footprint, View&, Lock* = nullptr);
[[nodiscard]] Result<bool> try_view(Draft&, Selection, Footprint, View&, Lock&);
// Capture material/cut/geometry/authority once, without claiming target backing.
// Error leaves preparation closed. Borrowed footprints outlive advance; retain
// any additional owner interpretation/selection metadata needed after this call.
[[nodiscard]] Result<void> begin(ViewRequest&, const journal::ReadContext&, Selection,
    BufferCache&, Footprint, std::span<Ref<port::Request>>, Lock* = nullptr,
    ViewMethod = ViewMethod::populate);
[[nodiscard]] Result<void> begin(ViewRequest&, Draft&, Selection,
    Footprint, std::span<Ref<port::Request>>, Lock&, ViewMethod = ViewMethod::copy);
// Capture into independently retained reserved storage. Copies exact ranges;
// native source remains unchanged. Failure leaves the RetainedView closed.
[[nodiscard]] Result<Ref<RetainedView>> retain_view(RetainedView&, const View&);
[[nodiscard]] Progress advance(ViewRequest&, View&, allocation::Pool&);
// Owner proved exact native encoding; Source.extent maps to Part byte zero.
// Validates built-in memory, extent/alignment/padding/coverage; rejects writes.
[[nodiscard]] Result<void> bind_native(ViewRequest&, port::Source);
void cancel(ViewRequest&) noexcept;
void release(View&, allocation::ReleaseHint = {}) noexcept;
[[nodiscard]] Result<Bytes> read(const View&, ByteRange);
[[nodiscard]] Result<MutableBytes> write(View&, ByteRange);
// Capture immutable coverage; owned backing uses supplied storage, Source backing
// retains/slices its resource. Mutable aliases fail; owner copying is explicit.
[[nodiscard]] Result<port::Source> capture_native(View&, ByteRange, SourceId, port::MemoryResource&);
[[nodiscard]] Result<Ref<Material>> capture(allocation::Pool&, SourceId,
    ProgramId format, Bytes metadata, std::span<const port::Source>,
    std::span<const Ref<Material>> dependencies, Populate);

[[nodiscard]] Result<Object*> create(Draft&, ObjectId);
[[nodiscard]] Result<Object*> edit(Draft&, ObjectId);
[[nodiscard]] Result<Representation*> edit(Draft&, Object&, ReplicaId);
[[nodiscard]] Result<Part*> edit(Draft&, Representation&, PartId);
[[nodiscard]] Result<void> erase(Draft&, ObjectId);
[[nodiscard]] Result<Representation*> add_representation(Draft&, Object&, ReplicaId);
[[nodiscard]] Result<void> remove_representation(Draft&, Object&, ReplicaId);
[[nodiscard]] Result<Part*> add_part(Draft&, Representation&, PartId);
[[nodiscard]] Result<void> remove_part(Draft&, Representation&, PartId);
// Preserve registered physical footprints, including readable padding. A rare
// grow can capture their maximum extent with one temporary allocation Hold;
// disjoint in-place growth remains possible. Source backing is immutable.
[[nodiscard]] Result<void> grow(Draft&, Part&, std::uint64_t, allocation::Pool&);
[[nodiscard]] Result<void> state(Draft&, Object&, Bytes logical);
[[nodiscard]] Result<void> state(Draft&, Representation&, Bytes physical);
[[nodiscard]] Result<void> stage(Draft&, Part&, Ref<Material>);
// On the execution lane, close writers and fill frozen on success. This changes
// no visible State. Synchronized handoff lets Journal consume the sealed result at its
// publication lane; input/basis provenance survives that handoff.
[[nodiscard]] Progress freeze(Draft&);
// Clear sealed before discarding private edits/frozen result; invalidate old
// write access. Preserve exact admitted input/predecessor for repair/retry.
// Destroying Draft never resolves that admitted obligation.
void discard(Draft&) noexcept;

// ReadContext qualifies selection once; hierarchy pointers borrow it or Draft.
// Keep that context while borrowed hierarchy/owner metadata is still used.
// View borrows footprints through release and prevents eviction/rebind of actual
// backing. Buffer owns the Allocation or protected immutable Source. Registration
// preserves addresses and readable padding without a per-View physical capture.
// Qualification/staging/freeze run on the assigned thread. Qualified conflict-safe regions may
// run ordinary threads within a stable owner scope; join accesses before changing
// authority/registration, capturing or freezing. Escape requires RetainedView.
// Cold metadata keeps material identity, not backing. Requests retain captured
// targets even before acceptance; stale target results cannot fill replacements.
// Waiting/escaping uses capture revocable authority generation; direct native
// calls use the live qualified Lock. Actual physical uses outlive Task interest.
// Lock.writer borrows only an open matching Draft generation. Close authority
// and clear writer before destroying Draft; retained uses retain bytes/Lock,
// never a stale Draft pointer. Finish native writes before closing.
} // namespace orbital::candidate::object
