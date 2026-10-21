# Selected access

Historical findings; the prototype and its launchers were retired during the
October 21 curation. The text below describes the captured experiment, including
its original API and qualifications. Current interfaces belong in
[Orbital material](../../../../orbital/docs/material.md);
[current integration checks](../../../benchmarks/retired-drivers.md#current-integration-checks)
retain the production consequences. [Recovery](README.md) pins the original tree.

Can one protected logical selection support cheap native reads, another physical
representation of the same version, and explicit ownership when work waits or
escapes? This bounded type/TU prototype says yes in a single-owner fixture. It
also exposed costs and lifetime mistakes worth resolving before implementation.

The immutable Representation owns its interpretation and complete Part closure.
Logical publication replaces an immutable State; installing an equivalent
representation only adds a local choice for an exact retained Object version.
Already selected Parts never follow that choice or the latest State. Conversion
may change Part count and reuse local Part IDs.

This is disposable Orbital scaffolding, not Engine or container design. The
[Ikea fixture](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-representation-conversion/fixture.h) supplies actual
SeriesPack-column/TuplePack-row conversion separately. `capture` retains its
completed opaque owner and borrows encoded bytes/Part interpretation without
copying payloads. The caller promises a complete immutable closure and supplies
the equivalence law; Orbital does not infer it from format IDs or byte equality.

## Concrete seams and callers

[access.h](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-selected-access/access.h) is the small public include; it does not expose Allocation
or catalog definitions. [version.cpp](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-selected-access/version.cpp) owns logical selection,
private changes and preparation/install. [representation.cpp](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-selected-access/representation.cpp)
owns descriptor construction/capture. [view.cpp](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-selected-access/view.cpp) owns qualification and
the optional reconstruction path. The ordinary [consumer](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-selected-access/consumer.cpp) compiles
separately using only public declarations; [caller.cpp](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-selected-access/caller.cpp) walks through
borrowed access, escape, conversion and a newer publication.

| Call | Ownership and work |
| --- | --- |
| `store.borrow()` | No ownership increment; an uninterrupted borrow ending before logical installation or Store destruction |
| `store.read()` | Owns the coherent State snapshot; may survive publication and protect later Object/Part discovery |
| `bind(context, object[, alternative])` | Qualifies exact version; borrows context and the alternative handle. Temporary owners are rejected |
| `ready(selection, part, requirements)` | Checks format, body bounds, alignment and padding; returns borrowed native bytes with no allocation, registration, Lock or independent retain |
| `retain(context, selection)` | Owns only the resolved Representation, not unrelated Objects through the entire snapshot. Keep ReadContext separately for coherent future discovery |
| `RetainedView::make(retained, view)` | Captures the still-valid View's actual backing and interpretation; moves rebind its internal ownership pointers |
| `slot.submit(retained, part, pool)` | Fallible target allocation precedes replacing interest; the backend/harness owns the accepted Request through retirement |
| `finish → settle → retire` | Physical copy, owner receipt and actual dependency retirement are separate. Cancellation only ends interest |

Native View borrowing ends with its selected owners. A buffered View borrows the
Slot's ready owners and ends before submit/cancel/destruction; promote it **before**
those operations. A span is not lifetime protection. The backend must keep every
accepted Request even after Slot destruction: this fixture is not a Port registry
surviving arbitrary backend loss.

Private construction uses the actual allocator. WriteView keeps private storage
alive through Builder replacement/destruction, and its move clears the source
span. `freeze` refuses while a writer is open. Draft captures an exact predecessor;
CommitOwner performs fallible preparation before a pointer-swap installation.
Prepared retains superseded roots until explicit retirement. This checks local
basis/epoch only: admitted-input/decision provenance and protocol outputs remain
an integration obligation, not an authority established by this fixture.

## What survived the histories

[check.cpp](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-selected-access/check.cpp) uses authored asymmetric bytes/metadata, weak closure
observers and actual Arena charges. Five grouped histories cover three-to-one
Part conversion, a read waiting between columns, late conversion targeting an old
version, conversion during a prepared logical edit, private failure, old/new reads,
zero body with meaningful metadata, external decoder retention, narrow two-Object
escape, Store reincarnation, failed/cancelled/orphaned completion, duplicate receipt
and retirement, and repeated 256 KiB pressure/refund/drain. Refused submission
preserves existing ready access. Alternative installation/forgetting/release also
returns to its physical budget baseline.

Final native and ASan/UBSan checks plus the external caller pass. Three deliberate
defects fail the same histories: accepting obsolete receipts, skipping format
qualification, and retaining the whole State for resolved access. The last defect
keeps an unrelated old Object alive. [evidence.json](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-selected-access/evidence.json) records the
source/binary/log hashes, selected repetitions and defect observations.

The audit also found and fixed real mistakes: temporary alternative owners,
write scopes outliving Builder, stale pointers after retained-view moves, overly
broad snapshot retention, extent overflow, and Store-address reuse. State now
retains a distinct Store incarnation token; no new work was added to native reads.

## Costs and simpler shapes

On the local AArch64 OrbStack guest, seven pinned sequential 250K-iteration trials
gave these median **loop means**, including the body and loop:

| Word caller | ns/iteration |
| --- | ---: |
| Already qualified native bytes | 0.34 |
| Experiment-only fused fixed-word path | 1.42 |
| Reused Selection, compiled qualification | 5.21 |
| Reused owned ReadContext | 8.45 |
| Fresh uninterrupted borrow | 9.57 |
| Fresh owned snapshot | 14.86 |
| Owned snapshot reused for 64 calls | 8.40 |
| Capture alternative and read | 15.86 |

ReadContext is 32 bytes, Selection 16, View 32, Retained 16, Requirements 40 and
`expected<View>` 40 on this build. The ordinary compiled word caller has a 96-byte
frame with a reused Selection and 144 bytes when binding from ReadContext.
Returning carriers and passing general requirements materializes stack state even
when the body is one word. The fused diagnostic knows a fixed legal word layout;
it omits general validation and is not an interchangeable checked API.

Carry forward **one qualification reused around ordinary native work**, compiled
cold ownership/preparation, and narrow resolved retention. Consider a separate
small uninterrupted-borrow shape or a fused qualified body when the real caller
supports it; don't promote this combined ReadContext or its flat catalog layout.
The prototype needs no compulsory BufferCache, per-Part owner, native read Lock,
native View registration or extra Allocation Hold where the captured immutable
owner already protects the bytes. Generation checks and actual backend retention
remain necessary on the wait path. Sparse partial-ready coverage is not tested.

These are local diagnostic leads, not per-request percentiles, native Zen 5
results or evidence of LMDB headroom. Fixed case ordering, tiny payloads and the VM
limit attribution. State copies a vector; standard-library metadata/control blocks
are outside the Arena budget. Borrowed-context pointer checks are not concurrent
reclamation or an ABA-safe lease. Reconstruction copies a whole Part into a
64-byte-aligned target; general footprints, eviction and larger alignment demands
remain unimplemented. No distributed recovery/consensus, no-COW mutation, faulting,
multi-worker ownership or actual Port acceptance is claimed.
Reads select completed States; fixed transaction positions, pending predecessor
results and distributed reconstruction/custody obligations are not implemented.
