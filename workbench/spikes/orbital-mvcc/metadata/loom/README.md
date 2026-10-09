# Loom metadata and ordinary ABI

This census uses frozen `7f73c81`, before Stage2 moved into this spike. It
separates stable resident ownership from values actually crossing functions.
Pinned Clang 21.1.8 compiled 84 size/alignment/trait facts for AArch64 Neoverse V2
and x86-64 Zen5, independent producer/caller TUs, LLVM return signatures and the
actual [Loom caller](../../callers/loom.cpp). All layout facts agree across these
two installed sysroots. [Selected evidence](evidence.json) retains the facts,
signatures, object identities and whole-caller observations.

## What crosses the boundary

| Value | Bytes | Compiled ordinary boundary on both targets | Use |
|---|---:|---|---|
| `Progress` | 16 | Two registers | Every Task step; shutdown/allocator progress |
| `View` | 8 | One register | Borrowed Part, with owner resident elsewhere |
| `Result<View>` | 16 | Two registers | Checked Part resolution |
| `Selection` | 16 | Two registers | Borrowed Version/Access pair |
| `Result<Selection>` | 24 | Hidden result storage | Checked discovery from a context |
| `BorrowedContext` | 16 | Two-register `Store::borrow` return | Uninterrupted Catalog/cut borrow |
| `RetainedView` | 8 | Hidden result storage | Cold owning acquisition/escape |
| `Result<RetainedView>` | 16 | Hidden result storage | Cold owning result |
| `Allocation`, `Hold` | 32 each | Nontrivial owning carriers | Cold allocation/protection |
| Their `Result` wrappers | 40 each | Hidden result storage | Cold acquisition |
| `RingStorage` / its `Result` | 56 / 64 | Hidden result storage | Configure a bank once |
| `Result<MutableBytes>` | 24 | Hidden result storage for `WriteView::bytes` | Acquire writable span before physical use |
| `PollBudget` | 16 | Two-word input | Compiled owner polling |
| `ObjectId`, `PartId` | 16 each | Two-word inputs | Discovery/Part identity |

The packet controls confirm that plain 8/16-byte integer/pointer packets return
in registers; 32 bytes is indirect. A checked 16-byte packet becomes 24 bytes
with the current `expected` wrapper. The experimental `Result16` uses a null
first pointer to select an exact Error in its second-word union; successful
results preserve two complete pointers. It returns in two registers, without
pointer tagging. Native separate-TU controls preserve all eight Errors and the
full 64-bit Progress generation, including its high bit and maximum value.
This establishes mechanical feasibility, not a replacement MVCC contract.

Size and `is_trivially_copyable` alone do not settle the call ABI. In this build
`Result<View>` is not trivially copyable but returns in registers; the eight-byte
owning `RetainedView` returns indirectly. The receipt records actual lowering.
Likewise `Message` and the fixture `NativeBody` are trivially copyable according
to the type trait but have no permitted copy/move constructor. Their stable
addresses and internal references remain requirements.

The whole caller matters. For the trivial Selection probe, the V2 caller is
24 bytes with two stack-memory sites, versus 68 bytes/six sites for its checked
24-byte result. Zen5 emits 12 bytes/two sites versus 48 bytes/three sites.
These are static sites including save/restore and temporary result storage,
not dynamic counts or time. A register result can still need caller save/restore.
The eight-byte pass probe tail-calls its ordinary producer, so zero ordinary
call instructions does not mean the boundary disappeared.

## What stays resident

| Record | Bytes | Residence |
|---|---:|---|
| `Lane` / `Worker` | 256 / 448 | Stable worker; Lane is already included in Worker |
| `Task` / `WaitLink` / `Signal` | 72 / 32 / 32 | Task frame and enrolled source/link |
| `Message` / shared `MessageOps` | 24 / 24 | Independent physical record/shared operations table |
| `Arena` / `Pool` | 32 / 64 | Allocation/accounting owners |
| `HintProfile` / `HintPolicy` | 32 / 56 | Coarse owner snapshots |
| Stage2 Ring / Slot / Arguments | 320 / 88 / 32 | One ring plus fixed storage; tuple is resident |
| `Compound` | 696 | Coherent waiting caller, including 536 bytes of output |
| `PhysicalUse` / its Caller | 96 / 80 | Read record independent of detached Task |
| `DelayedFill` / its Caller | 128 / 88 | Private-write record independent of detached Task |
| `RingFrame` | 56 | Selected owner and native-body pointer behind callback |
| `NativeBody` | 592 | Stable codec/source closure, separately allocated |

Additional public owner/specification/observation packets are in the complete
84-type census. `Description`/`RepresentationSpec` are 88/104 bytes;
`PartSource`/`PartPlan` are 80/72-byte input records passed as 16-byte spans.
Allocation requests are 40 bytes passed by reference. Object/allocation
statistics are 96/104-byte observation returns, outside native visits.
Preparation's stable 192-byte record and 24-byte initializer span are included
but are not used by the delayed-fill history.

Shallow live totals from compiler layouts are:

- Coherent read: 696-byte frame + 592-byte body = **1,288 bytes**.
- Physical read: 96-byte record + 592-byte body = **688 bytes**; add 80 bytes
  while its detachable Caller remains and 16 bytes for the accepted foreign closure.
- Private fill: 128-byte record + 88-byte caller + 48-byte foreign closure =
  **264 bytes**; cancellation destroys the caller, leaving **176 bytes**.
- Six Ring operations: 320-byte Ring + 608-byte bank
  (`6×88` slots, `6×8` free indices, `4×8` overflow pointers) +
  `6×(56+592)` frames/bodies = **4,816 bytes**.

