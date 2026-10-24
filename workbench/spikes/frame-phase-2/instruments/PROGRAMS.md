# Fixed policies and changing data: first witnesses

Ikea LEAD's concrete proposal following [the direction discussion](DIRECTION.md).
Claude owns the shared-image implementation; A owns these traces and accounts.
These are forcing experiments, not production workload frequencies, a selected
group size or a new lifecycle framework.

The named policies below are experimental compositions, not a closed set of
Frame formats. Engine must still be able to compose encoding stages, with
efficient consumers over their combinations. The integer image is a scaffold
for measuring those seams; a policy comparison cannot settle composition laws
that it does not exercise. Prediction/checkpoints, presence with original-row
selections and independent filtering evidence remain distinct seam challengers.

## The first architectural witness

Keep four grains explicit: 256-row body; logical correction group; body-placement
domain; independently owned backing that can be replaced and reclaimed.
Today `image.h` groups every block of one allocation and compacts the whole
allocation. Calling a smaller logical set a group does not make folding local.

Compare today's global grouping with independently owned logical groups whose
bodies are grouped only inside that group. Use 16 blocks as one forcing case
and a whole 256-block part as the larger control. Keep descriptor/index bytes
and readable suffixes in the account. Neither grain is a recommendation.

Begin with a small semantic trace, before full timing:

1. Retain an immutable reader of the original two-group image. Demand a point,
   full SUM and COUNT from it.
2. Change one constant block into a nonconstant plane, and one narrow block
   into a wide body; keep the other group unchanged.
3. Fold only affected material. Re-read both old and current views. Check
   original-row routing, kind-index membership, read envelopes and untouched
   neighbours. Record actual newly allocated backing and copied/stored ranges.
4. Release the old reader. Report which bytes are actually reclaimable and
   which remain retained by the current view, schema, dictionary or other owner.
5. Repeat with one edited block in every logical group.

Explicit immutable owners and retained borrows suffice; no lock manager or
transaction protocol is required. Compare both group-local and global placement
on reads as well as folding: cheaper copies may cost scan locality. A shared
outer byte vector with changed offsets is not independent group backing.

## Corrections must describe the complete operation

Use a tiny original-row A/B fixture whose values straddle a fixed threshold.
In one ordered edit batch, move A across the threshold, replace B at that row,
assign the same row again, and restore another row to its base value. In a
coded arm add a dictionary-novel value. Demand SUM, COUNT, A-predicate to
SUM(B)/COUNT, and points after each batch; include all/empty coarse masks and
an excluded inaccessible group.

For each distinct overridden row, the complete query's contribution must be
removed from the immutable base and its current contribution added. For the
pair, that means both old/new predicate truth and old/new payload, not merely
the payload's SUM delta. Repeated assignments retain input order and coalesce
to one current contribution. A restoration is allowed to remove an override.
Wide arithmetic and intermediate subtraction must remain exact; a transient
negative correction cannot be represented by wrapping an unsigned accumulator.

Immutable-base bounds, dictionary translations and weights describe that base,
not the combined logical image. A novel correction can remain outside its
dictionary, but may not disappear because base evidence pruned the block.
Folding changes the physical basis; old correction bundles must not attach to
the new basis, even if the logical version is equal.

## Stored-policy and read traces

Commit to an admitted representation policy and ownership grain before a trace,
not widths/kinds that are forbidden to change. Declare its lawful local fitting,
repair and fallback rules. Start with native narrow/FOR, the current integer
image, packed-only, and sorted dictionaries. Coded SUM joins when its weighted
leaf is ready. The mutation-native control must be adaptive at the same
independent ownership grain: fitting stores, widening/rebase when necessary and
conservative bounds. A global FOR array widened by one edit is insufficient as
the sole mutable control; phase 1's [native program](../../frame-phase-1/lifecycle/NATIVE.md)
already demonstrates the stronger alternative.

