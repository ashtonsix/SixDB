# What front A should help decide next

Ikea LEAD, 24 October 2026. A direction proposal for discussion with Claude,
not a new Frame contract, a narrowed brief or a graduation claim.

My contribution has become too concentrated on servicing requests and proving
individual comparison entry points. Those checks have caught consequential
errors, but their purpose is to change a representation or execution decision.
The next contribution should own such a decision across the controls, consumer
and lifecycle, rather than acquire more peers by default.

## The evidence changes the question

Phase 1's [inventory](../../frame-phase-1/INVENTORY.md) identified structure
replacing rows, workload-dependent representation choices, and execution
fusion across planning boundaries. Phase 2 makes those claims more credible:
front C's [integer leaves](../bodies/README.md) remove large code costs, while
the [shared image](../substrate/README.md#the-shared-image-substrate-over-front-cs-leaves)
shows that placement and traversal become consequential once leaves are fast.
The old attribution to dispatch did not survive that sequence.

The [complete-query panel](cells/FINDINGS.md) supplies a second correction.
The same image loses materially in small repeated queries and wins under
shared capacity pressure. On Zen 5 AVX-512 the complete 2,048-row
UserID-predicate/URLHash-SUM cell takes 399 ns versus native FOR's 166 ns with
one worker and one replica. With eight workers and a 512-replica pool per
worker, aggregate reciprocal throughput is 184 versus 580 ns/query. OS's
small full SUM still loses about 2.1×. Graviton 4's larger-pool pair advantage
is modest, not Zen's large reversal. These are capacity/conditioning
observations, not identification of a servicing cache or measured bus traffic.

I favour **a compact set of lawful representations with strong complete
consumers and inexpensive transitions**. Universal compressed hot-loop
dominance is neither established nor necessary. Native narrow/FOR must be an
ordinary candidate inside that set. This preserves the broad operation-family
goal; it changes the order of investigation, not the eventual surface.

## Choose one stored image across a workload

Runs versus packed, plane versus byte lane, and coded versus uncoded reverse
by operation. A collection of each operation's best result does not show a
useful representation: bytes are usually chosen before the next query arrives.
Late query dispatch cannot recover having stored the wrong body.

I propose a small manually enumerated mixed-workload witness before a general
analyser. Use the same original rows and one stored candidate per complete
trace; charge preparation and optional material. Compare native narrow/FOR,
the current runs/planes image, and a few genuine alternative programs. Record
natural bytes beside execution cost; a memory budget can exclude candidates
explicitly. Do not force every representation into an artificial equal size.

Cross full reductions, predicate(A) to consumed payload(B), points/gathers
and writes. Start with a few named mixes and vary their frequency, rather than
the Cartesian product of every codec and operation. The account is construction
plus preparation, queries, edits and folds, with retained bytes beside it.
Compare the best fixed candidate with the unattainable per-operation winner.
That gap measures the cost of committing to one representation. A small gap
supports a few useful presets; a large gap identifies where an extra index,
different grain or real transition might pay. It does not justify storing all
representations.

Body choice and width rounding are weighted decisions, not invisible fitting
details merely because logical operations agree. Grouped placement and
kind-batched traversal are coupled under cold scans. Engine should admit an
objective and candidate families; Ikea should fit and execute them. Test
whether that modest information suffices before expanding the planning interface.

## Selection geometry should drive complete operations

Selectivity alone is insufficient. Earlier UserID predicates skip almost all
payload blocks, while similarly sparse RegionID predicates touch most of them.
That changes the useful algorithm, not just a constant.

The next selective witness should hold selected-row count fixed while varying
the number and clustering of touched blocks, and contrast correlated with
independent predicate/payload fields. Include all/empty coarse blocks and
all/empty/partial fine selections. Supplied masks remain a separate case from
masks a real predicate must produce. Price acquisition and the consumed result.

I expect three useful shapes: skip a block, run an unmasked leaf for a fully
selected block, or run a partial-selection leaf. A dense dispersed selection
may instead favour one native fused pass. Measure those crossovers. The useful
seam is a prepared complete operation with correct original-row coordinates
and validity; it need not materialize a bitmap between every stage. Staging
should acquire payload justified by the resolved selection and price speculative
work separately.

The dictionary experiment exposes a related seam. Threshold translation lets
ordinary code masks work, but logical SUM needs per-code weights. Composition
therefore needs semantic leaf parameters, not only residual arithmetic. Front
C's weighted leaves are a concrete test. Charge preparation and reuse: retained
truth tables or weights can exceed the body they accelerate.

## Mutation may change the placement answer

The [relocation probe](../substrate/README.md#placement-under-relocating-writes)
shows disrupted order taxing scans while points and staged visits move little.
Its primitive copies an unchanged whole body to the end and repoints its
descriptor. It isolates spatial damage; it is not a real edit-policy cost.
Relocated-block percentage is a useful signal, not a sufficient compaction rule.

The promising lateral alternative is to leave a grouped immutable base in
place and collect bounded corrections near the group. Pristine groups could
retain today's path; changed groups acquire corrections and fold them into the
operation. Spatial order survives until a fold, at the cost of lookup, bytes,
evidence maintenance and fold work. This may lose badly when edits spread over
most blocks; retain that counterexample. Fixed-basis in-place repair and a
native mutable region are the other real controls.

I should own this comparison jointly with B, reusing phase 1's
[repair](../../frame-phase-1/lifecycle/REPAIR.md) and
[history](../../frame-phase-1/lifecycle/PROGRAM-COSTS.md) findings and the new
[physical-envelope witness](../change/README.md). Apply the same edits to a
few repeatedly hot rows and to one row in every block. Interleave scans and
points, change the read/write phase, and charge the final fold. No-fold is the
accumulating-debt control. Account for current plus retained old bytes with an
old reader held, without importing the full MVCC system.

Compaction must repay itself over remaining work. In a simple scan-only model,
if compaction costs C and saves d nanoseconds per scan, it needs more than C/d
future scans to amortize; copy/rebuild, extra storage and retention also matter.
Validate that model rather than derive a universal 1% rule. Scattered bytes,
hotness and expected scans matter alongside relocated share.

Keep logical dependencies, physical read/store authority before access, and
actual issued-byte effects separate. An immutable read window cannot silently
become a writer's window. Run-start maps, heads and statistics must describe
the same body edition or be invalidated. Ikea's lifecycle context can prevent
a fast read experiment from becoming an expensive write architecture.

## Construction and uncovered mechanisms deserve the next breadth

Encode/fitting cost and drift cannot be inferred from read wins. Before
elaborating an analyser, price a cheap prior-based proposal, recognition and
refinement against exact fitting, followed by actual emission. The
[bounds investigation](../../../notebook/pfor-bounds.md) warns that a feasible
guess can waste bits and narrower residuals need not minimize physical bytes.
Include bad priors and changing distributions; show how many later reads repay
construction and any size penalty.

The next breadth should test different laws: real floats/decimals and time
series for domain mapping, ALP opt-out and predictor/checkpoint choices;
encoded transforms, grouping and reorder for structure-preserving consumers.
Another integer SUM matrix cannot answer those questions. The ALP audit's
negative results mean an automatic decimal path is not a general-IEEE control;
correct RD fallback and declared aggregate semantics remain necessary.

This proposes priorities, not more threads. Finish the bounded B/C improvements,
then use these witnesses to challenge the seams before expanding their surface.

## My revised contribution

The actual [native controls](cells/README.md) are linked by B at `c07f25b4`.
Pinned [Calico](calico/README.md) now has callable exact signed/unsigned SUM
controls and functional qualification; corpus preparation is the last bounded
helper contribution before same-work timing. Its extreme range-predicate
overflow stays explicit. Native Polars and DuckDB entry points are functionally
qualified, but complete comparable timings are still owed. Those are different
states; the baseline roster should reflect them.

I will concentrate on the mixed-workload/transition question, starting with
honest Calico/native complete SUM and construction accounts, and jointly specify
corrections versus relocation with B. Further controls should enable a named
choice. System peers remain valuable at representative complete endpoints;
qualifying every peer for every microcell is not the immediate priority.

Existing instruments suffice for these bounded questions on small workers.
An expensive full host should answer a remaining topology/saturation question
that can change a choice. Warm repeat, flushed first pass and concurrent
capacity results keep distinct meanings. A flush alone does not prove DRAM
service or saturation; MECHANISMS.md's stronger shorthand should change.

Files remain the durable detailed account. A short direct message in the active
Claude thread should carry a decision-changing finding, recommendation and
pointer. A file or transport receipt alone does not prove the working peer has
seen it. The goal is fewer handoffs and more joint decisions, without settled
acknowledgment chains.

## Discussion with Claude, same day

Delivered directly into the active Frame phase-2 conversation through the
Claude UI, and read there. Claude agrees with the fixed-policy comparison,
selection geometry and lifecycle direction. Agreed work split:

- A defines the read/edit traces, fold histories and physical retained-byte
  account; B implements the shared-image driver and correction-store support.
- B owns selection geometry, charging preparation, and the corrections to
  MECHANISMS.md's conditioning language and baseline roster (`cd62bcd4`).
- Include a real two-image option under the budget. Both constructions,
  synchronization, evidence and retained generations count. The image pair is
  chosen before the trace; the per-operation oracle remains diagnostic.
- Use the qualified leaves for representation rankings. Retain actual edit
  and fold costs in the lifecycle experiment, even while their implementation
  is immature; distinguish that limitation from a representation limit.
- Construction/drift work starts with A's bounded accounts. A separate front
  can be considered when its seam is clear; this agreement starts no thread.

Further source review and an independent subagent challenge identify the most
consequential seam: **logical group, placement domain and independently
replaceable/reclaimable backing are different grains**. Today's `build(by_kind)`
sorts every block in an image and `compact` rewrites its single byte vector.
Those results cannot establish group-local folding. Phase 1 already encoded
16× fewer rows while copied pressure bytes fell only 42,304 to 35,136 because
unchanged neighbours shared backing
([changed-block fold](../../frame-phase-1/lifecycle/BLOCK-FOLD.md)).

The [proposed first witnesses](PROGRAMS.md) challenge that physical seam and
the correctness of complete-query corrections before enlarging the workload
driver. They also preserve a strong adaptive native control, changing
predicates, terminal-state distinctions and genuinely budgeted mirrors.
