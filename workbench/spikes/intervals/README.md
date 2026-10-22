# Intervals

Research and qualification for the maintained `ikea::intervals` module. Common
borrowed search/geometry now lives in Ikea; StreamPack owns compact image and
retirement responsibilities. The [graduation account](GRADUATION.md) records the
real ownership migration and consumer checks. The earlier frozen evidence remains
valid for its original source; it is not rewritten as a current measurement.

## Starting evidence

The [Boundlist design](../boundlist/DESIGN.md) and
[portfolio](../boundlist/PORTFOLIO.md) already established three useful codecs,
carried traversal, explicit selection, native kernels and source-qualified damage.
They also found regressions through flattened adapters, losing inverse/edit cells,
and Relative Ends carrier-width cliffs. Reuse those bodies and strong maintained
typed controls; do not infer a universal codec from metadata bytes.

The [recommendation](../boundlist/RECOMMENDATION.md#storage-and-composition) requires
G16 for StreamPack's old format, not generic sources. The old prototype already
parameterised spacing. Frame's transferred checkpoint experiment keeps native G16
execution inside G16/32/64/128/256 storage regions. Its last owner/start edits were
unqualified at transfer; this spike freshly checks them before timing. The
[Frame point study](../ikea-frame/point/README.md) owns prediction reconstruction,
packed residuals and exception algebra. Those are not nonnegative interval sizes.

## Maintained contract and module split

The maintained target and headers are documented in
[Ikea usage](../../../ikea/docs/intervals/usage.md). The spike no longer exports a
shadow include directory or a facade depending on Boundlist. Transitional old
Boundlist includes point to the new owners for historical controls.

The production responsibilities are:

| Layer | Owns | Consumer obligations |
| --- | --- | --- |
| Ordered source/search | Count and endpoint access contract; lower/upper search and unresolved read phases | Admit ordering and coherent backing; borrow native or decoded endpoints |
| Interval geometry | Origin, half-open bounds, locate/reverse/overlap/carried range | Preserve rank identity and coordinate domain |
| Representations | Absolute Ends, Sizes, Relative Ends, optional sparse checkpoints, fixed geometry | Choose bounds/carriers/spacing per image; retain metadata with old readers |
| External operation filters | Borrowed rank selection, including inverted tombstone bits | Owner allocates/updates/retains bitmap; selection does not reassign coordinates |
| StreamPack | Used lengths, payloads, retirement, compaction, publication coordination | Preserve existing value semantics and versioned byte laws |
| Frame | Encoded body, presence/NULL state, block masks, signed predictor and reconstruction | Skip unselected descriptors/body before entering geometry; admit arithmetic law |
| Engine | Directory routing/layout, choice of curated representation | No mandatory compact header for decoder projections |

Selection can be passed for one operation or attached for a batch. Excluded point
owners return absent, never a later owner. Zero-size intervals retain ranks and
can be visited; they own no points. Presence, NULL validity, tombstones and operation
selection remain independent. Filters cover exactly ceil(count/8) readable bytes.

Ordered search sources need only `size()` as unsigned and ordered `at(rank)`.
Interval geometry separately adds `origin()` and its coordinate law. Optional
`start`, `slot_size`, `extent`, carried traversal and admitted owner hooks remove
redundant work. Generic count/domain are not restricted to 256. Sparse local fields
are u8/u16 sizes or native relative ends; checkpoint carriers and working
coordinates are separate. At least u32 arithmetic avoids a narrowing carry on
every visited rank, while u64 origins remain u64. Native tail padding and phase
read windows are explicit read obligations, not writable slack.

Frame RLE's ≤256-position u8 end-minus-one projection is an important immutable
control: endpoint 256 is projected into a wider domain, and no checkpoints,
tombstones or compact control are needed. Frame predicted residuals at up to 64 bits
need a separate prefix/reconstruction capability and overflow law; unsigned u8/u16
interval sizes are insufficient to cover that whole operation.

## Experiment and reproduction

Use the pinned Linux compiler with `orb -m ubuntu` on macOS:

```sh
cmake -S . -B build/clang/intervals -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=intervals
cmake --build build/clang/intervals --target ikea_intervals_sparse_check intervals_consumer --parallel 2
build/clang/intervals/ikea/ikea_intervals_sparse_check
build/clang/intervals/workbench/spikes/intervals/intervals_consumer --maximum 255 --count 256 --trials 11 --repeat 32
```

`ikea/test/intervals/sparse.cpp` uses independent scalar endpoints/intersections across near-top origins,
zero groups, signed/wider queries, partial native tails and ordered replacements.
`consumer.cpp` consumes payload/children after point, inverse, short/full-range work,
with maintained typed compact and direct controls. It rotates arm order and retains
all samples. Bytes distinguish assigned data with native field padding, controls,
capacity and native binding/owner storage. At partial counts, the checkpoint
capacity read grant also needs readable backing; see the accounting note in
[the native study](RAISE-BAR.md#final-native-audit-and-qualification-limits). Full ranges are ns per complete traversal, not ns per rank.
Local ARM VM warm measurements are exploratory; they establish neither DRAM
residence nor a Zen 5 default. Cold checkpoint→local→payload service and fleet
qualification remain separate from this first experiment.

## What the seam must preserve

Use `with_geometry(source, body)` once around useful ready owner work. The body
can fuse the owner's U/liveness/payload stages; it is not required to reconstruct
an intermediate interval or lose typed access. `start` is the checked start-only
projection; `start_unchecked` is available after owner admission. A generic
start-plus-length reconstruction is a diagnostic control, not a replacement for
StreamPack's faster fused value body. Directory uses endpoint search independently
of interval arithmetic and keeps its exact `memcmp` domain and equality direction.

A sparse source's optional `CheckpointCapacity` is a **readable-tail proof**, not
an assigned count or a generic 256-rank limit. It enables a native directory mask
while excluding all inactive lanes. Without that grant, search uses exact readable
counts. The field `stride` separates native geometry groups from interleaved
StreamPack companions. Prepared phases retain checkpoint routing and expose the
whole possible strided local window. A sparse G64 Sizes phase can need four
native windows; it cannot advertise a G16 footprint. The original source/filter
and their coherent retained image outlive completion.

`size_editor` is a checked borrowed Sizes experiment: append and resize issue
source-qualified damage for actual stores before writing. Owners admit disjoint
placement, contents and exclusivity; effects capacity/value/rank/extent checks
happen before stores. There is no allocator, portable descriptor or publication
protocol. Changing one boundary while preserving later endpoints remains a
distinct maintained operation, not an alias for resize. Only these Sizes edits
have a new candidate body; the retained production codecs remain the controls.

## Handoff provenance and remaining production work

The geometry-only `ikea-frame/checkpoints` prototype was transferred here and
removed after build isolation. Frame retains its separate `point/` work. Exact
historical source remains in ignored immutable captures:

- `build/captures/frame-checkpoints-20261022`, source digest
  `9ce4a8a80b785633eb4738122b5d30c980950cf198c93d86f2f41b3c89f7e6a8`.
- `build/captures/frame-checkpoints-owner-20261022`, source digest
  `2583bde5d8f04c45683f8d65dbda5847e72b9f557847e950836e61723c3e688d`.
  This second capture includes the owner/start repair freshly checked here.

The production move is recorded in [graduation](GRADUATION.md). Common bodies
are physically owned by Intervals; StreamPack owns compact storage and retirement.
No old persisted bytes are retagged. Historical comparisons retain their frozen
sources and byte/admission context.

For full local reproduction, configure as above, then run:

```sh
python3 workbench/spikes/intervals/run.py --cpu 8 --output build/intervals/new-run
```

The runner builds/runs validation, records binary/source hashes, rotates primitive
arms, reverses geometry case/process order and before/after Directory process order,
and checks that measured sources did not change. `report.py` summarizes every arm
with sample count and min/median/max, plus bytes. It reuses the existing complete
Directory driver with real Objects; those rows contain seven-repetition medians
and minima, not raw inner samples. Its fixed metric order and local VM noise remain
limits. [Findings](FINDINGS.md) records selected options, rejected replacements and
remaining qualification.

## Performance follow-through

[Raising the bar](RAISE-BAR.md) follows LEAD's plan through complete linked caller
inspection, dependency/resource analysis, rejected algorithms and native Zen5/V2
comparisons. `latency.cpp` separates true dependent C1 from independent C4/C8
requests and clustered/random patterns; `consumer.cpp` keeps the precomputed
useful-work witness. Their results are different contexts and are not pooled.
`control/sparse_before.h` is the literal 66743fdf source with only its namespace
changed; `python3 workbench/spikes/intervals/control/audit.py --git` verifies it.

`source.for_independent_reads()` is a compile-time G64 prefix execution option
over the same bytes. It may read all four admitted native windows and benefits
some throughput workloads; it is not the C1/clustered default. Inverse keeps the
serial route. Sparse endpoint reads now avoid a next-window load at exact16/32/48
tails without adding that check to admitted start reads. All 1920 fixture cases,
strided companions, prepared operations, external filters and protected tails
remain part of validation.

Native reproduction uses `raise-bar-native.sh` with the shared worker helper;
`raise-bar-report.py RESULTS` preserves all arms and losses in separate chain,
geometry, seam and edit panels. It records explicit ISA/tuning, source/binary
identities, reversed case/process order and rotated arms. The retained evidence
and recovery references are linked from the findings above.