Reuse the original ClickBench u64 sidecars and their existing hashes. For a
65,536-row part, take the first, middle and last complete part as declared
forcing inputs, keeping their original row order. Do not silently infer general
population coverage. The existing many-small-query capacity panel remains a
different workload rather than being relabelled as this trace.

Each read mix has a fixed 16-command template:

| Mix | Commands |
| --- | --- |
| Reduction | 8 full SUM and 8 full COUNT(A≥t) |
| Selection | 8 A≥t → SUM(B)/COUNT, 4 COUNT(A≥t), 4 SUM(B) |
| Point-heavy | 14 batches of 256 independent consumed point requests, 1 SUM(B), 1 pair |

Freeze original-row request lists, command order and query values independently
of candidates. A point batch consumes an exact SUM of requested values, including
duplicates; it is not 256 dependent loads. Use one repeated-threshold version
and one changing-threshold version drawn from the same fixed query list.
Changing data changes answers/selectivity; never retune thresholds to preserve
a favourable outcome. B's selection-geometry experiment owns the orthogonal
selected-rows-versus-touched-blocks comparison.

No whole-query answer cache is implicit in an arm. If a real block/part summary
is evaluated, make it a separately charged evidence choice with construction,
maintenance and validity. It cannot answer partial selections for free.

## Histories and terminal conditions

Reuse phase 1's repeated-hot/dispersed histories and semantic oracles where
possible: eight ordered 64-edit batches, repeated positions versus broad
block coverage, with input-order last assignment winning. Keep fitting edits
and width/dictionary novelty separately labelled. Edit both predicate and
payload fields so a payload-only correction cannot pass accidentally.

Cross two schedules with the same edit list and read-command multiset: writes
followed by a read phase, and reads interleaved between edit batches. Include
at least two permitted fold points, and retain the initial reader either
through all edits or release it before the later fold. Use no-fold, explicit
fold, fitting repair/relocation and native as real arms. Phase-1
[history findings](../../frame-phase-1/lifecycle/PROGRAM-COSTS.md) already show
that reducing codec work or tidy bytes need not improve the complete history.

Report continued operation with outstanding debt separately from compact export
and disposal. Show a separately measured finalization cost and final state;
do not force a fold at an artificial end and call it universally required.
Conversely, a no-fold win is not proof of bounded sustained operation. Hold
future-query counts fixed when evaluating the simple C/d break-even; its scan
saving changes with selections, further edits and conditioning.

## Budget, mirrors and measurement

Include a fixed Frame-plus-native pair under an explicit memory budget. Also
permit one complete image plus a mirror of declared hot groups, so the budget
does not arbitrarily exclude useful partial duplication. Choose the image pair,
mirrored groups and routing rule before the trace. A later phase change can
test that commitment. Outside-mirror queries need a real fallback; changes must
refresh or invalidate mirrored material before it is used. Price maintenance
even when one role sees no immediate query.

Report natural retained-byte/time frontiers and mark which candidates satisfy
the budget. Account separately for serialized bytes, actual retained capacities,
shareable interpretation/preparation, corrections, obsolete backing, old
readers, mirrors, scratch and peak build/fold coexistence. Deduplicate physical
backing shared by views. Distinguish known capacity from unavailable private
allocator overhead. Make the budget's steady versus peak scope explicit.

The per-operation oracle is a specialization diagnostic: its isolated fastest
times can have incompatible residence/preparation and are not a runnable rival.
Run actual fixed-policy and two-image interleavings. Replay one changed mix
without refitting objectives to distinguish a useful policy from hindsight.

Use existing independent original-value oracles and actual consumed outputs;
perform costly storage censuses in separate untimed replays, as phase 1 required.
Measure complete phases, with construction/preparation and fold costs visible,
and retain operation-family breakdowns without per-row clock calls. Read-only
rankings use qualified leaves; immature mutation costs remain visible and
explicitly attributed. Existing small-worker conditioning, balanced order and
common-window tools suffice initially. No full-host run follows from this plan.
