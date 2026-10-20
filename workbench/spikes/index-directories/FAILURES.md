# Editor failure, rollback and retry

The `failure` driver extends the composed directory through construction
failure, preparation refusal, accepted-packet abort and stale-basis retry.
It measures the normal writer cost of retaining an undo log, alongside an
unprotected no-fault control. The supplied segment identities, layers, ordering
and query semantics are unchanged.

## Private state and native ownership

`try_edit`, `migrate` and `try_extend_schema` return an Orbital error after
restoring the private author state when construction or publication fails.
The existing `edit`/`extend_schema` convenience calls still terminate on a
returned error, after rollback. These are spike interfaces, not a promoted
Engine transaction API. Valid input is assumed; programmer assertions retain
their existing meaning.

The writer records changed scalar values, row/descriptor values, map insertion,
map extraction and replacement of overflow-prefix vectors. Erased map nodes
remain owned by the log until commit. Reverse replay reinserts parents before
restoring their older nested edits, using the retained nodes and their original
allocator. It needs no allocation. Field/logical-key frequency maps are edited
in place; the log does not clone complete histograms or the whole author model.
Successful publication discards old nodes and saved values.

Value-only replacements now apply old/new field contributions directly. They
leave row count and logical-key frequencies untouched, save sum/presence only
when those scalars change, and retain a removed last-witness node with its
original count. Fresh field counters need only an insertion inverse. Unchanged
directory descriptors and logical bounds require no author-cache snapshot.
The original remove/re-add model remains a measured control. Both use the same
native summary patch and publish the same five segment versions.

This applies the distinction in the earlier
[aggregate-maintenance study](../aggregate-maintenance/CONCLUSIONS.md).
Complete ordered field-frequency maps still supply exact extrema in this
fixture; the experiment has not selected those maps as an Engine representation
or implemented buffered summary maintenance. The earlier count/sum buffer
results do not establish extrema, NULL or retained-snapshot behavior.

Journal records and their pointer vector use a per-attempt monotonic arena with
8 KiB of local storage. Larger attempts allocate spill blocks. The writer also
retains its prior overflow-prefix vector when rebuilding that vector. Reusable
Core/scratch capacity can grow during a failed attempt; rollback restores
logical state, rather than shrinking every reserved allocation.

The native capture helper owns newly allocated Geometry and locator proofs
until the captured representation has retained them. Raw metadata slots are
freed if their constructors throw. Writer scratch always clears borrowed source
certificates and private Loom Allocations on return. Hash construction and the
capture caller hold hash owners through failures as well. This closes failure
paths that a successful copy/share panel could not establish.

The helper converts typed callee errors and `std::bad_alloc` into a capacity or
other Orbital error at the writer boundary. An accepted `Prepared` packet is
aborted by its destructor on failure before installation. A retry constructs a
new packet and selects a fresh basis; an accepted-and-aborted packet is not
reused. Successful installation is followed only by nonthrowing bookkeeping and
journal commit.

## Failure histories

The checkpoint sweep crosses J=0/2, raw/compact locators and flat, occupied-page
and branched directories. Each case fails every reached checkpoint in turn:
native allocation, new Geometry, new locator set, representation capture,
before prepare and after actual prepare acceptance. These are injected typed
failures at the named boundaries, including accepted-packet abort; they are not
claims of actual OS allocation exhaustion.

Operations include first population, value replacement, insertion plus removal,
complete removal, schema extension and selected-segment migration. After every
failure, the checks compare author rows, segment population, fresh published
reads and an older retained reader with independent record-derived summaries.
The final identical request then succeeds. Failed schema extension also has to
remove its private definition overrides so the same IDs can be added on retry.
An additional consecutive-value history removes repeated extrema witnesses,
removes their last witness, crosses the all-null boundary, changes a null
payload and repeats an unchanged value. Its failed attempts and retained
readers are checked against record-derived summaries at every layer.

A separate fresh-Store sweep throws from every actual metadata allocation in
the attempt. It repeats with journal spill storage on that same failing resource
and zero local journal bytes. This exercises partial proof construction, native
capture and preparation refusal, plus undo-log allocation failure before a
mutation is applied. Fresh Stores avoid warming away later allocator requests.
The small map check disables allocation while replay restores an extracted node;
it also refuses an actual map-node allocation after recording its inverse.

Another history refuses actual Store preparation with the metadata allocator.
For staleness, an independent publication advances the same Core with an
unchanged schema payload while the editor still has its earlier basis. Prepare
returns `stale`; rollback preserves that independent publication and restores
the author rows. A fresh-basis retry succeeds while the old reader remains
valid. The fixture has one synchronous author; this is not concurrent author-map
rebasing or cross-worker reference-count qualification.

## Measurement boundary

