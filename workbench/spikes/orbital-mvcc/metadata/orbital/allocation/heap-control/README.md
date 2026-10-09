# Exact metadata heap control

Replace the separately page-rounded writable metadata mapping with one stable
heap block. The same patch composes with original Direct4 or
[bounded16 Direct4](../trim-batch16/README.md). [Heap-only](heap-only.md) retains
the full trim inventory and charges 5,256 / 6,792 B per Group; the bounded16
combination charges 4,016 / 4,600 B with separately measured batching costs. Bindings,
grids, per-page state and trim scratch need at most 8-byte alignment and never
use metadata protection or discard operations. Ordinary nothrow `operator new`
provides that alignment; payload reservation and preparation keep their VM path.

[`heap-control.patch`](heap-control.patch) is the isolated runtime change;
[`heap-control-cut.json`](heap-control-cut.json) identifies its exact bounded16
base and runtime inputs. `create` computes the exact extent before the owner's
budget claim. The existing preparation helper allocates and initializes that
block after payload reservation, and publishes completion before any identity
becomes available. Binding pointers and acquire/release publication remain
unchanged. Group, FiniteGroup and their embedded replies stay separate owners.

Heap ENOMEM reports `capacity`, including through deferred replies; payload VM
failure reports `io`. Retirement frees metadata and retains any failed payload
unmap for retry. The quarantined Group retains the full logical charge until
its owner handles confirmed completion. Pending reserved-VA statistics include
only the planned payload reservation.

At 256 identities with bounded16 trim buffers:

| Per-Group bytes | 4 KiB A | 4 KiB B | Modeled 16 KiB A | Modeled 16 KiB B |
|---|---:|---:|---:|---:|
| Previous metadata mapping | 4,096 | 4,096 | 16,384 | 16,384 |
| Exact metadata heap request | 1,968 | 2,488 | 1,432 | 1,568 |
| Group metadata charge | **4,016** | **4,600** | **3,424** | **3,576** |
| Observed summed heap usable extent | 4,064 | 4,640 | 3,456 | 3,616 |

Usable extents were sampled with glibc 2.42 on the 4 KiB AArch64 guest, including
the allocations sized for modeled 16 KiB geometry. They are an alternative
observation, not bytes to add to requested charge. Neither column includes
allocator chunk headers/arenas, RSS, Arena/Pool setup, the first 8-byte owner-vector
capacity, or Object metadata. Heap allocation/free may lock, perform internal VM
work or retain allocator pages; this establishes no timing or RSS advantage.

[`qualification.json`](qualification.json) records six native checks using
Clang 21.1.8, `-O3 -g`, with assertions enabled. The isolated
[`test adaptations`](qualification-tests.patch) replace the control page-rounding
expectation and second metadata mmap fault with exact heap ENOMEM after a real
payload reservation, for both palettes. They check synchronous/deferred error
status, no exposed value or retained allocation/preparation pin, conserved charge
and partial ownership, and payload unmap failure followed by successful retry.
Existing prepared cancellation, shared live-neighbor cleanup, paused retirement,
retained readers and tight-budget histories also pass.

Independent review verified the runtime/test patch replays and recorded hashes.
Only preparation/reclamation outputs remained in the original test logs; root
confirmed the other four native binaries and preserved their outputs. The
[matched trim comparison](../trim-batch16/comparison-qualification.json) adds
ten explicit `-DNDEBUG` reclamation histories for this heap cut. It confirms
the same logical work as batch16, rather than a new trim policy or a speed win.

The separate [heap-only control](heap-only.md) applies the same runtime patch
directly to frozen Direct4 with its original full trim reserve. Its requested
metadata totals are 5,256 bytes for A and 6,792 for B. The
[heap-only receipt](heap-only-native-readiness.json) covers one ordinary native
smoke and actual census; the six suites above qualify bounded16 plus heap.
The receipt also records the corrected explicit-target patch application after
an initial ignored-subtree application skipped the patch.

The local control and qualification inputs are under
`build/orbital-mvcc-metadata/heap-control/`. From the repository on Linux:

```sh
cmake -S build/orbital-mvcc-metadata/heap-control \
  -B build/orbital-mvcc-metadata/heap-control/native -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-21
cmake --build build/orbital-mvcc-metadata/heap-control/native --target \
  heap_smoke heap_allocation heap_lifetime heap_preparation \
  heap_reclamation heap_budget heap_finite
ctest --test-dir build/orbital-mvcc-metadata/heap-control/native \
  --output-on-failure -R '^heap_(allocation|lifetime|preparation|reclamation|budget|finite)$'
```

Prefix Linux commands with `orb -m ubuntu` from this macOS/OrbStack workspace.
The original Direct4 routing's pinned Clang/Linux reachability limits still apply.
