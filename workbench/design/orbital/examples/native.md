# Native batches and memory rotation

Begin with useful native work. Engine supplies a legal operation, Ikea supplies
its native regions, and Orbital supplies qualified bytes. Loom need not see
every kernel or value. This example sums byte lookups to expose the boundary;
it does not recommend a byte-array database layout. The C++ blocks compile in
reading order against actual Loom, with future Object/Journal calls below.

The caller validates offsets and qualifies the complete accessed region. Its
View, footprint arrays, authority and owner state stay stable through use.
Keeping backing allocated alone is insufficient. A direct call needs no Task,
Ring or per-value retain:

```cpp
#include <loom/ring.h>
#include <orbital/journal.h>
#include <array>
#include <optional>

namespace candidate {
using namespace orbital;

std::uint64_t lookup_native(Bytes bytes, std::span<const std::size_t> offsets) noexcept {
  std::uint64_t sum = 0;
  for (auto offset : offsets) sum += std::to_integer<unsigned>(bytes[offset]);
  return sum;
}
```

Choose rotation when independent memory accesses repay it. The operation owns
one concrete Loom Ring bank, allocated from its Worker's Pool and configured
once outside dispatch. Keep it for later batches; close it before Worker
shutdown. No generic registry or callback erasure is required.

```cpp
struct LookupJob;
using ByteRing = loom::Ring<LookupJob*, const std::byte*>;

Result<void> prepare_bank(loom::Worker& worker, ByteRing& bank) {
  return bank.configure(worker, {.active = 16, .overflow = 16});
}

struct LookupJob {
  loom::Task task;
  ByteRing* bank{};
  Bytes bytes;
  std::span<const std::size_t> offsets;
  std::size_t next{};
  std::uint64_t sum{}, resume_budget{};
};

LOOM_NATIVE_CC void consume_byte(ByteRing* bank, ByteRing::Slot* slot,
    LookupJob* job, const std::byte* address) {
  job->sum += std::to_integer<unsigned>(*address);
  bank->complete(*slot);
  [[clang::musttail]] return ByteRing::yield(bank, slot, job, address);
}
```

The native ABI carries pointers, not owning handles or temporary Views. The
job and every accepted address remain protected through active and overflow
entries. Prefetch reports no readiness. Here offset validation proves each
one-address hint; it grants no right to load the rest of the cache line.

An outer Task fills bounded inventory, spins a bounded quantum and returns to
ordinary service. Its sum changes only when a native callback consumes a byte.
The real `spin` result counts visits, not values or bytes ready. Completion
leaves the configured bank empty for reuse.

```cpp
Progress advance_lookup(LookupJob& job) noexcept {
  while (job.next != job.offsets.size()) {
    const auto* address = job.bytes.data() + job.offsets[job.next];
    if (!job.bank->push(consume_byte, {&job, address},
        {address, loom::Hint::l1_1})) break;
    ++job.next;
  }
  job.bank->spin({job.resume_budget});
  if (job.next == job.offsets.size() && !job.bank->pending())
    return Progress::done();
  return Progress::yield();
}
```

Start a fresh stable frame on the bank's assigned Worker. The caller gives this
job exclusive use of the bank until drain or discard. Refusal changes no bank
inventory. Start queues without running inline, so setup finishes before poll.
The qualified native scope and offsets remain stable across those polls; an
uninterrupted Allocation borrow alone does not establish that guarantee.

```cpp
Result<void> start_lookup(LookupJob& job, loom::Worker& worker, ByteRing& bank,
    Bytes bytes, std::span<const std::size_t> offsets, std::uint64_t resume_budget) {
  if (!resume_budget || !bank.configured() || bank.pending())
    return std::unexpected(Error::invalid_argument);
  for (auto offset : offsets)
    if (offset >= bytes.size()) return std::unexpected(Error::invalid_argument);
  job.bank = &bank;
  job.bytes = bytes;
  job.offsets = offsets;
  job.resume_budget = resume_budget;
  job.task.context = &job;
  job.task.step = [](loom::Task& task) noexcept {
    return advance_lookup(*static_cast<LookupJob*>(task.context));
  };
  if (!loom::start(worker, job.task)) return std::unexpected(Error::capacity);
  return {};
}
```

On `done`, the sum is final and the frame has no Worker registration. At a
returned boundary, detach and discard before ending an abandoned job's native
scope. The operation bank remains configured. Destroy frames only after their
borrowers stop, and call `bank.close()` before shutting down its Worker.

```cpp
void abandon_lookup(LookupJob& job) noexcept {
  loom::detach(job.task);
  if (job.bank && job.bank->pending()) job.bank->discard();
}
```

Callback grain, active width, overflow, hints and outer poll cadence are
independent choices. Only native memory rotation belongs inside the tiny
switch. Task yield returns to the scheduler for completion service and other
work. An operation preserves finished private writes and outstanding uses
across its slower waits; discard never resolves admitted Journal input.
The [Loom findings](../../../spikes/loom-execution/FINDINGS.md) support this
split, not this toy operation's performance. The measured SeriesPack/Bec256
pair favors direct work. Validate each concrete native ABI with the pinned
compiler before treating it as usable.

## Move one item to a slow path

A prefix stage may discover which Part contains its value. That stage ends at
a native boundary and returns the discovered locator to its owner; the ring
dispatcher does not test every slot for Port readiness. The owner resolves the
locator against the query's selected context and uses this next step. This
example handles one item in an already accepted parent with a
[ColdRead](jobs.md#read-a-cold-byte) frame. Before changing phase, the parent
retires that item's native entry and leaves its operation bank empty. Other Tasks
and Rings continue independently. Output here is one byte, already reserved.

```cpp
struct ColdRead;
Result<void> continue_read(loom::Task&, ColdRead&, object::BufferCache&,
    const journal::ReadContext&, object::Selection,
    ByteRange, std::span<Ref<port::Request>>);

Result<std::optional<std::byte>> value_after_prefix(loom::Task& parent,
    const journal::ReadContext& context, object::Selection selected, ByteRange range,
    ColdRead& cold, object::BufferCache& cache, std::span<Ref<port::Request>> requests) {
  if (range.size != 1) return std::unexpected(Error::invalid_argument);
  const std::array ranges{range};
  object::View view;
  auto ready = object::try_view(context, selected, cache, {ranges, {}}, view);
  if (!ready) return std::unexpected(ready.error());
  if (*ready) {
    auto bytes = object::read(view, range);
    if (!bytes) return std::unexpected(bytes.error());
    auto value = bytes->front();
    object::release(view, {.temperature = allocation::Temperature::hot});
    return std::optional{value};
  }
  auto started = continue_read(parent, cold, cache, context, selected, range, requests);
  if (!started) return std::unexpected(started.error());
  return std::nullopt;
}

} // namespace candidate
```

A value completes this item directly. On `nullopt`, the parent returns Task
yield; its next visit runs the cold continuation using the same accepted slot.
There is no second Task acceptance or parked memory ring. A preparation failure keeps
the discovered locator and completed prefix work for error handling or retry.
Once the ViewRequest stores the
Material, it keeps that version/identity across waits; it never selects a newer State
on resume. Copy any additional decoding metadata before releasing query context.
The cold job owns only state needed after this boundary. A larger batch can use
its own bounded fan-in; this one-item example does not define that policy.
