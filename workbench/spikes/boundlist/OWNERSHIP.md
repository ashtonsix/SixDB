# Geometry and value ownership

Boundlist owns ordered boundaries and reservation geometry. StreamPack owns
byte-value semantics: optional used lengths (U), value packets, value admission,
packing and selected transfer. A geometry primitive does not decide how much
of a reservation is meaningful or how its consumer should compact it.

The original extraction moved the whole metadata recipe into Boundlist, including
U-bearing locations/packets, U validation, replacement and compaction policy.
The first recommendation to remove U understated that coupling. This change
reassesses complete operations rather than treating the schema edit as proof
that the boundary is cheap.

## Implemented boundary

| Boundlist | StreamPack / owning consumer |
| --- | --- |
| Ordered sources, boundary/range search, population selection | Interpreting a reservation as a byte value |
| Offset/R locations, geometry packets, physical inverse | U, value locations/native packets and value read windows |
| Geometry encoding/admission, count/extent, retirement | U <= R, U-tail and payload admission, full-value acceptance |
| Initialize/append and small geometry rewrite/control kernels | Value append/replace/extend and coordinated damage |
| No compaction policy | Soft packing and selected transfer, including complete preflight |
| No rank-holder policy | Sorting/deduplication, remaps, cross-block decisions and publication |

StreamPack keeps a curated optional U companion. Its absence means **U = R**;
it does not accept a short value without recording its length. Full, immortal,
fixed and zero schema omissions remain independent useful facts. An external
qualified count can clip a consumer of a full reservation without changing
StreamPack's own value length.

Module ownership does not create a physical boundary. Grouped R/U still share
one lengths window. Boundlist borrows strided R directly from the named
StreamPack source and shared format. It neither initializes sibling U nor
publishes effects under a temporary projected-source identity.

The shared typed `slots(first, count, original_start, sink)` walker passes each
rank's original offset and R, including retired and zero reservations. It selects
fixed/Direct/Width traversal outside the loop. A consumer can rewrite the slot
already read while unread geometry and count remain stable. This matters for
Direct: subtracting an already rewritten preceding endpoint would corrupt the
next original R. StreamPack composes movement, U/R shrink decisions, retired-U
clearing and geometry stores inside that single walk. Boundlist's live `range`
adds population selection over the same traversal.

Native `decode16` similarly provides offsets/R through an inline callback;
StreamPack supplies U in the same native consumer, without an extra packet
handoff or decoded array. Its Direct value-only path keeps the start+U projection
and does not load the current endpoint just to derive an unused R. Geometry and
value read windows are separate projections of the same retained source.

Validation shares one geometry walk with an inline sibling-field check.
StreamPack owns the U and payload tests. Mutation effects still cover actual
issued stores on the original source, including preserving stores and initialized
vector padding. Checked commands complete refusal checks before changing bytes
or effect output. Retained readers keep geometry, U, payload and population as
one coherent version.

## Representation and interface

StreamPack's public value operations, eight-byte **version-2** schema and physical
metadata/payload recipes are unchanged. Its prepared layout stays 44 B, view
72 B and typed access 16 B on these 64-bit targets. Geometry preparation is
shared without duplicating a schema object.

Standalone `boundlist::sidecar` has a geometry-only **version-3** description:
family, maximum, extent limit, optional fixed reservation and immortal. It has
no U placement/full flag, U-bearing location, U-only replacement, `soft_pack`
or selected `transfer`. Append takes just R. The total bound is named
`extent_limit`, since units need not be bytes. The prepared geometry layout is
28 B (previously 44 B); its view remains 56 B and typed access 16 B. These are
native object sizes, not persistent metadata savings from removing already
omittable U. A v2 value schema is deliberately rejected as standalone geometry.

Engine's generic ordered routing sources need no migration. Its StreamPack
consumers retain full-schema acceptance, one schema dispatch around the complete
operation, coherent length/geometry/payload and distinguishable U-only effects.
Engine owns the index-directory retry; this change does not alter its generic
search APIs or require new routing wrappers.

