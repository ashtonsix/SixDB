# Index directory implementation

Implementation spike opened 20 October 2026. It investigates how to implement
Ashton's index directory with tables, Ikea hashtables and boundlists, and how
to maintain it through physical-key assignment, rebalance and Orbital publication.
The executable spike now uses real Ikea containers and Orbital/Loom allocation,
Objects and publication. Storage controls, five-layer edits, ordered placement,
directory pressure, staged reads, full byte-key search, memory locators and
schema migration and editor failure/retry have separate drivers.
The directory tracks segment identities, logical bounds, representations and
Part locators. `Row`/`EntryState` are fixture producers and independent oracle
inputs; they are not an Engine record store or a selected aggregate-maintenance
implementation. Directory measurements report segments per layer and separate
directory work from fixture record edits and segment-body construction. The
future Engine-versus-LMDB driver remains a separate proposal.
The [findings](FINDINGS.md) track measurements, rejected cases and remaining work;
this document retains the implementation questions. The [prior-art survey](PRIOR-ART.md)
supplies implementation mechanisms and a source-based assessment of Calico.
The [coverage audit](PROGRESS.md) distinguishes completed experiments from work
still required by this plan; [memory locators](LOCATORS.md) owns that experiment's
encoding, lifetime argument and measurements.
The [schema comparison](SCHEMA.md) owns definition lookup, explicit selected
segment migration and native-source versus model-image conversion.
The [failure panel](FAILURES.md) owns undo logging, construction/refusal/abort
histories and retry cost, including allocation-state sensitivity in comparisons.
The [maintenance panel](MAINTENANCE.md) owns stable directory-page fences,
split/merge histories and page-local StreamPack overflow coordinates.

The current update implementations remain far behind the historical Calico/LMDB
scale. Findings include broad-copy failures, an Orbital ObjectId hash defect
repaired by its owner, occupied-entry directory pages, sized hash owners and
retained losing layouts. This is experimental code, not a promoted Engine API.

Boundlist is now available as `<ikea/boundlist.h>` / `ikea::boundlist`, with its
production qualification and StreamPack extraction complete. The experiments
below use that module; this spike has no remaining dependency on its graduation.

## Supplied model

- Records have unique six-byte physical keys and K-byte logical keys, sharing
  their first J bytes. Physical order preserves full logical order, including
  slots within segments. Equal logical keys have no required internal order.
- The tree has the five layers below. Every record contributes at every layer;
  a segment exists exactly when its underlying record population is positive.
  Within an index, layer and physical prefix identify the segment.
- Layer 4 holds records. Higher layers hold summaries of the records matching
  each entry's prefix: count and per-field min, max, sum and not-null count,
  with optional filters, sketches, histograms and other data structures.
- Each segment is an Orbital Object and has one representation, with several
  Parts. The index's schema can define many segment representations, including
  historical representations still used by segments that have not migrated.
  The set of allowed representations can vary by layer. The index also has a
  Part collection, directory and auxiliary stores such as persistent cursors,
  query plans and workload telemetry.
- A query starts in the directory, seeking the first overlapping segment dirent
  at a plan-chosen layer, typically the lowest common ancestor. Ordering and
  continuation are within a layer. There is no order across layers. A change
  of layer is an explicit tree-navigation operation.
- The proposed directory uses ordinary tables for the first two layers,
  hashtables in the shared-prefix region and boundlists for the remaining layers.
- A dirent is 64 bytes: six eight-byte Part locators and sixteen metadata bytes.
  Locators can name memory or remote resources; metadata includes representation,
  mapping, StreamPack overflow coordinates and MVCC information.

| Layer | Fixed physical prefix | Local 16-bit window | Maximum segments |
| --- | --- | --- | --- |
| 0 | Empty | 0:16 | 1 |
| 1 | First byte | 8:24 | 256 |
| 2 | First two bytes | 16:32 | 65,536 |
| 3 | First three bytes | 24:40 | 16,777,216 |
| 4 | First four bytes | 32:48 | 4,294,967,296 |

Bit positions are by significance, most significant first. Each segment has up
to 65,536 positions. An entry fixes L+2 prefix bytes: one-layer descent selects
a 256-position slice of the next segment; two-layer descent selects a whole
segment. Full counts and the fence after the maximum position need encodings
that can express them.

