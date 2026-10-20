# Boundlist checks

`ikea_boundlist_check` tests production ordered sources and payload-free metadata.
The search oracle walks ranks independently, using widened signed arithmetic for
mixed integer queries. It covers every native count from 0 through 513,
unaligned and exact protected-page tails, u8/u16/u32/u64 lanes, queries outside
stored domains, sparse/inverted population, repeated endpoints, empty ranges,
all four endpoint-closure policies and local holes. Custom sources exercise
strings, wider integers and a virtual top fence without a universal integer cap.
Delta tests cover both lane widths, strided siblings, nonzero/high origins,
zero/max deltas, partial groups and checkpoint count arithmetic at UINT_MAX.
Prepared finishing and actual interleaved eight-query searches must agree with
the independent expected results.
Reusable numeric search bindings add counts 4096/4103/16384, affine and skewed
geometry, constant arrays, duplicate runs and hidden jumps between preparation
samples. Lower/upper queries include mixed signed domains, exact guarded tails,
unaligned arrays, prepared finishing and interleaved mixed-source batches.

Geometry histories cover Absolute Ends, Sizes and Relative Ends, optional
retirement, fixed/zero size domains, G16 initialization and source-qualified
effects. Independent sizes/ends define physical reverse, overlap and a shared
original-coordinate rewrite. Literal legacy descriptors constrain compatibility.
Used lengths, payload validation, packing and transfer are tested in StreamPack,
where those behaviors live.

`ikea_boundlist_edits_check` runs 41,472 independent one-boundary/suffix-resize
cases, including G16 crossings, maximal carriers, effect-capacity refusal and
actual issued store coverage. Population-aware prepared searches finish after
the temporary typed dispatch object has died, while retaining the original named
source. Generic relative sources cover wide/nonzero origins and duplicate ends.

Build `ikea_boundlist_check`, `ikea_boundlist_edits_check`,
`ikea_streampack_check`, `ikea_streampack_boundaries_check`,
`ikea_streampack_encodings_check` and the two `ikea_example_boundlist_*` targets.
Run `ctest --test-dir BUILD -R '^ikea_(boundlist|streampack|example_boundlist)'`.
The targets disable both compiler vectorizers. `ikea_validate` includes them.
`python3 ikea/test/headers.py BUILD --module boundlist` checks ordinary/author
headers independently under the chosen execution profile.

Native consumer codegen/performance, sanitizer checks and real Loom/Orbital
retained-reader histories remain in the
[portfolio evidence](../../../workbench/spikes/boundlist/PORTFOLIO.md).
