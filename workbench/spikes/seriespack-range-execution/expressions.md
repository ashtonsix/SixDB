# Expression-consuming runtime materialization

This note retains the first per-row and scalar-run traversals. Neither was
ready for promotion. Grouping scalar edges recovers
60 separated improvements against the first materializer, but still leaves
36 separated losses against ordinary decoding. The first per-row traversal
had only 6 wins and 62 separated losses. Their semantics and compilation
boundary work; the ordinary production range driver has adopted neither.

Later investigations separate [native edges](edges.md) and
[short output stores](stores.md). Those notes own their
successor measurements and remaining losses; the numbers below describe the
retained earlier programs.

The ignored region prototype consumes the same authored expression for native
regions and scalar edges, with working lanes chosen from that expression's bit
domain and output widening confined to the sink. Its actual selected callbacks
compile without inner calls or expression/native-value stack staging. Those
semantic and compilation results remain separate from the failed traversal's
timing result.

The [retained evidence](evidence/materialize-region-20260910/provenance.json)
includes exact source/flags, objects, guard binaries and the original outlined
callback in a verified recovery bundle. It supplements the already integrated
[materialize authoring core](evidence/materialize-core-20260910/integration.json)
and the [runtime caller measurements](runtime.md).

## Concrete scope and checks

The source slice is Local K7/H0 and K23/H16, Striped K12/H0 and K28/H16, plus
projected heads. A runtime traversal handles requested original coordinates,
using admitted contiguous 8/16-row native regions and scalar edges. Each body,
tail and head resolves its actual source and independent stride. The native
executor calls `composition::materialize`; scalar edges call `composition::read`.
The W12 scalar residual projection reads only its own byte, with no hidden body
read. That experimental spelling should share the owning physical helper before
promotion.

The private sink accepts only u32/u64 outputs, contiguous original coordinates
and an explicit all-selected admission. Native values remain NEON/YMM/ZMM
values with only the low Count lanes promised. Working width, Count and output
width are separate compile-time facts. Register extracts and unsigned widening
produce exact stores, including 32 bytes for an eight-row u32 output even from
a ZMM. No scratch array bridges native working values to output.

Native NEON and AVX2 under QEMU each pass 581,604 guarded queries, including
146,928 calls through the actual separately compiled callback pointers. The
770 guarded source placements cover independent child values/strides, dense
and gapped planes, final partial logical tiles, empty requests, every origin
and lengths around 8/16/32-row boundaries. Every destination is exact and
protected at either end; writable bytes outside it remain canaries.

Projected-head checks make unused payload and opposite-head planes inaccessible.
A substituted W12 tail has its body in a protected page and only its residual
stripe readable; scalar, native and mixed queries still succeed with the body
supplied by another child. These are component admissions, not permission to
read the deliberately inaccessible parent. The standalone typed sink also
passes 12,928 cases per executed target, poisoning unclaimed high native lanes.
AVX512 guard binaries compiled in the local records. The subsequent first
timing job executed both native AVX2/AVX512 sink and expression guards
successfully under its matched full-feature flags.

## Deliberate compilation boundary

Initially, Clang outlined six expression drivers behind the selected NEON
callbacks. K7, K23 and K28 callbacks each allocated a 48-byte frame and stored
four source pointers as a 32-byte expression aggregate before calling a driver.
K12 already inlined. A single `always_inline` annotation on the authored driver
places that traversal inside the deliberately separate physical callback. With
identical flags, all four NEON callbacks then have no calls or stack use.
The [before/after record](evidence/materialize-region-20260910/inline-boundary.json)
preserves both source/object identities and the actual aggregate handoff.

All 12 current target callbacks contain no inner calls or native-value/aggregate
stack staging. x86 still saves some general registers; that is actual callback
overhead. The [code audit](evidence/materialize-region-20260910/callback-codegen.json)
records per-format sizes and saves. This establishes that the authored boundary
can compile away inside a chosen physical region. It neither measures the cost
of the old handoff nor justifies recursively forcing arbitrary composed stages
into their callers.

The four formats test the boundary and do not define the eventual physical
scope. The current sink lacks u8/u16 outputs and arbitrary selections/maps.
Projected heads conservatively inherit payload-derived traversal boundaries;
a later head-only executor may choose its own admitted plane grain. The next
timed comparison must preserve the actual runtime records, typed output and
scalar consumer, including the expensive origin16→17 and count16→17 cases.