## Qualification

The frozen control is the actual public headers/TUs from `35f056331787ef98a02b618c7c1034c78ac8af9a`,
recovered by `u-control.py`, not an emulation compiled against the new layout.
The same complete drivers link separately against that control and current Ikea.
Five alternating before/after trials cover 608 ownership cells per profile:
point C1/C8, ordered/shuffled projections and carried ranges; native direct/CPS
16-value consumers; append/replace; and soft packing. Extend and selected transfer
are covered by behavior/retained-reader checks, not separate before/after timers.
Checksums and all reported stored/binding/scratch footprints must agree.

The native profiles are Neoverse V2 NEON, Zen 5 AVX2/AVX-512 and Granite Rapids
AVX2/AVX-512, with pinned Clang 21.1.8, explicit SIMD and auto-vectorization off.
Read timings are ns/value; native packets are ns/16-value group; mutation is
ns/operation; packing is ns/256-rank block. C1 is dependent latency, C8 is
reciprocal throughput. Projection/range/block costs are not relabeled latency.
Input restoration and correctness checks stay outside the packing timer.

Independent checks cover frozen schema bytes, zero/internal empty groups,
partial G16 tails, full/fixed/immortal variants, geometry rewrites that preserve
original traversal, exact issued-effect coverage and refusal-before-write.
Actual Loom/Orbital checks run 192 retained-reader histories per profile, including
packing, selected transfer and holder repair. They use safe full-copy versions;
they do not establish selective-sharing MVCC economics. Local ASan/UBSan covers
the behavior checks, retained-reader material, codecs and complete native packet
consumers. Full logs, binaries, disassembly and captured sources remain recoverable
from the worker bundles.

The [absolute scorecard](evidence/ownership/scorecard.md) and complete
[ownership](evidence/ownership/ownership.csv) / [encoding](evidence/ownership/encoding.csv)
matrices retain the final 3,040 ownership and 16,400 codec cells, including every
timing repetition. [Source identities](evidence/ownership/sources.json) name both
cohorts and all six raw worker archives. Latest native code uses the shared
walker; the codec cohort has identical measurement kernels except a corrected
CSV label.

The boundary is affordable, but not literally free in every consumer. For
grouped Width/U with maximum 4095, hot dependent value reads are 11.48 → 11.60 ns
on NEON, 9.88 → 9.91 ns on Zen 5 AVX2 and 10.73 → 10.75 ns on Granite AVX2.
The corresponding independent-eight costs are 2.69 → 2.68, 1.56 → 1.60 and
2.15 → 2.38 ns/value. Shrinking packing is 2371 → 2206, 1239 → 1230 and
1792 → 1829 ns/block for dependent block streams. Other cases lose: the
scorecard explicitly keeps Zen AVX2's 4.52 → 5.66 ns native Direct group,
roughly 1 ns append penalties, and Granite's 1293 → 1500 ns full-Direct block
packing. Large dependent-read trials have considerable process-run variation.
These results support the cleaner ownership boundary, not a universal speedup.

Disassembly inspection confirms that the shared traversal/store helpers remain
inside the complete packing consumer; the inspected Granite Direct loop calls
`memmove`, not a geometry wrapper or erased sink. Prepared-layout displacements,
instruction scheduling and some arithmetic differ from the frozen source, so
the split is not described as instruction-identical.

## Geometry encoding decision

Keep **Direct absolute ends** and **Width checkpoint + sizes** as the stored
recipes. Do not require redundant checkpoints for Direct, or add a third stored
recipe in this ownership change. Direct remains the cheap rank-access choice;
sizes retain the smallest small-slot representation and the cheaper width-change
path. This is not a claim that relative ends have no useful region.

The geometry experiment compares current Direct, current sizes, checkpoint +
group-relative ends, Direct with redundant checkpoints, an independent fused
native Direct scan, and a stronger full-prefix sizes inverse. Reservation-change
controls use explicit SIMD for suffix/checkpoint updates. The rebuild control
also generates fixture widths and initializes population; it is not an optimized
bulk encoder or a pure codec-store cost. Queries are
admitted in-domain points; they do not include the full public query-domain
frontend. Checked reverse includes the current extent check. None of these
new-codec numbers qualifies a complete StreamPack codec replacement.

