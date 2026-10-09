# Uses that outlive the caller

Native C++ uses a protected view directly. Isolated extensions and I/O can
continue after that native scope ends, so they capture their own uses. These
are separate examples; the C++ blocks form one translation unit, in order.

## Run an isolated extension

Start on the view's assigned thread. Supply a pooled execution record, RetainedView
with reserved footprint storage, and retained descriptor storage. Arguments are
an immutable captured source. Interpretation owns the runtime profile; executor
choice does not change those semantics. Pool setup is outside this example.

```cpp
#include <orbital/candidate/journal.h>
#include <orbital/candidate/executor.h>
#include <array>
#include <utility>

namespace orbital::candidate::examples {

Progress start_extension(Ref<execution::Execution> execution, execution::Executor& executor,
    worker::Lane& caller, Ref<journal::Interpretation> interpretation,
    execution::ExecutionContext context, port::Source arguments,
    const object::View& view, ByteRange coverage, bool writable,
    object::RetainedView& retained_view, std::span<execution::Projection> descriptors) {
  auto retained = object::retain_view(retained_view, view);
  if (!retained) return Progress::fail(retained.error());
```

Describe the exact region and rights. `execution::prepare` copies this local projection
into supplied descriptors and rejects holes or escalated permissions. The RetainedView
owns copied footprints and physical protection, so the local array and original
view can end after preparation. The caller lane receives completion.

```cpp
  const std::array projections{execution::Projection{std::move(*retained), coverage, writable}};
  auto prepared = execution::prepare(*execution, executor, caller,
      std::move(interpretation), context, std::move(arguments), projections, descriptors);
  if (!prepared) return Progress::fail(prepared.error());
  return execution::start(std::move(execution));
}
```

Return control while execution proceeds. A capacity wait before acceptance
leaves it idle; resume with `execution::start(execution)`, without repeating capture
and preparation. Pending work is observed without duplicate submission.
Cancellation does not end physical use. Failed writable execution may already
have changed private projected bytes; repair or discard those before retry.

The owner chooses whether to check and whether checking delays publication.
For synchronous checking, launch checker executions through the same path.
Share immutable input; give each run independent writable projections/results.
Later on the caller thread, process completion Messages and wait for all reports.
Verification compares the main outcome and complete interaction. Fresh
observations must first become agreed owner input; matching reports alone
cannot authorize their import.

Engine/Ikea supplies the ordinary import function. It revalidates original
changes/authority/coverage and applies output with unit/index/summary maintenance.

```cpp
Result<void> import_and_maintain(object::Draft&, const execution::Execution&) noexcept;

Result<void> finish_extension(object::Draft& draft, Ref<execution::Execution> main,
    std::span<const Ref<execution::Execution>> checkers, bool compare_exact_representation) {
  auto& caller = *draft.lane;
  if (main->finished.lane != &caller)
    return std::unexpected(Error::invalid_argument);
  worker::poll(caller, 32);
  auto progress = execution::start(main);
  if (progress.kind() == Progress::Kind::failed)
    return std::unexpected(progress.failure());
  auto report = execution::inspect(*main);
  if (!report) return std::unexpected(report.error());
  for (const auto& checker : checkers) {
    if (checker->finished.lane != &caller)
      return std::unexpected(Error::invalid_argument);
    auto progress = execution::start(checker);
    if (progress.kind() == Progress::Kind::failed)
      return std::unexpected(progress.failure());
    auto checked = execution::inspect(*checker);
    if (!checked) return std::unexpected(checked.error());
  }
  auto verified = execution::verify(*main, checkers, compare_exact_representation);
  if (!verified) return verified;
  return import_and_maintain(draft, *main);
}
```

A pending report returns `unavailable`; return later. Once import starts,
failure can leave partial private stores that need repair/discard before retry.
Stop calling this function after the first successful import.
Keep execution handles and output through import, recovery or failure handling.
Import does not publish: the [epoch path](jobs.md#execute-an-admitted-epoch)
still installs the complete State. Executions/projections retire after actual
use and completion-message handling, independently of the caller's Task.

## Send committed output

Start with committed outbox intent and stable contribution identity. Shore's
receiver protocol supplies credit before producing payload. The transfer pool
provides request/range/result storage and retains this entire record through
physical completion and completion-message handling.

```cpp
struct ReceiverCredit;
void retain_ref(ReceiverCredit&) noexcept;
void release_ref(ReceiverCredit&) noexcept;
struct SinkTransfer {
  port::Request request;
  allocation::Allocation bytes;
  Ref<ReceiverCredit> credit;
  std::uint64_t contribution{}, offset{};
};
```

On the caller thread, hold the already produced payload and prepare the send.
The owner keeps bytes stable through physical completion. Advance a prepared
request instead of retargeting or submitting it twice.

```cpp
Progress send(SinkTransfer& transfer, worker::Lane& caller, port::Destination sink,
    std::span<const port::TransferRange> ranges) {
  if (transfer.request.prepared) return port::advance(transfer.request);
  auto memory = allocation::hold(transfer.bytes, {0, transfer.bytes.size});
  if (!memory) return Progress::fail(memory.error());
  return port::write(transfer.request, caller, std::move(sink), std::move(*memory),
      ranges, port::Persistence::none);
}
```

Return control. Submission acceptance is not successful delivery. On a later
caller-thread visit, process completion Messages and inspect the same frozen request.
A terminal failure can still include transferred ranges needed for recovery.
The completion pointer borrows the transfer record.

```cpp
Result<const port::Completion*> inspect_send(SinkTransfer& transfer, worker::Lane& caller) {
  if (transfer.request.finished.lane != &caller)
    return std::unexpected(Error::invalid_argument);
  worker::poll(caller, 32);
  auto progress = port::advance(transfer.request);
  auto completion = port::inspect(transfer.request);
  if (completion) return completion;
  if (progress.kind() == Progress::Kind::failed)
    return std::unexpected(progress.failure());
  return completion;
}

} // namespace orbital::candidate::examples
```

Inspect outcome and known transferred prefixes together. Partial/unknown
effects need receiver reconciliation, not blind retry. Complete local send
still cannot return credit: retain the transfer through ACK or explicit
reconciliation. Commit delivery/source progress together, then retire resolved
credit and backing through their owning protocols.
