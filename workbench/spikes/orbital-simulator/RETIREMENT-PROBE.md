# Checkpoint, reconstruction and physical retirement probe

This closes one narrow gap in the learning spike before the maintained
simulator grows: can an old version be reconstructed from retained bytes after
restart, while obsolete records are actually removed? It also separates that
logical obligation from a backend operation's physical borrow of memory.
These are executable protocol fixtures, not a proposed Orbital GC policy.

## What runs

`retirement_probe.py` starts with a 1-KiB byte string, four persisted XOR
operations, a persisted decoder identity, and two durable roots: an old reader
at version 2 and a replay consumer needing the original chain through version
4. The owner reconstructs version 4 through storage reads, writes a byte-bearing
checkpoint, and only after that write completes replaces the current head.
The collector enumerates keys in pages of two and deletes records unreachable
from the current head and surviving roots.

In separate paired cases, one root closes while the other remains. After
collection, the storage device resets and the owner is recreated with no saved
values. Its first access through the remaining root must load the descriptor,
decoder, base and operations and reconstruct the exact expected bytes. It then
closes that root and collects the old chain. Another device reset verifies that
the surviving checkpoint really reconstructs the current bytes. A fault variant
also cuts power after the checkpoint is durable but before its callback.

A separate memory fixture gives two backend jobs the same frame, closes the
owner's view, and crashes and restarts the owning process. A request to reuse
the entire memory pool fails while either borrower remains and succeeds after
both retire. The negative control omits only the second borrower's pin; reuse
then becomes possible after the first completion while the second job still
runs. Old callbacks are discarded without leaking their remaining references.

The independent storage oracle rebuilds durable inventory from write/deletion
receipts. Every deletion is checked against live roots; head publication is
checked against actual checkpoint bytes. Reconstructed bytes and version must
match the authored history and the durable root, so returning the latest value
for an old root cannot pass merely because it is internally consistent.

## Observed result

The retained receipt is [evidence/retirement-probe.json](evidence/retirement-probe.json).
Seeds 1, 7 and 19 cover 24 storage histories and six borrower histories:

| Control | Histories | Observation |
| --- | ---: | --- |
| Retain either root, with/without lost checkpoint callback | 12 | Old bytes and later checkpoint bytes recovered; no oracle errors |
| Omit live reader or replay root during collection | 6 | Actual delayed reconstruction fails; premature deletion identified |
| Publish head before checkpoint bytes are durable | 3 | Head ordering rejected; checkpoint unavailable after reset |
| Existing whole-prefix scan instead of paged listing | 3 | Explicit memory wait; old-read milestone never reached |
| Both physical borrowers pinned | 3 | Reuse blocked until both retire; no error or leaked frame |
| Second physical borrower unpinned | 3 | Actual reuse available too early; lifetime violation identified |

In the successful storage fixtures, modeled charged memory peaks at 6,262 bytes
within an 8,192-byte pool. After root release, 2,192 durable encoded bytes remain
and 3,171 bytes have been reclaimed. With the same data and budget, whole-prefix
scan stalls while holding the 4-KiB workspace. These are synthetic accounting
results, not machine memory measurements or a throughput/capacity estimate.
All negative controls are intentional failures, not unexplained simulator errors.

## Implications for the maintained simulator

- Keep durable root policy in actors, with checkpoint publication and physical
  deletion as separately observable operations. A completed latest checkpoint
  does not discharge an old context or a lagging consumer's replay dependency,
  including its decoder version.
- Provide bounded directory access and durable retirement at the storage port.
  A whole-record-prefix scan can prevent reclamation precisely when memory is
  scarce. Directory consistency during concurrent changes remains a separate
  contract; this fixture has only one serialized owner.
- Represent backend borrows independently of an actor's view ownership and
  callback lifetime. Prefer an operation that receives a borrowed buffer
  capability to one that receives payload and an optional unrelated pin list.
- Let port adapters be supplied at construction. The spike hardcodes `Context`
  inside dispatch, so `retirement_ports.py` has to copy that dispatch shell to
  add two physical operations. That is avoidable coupling in a lasting runtime.

The adapter charges listing replies and deletion requests through the existing
disk queue, bandwidth, latency and buffer leases. It assumes atomic per-record
deletion, just as existing writes are atomic. Its environment-side directory
search sorts an in-memory map; it models bounded responses, not directory CPU
complexity or a filesystem implementation. Its paging is not a stable snapshot
under concurrent mutation.

The 4-KiB actor workspace is an explicit synthetic allowance, not a measurement
of Python allocations. Payloads are real reconstructed bytes encoded as hex
across JSON ports. The memory fixture tests pin accounting and allocator reuse,
not actual pointer aliasing, use-after-free, mappings, COW, or page faults.
The fixtures do not compose a concurrent collector with checked transactions,
prove finite storage under indefinite unresolved roots, implement distributed
root ownership, or settle an Orbital checkpoint format. Their purpose is to
identify storage and lease boundaries that the maintained simulator must expose.

## Reproduce

From the repository root on the macOS/OrbStack workspace:

```sh
orb -m ubuntu python3 -m unittest discover -s workbench/spikes/orbital-simulator -p 'test_retirement_probe.py' -v
orb -m ubuntu python3 workbench/spikes/orbital-simulator/retirement_probe.py --output workbench/spikes/orbital-simulator/evidence/retirement-probe.json
```

Nine test methods pass, including exact scheduler replay and a common latest-
value oracle negative. The campaign records complete case inputs, host budgets,
source hashes, trace hashes, counters, missing milestones and explicit waits;
it rejects a source change during execution. A separate read-only review found
no additional defect within the stated serialized fixture assumptions.
