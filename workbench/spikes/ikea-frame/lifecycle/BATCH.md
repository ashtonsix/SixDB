# Charge batch preparation before choosing a writer

Batch preparation is a useful override-writing alternative, but not a universal
replacement. It helps dispersed assignments and loses on repeated-hot ones.
Indexing sparse repairs by block does not materially close the native-write gap.
The next investigation should distinguish repair-plan construction from copying
and owner work, rather than continue tuning the batch index.

The [sparse-write study](REPAIR.md) removed whole-block proof on fitting edits,
but retained repeated scans of each batch and per-assignment sorted-vector
insertion for overrides. The earlier [aggregate-maintenance results](../../aggregate-maintenance/dirty-buffer/FINDINGS.md)
already warn that sorting, buffering and reset must earn their cost, especially
on dispersed writes. The charged native-coalescing control was slower in the
previous Frame cohort. This probe therefore retains all those programs and
measures the proposed preparation inside each complete phase.

## Same semantics and work

A private `IndexedBatch` selects original-coordinate commands, sorts by row and
original ordinal, and indexes block/group ranges. It preserves every assignment
to a row in its original order. Sparse repair consumes the indexed block range;
keeping intermediate assignments preserves fallback folding's hidden NULL fill.

The batch override writer merges sorted row runs with the existing serialized
override columns. Each run contributes its last assignment. Existing corrections
retain their immutable original contribution; a new row acquires it once from
the base. Returning to that base removes a correction. The writer allocates a
new output vector and Part; sorting, allocations, merging and copying all count.
It does not copy or rewrite the immutable base/flags.

Across all 96 cells per policy, old/indexed sparse writers have identical copied
bytes, encoded/materialised/examined rows, repaired blocks and rejection reasons.
Old/batch override and old/batch fold pairs also match those work counters.
These are logical counts, not issued traffic. No fill-policy change explains
the comparison.

## Complete eight-publication phases

Mixed NULL/presence trace, all blocks selected, no old reader, one demanded query
per publication. Median microseconds from the bounded cohort:

| Input / edits | Native FOR | Old overrides | Batch overrides | Sparse repair | Indexed sparse |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pressure / repeated-hot | 64.1 | 68.5 | 77.4 | 83.5 | 90.2 |
| Pressure / dispersed | 85.3 | 165.9 | 151.6 | 180.6 | 179.4 |
| EventTime midpoint / repeated-hot | 53.5 | 66.7 | 68.5 | 72.9 | 73.0 |
| EventTime midpoint / dispersed | 83.6 | 154.4 | 139.0 | 108.8 | 106.8 |
| UserID prefix / repeated-hot | 67.2 | 38.7 | 41.6 | 69.9 | 70.6 |
| UserID prefix / dispersed | 106.1 | 135.8 | 117.0 | 772.9 | 763.2 |
| URLHash prefix / repeated-hot | 67.9 | 69.6 | 73.6 | 113.3 | 115.0 |
| URLHash prefix / dispersed | 103.4 | 167.8 | 148.8 | 1,397.6 | 1,386.7 |

Batch merging saves about 9–14% of dispersed override phase time here. Repeated
hot rows are already cheap in the small existing override vector, and preparation
adds cost. Sparse block indexing offers little benefit despite avoiding repeated
batch scans. It leaves the full group-copy envelope and repair-plan machinery
unchanged; these timings do not establish which remaining component dominates.

Run-heavy UserID still strongly rewards retaining the immutable base's programs.
Its dispersed mixed read-rich phase takes 1.48 ms with batch overrides, versus
3.70 ms with indexed sparse/fallback folding and 6.70 ms with native FOR. The
all-valid form is 1.48/2.09/3.50 ms respectively. Batch merging itself does not
materially change those reads: old overrides take 1.51/1.49 ms. Overrides still
owe their later folding; they are not a settled cheaper write policy.

The separate full-surface chronology retains novelty, moving hotspots, all four
coarse masks and old readers. In pressure's all-selected novelty case, batch
overrides take 140.8 us versus native's 190.1 us, but charging the final fold
takes 386.8 us. That is a useful reason to study sustained lifetimes, not a
claim of a debt-free writer win. Never pool full-surface and bounded timings.

## Ownership and qualification

The focused checks cover 4,830,001 batch observations against sequential
assignment oracles; 328,160 indexed-repair checks across four checkpoint modes
and raw/sorted/frequency dictionaries; and 984,480 checks through actual owners.
The last includes a folded alternative followed by a genuine next publication,
with initial and folded readers still live. Immutable original contributions,
old/new queries, base-Part reuse, masks, no-op removal and protected-none command
memory are checked. Final owner metadata and Arena charges reach zero.

The complete run has 32,768 timed histories and 127,926,272 original-coordinate
point checks, plus exact demanded aggregates and admission at every observed
publication. Sixteen arms retain native/raw, whole/block/dense/sparse writers,
native coalescing, debt/fold/thaw, the three new programs and exact duplicates of
native, old fold and batch overrides. Bounded cells have 32 samples; full-surface
cells have 16. Position and predecessor balancing is unchanged.

Across nonempty phases of at least 5 us, duplicate ratios span .928–1.096 with
median 1.000. In the displayed eight cells they span .979–1.038. Treat small
differences as unresolved; neither the sparse index nor a universal batch default
is justified. Empty/tiny timings remain in evidence but are not ranked.

Exact frozen source:
`f375808ae0780897cd8649138b317b6dbb18bfcd7f24bb9234e58fb976b3ce0b`.
It uses encoding 2c3bdf87/Ikea 2ce62539 and the new private lifecycle code, without
Claude's later guard/performance/NULL-fill changes. Local pinned Clang 21.1.8,
O3/native/generic AArch64 VM only. No fleet, cold/Loom or production-tail claim.
The [earlier accounting limits](REPAIR.md#qualification-scope-and-recovery) still
apply, including primary retention after same-version adoption and excluded
codec/query scratch from Pool capacity.

[All cells, calibration, inputs and checks](evidence/batch-local/) retain the
negative cases. Recovery includes full frozen source, exact binaries, raw
repetitions, runner and the four small hash-verified input slices. No public
Frame API/default/graduation is selected.