The first two full dirent arrays cost 16,448 bytes before presence metadata,
schema and retained versions. A segment prefix is derivable from logical bytes
when L <= J. These arithmetic facts guide the implementation; the experiment's
queries supply their entry layer explicitly.

## Implementation questions and experiments

### Directory storage and access

Build the supplied table/hash/boundlist composition. Keep each lookup and cursor
qualified by index, layer and snapshot. Test exact-prefix resolution, the initial
logical-range seek, and continuation at that same layer. Tree descent supplies
the destination layer and prefix explicitly.

| Component | Implementation choices to measure |
| --- | --- |
| First two layers | Direct 64-byte entries; explicit presence bits versus an absent-entry encoding; aligned allocation and partial-field access. |
| Shared-prefix hashes | Actual Ikea hashtable with 64-byte values; bucket geometry, occupancy, overflow and growth. Charge full-value materialization and the selected Part read. |
| Missing-prefix seek and continuation | Per-layer presence/rank metadata or links; compare reuse of ancestor population where useful, including its dependent fetch and decode. An exact hash miss still needs the specified seek result. |
| Lower-layer boundlists | One list per layer versus partitioning by a preserved prefix or directory page; bounds separate from descriptors versus colocated/tiled placement; direct rank-to-dirent access. |
| Directory edits | Coarse copies versus page/bucket edits, spare capacity and split/merge hysteresis. Compare an interior edit as unrelated directory population grows. |

When logical bytes do not supply the selected layer's physical prefix, compare
searching bounds alongside the ordinary tables with reusing segment metadata
for the logical seek. Direct array addressing resolves a known prefix.

