# Boundlist checks

`ikea_boundlist_check` tests production ordered sources and payload-free metadata.
The search oracle walks ranks independently, using widened signed arithmetic for
mixed integer queries. It covers every native count from 0 through 513,
unaligned and exact protected-page tails, u8/u16/u32/u64 lanes, queries outside
stored domains, sparse/inverted population, repeated endpoints, empty ranges,
all four endpoint-closure policies and local holes. Custom sources exercise
strings, wider integers and a virtual top fence without a universal integer cap.
Width tests cover both lane widths, strided siblings, nonzero/high origins,
zero/max deltas, partial groups and checkpoint count arithmetic at UINT_MAX.
Prepared finishing and actual interleaved eight-query searches must agree with
the independent expected results.
Reusable numeric search bindings add counts4096/4103/16384, affine and skewed
geometry, constant arrays, duplicate runs and hidden jumps between preparation
samples. Lower/upper queries include mixed signed domains, exact guarded tails,
unaligned arrays, prepared finishing and interleaved mixed-source batches.

Metadata histories cover Direct/Width, grouped/separated lengths, omitted U/D,
fixed/zero values, G16 initialization, source-qualified effects and checked
refusal. Independent reservations/U/dead state define physical reverse, slack,
range overlap, packing and selected transfer. A literal version-2 schema and
manually authored Direct endpoints constrain the unchanged format independently.
StreamPack's [boundary suite](../streampack/README.md) adds actual 65535-byte
reservations, full capacity, wire fixtures and guarded metadata padding.

Build `ikea_boundlist_check`, `ikea_streampack_check`,
`ikea_streampack_boundaries_check` and the two `ikea_example_boundlist_*` targets;
run `ctest --test-dir BUILD -R '^ikea_(boundlist|streampack|example_boundlist)'`.
The targets disable both compiler vectorizers. `ikea_validate` includes them.
`python3 ikea/test/headers.py BUILD --module boundlist` checks ordinary/author
headers independently under the chosen execution profile.

Native consumer codegen/performance, sanitizer checks and real Loom/Orbital
retained-reader histories remain in the
[implementation evidence](../../../workbench/spikes/boundlist/GRADUATION.md).