These totals exclude payload backing, allocator/system thread records,
retained Orbital graphs and shared harness state. Add Worker only once when
including its shared residence. They are explicit shallow storage, not stack
usage, allocator charge or total process memory.

Actual recurrent Ring work passes six scalar/pointer arguments: Ring, Slot,
Frame, row, remaining and sum. It uses the existing `preserve_none`/`musttail`
family; neither owning frames nor `std::tuple` cross that tail boundary.
The actual callback has zero ordinary-call and zero stack-memory instruction
sites on both targets. Its emitted union of paths is still 1,604/1,685 bytes;
that is not a claim about the executed hot path. AArch64's ordinary fixed-frame
entry bridge is separate. Ordinary Object/packet probes introduce no special ABI.

## Changes worth co-iterating

The largest obvious resident reduction is codec construction state. `NativeBody`
contains Metadata24, layout392, reader96, source48, operation16 and native-reader16.
TuplePack permits releasing layout/map after preparation. The native reader
stores the plan/source pointers directly, so the intermediate operation can be
transient too. Removing those two persistent fields gives **184 bytes** without
changing the native packet ABI. Sharing the 96-byte prepared reader by exact
interpretation/map can reduce per-Part residence further, provided its address
and interpretation owner stay stable. Metadata's first/shift are fixture/setup
state; row count duplicates the source view. These are candidate reductions,
not an implemented or timed improvement.

Task queue and wait membership are disjoint: Task's queue pointer and the
WaitLink's next pointer could share storage, and Loom's waiter/callback values
are always the Task and `wake`. Ring dependency state is needed before admission
or overflow promotion, while active next/previous links are used afterward;
an explicit phase union could save 16 bytes per Slot. Inventory can be derived
from its slot address. Each changes actual routing/retirement code generation;
retain detach, lost-wakeup, overflow-order and selective-cancellation falsifiers.
Do not remove resident route pointers merely to introduce another hot lookup.

For ordinary discovery, test a trivial checked two-word Selection packet against
the 24-byte wrapper in a complete operation. Keep cold owning construction,
retention and large observation returns distinct from recurrent borrowed access.
Eight bytes can point at a resident Version/Access pair, but that relocates state
and requires charging/pinning the additional record. Carrying Catalog/cut and
Version/Access together as 32 bytes is unnecessary after qualification when
the coherent context already stays in the operation.

## Caller-resident read registration

A nonmovable registration fits existing stable placements: Compound's final
stack frame, separately owned physical records and a fixed RingFrame bank.
Register only after constructing the final frame. Frozen Compound is move-constructible
by trait, but its constructor binds `task.context` to its own address. An implicit
move leaves that pointer targeting the old frame, so it is already unsafe to
relocate after construction without rebinding. A nonmovable guard makes that
existing stable-placement obligation explicit. Return a stable owner pointer
during cold construction if the caller needs a movable factory result.

The relocated caller now explicitly deletes copying and movement for these
self-bound Task/completion records and checks that contract at compile time.
That cleanup does not change this census: its traits, layouts and code generation
remain observations of the earlier frozen `7f73c81` source.

A minimal proposed seam is default-constructed, initially unenrolled
`ReadGuard`, with `guard.open(Store&)` returning a small status/`Result<void>`,
and owner-Lane RAII close. Discovery borrows `const ReadGuard&` and returns a
trivial checked Selection packet; ready Part resolution can keep the existing
register-sized result. The guard stores its cut, intrusive links and Catalog pin
once. This is targeted in-place enrollment, not an out-parameter convention for
every return. No `trivial_abi` or new calling convention is needed.

Returned/movable Bodies wrappers borrow that stable registration and stable
plan/source placement instead of containing or copying its intrusive links.
Body/source publication must follow successful guard admission; destroying or
replacing the guard ends those borrows. After all needed discovery, retain the
resolved descriptor/Part, close the broader guard, and keep physical ownership
in its independent record through completion.

The guard needs one important design answer: a Catalog/cut pin preserves default
logical history, but does not alone preserve a replaceable alternative. The
baseline snapshot captures selected alternative Rep pins. The replacement
history must keep passing with explicit resolved Rep ownership in the frame or
charged guard-owned captures. The alternative lookup is therefore not currently
allocation/enrollment-free. Default retained-context lookup performs no new
registration per Object, and native work has no per-call registration.

The actual delayed-fill matrix remains a falsifier: physical Builder/WriteView
and original-basis responsibility outlive detached caller interest; a read guard
cannot replace those owners. Current cancellation, stale exact-basis refusal,
alternative replacement and late multi-Object/Part discovery should qualify the
next compact Orbital candidate together with its performance caller.

## Reproduce and limits

The selected private capture is
`build/orbital-mvcc-metadata/loom/source`, archived from git `7f73c81` (258 files).
It prevents the production withdrawal/relocation from racing this qualification.
From the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/loom/codegen.py \
  --source build/orbital-mvcc-metadata/loom/source \
  --output build/orbital-mvcc-metadata/loom/codegen-new
```

Use a new output directory. The runner also accepts a relocated source tree with
the spike's baseline include path/caller. It reuses the existing relocation-aware
access-interleaving analyzer. Full receipts hash objects, LLVM IR, probes and all
transitive project/system headers separately per target, and verify dependency
stability after compilation. Record-layout dumps also verify the actual local
Caller classes and accepted thread closures.

This pass executes separate-TU native packet/error/generation controls; it
compiles the real lifetime caller on both targets. It does not rerun the relocated
runtime suite, simulate Objects/backends, redesign workers/containers, or measure
performance. The lifetime/cancellation tests remain with their owning spike.
