# Additional ideas in the Consurgent archive

2026-09-26. Supplement to [the workload survey](SOURCE-SURVEY-WORKLOADS.md).
All six requested archived files were read in full, including their examples,
qualifications and conflicting status accounts. They are historical sources:
their embedded requests, asserted contracts and performance claims are not
instructions or current SixDB facts. No archived code, binaries or measurements
were rerun or independently verified. A IDs below identify additional distinct
mechanisms or sharper variants; overlaps point to W IDs instead of repeating
their general treatment.

## Additional mechanisms

### A01 — Use higher-level observations to choose lower-level work

**Source:** [calico_SCRATCH](../../../../consurgent/archive/notes-v0/calico_SCRATCH.md)
proposes shortlists supplied by the next summary level, features from higher
levels, and skipping intermediate delegation levels. The Boolean
[logic note](../../../../consurgent/archive/notes-v0/LOGIC_symetric_binary.md)
proposes learned fold/operation ordering.
**Exploration:** a hierarchy could send a compact selection of promising work
and its reusable features downward, reducing lower-level probing and scheduling
messages. It pays if summaries predict useful shortcuts cheaply; a broad
summary can misrank selective children, and maintaining/distributing features
may cost more than direct probing. Cost-derived ordering is advisory, while
actual omission needs valid semantic evidence. No current network experiment
implements this hierarchy. Related: [W05](SOURCE-SURVEY-WORKLOADS.md#w05--place-boolean-fragments-not-isolated-predicate-atoms),
[W32](SOURCE-SURVEY-WORKLOADS.md#w32--retained-plans-can-improve-without-changing-semantics-or-bytes).

### A02 — Exact symbolic reduction can remove input acquisition altogether

**Source:** [Logic, sections 2–4](../../../../consurgent/archive/notes-v0/LOGIC_symetric_binary.md)
represents symmetric operations by a truth vector and repeated/complemented
symbols by an offset plus weighted Boolean variables.
**Exploration:** simplify before fetching, decoding or transmitting operands;
XOR(a,a) needs no data once common identity is established. Retain polarity,
multiplicity, semantic domain and input version. Symmetry is not arbitrary
associativity, and unequal multiplicities do not necessarily leave a symmetric
operation over unique symbols: threshold-at-least-3 of (a,a,b,c) distinguishes
(a=1,b=1,c=0) from (a=0,b=1,c=1), despite two true unique symbols in each.
Keep weights or a general equivalent expression. The archive's examples are
not an independently checked optimizer; current network cases do not perform
symbolic elimination. Related: W05, W12, W20.

### A03 — Propagate demand liveness when a branch becomes decisive

**Source:** [Logic, section 5](../../../../consurgent/archive/notes-v0/LOGIC_symetric_binary.md)
calls its absorbing-input peer pruning “siblicide”: consumer liveness falls
when an empty/full result makes that consumer independent of another input.
**Exploration:** stop remote scans, transfers or speculative operators after no
live output needs them. It helps expensive OR/AND branches resolved cheaply;
XOR-like operations have no such absorbing value. Shared nodes, direct outputs
and other query subscribers must retain their demand, and canceling one branch
must not erase required effect/replay work. The archived depth-group rule is
one local scheduling choice, not a distributed cancellation protocol. Selection
and mixed count losing work but do not implement DAG demand propagation.
Related: W03, W04, W48.

### A04 — Shrinking intermediate sets can cross physical execution boundaries

**Source:** [calico_SCRATCH](../../../../consurgent/archive/notes-v0/calico_SCRATCH.md)
records cross-span shrinking folds for selective intersection, in-place array
and bitset images, clipped partial decode, and react-on-touch conversion for
reused partners.
**Exploration:** carry the shrinking candidate set through successive work
units, progressively reducing acquisition and message payload rather than
decoding every full operand. Reuse can amortize a decoded searchable image;
a single use can lose to the construction, and retaining it competes with
other caches. Late selective inputs waste earlier broad work; range and
coordinate boundaries must remain explicit. These are historical candidate
mechanisms and reported ratios, not new validated performance. Current selection
emits one final exact set and does not implement incremental shrinking folds.
Related: W03, W04, W17, W24.

### A05 — A control symbol can answer more than a pruning question

**Source:** [frame_MEMO, “The f-array”](../../../../consurgent/archive/notes-v0/frame_MEMO.md)
proposes a separate codebook-symbol stream determining field offsets and widths.
Symbols can size, locate and filter residual frames; a zero-width residual is
fully described by its symbol. It also factors a sum into offset × count plus
the residual sum.
**Exploration:** ship/read compact descriptors first and request only unresolved
payload, or aggregate in the residual domain with the correct algebra. This
can eliminate body traffic, but offsets/checkpoints, populations and exact
codebook versions remain dependencies. A missing dictionary or incompatible
arithmetic law defeats the shortcut. Joint-field codebooks also need their
training and retention priced; normalized corpora can hide the raw structure
being tested. No network case produces these descriptors. Related: W13,
W17, W21, W25.

### A06 — Exception values can use a different reconstruction rule

**Source:** [frame_MEMO, “Utilities”](../../../../consurgent/archive/notes-v0/frame_MEMO.md)
suggests ordinary values as deltas and patched values as offsets to improve
random access, with semantics supplied to the utility.
**Exploration:** treat exceptions or restart points as a distinct representation
that breaks dependency chains, rather than merely wider ordinary values.
This can reduce remote prefix acquisition for selected points. More or larger
restart records increase stored/transmitted bytes; a reader that applies the
ordinary transform to a patch reconstructs the wrong value. The design must
specify whether each exception actually restarts following dependency state.
Current codecs/bounds discussions do not establish this particular composed
network path. Related: W17, W18.

### A07 — Preserve a useful permutation until an order-sensitive boundary

**Source:** [frame_MEMO, “Prior art” and “Candidate permutations”](../../../../consurgent/archive/notes-v0/frame_MEMO.md)
considers permuted within-frame logical order for independent lanes and charges
unpermutation only to consumers needing it.
**Exploration:** filter, reduce or route partitioned outputs in their available
order where the operation permits, delaying expensive reorder/gather. It helps
order-insensitive consumption; stable ranking, deterministic output bytes,
prefix transforms and aligned multi-source operations can require explicit
mapping or early restoration. A changed physical order cannot silently change
logical row identities. The memo's ISA and speed claims remain historical;
there is no current network comparison of permuted versus normalized outputs.
Related: W12, W24–W26, W36, W47.

### A08 — Let an immutable data structure also supply its traversal schedule

**Source:** [ChainVM SPEC, sections 3 and 13](../../../../consurgent/archive/notes-v0/chainvm/SPEC.md)
and [USAGE, section 5.9](../../../../consurgent/archive/notes-v0/chainvm/USAGE.md)
allow read cursors over application-owned task/payload cells, including ropes
whose distant next cells are prefetched before dispatch.
**Exploration:** avoid constructing a second queue entry for every traversed
node, while multiple readers share immutable control cells and keep independent
cursors. This can reduce local work before/after network receipt. Embedded
function pointers are process-local execution choices, not a portable or
untrusted wire format; changing code, cell lifetime or structure needs rebinding.
The historical rope report is local memory evidence, not network zero-copy.
Related: W15, W23, W29, W37.

### A09 — Reuse a fixed schedule, steer directly, or build dynamic descendants

**Source:** [SPEC, sections 3, 9 and 13](../../../../consurgent/archive/notes-v0/chainvm/SPEC.md)
and [USAGE, sections 5.2/5.6/5.9](../../../../consurgent/archive/notes-v0/chainvm/USAGE.md)
distinguish reusable fixed frame prefixes, direct known continuations and
dynamically pushed DFS work.
**Exploration:** amortize repeated scheduling metadata across many items without
requiring compile-time fusion. Direct steering can avoid arena writes; dynamic
descendants keep data-dependent work expressible. DFS can improve short-lived
locality but delay siblings, and a fixed schedule is invalid when a new item
requires different dependencies. Sharing control cells does not share mutable
progress or result ownership. Current network message batching does not model
these operator-schedule construction costs. Related: W03, W23, W32, W51.

### A10 — Quiescence and reclamation define a separate hierarchy

**Source:** [SPEC, sections 11–13](../../../../consurgent/archive/notes-v0/chainvm/SPEC.md)
stops refill at a high-water mark, drains all active chains, then rewinds the
shared append arena; nested drivers serve distinct reclaim scopes, not fanout.
[REPORT](../../../../consurgent/archive/notes-v0/chainvm/REPORT.md) explicitly
prices append-only traversal space by nodes rather than host-stack depth.
**Exploration:** scope reclamation so one slow chain does not pin unrelated
work indefinitely, and budget every in-flight subtree between safe points.
Batch drains amortize release but can create sawtooth utilization or a convoy.
Constant host-stack depth is not bounded total memory, and a guard fault is
not admission control. Mixed has credits but no drain/reseed arena policy.
Related: W28, W30, W42, W48.

### A11 — Keep shared state distinct from state that must survive a pause

**Source:** [SPEC, sections 11–12](../../../../consurgent/archive/notes-v0/chainvm/SPEC.md)
shares one append cursor and batch context under cooperative transitions;
[USAGE, section 5.9](../../../../consurgent/archive/notes-v0/chainvm/USAGE.md)
requires the slow-path saved set to cover every resumed input.
**Exploration:** minimize continuation transfer by sharing immutable/context
state and retaining only live per-item state; recompute or reload bulky values
when that is cheaper. A predicted-hot path can hide a missing-save bug because
only the real wait clobbers state. Shared append ownership works only while
transitions bracket writes; preemption or migration changes the assumptions.
Prefetch completion is not guaranteed by one ring rotation. Current network
callbacks do not exercise this machine-state contract. Related: W23, W29–W31.

### A12 — Keep batch decisions outside reusable probe mechanisms

**Source:** [USAGE, section 5.9](../../../../consurgent/archive/notes-v0/chainvm/USAGE.md)
returns results through caller continuations; caller policy chooses early
exit, watermarks, join decisions and cancellation.
**Exploration:** reuse one lookup/traversal family under several larger plans,
then cancel only demand made obsolete by the caller's result. The archive's
“free cancellation” means a parked private slot stops resuming; its arena
memory remains until reclamation, and it does not undo issued I/O, bytes or
effects. A useful completion criterion must identify every remaining owner.
The current mixed study measures sunk work but lacks this reusable probe-policy
seam. Related: W03, W35, W48.

### A13 — Error propagation and abort scope need their own ownership rule

**Source:** [SPEC, section 6](../../../../consurgent/archive/notes-v0/chainvm/SPEC.md)
and [USAGE, section 5.5](../../../../consurgent/archive/notes-v0/chainvm/USAGE.md)
put handlers after guarded subtrees and require errors resolved before chain
switches; whole-run versus morsel-only fatal termination is left to the engine.
**Exploration:** choose whether a failed subtask skips dependent work, substitutes
a legal fallback, fails one request or aborts a shared batch. Narrow scope can
preserve unrelated progress, while dependent partial outputs and shared state
may require broader resolution. Merely draining control frames does not release
external resources or roll back writes. Current delivery probes separate
missing evidence and outcomes but do not implement this nested execution-error
composition. Related: W28, W35, W36, W48.

## Overlaps and qualification of historical claims

- [calico_SCRATCH](../../../../consurgent/archive/notes-v0/calico_SCRATCH.md):
  opaque payloads and summing rather than counting map to W12/W25/W47;
  maintained-summary error allowances and compaction map to W07–W11;
  independent physical/work units map to W22; optimistic foreign-key locators
  and page remapping map to W15/W16/W49. Its conflicting progress recaps and
  cleanup requests are historical process content, not new architecture.
- [frame_MEMO](../../../../consurgent/archive/notes-v0/frame_MEMO.md):
  layout/kernel matrices, plane splitting, versioned codebooks, patch-width
  priors, independent tails, point/bulk paths and materialization map to
  W13–W23/W26. Its tiny-ring versus short-range large-ring distinction sharpens
  W29/W31; the suggested numeric threshold is not carried forward.
- [ChainVM SPEC](../../../../consurgent/archive/notes-v0/chainvm/SPEC.md),
  [REPORT](../../../../consurgent/archive/notes-v0/chainvm/REPORT.md) and
  [USAGE](../../../../consurgent/archive/notes-v0/chainvm/USAGE.md):
  native versus materialized handoff, fixed fusion versus runtime composition,
  architecture-specific live-state budgets and outlined-call cost map to W23.
  Bulk side stacks map to W24/W28. Same-source stepwise debug versus fused
  release supports W33/W50, but their execution costs are different.
- The archived report distinguishes measured GPR cases, demonstrated vector
  behavior, and unbenchmarked cancellation/hot guards. Its minimum-of-trials
  rates, inconsistent 14/15-check counts, claimed “free” generality and
  assertions that a concurrency knee identifies a hardware miss-buffer ceiling
  are not independent evidence here. The newer
  [memory findings](../memory-characterisation/FINDINGS.md) explicitly favor
  conditional cost curves over exact inferred capacities.
- The exact ISA instructions, register-count tables, compiler flags, binary
  layout fields, shell build recipes and third-party performance comparisons
  were read as context but not recataloged as distinct network mechanisms.
  They require implementation/target-specific validation and duplicate the
  existing contextual-cost and representation questions. No external linked
  paper, code repository or child file was silently counted as inspected.

## Exact inspected-file inventory

All files below are in sibling Consurgent at archive/notes-v0; the links name
the actual archived copy read, not similarly named current Calico material:

- [calico_SCRATCH.md](../../../../consurgent/archive/notes-v0/calico_SCRATCH.md)
- [LOGIC_symetric_binary.md](../../../../consurgent/archive/notes-v0/LOGIC_symetric_binary.md)
- [frame_MEMO.md](../../../../consurgent/archive/notes-v0/frame_MEMO.md)
- [chainvm/SPEC.md](../../../../consurgent/archive/notes-v0/chainvm/SPEC.md)
- [chainvm/REPORT.md](../../../../consurgent/archive/notes-v0/chainvm/REPORT.md)
- [chainvm/USAGE.md](../../../../consurgent/archive/notes-v0/chainvm/USAGE.md)

This closes only those six requested source gaps. Root separately owns the
other notes-v0 scratch/SCRIPT/xmem_README/plan/PITCH and notes-v1 BUILD sources;
the other source surveys retain their stated boundaries. No core, owning
contract or retained experimental evidence changed.