The panel uses 256/4,096 records, J=0/2, compact locators, four auxiliary Parts,
64-entry summary chunks and the existing occupied-page directory. The final
panel compares direct replacement without undo, the original remove/re-add
model with undo, direct replacement with undo, and direct replacement with one
failed attempt before preparation for every eight successful writes. All arms
skip unchanged descriptor-cache writes. Each burst has 32
successful value replacements or physical relabels and a retained reader.
Arm order rotates across trials. Physical labels preserve full logical order;
relabels here move a record one slot within its legal gap.

`journal_records` and `journal_record_bytes` count constructed inverse records,
including their headers/padding. They exclude the pointer vector, unused arena
space, saved map nodes and overflow-vector storage. `journal_heap_bytes` counts
requested upstream arena blocks, including their slack; it is neither a complete
scratch metric nor RSS. Native copying/allocation counters count successful
attempts; `discarded_copied_bytes` separately charges the failed attempts' native
copies. Timing includes failure, unwind, rollback and retry. Timings remain
burst averages, not service latency quantiles.

The first local panel exposed unnecessary snapshots of overflow coordinates
that were assigned their existing values on every relabel. The final code logs
those fields only when they change. The current structural path still scans
and rebuilds broad rosters/pages; bounded split/merge maintenance remains an
open directory question. This experiment does not hide that cost inside a
whole-index snapshot or claim to remove it.

Mapped backing, portable reopen, durable failure/recovery and suspension across
resource waits remain in the [coverage audit](PROGRESS.md). The checkpoints and
metadata allocator sweeps do not exhaust every global allocation/backend path.

## Measured result

The final [three-target panel](evidence/README.md) uses worker group
`20261020T105458Z-a6f4fb38`. Each target passes 9,423 injected boundary failures
and 2,916 actual metadata/journal allocation failures. Local ASan/UBSan passes
all nine directory drivers. The checked consecutive-value history is included;
only its explanatory comment was clarified after source capture.

At 4,096 rows, inverse-record bytes per successful value replacement fall as
follows. These are burst averages, including headers and padding:

| J | Original undo editor | Skip identical dirent saves, generic model | Direct replacement |
| --- | ---: | ---: | ---: |
| 0 | 2,900.25 B / 77.59 records | 1,940.25 B / 72.59 records | 494 B / 17 records |
| 2 | 2,766 B / 73.44 records | 1,806 B / 68.44 records | 507.5 B / 17 records |

The original column comes from the fresh-Pool isolation group
`20261020T104256Z-259f9f5b`; the other columns are matched final-source controls.
The combined reduction is about 82–83%. None of these value bursts spills the
journal arena. This does not reduce its fixed 8 KiB stack reservation, and the
record counter excludes other scratch and retained node storage.

The native work is identical across the final controls: five changed Objects
per replacement, with 5,840 copied/5,888 allocated bytes at J=0 and
6,440 copied/6,464 allocated bytes at J=2. These native bytes are separate from
the undo-record counters. Direct deltas remove author work; they do not remove
summary chunk copies or five-layer publication.

Five-trial medians at 4,096 rows, in microseconds per successful replacement:

| Target | J | Direct, no undo | Generic model + undo | Direct + undo | Direct + undo + retry |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen 5 | 0 | 21.49 | 23.79 | 23.24 | 24.65 |
| Zen 5 | 2 | 13.39 | 14.45 | 13.12 | 16.21 |
| Granite Rapids | 0 | 32.51 | 35.20 | 34.43 | 38.25 |
| Granite Rapids | 2 | 19.15 | 21.17 | 19.48 | 22.39 |
| Neoverse V2 | 0 | 34.25 | 37.71 | 34.93 | 39.16 |
| Neoverse V2 | 2 | 21.72 | 23.42 | 21.81 | 25.67 |

The retry arm includes four fully constructed, rolled-back attempts per 32
successful writes. These are complete editor bursts, not isolated undo costs.
For example, Zen 5/J=0's direct and generic timing ranges overlap substantially
despite the direct model phase falling from a 7.30 to 3.64 microsecond median.
Do not interpret the slightly faster Zen 5/J=2 logged median than its
unprotected control as a logging speedup. Raw trials and phase counters remain
with each selection. All 256-row cases and structural controls are retained.

Structural relabeling is still poor: direct+undo medians at 4,096 rows are
910/76 microseconds for J=0/2 on Zen 5, 1,664/114 on Granite Rapids and
1,508/127 on Neoverse V2. J=0 still logs about 15.4 KiB and spills while broad
roster/page rebuilding persists. The direct value recipe cannot fix that path;
bounded structural maintenance remains active work.

The first group, `20261020T103456Z-e4f62465`, shared one Runtime across fixtures.
It exposed bimodal helper costs tied to cohort history, including an apparent
undo win despite equal native bytes and Objects. The isolation group introduced
a fresh Worker/Pool/Arena per fixture outside timing; that is a control for
cross-fixture Pool history, not proof of cache equivalence or the mechanism
behind every timing regime. Mapping, first touch, search state and ordinary
heap/cache placement have not been separated. Retain aged/churned allocation
histories in the eventual Engine comparison as well as these fresh-state controls.
