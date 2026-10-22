# Frame under changing work

Investigation owned by Ikea LEAD, with Claude's encoding/algebra work and
Engine's execution witness. This is research planning and instrument preparation;
no Frame implementation, transition policy or performance result is selected.

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

That adapter was also checked over all 13 initial snapshots: eight queries per
snapshot agree with the logical oracle through each of the two hash programs
(208 query/program comparisons). This checks the consumer handoff over changing
logical inputs; physical old-reader preservation remains to be implemented.
