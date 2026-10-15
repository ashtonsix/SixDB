# Read windows over the capacity families

The capacity comparison improves memory at useful occupancies, but larger wide
records make whole-bucket prefetch expensive. This experiment holds the stored
format and lookup bodies fixed while comparing three execution controls:

1. Hint the whole bucket and consume a matching value in the same visit.
2. Hint the header, selectors and suffixes; consume a matching value in that visit.
3. Hint the same lookup region, then hint and yield for the located value/tag.

Sets have no value stage. Key-overflow and external multimap traversal keep
their existing directory/payload boundaries. The located rank reuses the probe's
link field in its distinct state; it adds no owner, allocation or new retention
mechanism. The entire read cohort still requires a stable immutable table,
interpretation, directories and allocations. A hint window is not a protection
window. The actual ObjectStore test must retain a located value/descriptor
across publication and old-version retirement attempts.

The native selector's physical window description is shared with its hint
description, including admitted reads beyond the logical selector/suffix field
inside the initialized bucket. Located value/tag hints use the layout solver's
field windows and combine adjacent windows before yielding. The normal direct
reader remains fused; this is an execution comparison, not a new layout or
production option matrix.

Use the medium and large capacity families from CAPACITY-FAMILIES.md, including
their independently sized secondary tier. The existing complete-value driver
supplies ordinary Abseil, dependent C1 reads, C32 throughput, direct/manual/Loom
execution, hit/mixed graphs and matched caller successor hints from the first
build. Narrow values use the same external successor graph for both contenders.
The old word-only owner is omitted from this paired experiment; its former
co-resident cold timings must not be silently reused as this cohort's control.

Every timed history has complete-byte/multiplicity oracles and exact Ring visit
counts. Untimed inspection checks that each emitted range belongs to a live
physical extent and reports total hinted bytes/windows per graph. These are
logical hints before cache-line suppression and cache reuse, not measured
traffic. Logical allocation bytes, backing, caller graph, jobs and Ring storage
remain separate. Recorded physical mappings/RSS and host cache geometry qualify
large-residence claims; target names a10/a18/a21/a25 describe reference bucket
levels, not cache tiers.

First implementation source is captured as `read-windows-source-20261015`, digest
`60cb000baf25ee2eea7a68ed0ab88abea5f8da9b0a843022ad2e0e9596e34db6`.
The local panel completes 30 configurations / 840 timing rows, all eight key
widths at forced key tiers, and retained ObjectStore reads. Independent paired
accounting verifies identical storage across policies, equal visits for whole
and prefix hints, exact added value-stage work, and no value stages for sets.
This small local cohort is diagnostic, not a native cold result.

Local wide-map ready callbacks and Abseil lookups have no forwarding calls or
ready-path stack references. Narrow W2 multimaps materialize inline values in
a 32-byte ready frame in the whole-bucket control as well as the selective
controls. Record and charge that frame in the comparison; a frame preference
must not silently exclude this container. The code-generation report still
rejects outlined ready calls and baseline lookups. A future inline-bag bulk
consumer may avoid materialization; it needs a whole-operation comparison.

No universal hint policy is selected. Actual growth, worker consolidation,
retained-version mutation costs and multimap owner selection remain part of the
wider task.

Native V4 source is `read-windows-source-20261015-v4`, digest
`fa1654bcffe23d410192de4bc21762ada48060056b9a4faecaf4ec967718fff0`.
It also strengthens the retained ObjectStore consumer to verify full W64 map
bytes and a located value/descriptor queued across publication. Both whole and
split publication checks pass locally; all eight key-width and both publication
checks also pass ASan/UBSan with always-on stack-use-after-return. Native Zen 5 AVX2
`20261015T055823Z-db8cff08` and V2 NEON `20261015T055811Z-9e34fe1e` complete:
60 configurations, 5,040 timing rows, nine boundary checks and 60 final-binary
audits each. Each report qualifies 560 matched operation groups. Granite AVX-512
`20261015T055817Z-3134a4dc` reached its execution deadline during the final
large-family mixed graph. The qualified recovery below preserves its timeout
and selects only complete matched process groups.

## Native direction

The wide-map result supports a true dependent value stage when cold. Prefix-only
hinting can leave an unprepared dependent value fetch in the ready visit;
full-bucket hinting requests lines covering many values the lookup never consumes.
That explains a plausible trade-off, not measured hardware traffic. Neither is a good
universal compromise. Narrow values and bags often prefer whole-bucket hints,
because the extra stage costs more than it hides. The same stored format supports
these execution choices without changing protection or lifetime authority.

C32 all-hit **ns/query**, 1,572,864 keys, medium capacity family:

| Host / case | Whole bucket | Prefix, eager value | Located value stage | Matched Abseil range |
| --- | ---: | ---: | ---: | ---: |
| Zen5 K40 set | 11.00 | 10.95 | 10.88 | 7.16–7.23 |
| Zen5 K40/W2 map | 7.87 | 9.93 | 16.04 | 22.97–28.08 |
| Zen5 K64/W64 map | 32.56 | 57.38 | 17.26 | 37.21–41.15 |
| Zen5 K40/W2 multimap | 16.74 | 63.33 | 29.05 | 37.92–40.38 |
| Zen5 K64/W64 multimap | 46.89 | 75.99 | 58.74 | 84.99–88.14 |
| V2 K40 set | 14.88 | 14.85 | 14.83 | 11.88–14.54 |
| V2 K40/W2 map | 13.48 | 16.55 | 21.69 | 21.23–24.08 |
| V2 K64/W64 map | 65.26 | 55.66 | 25.16 | 35.05–35.10 |
| V2 K40/W2 multimap | 30.46 | 51.99 | 33.75 | 38.70–40.32 |
| V2 K64/W64 multimap | 81.83 | 81.33 | 85.97 | 66.05–66.31 |

