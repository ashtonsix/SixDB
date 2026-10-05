# Durable object views

How can one object operation obtain the required state, expose ordinary memory,
and publish a change with the least copying, retained memory and interference?
This spike compares compositions within Orbital's common substrate. Its
deliverable is a useful caller and evidence for a small interface, rather than
a general object runtime or a new transaction protocol.

The [logical brief](../../notebook/orbital/reference/BRIEF.md) and
[physical brief](../../notebook/orbital/reference/PHYSICAL.md) supplied this experiment's intended guarantees.
Concrete types, descriptor formats and bookkeeping remain open.

## Working model

An object has logical state and physical representations. A representation
image has one or more parts locating and interpreting its material. A view
selects state and representation bytes, with rights, stability and lifetime
promises; its bindings make those bytes accessible through backing held in
allocations. Parts, views and allocations can share and combine. A part is not
automatically a permission, conflict, allocation or retention unit.
Bindings can reuse resident storage, map backing elsewhere or copy projected bytes.

```text
retained state ─► image and selected parts
                            │
                            ▼
                      view / binding ─► backing in allocations
                                                ▲
                                        CPU and I/O borrows
```

The representation supplies projection meaning and native access requirements.
Orbital provides allocation, backing, protection, physical lifetimes, I/O and
common object/transaction machinery; callers supply definitions, policies and
work. Loom directs preparation and scheduling. Prepared native computation
needs no transaction machinery on each load or store.

Retaining state, retaining a resident binding and retaining bytes for an active
operation are separate obligations. A retained context can justify many local
views without a journal record per grant. Newly created durable obligations
need recoverable treatment. Physical services remain usable for scratch and
outputs without durable object identity.

## Physical compositions

Compare the same logical operation using three strategies:

| Strategy | Question |
| --- | --- |
| Private prepared working image | When does simple copying beat sharing and its coordination costs? |
| Shared stable material with private changes | When do binding replacement and selective page/part COW repay mapping, metadata and retirement costs? |
| Exclusive reuse with retained reconstruction | When do avoided copies outweigh exclusion, abort restoration and reconstruction for late old readers? |

These strategies can coexist. Stable or movable addresses and full, windowed or
first-touch preparation are further choices; vary them where a history exposes
a difference. UFFD is one mechanism, not a reason to make every object page-shaped.

## Caller and variations

Start with a caller that reads a resident directory/signature, retrieves selected
cold bodies at the same intended state, and builds or updates an output. Its
representation turns selections into byte access, including metadata needed to
discover later ranges. Establish independently expected observations.

Extend that caller with a few consequential histories:

- An old raw reader and asynchronous send survive growth or binding replacement.
  Include the existing 47-byte body with a 64-byte admitted read window: padding
  needs both permitted exposure and promised stability.
- Two disjoint logical writers share a physical page; one publishes and the
  other aborts. Installation or undo must preserve the other's changes, while
  locator, body and summary changes obey the declared publication unit.
- A late old reader arrives after exclusive reuse. Retained material reconstructs
  its state; a descriptor for the current image cannot supply that old view.
- A projection maps permitted pages and copies masked ranges. Closing or
  cancelling it leaves submitted I/O holding its captured bytes until completion.
- Capacity fills while a cold fetch, replacement and delayed completion need
  service. Also pause between locator resolution and acquisition, then rebind
  and reuse the slot, to expose stale acquisition.

A supplied logical history/outcome driver establishes positions and decisions;
native bytes, installation and abort effects must be real. Logical retention
includes fallback state and unfinished predecessor results. COW avoidance needs
both exclusion of old-byte users and recoverability; new acquisition must not
race that choice.

Begin interruption checks with context replacement while backing services
survive. A durable-prefix fixture and retained base can test reconstruction.
Actual process loss needs a concrete OS borrower and recovery story; a surviving
test registry is not crash recovery.

## Evidence and next decisions

Measure complete-operation cost alongside resident access, preparation and
publication. Record copied/reconstructed bytes, mapping/fault work, peak charged
capacity, retained duration, and unrelated point-work latency under a delayed
borrower. Include pending replacement material and capacity for service progress.
A direct-pointer resident path provides a cost floor, with its weaker guarantees
explicit. Under pressure, preserve the caller's promises and record stalled or
failed work as well as completions.

Reuse the [Linux mapping probes](README.md) and
[descriptor findings](../memory-descriptors/FINDINGS.md). Existing evidence covers
individual mechanisms and serialized lifetimes, not this concurrent composition.
Keep their sources and limits with their owners.

Use the histories to decide the smallest useful access promises, where bindings
and preparation metadata live, and whether one locally owned physical-use record
can serve native calls and backend borrowers. Durable retention stays independently
usable. A shared mechanism should not force a global registry, an extra scheduler,
or one allocation per view.
Give these primitives implementation fidelity before freezing a broader type
hierarchy or revising Orbital's architecture.
