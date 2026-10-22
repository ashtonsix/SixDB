# Frame under changing work

Investigation owned by Ikea LEAD, with Claude's encoding/algebra work and
Engine's execution witness. The [native phase baselines](NATIVE.md) now price
actual publication, queries,
conversion and retained readers. No Frame public interface or transition policy
is selected.

## Starting point

The [architectural review](../REVIEW.md#6-make-writable-plain-regions-a-deliberate-state)
proposes retaining plain regions during churn, selective folding and cleaning
that restores useful computation. Claude's integration makes plain a common
form for a streaming tail and a thawed region; these need not become separate
public representations. The question is when changing form earns its cost.

Prior findings change the experiment:

- [StreamPack](../../streampack/RECOMMENDATION.md#what-the-whole-spike-taught-us)
  found that cleaning and reader-phase ownership changed complete work much
  more than some kernel improvements. Faster isolated updates did not reliably
  predict mixed-workload winners. Do not stop a write timer before slack fills.
- [Aggregate maintenance](../../aggregate-maintenance/CONCLUSIONS.md)
  found that reducing ancestor adjustments could cost much more CPU; lighter
  buffering helped repeated hot locations but lost on dispersed writes.
  Distinct dirty positions and their topology matter alongside write count.
- [Orbital views](../../orbital-views/FINDINGS.md) distinguishes logical effects,
  actual preserving stores, copied material and independently retained backing.
  Current [material composition](../../../../orbital/docs/material.md) can
  retain unchanged bytes without making sibling slices independently reclaimable.

These findings do not select an adaptive Frame policy. They identify the costs
and counterexamples that a useful experiment must preserve.

## Questions to discriminate

| Question | Competing explanations | Evidence that would distinguish them |
| --- | --- | --- |
| What makes an encoded region unsuitable for writes? | New distinct overrides, repeated edits to existing overrides, width/dictionary novelty, prediction dependency, or shared-store traffic | Equal write counts with different distinct positions, novelty and clustering; separate lookup/update, insertion/movement and cleaning costs |
| When should a region stay plain? | Plain pays only at high churn, or its cheaper reads/writes offset larger backing even through a later read phase | Complete phase traces with conversion charged, not steady-state kernels alone |
| How local can repair really be? | A changed block can fold independently, or dictionary/category/presence dependencies widen the required rebuild | Track the transitive interpretation dependencies, old readers and actual unchanged-material reuse |
| What does cleaning buy? | Fewer sideband bytes, cheaper selected operations, tighter pruning, or less retained backing | Compare equal-density but differently clustered overrides and changed extrema; price each consumer and eventual release |
| Which physical ownership grain pays? | Block/group ownership saves copying, or metadata/slack and many small allocations dominate | Same logical trace and encoding with independent versus shared backing; charge peak and retained bytes as well as stores |

## One chronology shared with Engine

Use ordinary logical rows/queries from Engine's [execution witness](../execution/README.md)
and real-column characteristics from [encoding](../encoding/README.md). The
initial arithmetic instrument uses a named exact bounded integer domain; it
does not choose SixDB's global float, decimal or NULL policy.

The chronology keeps the logical input and demanded answers fixed across
candidate representations:

1. Build a frozen image; run a mix of selected queries.
2. Retain a reader on that image. Apply an ordered batch to a private successor.
3. Query the new image and the retained old image, including NULL transitions
   and repeated writes to one position.
4. Fold, thaw or re-encode according to an explicitly named experimental policy;
   query again without changing logical contents.
5. Change the access phase while keeping the data; let execution rebind without
   necessarily migrating bytes.
6. Release the old reader and observe eventual reclaim separately from publication.

The first trace contrasts a repeatedly edited hot subset with the same number
of dispersed edits, then a read phase. Follow with a moving hot set and
width/dictionary novelty. These are forcing cases, not assumed workload frequencies.
Vary reader duration independently: no retained old reader, a reader across one
publication, and a reader spanning several conversions.

Physical choices to compare remain open: base plus sorted bitmap-ranked overrides,
private append/coalesce before publication, changed-block folding, plain writable
regions and the proposed whole-group re-encode control. Reuse the same plain
form for append and thaw when its meaning matches. A policy which never cleans
is a useful debt control, not a completed steady-state comparison.

## Keep semantics separate from physical work

Carry block masks through reads, reductions, replacement and movement experiments
wherever their semantics allow; derive whole-group skips from those block bits.
Excluded regions must avoid payload reads, decoding and writes; locating the next selected variable-size
region can still require addressing metadata. Keep row masks, presence and NULLs
separate. Returning a compacted result must not silently change what later masks
mean in original coordinates.

Compare all/none, scattered blocks and contiguous selected runs at equal density,
with and without fine selection inside active groups. Charge mask traversal and
preparation, and check issued stores and copied bytes as well as result equality.
An all mask should retain the unmasked path's throughput; a none mask should
perform no input-payload access or payload mutation/copy stores, while still
producing any required empty result. Native access checks and
timings must establish this: logical counters alone cannot prove skipped memory
traffic. Prediction/checkpoint, dictionary and presence dependencies may require
material outside the selected output groups; expose those dependency halos and
their costs rather than reading excluded regions unconditionally.

The independent logical oracle must observe every published image and retained
reader. A physical re-encode has zero logical contribution. The same update
can have different stored deltas under dictionary, affine, split or predicted
encodings. Fused maintenance should exploit lawful cheap cases without making
packed `sum_change` the definition of a logical update.

Deletion of an extremum witness can invalidate exact MIN while retaining a safe
pruning bound. Query demand determines whether tightening that evidence is worth
doing now. Selected reductions should exclude overridden outputs from the base
answer, while retaining predecessor residuals needed by later selected values.

The head/bounds experiment suggests another concrete lifecycle comparison: keep
evidence over the immutable **base**, and compose it with coalesced logical
overrides. Under the existing exact bounded integer law, SUM is base SUM minus
the old base contributions of distinct overridden positions plus their current
contributions; COUNT similarly subtracts/adds the predicate bits (with presence
and NULL handled explicitly). Repeated writes to one position change one current
contribution, not several subtractions. Storing the old logical value in an
override costs space but can avoid a predicted point read on every later query.

That program need not rebase a whole part's head plane or extend/reorder its
dictionary for each novel override. The old evidence and dictionary still mean
exactly the old base; selected override contributions are separate exact work.
Compare it with widening/dirty evidence over the combined image, and charge
lookup/coalescing, override bytes, complete-query correction and eventual fold.
After folding changes the base, evidence/editions and any retained old reader
must remain coherent. This is a proposed discriminating program, not a measured
lifecycle result or a selected mutation format.

Dictionary, routing and compacted-presence editions are immutable decoding
dependencies until replaced. The current query category need not be the routing
basis for old bytes. A one-group conversion must either preserve those editions
or repair every dependent region it changes. Keep a category-changing arm as
soon as the cross-column image exists; do not let the initial independent-column
control silently make this problem disappear.

Logical row/field grants and the physical serialization envelope are different.
Use Ikea's current source-qualified store coverage and current coherent local
Object publication for the owner witness. Durable epoch admission and generic
Draft/lock services are not implemented prerequisites to invent in this study.

## Measurements and useful boundaries

Report complete trace throughput, per-operation distributions and the largest
cleaning pause. Separate analysis, encoding, lookup/update, repack/fold, publication
and release without removing their cost from the total. Distinguish declared
logical changes, issued stores, copied bytes, allocation capacity, mapped backing,
query scratch and retained generations. Counts or modeled byte costs are not
timings or cache-service evidence.

The control set includes tuned plain/current-Ikea work, the proposed fixed packed
basis, and strong complete-operation consumers. Codec peers and system peers
answer different questions. A raw in-place setter without retention/publication
is a kernel control, not a fair transactional-write baseline.

Begin with semantic histories and a directly inspected primitive composition;
price lifecycle policies with native execution once the competing encoding
bodies exist. Avoid timing a Python state model and presenting it as Frame
performance. Native jobs and source captures are coordinated through Workbench;
the question index remains [QUESTIONS.md](../QUESTIONS.md).

## First executable instrument

`history.py` reuses Engine's `execution/workload.py` logical document and oracle.
It emits repeated-hot, dispersed and width-novelty mutation phases, read and
conversion markers, and an old-reader hold/release. It names original positions
and snapshot/dictionary editions; it imposes no encoded-byte format. Logical
dictionary extension in the reference is not a requirement to extend an encoded
dictionary rather than add an override.

Run from the repository root on Linux (prefix `orb -m ubuntu` from the Mac):

```sh
python3 workbench/spikes/ikea-frame/lifecycle/history.py check
python3 workbench/spikes/ikea-frame/lifecycle/history.py generate \
  --output build/ikea-frame/lifecycle/first-trace --rows 256 --batches 4 --updates 128
```

The initial run emits 13 logical snapshots and 32 events. Literal small-case
expectations cover retained old values, NULL versus zero, dictionary novelty,
ordered repeated writes and refusal without altering the basis. Generated
traces check replay and answer digests against that same logical instrument.
These are semantic checks, not performance measurements or validation of a
physical retained-reader implementation. The emitted snapshots can feed the
same actual hash-output adapter as Engine's static witness.

## Fitting delta replacement before folding

The existing delta body stores `s_i = x_i - x_(i-1) - rbase` and checkpoints of
the stored residual prefix. For a fixed anchor and `rbase`, changing an interior
logical value by `d` repairs `s_j += d` and `s_(j+1) -= d`. The prefix correction
cancels after row j; only a checkpoint exactly through j needs changing. The
last row needs one residual and no checkpoint. This is a candidate cheap write
case before paying for overrides, thawing or re-encoding.

The prior [aggregate-maintenance closeout](../../aggregate-maintenance/CONCLUSIONS.md)
warns that fewer adjustments do not establish cheaper complete work, and
[StreamPack's lifecycle findings](../../streampack/RECOMMENDATION.md#what-the-whole-spike-taught-us)
show that publication and cleaning can dominate isolated writers. This bounded
experiment therefore checks the physical repair law first; it does not time a
private setter and claim a transactional write win.

`delta_repair.h` uses actual typed SeriesPack mutation and resolves its source
journals before the borrowed views expire. Both new residuals must fit before
any store. Logical and physical locality differ: packed setters can store
preserved neighbours, so the caller must isolate the complete issued-store
envelope rather than infer safety from the two logical rows. The original
logical SUM change is `d`, even though the two residual adjustments cancel.

`frame_delta_repair_check` exercises widths 1/8/13/24/32/56/63/64, all four
checkpoint modes, monotone and zigzag inputs, every interior/last position and inverse edits. It compares
all points, exact SUM and COUNT to originals; checks reported byte coverage,
checkpoint locality and refusal without mutation; and protects a coarse-skipped
column, with an active positive control that must fault. The packed-CP and wide
wrapping-prefix cases use the existing material's exact geometry. All 32,640
fitting edits pass on the frozen f760 owner; its COUNT uses the original scan.
The newer monotone-COUNT implementation requires its own coherent source check.

Anchor changes, exceptions, dictionaries and new residual widths are explicitly
refused here. With exception gaps, multiple checkpoint positions can lie between
ordinary neighbours, so the one-checkpoint result must not be generalised.
Private writable-body acquisition, locks, retained-reader publication, actual
write timing and the frequency of fitting repairs on real traces remain to be
priced by the lifecycle experiment.
Any mutable-base logical heads, bounds or summaries also need repair or valid
invalidation. Two residual changes do not remove that separate evidence work.
The [native check and identities](evidence/delta-repair-local/identity.json) and
[verified recovery](evidence/delta-repair-local/artifact.json) retain this
mechanism result. Its stack/result plumbing is a correctness instrument, not a
selected hot writer interface or performance baseline.

That adapter was also checked over all 13 initial snapshots: eight queries per
snapshot agree with the logical oracle through each of the two hash programs
(208 query/program comparisons). This checks the consumer handoff over changing
logical inputs; physical retained-reader and adoption checks now have their own native evidence
in [the phase baselines](NATIVE.md).


## Native physical baselines

`frame_phase_material_check` verifies separate presence/validity, bounded owner
borrows, exact masked aggregates, overrides, actual publication/adoption and
escaping dependency bundles. `frame_phase_measure` compares native/raw/FOR,
whole-group re-encode, base/overrides and thaw/reseal under identical traces.
The [findings and limits](NATIVE.md) distinguish real phase timings from the
older logical instrument and fixed-basis repair law. These controls are private
research machinery, not a promoted Frame mutation API.


Current findings: [sparse proof, native coalescing and NULL-aware reads](REPAIR.md).
[Changed-block folding](BLOCK-FOLD.md) retains the cheaper fallback and its
copy-grain limitations. Sources and hardware conditions differ across these
cohorts; their paired controls, rather than cross-cohort subtraction, own the
comparisons.

The [batch follow-up](BATCH.md) charges sorting/indexing and linear override
merging. Dispersed override writes improve; repeated-hot edits and sparse block
indexing do not justify a universal replacement. Fold debt remains explicit.
