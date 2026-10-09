# Send a native partition to another worker

Engine chooses a legal partition. Loom binds its route and receiving capacity
before the batch; CPU placement grants no shard or publication authority.
This same-host example computes one checksum into reserved output. Its C++
blocks compile in reading order; the private channel and Message handlers remain
declarations, not a new transport implementation.

The caller supplies independent pooled records. Each accepted work item owns
its retained input View, output hold and reserved completion/ACK records until actual
use and Message handling finish. These records cannot live in a detachable
Task's storage.

```cpp
#include <loom/worker.h>
#include <orbital/candidate/object.h>
#include <cstring>
#include <utility>

namespace orbital::candidate::examples {
namespace partition_route { struct Route; }

struct PartitionWork {
  std::atomic<std::uint32_t> references{};
  void (*retire)(PartitionWork&) noexcept{};
  allocation::Hold record_storage;
  Ref<object::RetainedView> input;
  Bytes bytes;
  allocation::Hold output;
  partition_route::Route* route{};
  worker::Lane* caller{};
  worker::Message finished, acknowledged;
  Signal done;
  std::atomic<bool> cancel_requested{};
  bool produced{}, ack_sent{};
};
void retain_ref(PartitionWork&) noexcept;
void release_ref(PartitionWork&) noexcept;

namespace partition_route {
extern const worker::MessageOps finished_ops;
// Refusal changes neither work nor route credit. Acceptance retains the record
// and its output credit, beyond queue dequeue, through ACK handling.
[[nodiscard]] bool send(Route&, const Ref<PartitionWork>&) noexcept;
// Post the work's reserved ACK at the bound receiver; it cannot be refused by
// workload saturation. ACK handling returns credit; final release retires work.
void acknowledge(Route&, Ref<PartitionWork>) noexcept;
}
```

Reserve output, record and channel resources before capturing input. Output is
exclusive to this Work until physical completion and acknowledgement. This
setup validates the native byte range once. It captures an escaping View;
the original native scope may close afterward. The Work's byte span borrows
that RetainedView, not the original frame.

```cpp
Result<void> prepare_partition(PartitionWork& work, worker::Lane& caller,
    partition_route::Route& route, const object::View& view, ByteRange range,
    object::RetainedView& retained_view,
    allocation::Hold output) {
  if (output.footprint.size < sizeof(std::uint64_t))
    return std::unexpected(Error::invalid_argument);
  auto bytes = object::read(view, range);
  if (!bytes) return std::unexpected(bytes.error());
  auto use = object::retain_view(retained_view, view);
  if (!use) return std::unexpected(use.error());
  work.input = std::move(*use);
  work.bytes = *bytes;
  work.output = std::move(output);
  work.route = &route;
  work.caller = &caller;
  work.done.lane = &caller;
  work.finished.context = &work;
  work.finished.ops = &partition_route::finished_ops;
  return {};
}
```

Try the bound route with a retained Work. A refusal leaves it with the caller;
retry or release its prepared resources. An accepted Work is queued or running
at the receiver. The receiver may check cancellation once at this outer native
boundary, then uses ordinary qualified bytes. The native region has an
owner-selected size bound; no per-byte scheduling or authority checks occur.

```cpp
void execute_partition(Ref<PartitionWork> work) noexcept {
  if (!work->cancel_requested.load(std::memory_order_acquire)) {
    std::uint64_t checksum = 0;
    for (auto byte : work->bytes)
      checksum = checksum * 131 + std::to_integer<unsigned>(byte);
    std::memcpy(work->output.address, &checksum, sizeof(checksum));
    work->produced = true;
  }
  worker::post(*work->caller, work->finished);
}
```

Physical access ends before posting. The completion Message retains the
Work and establishes happens-before. Its handler calls `handle_partition_completion` on the caller's thread
below. The retained Work keeps its bound route and cleanup state available even
after caller interest ends. ACK handling retires input/output uses and returns
credit; its reserved record remains alive through final Message release.
This private output uses host-native representation, not a wire format.

```cpp
void acknowledge_partition(PartitionWork& work) noexcept {
  if (!work.ack_sent) {
    work.ack_sent = true;
    partition_route::acknowledge(*work.route, Ref<PartitionWork>::retain(work));
  }
}

void handle_partition_completion(PartitionWork& work) noexcept {
  complete(work.done, {});
  if (work.cancel_requested.load(std::memory_order_acquire))
    acknowledge_partition(work);
}

Result<std::uint64_t> take_partition(PartitionWork& work) {
  if (work.done.state != Readiness::ready)
    return std::unexpected(Error::unavailable);
  if (work.ack_sent) return std::unexpected(Error::stale);
  Result<std::uint64_t> result{std::unexpected(Error::rejected)};
  if (work.produced && !work.cancel_requested.load(std::memory_order_acquire)) {
    std::uint64_t checksum;
    std::memcpy(&checksum, work.output.address, sizeof(checksum));
    result = checksum;
  }
  acknowledge_partition(work);
  return result;
}

void cancel_partition(PartitionWork& work) noexcept {
  work.cancel_requested.store(true, std::memory_order_release);
  if (work.done.state == Readiness::ready) acknowledge_partition(work);
}

} // namespace orbital::candidate::examples
```

Take and cancellation run on the caller's assigned thread. Cancel before dropping caller interest;
it cannot interrupt/reclaim an actual native use. If physical work is pending,
completion handler discards unwanted output and ACKs independently of any Task.
If completion Message was already handled, cancellation ACKs immediately. The caller-thread
flag prevents double ACK, including when cancellation follows consumption.
An accepted but unstarted cancellation still needs completion and credit return.
Even unwanted work follows physical drain and ACK before reuse.
Never put that ACK behind workload dispatch gated on the
credit the ACK restores. The caller, route and Lanes remain valid through all
reserved messages; the pool's retirement callback handles final record/storage
release.

Engine folds partition results at its logical boundaries. Worker completion
does not publish; [epoch installation](jobs.md#execute-an-admitted-epoch) stays
with Journal. The [pressure evidence](../../../spikes/loom-execution/FINDINGS.md#output-pressure-and-physical-completion)
tests real buffers, cancellation and reserved returns. Actual Orbital
View/Port/channel coupling still needs implementation validation.