Nine assigned counts (1, 15, 16, 17, 31, 32, 64, 128, 256), four maxima
(255, 4095, 4096, 65535) and populations of 1/64/4096/1,048,576 exercise partial
occupancy, rank access, upper bounds, overlap extrema, physical inverse,
rebuild and changed reservation widths. Each row's extent bound is assigned
count × maximum; the sweep does not independently vary tighter extent limits.
Six samples per cell use both format orders. A 4 KiB request table is held
constant; its calibration is reported and
not subtracted. Reported geometry backing is touched backing storage, **not**
measured per-arm RSS or proof of a cache level. In particular, the narrowest
large population fits within Granite's reported LLC; do not call every large
row DRAM.

At 256 assigned ranks under those extent bounds, encoded bytes including D and
control are:

| Maximum | Direct | Sizes | Relative ends | Direct + checkpoints |
| --- | ---: | ---: | ---: | ---: |
| 255 | 546 | 326 | 582 | 578 |
| 4095 | 1058 | 614 | 614 | 1122 |
| 4096 / 65535 | 1058 | 614 | 1126 | 1122 |

Group-relative ends are a serious 12-bit candidate: local ends still fit u16,
matching sizes' storage while removing point prefixes and speeding local inverse.
With these bounds, crossing from maximum 4095 to 4096 widens them to u32;
at maximum 255 they need two-byte ends against one-byte sizes. A tighter extent
limit <=65535 also permits u16 relative ends for larger reservation maxima, so
the opportunity is not intrinsically restricted to 12-bit schemas. Redundant
Direct checkpoints accelerate
inverse without improving ordinary rank access and add maintenance. Consequently
neither is an unconditional replacement or a reason to make checkpoints mandatory.
The retained scorecard keeps their wins and costs alongside both selected formats.

For example, on NEON at maximum 4095, sizes / relative ends / Direct cost
11.02 / 6.59 / 5.73 ns for hot dependent geometry points and
35.84 / 26.31 / 31.18 ns for checked reverse. Independent reverse costs
25.37 / 7.74 / 14.34 ns; changing R costs 14.21 / 18.48 / 44.12 ns.
Thus relative ends offer a genuine middle tradeoff. They remain measured research
rather than an automatically selected value format: full value/native/packing
consumers for that new format have not been qualified, and the smallest schemas
and widest unbounded-extent schemas favor sizes for storage. No generic-source
API prevents a
future consumer from testing that specialization without changing the two
curated stored recipes.

## Reproduce or recover

`u-control.py build/boundlist-u/control` recovers the pinned old implementation
from Git. Create a source capture with
`capture.py create build/captures/boundlist-u --replace workbench/spikes/boundlist/u-control=build/boundlist-u/control`,
then run `ownership-workers.json` through `worker_group.py run --source ...`.
Use `BOUNDLIST_OWNERSHIP_ONLY=1` in each member's environment to omit the larger
codec sweep. `ownership-native.sh` keeps C1/C8, native direct/CPS, mutations,
packing and geometry operations separate and checks consumed results.

The retained cohorts are `20261020T154528Z-18730a87` (codecs) and
`20261020T164848Z-27caeeca` (final ownership). Recover their receipts with
`worker_group.py recover GROUP`, then collect with `worker_group.py fetch`.
Run `ownership-report.py ENCODING_GROUP/group.json OUTPUT --ownership-group OWNERSHIP_GROUP/group.json`
to rebuild the CSVs. The report refuses missing cells, inconsistent checksums,
different before/after footprints or wrong repetition counts. The authored
scorecard selects labeled slices from those CSVs. The earlier guard-diagnosis
captures stay recoverable in S3; duplicate intermediate runners are not retained
as another active experiment. Native worker resources have been released.