Abseil range is the range of the best matched direct/prefetch median in each
policy's process, not variation within one measurement. Each value is a median
of three repetitions. All direct/manual/Ring and individual baseline rows remain
in the evidence; no baseline from an earlier owner cohort is reused.

The medium wide-map located stage also improves mixed C32 queries: Zen5
19.17 versus matched Abseil35.52 ns, V2 28.20 versus32.35. Large buckets give
20.68/23.69 ns hit/mixed on Zen5 and34.36/33.71 on V2; the latter is only parity
with its matched35.44/33.00 ns Abseil. This is another reason to co-select format
and execution on the target platform rather than prefer the larger family from
fresh memory alone. Sets have no added stage; their small differences between
policies 1/2 are noise/placement, not a distinct mechanism.

The warm 6,144-key cohorts generally favor direct execution. For example Zen5's
medium wide-map Ring is6.46 ns with prefix hints and7.41 with a located stage,
versus Abseil3.42; V2 is15.78/20.50 versus5.79. The cold win does not justify
using Ring for every owner.

Serial and memory costs remain visible. At the large medium-family wide map,
direct serial reads cost239.51 versus167.44 ns on Zen5 and193.48 versus148.67 on
V2 (prefix-policy paired processes). Logical bytes are142,508,588 versus
153,091,704/153,091,696. Wide multimap logical bytes are639,591,656 versus
337,546,312/337,546,304; serial is471.17/347.59 versus241.64/204.52 ns. Those
are substantial unresolved losses, even where Zen5's cold throughput wins.

Physical receipts show a130,023,424-byte primary allocation and12,058,624-byte
secondary allocation for the medium large wide map. These exceed the recorded
Zen5 8MiB and V2 36MiB LLCs individually/collectively; the paired processes have
about287MiB RSS before measurement. RSS belongs to the complete cohort, not one
container. Smaller sets/narrow maps do not inherit this DRAM characterization.
Granite's additional large-LLC test is qualified separately below.

All ready callbacks and baseline lookups pass the static forwarding-call audit.
Sets/maps have no ready stack references. W2 multimaps have28 such instructions
on V2 and38–44 on Zen5; W64 multimaps have34 on Zen5 and0 on V2. These are
instruction counts in the inspected callbacks, not bytes or a dynamic cost model.

## Granite completed groups, including actual DRAM

The parent job timed out; it is not relabeled successful. Sixty-five processes
finished all84rows with exit0, while the last large-family A21 whole-bucket
process finished only its hit graph. `read-windows-completed.py` excludes that
entire three-mode family, leaving **63successfully exited processes/5,292rows/
588matched operation groups**: the full ordinary60configuration panel and all
three medium-family A21 controls. All nine boundary checks and66static codegen
audits completed before timing. The excluded results remain recoverable in the
parent archive; no missing cell is inferred or mixed into a complete comparison.
`evidence/read-windows-granite` records this selection, original timeout outcome,
raw repetitions, qualified summaries and parent recovery reference.

C32 all-hit ns/query at1,572,864keys, medium family; matched Abseil ranges use
the better direct/prefetch median in each policy's own process. These ordinary
cohorts do not all exceed Granite's480MiB LLC.

| Case | Whole bucket | Prefix/eager | Located value | Matched Abseil range |
| --- | ---: | ---: | ---: | ---: |
| K40 set | 13.32 | 13.35 | 13.34 | 10.27–10.56 |
| K40/W2 map | 10.40 | 13.99 | 19.50 | 17.09–24.95 |
| K64/W64 map | 88.22 | 58.77 | 25.03 | 39.97–41.61 |
| K40/W2 multimap | 22.37 | 69.69 | 35.44 | 37.63–42.47 |
| K64/W64 multimap | 101.83 | 111.88 | 97.67 | 75.42–78.85 |

The12,582,912-key medium K64/W64 map has a1,040,187,392B primary allocation
and94,371,840B secondary allocation. The primary alone exceeds the recorded
491,520KiB LLC. Complete logical sizes are1,137,952,410B for the hierarchy and
1,224,736,376B for Abseil. Peak/RSS receipts describe the paired process, not
per-container RSS.

| Actual DRAM operation | Whole bucket | Prefix/eager | Located value | Matched Abseil for located value |
| --- | ---: | ---: | ---: | ---: |
| C32 hit throughput,ns/query | 144.76 | 64.35 | **40.53** | **49.41** |
| C32 mixed throughput,ns/query | 157.47 | 57.30 | **44.53** | **51.35** |
| C1 hit direct serial,ns/query | 371.76 | 374.50 | 365.92 | 238.43 |
| C1 mixed direct serial,ns/query | 325.87 | 332.89 | 326.10 | 278.25 |

The value stage earns selection for this cold wide-map consumer; eager full
bucket hinting is particularly poor. Its unchanged stored format retains about
7%less logical storage, while serial latency still loses materially. This is a
qualified cold throughput/memory trade-off, not overall parity. Narrow values
remain better with whole-bucket hints, and wide multimap memory/read losses are
still unresolved. Warm owners generally prefer direct execution.