## First timed traversal

Zen job `20260910T211507Z-8c722f1f`, capture
`e540ffe88abdbc550e081e38d731089f0b23c0cfc42cdae1a33fff9aa3c7367d`,
compares ordinary and authored providers in one binary using the same runtime
caller loop and scalarized callback ABI. Provider process order is ordinary,
authored, authored, ordinary, three sequential 30 ms repetitions each, pinned
CPU0. Both AVX2 and AVX512 families use the same full-feature Zen profile.
The previously measured ordinary program is retained as a separate linked
context control. It is not the primary denominator for the authored result.

The [audit](evidence/materialize-region-20260910/20260910T211507Z-8c722f1f/audit.json)
verifies all 516 captured sources and archive identities, the byte-identical
baseline relink, unchanged cached sources, actual candidate objects/callers,
native guards and 2,292 individual samples. An independent Python query/wire
model reconstructs every runtime counter and complete timed checksum. The
[compact records](evidence/materialize-region-20260910/20260910T211507Z-8c722f1f/provenance.json)
retain all provider blocks and previous-program controls; all 68 primary
directions agree at both ordering edges.

| Case | Ordinary | Authored |
| --- | ---: | ---: |
| AVX2 Striped12/u64, origin16/count16 | 3.611 | 2.825 |
| AVX2 Striped12/u32, origin16/count16 | 3.099 | 2.552 |
| AVX512 Striped28/u64, origin16/count16 | 3.965 | 3.137 |
| AVX512 Striped28/u64, origin17/count16 | 9.600 | 21.076 |
| AVX512 Striped28/u64, origin17/count17 | 10.834 | 22.539 |

Numbers are median CPU ns/query. The six wins all belong to aligned striped
regions, improving 13.1–21.8%. Every Local case and every shifted/mixed striped
case loses; two aligned AVX512/u32 cases also lose. The complete
[primary table](evidence/materialize-region-20260910/20260910T211507Z-8c722f1f/primary.csv)
keeps all extrema and ordering edges. The separate previous/new-program
comparison has 10 separated losses among 157 unchanged-control/ordinary cases;
those are retained in the adjacent context table and are not assigned to
authored decode execution.

The driver checks native-region conditions and computes physical tile addresses
again at each scalar row. A follow-up discriminator groups scalar edges into
runs with a fixed tile ordinal, still resolving every leaf from its actual
source and preserving original coordinates. It retains this exact measured
binary as the first-candidate control. That is an implementation hypothesis;
these timings do not isolate a per-branch cost or reject narrower working lanes.

## Scalar runs and remaining gaps

Zen job `20260910T212852Z-b2322870`, capture
`f63658eebc612d1f853828389738e5126202999719d4a4bd25aec0c94b6be8e6`,
replaces only the two candidate translation units in the first measured
materializer program. Its exact runtime caller and selector objects, libraries
and other benchmark objects are reused. Both programs contain ordinary and
authored providers. Binary order is before/after/after/before; provider order
is ordinary/authored in the first two blocks and authored/ordinary in the last
two. Each process runs three sequential 30 ms repetitions on CPU0.

The revision retains one coordinate tile ordinal throughout each scalar run,
clipped at the next Local8/Striped32 boundary or request end. Every expression
leaf still resolves its actual source and independent stride. Native regions,
working widths and the sink are unchanged. A new pragma also disables
vectorization, interleaving and unrolling for these runs. That is an additional
experimental variable, not a general authoring rule; the combined recovery
cannot be assigned solely to grouping or addressing.

The [audit](evidence/materialize-region-20260910/20260910T212852Z-b2322870/audit.json)
checks all 515 captured files, the byte-identical frozen-program relink,
unchanged private source cache, 37 consumed link inputs as recorded by the
worker, all native guards, and 2,700 individual samples with independently
reconstructed runtime counters/checksums. Actual matched-profile callbacks
still contain no inner calls or vector stack staging. Their static instruction
counts increase across all eight callbacks; this is not executed work per query.

Against the [frozen first candidate](evidence/materialize-region-20260910/20260910T212852Z-b2322870/frozen-recovery.csv),
61 of 68 medians improve, 60 with separated samples; four separated losses and
four overlaps remain. All four separated losses are aligned striped cases
that execute no scalar edge, ranging from 9.0% to 22.5% slower. Changed callback
layout or scheduling can affect that path even when its source-level native
operation is unchanged. The data do not isolate the cause.

