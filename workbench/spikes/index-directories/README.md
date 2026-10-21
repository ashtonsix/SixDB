# Index directories

The current performance-led replacement in `engine/` implements physical and
logical directory reads and a shared-prefix Ikea Hashtable recipe, with
correctness and performance considered together. The earlier
correctness-first backend was removed; its functional checks and API do not
qualify or constrain the replacement. The paper-first pause is superseded.
Incremental membership/bound repair, balancing, ordinary layers, schema/extras
and full operation/performance qualification remain unfinished. Shore frontend
work remains planning only.

Engine owns the replacement. Workbench supplies existing
spike evidence and independent expected-result reasoning. Preserve useful
workloads and oracles without tying them to the abandoned API. Do not spend a
measurement campaign qualifying the discarded backend. A structural performance
problem is grounds to replace an approach while implementing it.

- [Current implementation](IMPLEMENTATION.md): byte/operation ledger, actual
  multi-row caller, measured wins/losses and remaining scope.
- [Findings](FINDINGS.md): historical measurements, source identities and recovery.
- [Independent logical cases](evidence/logical-oracle-20261021/README.md): useful
  inputs and expected results, preserved without the discarded backend or API.
- [Budget](BUDGET.md): peer comparisons and planning assumptions, with their limits.
- [Design](DESIGN.md): composition costs and alternatives considered.
- [Pseudocode](PSEUDOCODE.md): the earlier algorithm sketch; it is neither an
  implementation pause nor a contract for the replacement.

The existing planning objective remains complete Engine at least twice as fast
as the faster valid Calico/LMDB comparator, initially allocating at most one
quarter of that peer's time to all directory, binding and coupled publication
work together. Five layers do not receive five separate budgets. These are
planning allocations, not measured achievements of a complete implementation.

## Evidence that remains useful

The historical spike checkpoint `a89b9f4e` includes real Ikea/Loom/Orbital
composition, six native checks, full eight-word consumers and saved-frame range
traversal. The expanded scan check covers 9,600 cases and 105,540 ordered selected
items. The later guarded driver also qualifies native run seeks, reused handles
and uninstrumented timed native code. Recover the measured source identified in
[Findings](FINDINGS.md) when using these controls.

The raw-child graph's read results qualify that graph. They do not establish the
performance of a stable-name tree or a new library path. Likewise, the one-row
point operation and multi-row equality with exact row refinement do different
work. Compare equivalent useful output, data, domain, layout and read lifetime
before attributing a difference to extraction or representation. Include actual
Ikea/Loom calls and publication, maintenance and cleanup when they belong to the
operation. Timings, logical counts and modeled costs remain separate evidence.

Historical fresh 262K native mapping is 32.324 MiB: 16 MiB independent Segment
material, 16 MiB dirents and 0.324 MiB routing. Metadata request/backing is
25.091/25.425 MiB; G16 fresh RSS is 69.738 MiB. These are per-Segment costs in a
one-row-per-Segment stress fixture, not complete Engine memory per record. G256
retains 12 MiB more in the aged-survivor witness. Controlled material spacing
changes point throughput by about 8%, without establishing a cache/TLB/bank cause
or universal G16 superiority.

Large unmasked scans have tight complete-request headroom. The pointer-bank
trial loses. At one selected bit per 64, the stronger native run peer narrows the
comparison but Engine's measured fixture still leads 5.75–7.31x. Zero-mask results
are modest bitset/lifecycle differences with no entries. These remain results of
the captured fixtures, not acceptance of a replacement implementation.

The historical grouped directory-record DUPFIXED peer remains unmeasured;
the current actual-row covering DUPFIXED62 peer is measured in
[Implementation](IMPLEMENTATION.md). Retained peer recipes and independent logical workloads can inform a
replacement without inheriting its storage layout. Resident measurements imply
no durability, remote I/O or complete Engine performance.

## Supplied model and composition

Each of five layers has prefix-defined Segments. The overlapping 16-bit windows
are 0:16, 8:24, 16:32, 24:40 and 32:48. A Segment has at most 65,536 positions
and exists only while nonempty. The last layer contains records; earlier layers
contain the specified summaries and may contain other structures. There is no
order across layers. Seek/continuation stays in one layer; explicit descent
changes the layer.

Physical keys are unique six-byte keys. Logical keys have K bytes and may repeat;
the first J bytes coincide. Physical placement preserves full logical order,
including slots within a Segment. Equal logical keys have no internal order.
Admit J<=min(6,K) and valid populations: a shared prefix has only 2^(48-8J)
physical positions. J6 allows at most one record per shared six-byte prefix,
including distinct longer logical keys with that prefix.

A Segment has one representation. Schema may retain many historical
representations, with different allowed sets by layer. A dirent is 64 bytes:
six eight-byte locators plus sixteen metadata bytes. Directory facts are
existence, routing bounds, representation and complete Part association.
Summary validity/corrections remain data facts. Stable directory locators do not
waive the selected Segment geometry or correction law.

Ikea operates on admitted borrowed/private bytes and reports coverage before
local writes; it supplies no beforeimages or isolation. Orbital seals captured
bodies/padding, retains sources and owns snapshot selection and atomic publication.
Loom supplies physical access and execution: ordinary calls for ready work,
retained Tasks for real waits. A root Part protects directory children, not
independently versioned Segment data; that data still needs the same live Scope.