Use a plain sorted per-layer bounds/descriptor array as an independent storage
control. Every candidate addresses the same segments. An upper directory over
pages is storage machinery for one layer, with its bytes and update costs counted.
Do not cap a layer's directory at a segment's 65,536 positions or StreamPack's
256 ranks. Use [Boundlist's qualified interface](../../../ikea/docs/boundlist/usage.md)
and [reference](../../../ikea/docs/boundlist/reference.md), with the
[graduation evidence](../boundlist/GRADUATION.md) and retained controls. Its
paired-range operation does not require this caller to perform two boundary
searches when it needs one initial seek.
The final [StreamPack extraction parity](../boundlist/evidence/parity/README.md)
supplements the search and Loom evidence; it does not establish mutable-directory
costs or selective-sharing efficiency.

Boundlist supports borrowed direct/width sources and custom `.size()`/`.at()`
sources; the byte compatibility recipe is separately named `boundlist::sidecar`.
Bind a directory source while its bounds, overflow and interpretation remain
protected by the selected snapshot. Sources support at most `UINT_MAX` ranks,
with count also the absence sentinel. Cover the possible 2^32 layer-4 segments
using partitioned lists with local ranks and a wider outer coordinate. Measure
partition overhead and include the full count/end position in capacity checks.

For repeated native numeric searches, compare ordinary sources with
`author::bind_search`, charging its preparation and 40/48-byte binding. Rebuild
the binding after any endpoint, count or placement change. Its checked
interpolation is a private implementation choice; applicability differs between
point and range consumers and across platforms. Use the
[qualified scorecard](../boundlist/evidence/graduation/scorecard.md) to select
controls, then measure amortization under actual directory-version lifetimes.

For logical seek metadata, compare exact occupied maxima with min/max or
separator encodings. Preserve populated segments with repeated bounds and find
the first duplicate when a run crosses segments. A candidate segment can contain
a logical gap; its local records or summaries still need evaluation. Exercise
equality, open bounds, absent prefixes, below-first and above-last at each
selected layer.
For unique point access at the record layer, exact inclusive maxima permit one
boundary search followed by one local exact search. Lossy abbreviations require
exact comparison or correction. Measure fence repair on insertion, deletion
and relabel, including long keys, binary zero bytes and all-equal populations.
No numeric or 64-bit ceiling on K is implied by the physical key width.

### Dirents, schema, overflow and Parts

Write an actual field/bit allocation for the sixteen metadata bytes. Establish
which facts are implicit in index/layer/prefix or shared schema, which require
stored bits, and how version selection uses Orbital. Keep the six locators and
charge every additional table, Control and retained history. Current
ObjectBinding occupies 24 bytes and does not fit that metadata allowance.
Representation identity, selected version and locator interpretation must agree.

Measure representation-ID encoding and schema lookup as the number of definitions
grows, including a long tail of historical definitions and differing allowed
sets by layer. Exercise segments using different representations at the same
layer, migration of selected segments while others retain older representations,
and retained readers that still need the earlier interpretation. Account for
schema bytes, layer-specific admission and lookup/binding costs as well as the
dirent field width. Include varied Part/overflow recipes for optional data
structures.

Measure the complete read from lookup through one, several or all Parts of the
segment's representation, including overflow-locator access where needed.
Inspect alignment, cache-line crossings and emitted loads; a 64-byte
record need not be cache-line aligned. Ikea's current 64-byte primary values
are contiguous per row, but its ordinary callback materializes a complete value.
Compare the appropriate native consumer before attributing that cost to storage.

Exercise memory pointers, mapped backing and opaque resource identifiers, with
explicit locator-kind and residency interpretation. Work out the StreamPack
overflow recipe for long bounds, additional metadata and any Part locators beyond
the six inline slots, including growth, rank repair, compaction and retained old
coordinates. Try independent Part allocations and useful colocated groups,
counting both access locality and the backing pinned by a selected Part. Keep
byte relocation separate from physical-key rebalance.

### Ordered physical placement and rebalance

Use a bounded placement driver to generate real directory and segment edits.
Compare packed placement with tail slack, local gaps, frontier padding and
hierarchical redistribution. Vary where spare positions sit within segments and
between occupied segment prefixes. Measure lookup/scan consequences alongside
mutation cost. A small population spread across all 48 bits is not free slack:
it creates segments and summaries at the corresponding prefixes.

Insertion must remain after strictly smaller keys and before strictly larger
ones. For duplicates, any hole in that legal run is usable. Include moving one
boundary duplicate to a hole later in its run to make room for an intervening
key. Compare that move with ordinary shifting. Exercise adjacent occupied slots,
append/prepend, fixed and moving hotspots, long duplicate runs, delete/reinsert
and consolidation. Each fixed J-prefix has 2^(48-8J) positions; test exhaustion
in small equivalent domains without borrowing across its boundary.

Separate physical-label slack from payload slack. Relabeling can leave packed
payload ranks unchanged; middle insertion can still shift columns. Vary the
fixture's supplied segment versions and Part changes while holding directory
code fixed. The actual record/aggregate storage implementation belongs to Engine
work and the later end-to-end comparison, rather than this directory experiment.
Record moved labels, shifted payload bytes, changed dirents and bound repairs.
A last-byte-only relabel leaves all four ancestor entry prefixes unchanged;
wider moves transfer the affected summary contributions. Maintain count and
nullable integer-field summaries in the fixture, checking deletion of extrema
and empty/nonempty transitions in the fixture oracle. A value replacement leaves
row count and logical-key membership unchanged; the fixture can derive its
before/after contributions directly.
The [aggregate-maintenance conclusions](../aggregate-maintenance/CONCLUSIONS.md)
separate additive corrections from last-witness extrema repair. Their eager,
bounded-append and location-addressed controls are relevant when summary writes
dominate. Price construction, corrected reads and full draining together;
fewer ancestor adjustments did not reliably mean less CPU in that study.
Its count/sum probes did not implement retained snapshots, nullable fields or
extrema repair. Aggregate-policy selection remains outside the directory's
ownership. Directory workloads should include different segment-update fanouts
and Part recipes, without treating fixture aggregate costs as directory latency.

### MVCC, execution and external references

Use actual Orbital allocations, Objects, Representations and Parts from the first
complete read/edit fixture. Publish compatible segment, directory, schema,
overflow and required summary changes together. Keep an old Scope through
interior edits, segment removal/recreation, representation conversion and byte
relocation. Check old and new reads, late discovery, absence and backing reuse.
Exercise construction failure, preparation refusal, abort and stale-basis retry;
current preparation requires the current local cut. Count retained and private
bytes through final settlement.

Boundlist's retained-reader histories qualify original-source effects, rank
remaps and native padded tails using full-copy versions. Reuse those cases;
measure selective copy/sharing and this directory's split/merge/rebalance costs
here. They have not been established by the extraction experiment.

Begin with ready direct calls. Compare lookahead and actual Loom Rings for
independent directory-to-Part dependencies; use Tasks when access suspends.
Carry the selected version and interpretation through waits and escaping Part
uses. Compare a useful bound batch with repeated binding, including setup and
drain. A pending Boundlist population phase also borrows its named source/access;
keep that object alive through completion, including across dispatch and waits.
Protect the actual checkpoint, width and population read windows. The exposed
`local_window()` is a hint target, not a complete read grant.
Test cached dirent locations, query plans and snapshot cursors after
relabel, directory rank changes and schema replacement. A physical position can
change, and a logical key cannot identify one particular duplicate; the fixture
must expose those effects to callers rather than silently promise stable handles.

Identify portable directory/schema/locator bytes and reconstruction work with
a changed-address reopen fixture. Record the required atomic durable update and
resource-resolution dependencies for Orbital integration. Local publication and
a delayed-resource control establish neither crash recovery nor remote-storage
performance. Keep those limits explicit in the recommendation.

## First executable comparisons

Start with small real five-layer fixtures for J=0 and a useful positive J. Time
the proposed composition and plain per-layer storage controls through actual
dirent and Part consumption. Include entry at multiple supplied layers, an exact
hit, a missing-prefix seek, same-layer continuation, a summary-only read and a
bounded descent that explicitly changes layer. Include value-only and key/bound
edits with a retained reader in the first panel.

Then extend the same fixture with overflow and directory growth, followed by
placement histories crossing segment boundaries. Compare the simple and
hierarchical redistribution policies on matched histories. Use boundlist's
selected bodies and controls; new kernel sweeps need a diagnosed directory cost.

Build expected results independently from logical records and physical-key
assignment. Derive segment membership separately at every layer; sort only
within that layer. Compare record/value multisets, nondecreasing logical order,
prefix-summary values and snapshot results, allowing arbitrary order among
equal keys. Small domains permit exhaustive seeks after each edit. Include
full/empty boundaries, duplicate runs crossing three segments, repeated bounds,
old overflow ranks, and full 65,536-position segment counts. Fixture-only record
identifiers help verify multiplicity without adding a production identity rule.

Report dependent latency and independent throughput separately. Vary J, K,
occupancy, layer population, duplicate length, insertion skew and hot/cold working
sets. Count metadata, overflow, scratch, changed/copied bytes and retained
versions alongside time; isolate lookup, lookup-plus-Part and complete edit
costs on the same traces. Start on the pinned Linux toolchain and Zen 5, then
qualify promising paths on Intel and NEON using existing controls. Follow the
[measurement conventions](../../benchmarks/README.md),
[tools](../../tools/README.md) and [datasets catalog](../../datasets/README.md).

The spike should produce a small runnable composition, measured storage and
placement choices, a concrete 64-byte dirent layout, and identified Ikea,
Orbital and Loom integration gaps. Retain losing cases and outstanding limits
with the recommendation. Query planning, the five-layer concept and aggregate
semantics remain the supplied context for this implementation work.

Implementation starts from [Ikea integration](../../../ikea/docs/integration.md),
[hashtable usage](../../../ikea/docs/hashtable/usage.md),
[Boundlist usage](../../../ikea/docs/boundlist/usage.md),
[StreamPack's reference](../../../ikea/docs/streampack/reference.md),
[Orbital material](../../../orbital/docs/material.md) and
[Loom execution](../../../loom/docs/execution.md). Reuse the module interfaces
and qualified controls; the directory's storage choices remain this study's work.

## Near-term Engine performance driver

Ashton also wants a new test driver to show end-to-end performance with Engine
in the critical path. Both previous drivers are stale and served their purpose;
their [retirement map](../../benchmarks/retired-drivers.md) identifies retained
evidence, production consequence checks and historical source recovery.
The [Engine-versus-LMDB proposal](ENGINE-VS-LMDB.md) develops end-to-end
demonstrations, strong LMDB controls and proposed performance targets once the
directory and required Engine operations are in place. No driver implementation
has started.