Against ordinary decoding in the revised program, 22 of 68 medians improve:
nine separated wins, 36 separated losses and 23 overlapping comparisons.
Direction agrees at both provider-order edges for 61 cases. All shifted/mixed
striped cases still lose. Local23/u32 supplies six separated wins; the other
three are aligned Striped12 outputs.

| Case | Ordinary | Scalar-run authored |
| --- | ---: | ---: |
| AVX2 Local23/u32, head-gapped mixed | 9.740 | 9.004 |
| AVX2 Striped12/u64, origin16/count16 | 3.472 | 2.884 |
| AVX512 Striped12/u32, origin16/count16 | 3.046 | 5.379 |
| AVX512 Striped28/u32, origin16/count16 | 3.924 | 6.108 |
| AVX512 Striped28/u64, origin17/count16 | 9.777 | 15.360 |
| AVX512 Striped28/u64, origin17/count17 | 10.611 | 16.569 |

Numbers are median CPU ns/query. The [complete primary table](evidence/materialize-region-20260910/20260910T212852Z-b2322870/primary.csv)
retains every extremum and ordering edge. Eight separated losses also appear
among 157 unchanged ordinary/control cases across programs, including Local6
AVX512 bulk decode +9.2% and Local1 AVX512 raw fixed16 +36.6%; they remain in
the adjacent context table and are not attributed to authored decode execution.

Two implementation questions remain distinct. Scalar-loop compiler treatment
and admitted native prefix/suffix materialization address boundary work. The
aligned AVX512/u32 losses have no scalar edge and warrant a separate short-store
comparison retaining the actual source, whole callback and scalar consumer.
Neither question establishes a store-forwarding latency explanation. Complete
readable physical tiles may support native partial regions, but each actual
child must supply its own admission and only active logical output may be
stored; a masked store must not be given an out-of-bounds base pointer.

## Isolating ordinary compiler loop treatment

Job `20260910T214130Z-b912001c`, capture
`d4c77530ee4cdb225738e34990a292cd26b0929487f7711b08906221e30ec72f`,
removes only the scalar-run pragma from the preceding source. The frozen
scalar-run program is relinked byte-identically; the same runtime/selector
objects, source cache, native guards and ordered comparison protocol are
retained. Its [audit and complete samples](evidence/materialize-region-20260910/20260910T214130Z-b912001c/provenance.json)
cover 2,700 observations. This version is also rejected as a general replacement.

Compared with forced scalar runs, only 11 of 68 medians improve: nine separated
wins, 45 separated losses and 14 overlapping comparisons. Compared with ordinary
decoding in the new program, 14 medians improve, with 12 separated wins,
50 separated losses and six overlaps. The [paired revision table](evidence/materialize-region-20260910/20260910T214130Z-b912001c/frozen-recovery.csv)
uses the forced-loop program as its denominator, unlike the earlier recovery
table whose denominator is the original per-row traversal.

Local7/u32 is a useful exception to retain. Its AVX2 origin1/count17 improves
from 10.853 to 9.172 ns/query; ordinary in the new program takes 9.941. AVX512
improves from 10.426 to 9.166 for the same shape. These gains do not fix shifted
striped regions or aligned AVX512/u32: Striped12/u32 origin16/count16 still takes
5.641 versus ordinary 2.830 ns/query.

Static instruction counts more than double in all eight callbacks; for example,
AVX512 Striped12 increases from 229 to 552. No inner calls or vector stack staging
appear. The default compiler generates additional vector-loop and alias-control
paths. In the actual traversal, scalar runs are at most seven rows: a longer
run would have been handled by a preceding native8/16 branch. That source-level
bound is not represented directly in the scalar loop. The emitted Striped12/u32
code nevertheless contains branches for eight- and 64-row scalar-loop lengths.
This explains excess generated paths, not their measured latency contribution.

The linked ordinary/control comparison has 19 separated losses and 22 separated
wins among 157 cases. Those movements remain visible in the context table;
neither an aggregate revision win nor a specific microarchitectural explanation
is inferred from them. Native edge work must retain the Local7/u32 exception
as a comparison, while short output stores remain a separate discriminator.

The subsequent [short-store comparison](stores.md) recovers
the aligned AVX512/u32 cases while retaining the scalar-run traversal. Native
partial edges are a [separate measured experiment](edges.md)
with the original full native sink; they recover 65 separated improvements but
leave 21 separated losses against ordinary decoding.
