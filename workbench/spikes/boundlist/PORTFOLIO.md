# Boundlist / StreamPack portfolio qualification

The implemented portfolio is **Absolute Ends, Sizes and Relative Ends**, with
optional used lengths owned by StreamPack. Sizes remains the default. Keep
Relative Ends where local ends are no wider than sizes and point/inverse work
matters; keep Sizes for narrower fields, size-only work and resizing; keep
Absolute Ends for the shortest point path or a tight total-extent carrier.
These are measured workload choices, not a universal codec ranking.

The [absolute scorecard](evidence/portfolio/scorecard.md) separates dependent
latency, reciprocal throughput and bytes. Every qualified cell, including losses,
is retained with sample count and min/median/max. The [design record](DESIGN.md)
explains what was adopted and rejected; the public
[reference](https://github.com/ashtonsix/SixDB/blob/719b0f5709c9190bd592888cf8fbbbd379bce960/ikea/docs/boundlist/reference.md) and
[usage](https://github.com/ashtonsix/SixDB/blob/719b0f5709c9190bd592888cf8fbbbd379bce960/ikea/docs/boundlist/usage.md) teach the resulting interface.
This report supersedes the encoding recommendation in [OWNERSHIP.md](OWNERSHIP.md).
Earlier search and extraction results remain historical evidence, not a second
current format specification.

## Decisions changed by qualification

- **Relative Ends graduates as a complete value format.** It shares validation,
  walking, native packets, physical reverse, append, packing and transfer with
  the other codecs. Its U carrier is independent. M4095/B1048320 gives two-byte
  sizes/local ends and four-byte checkpoints; Relative Ends and Sizes both use
  614 B geometry-only metadata at full capacity. M4096 widens local ends to
  four bytes and makes Sizes markedly smaller. Tight B can reverse that decision.
- **Keep grouped and planar, with one geometry-first planar order.** Grouped is
  the ordinary complete-value choice; planar earns its place for established
  U-only consumers ([earlier direct measurements](OWNERSHIP.md#qualification)).
  U-first reduced partial prefixes but added common-path address work. It was
  rejected. Independent U and natural alignment permit useful 48 B/96 B groups
  without a padding or plane-order schema menu.
- **Keep native word strides.** A compact 36 B prepared value layout inhibited
  folded addressing. Removing the dynamic geometry base gives a final 40 B
  StreamPack layout, versus the frozen 44 B; Boundlist remains 28 B. Typed accesses
  remain 16 B. These native sizes are not portable descriptors or allocations.
- **Use a carried encoder without self-referential effect state.** The first
  complete write replay caught a substantial NEON append regression despite
  favorable carried-construction results. Complete assembly showed cursor state
  materialized around memmove. Geometry now receives the transient effect mapping
  at append, avoiding a reference to a member of its containing cursor. Ordinary
  append and checked transfer retain one body. Packing uses the encoder's carried
  position instead of maintaining a duplicate output position.
- **Do not reconstruct a Sizes endpoint to publish the last extent.** The coherent
  control already provides it. The boundary kernel updates that value directly;
  checked admission still validates the requested point/order. The independently
  fused edit control exposed the unnecessary preparation cost.
- **Keep omissions and source composition.** Fixed/zero geometry, full U=R and
  immortal D remain useful. No mandatory Absolute Ends checkpoints, D/checkpoint
  interleaving, extra plane ordering, arbitrary-bit curated format or public
  algorithm menu was added. Arbitrary ordered/packed sources remain composable.

## What was measured

The frozen control is the actual public headers and five implementation TUs from
`b6e4ebd5671173653cac0b0f78ae20338ebe92fe`, recovered by `u-control.py` and injected
into captured sources. It is built beside the candidate with the same driver,
compiler and ISA. Independent raw fused readers/writers do not call the shared
geometry bodies. They are same-byte controls for each codec; their actual schema
work is charged, including a different legacy carrier where B narrows the new
format. No historical cohort is averaged into a final one.

| Panel | Coverage and complete useful result |
| --- | --- |
| Value/reverse | All three codecs; M15/16/255/4095/4096/65535; independent B including B255 with M65535; U present/absent, grouped/planar, 16/256 assigned ranks under fixed bounds; one/64 blocks. Selected payload words determine subsequent requests. Typed and ordinary reverse are separate. |
| Construction | Per-slot append, carried unpublished run and independent fused writer; payload copy/slack initialization plus geometry/U/D/control stores. |
| Packing | Every fifth rank retired; actual payload movement, U shrinking, geometry rewrite and final control. Original input restored outside timing, complete output checked afterward. |
| Shuffled transfer | Actual checked transfer with preflight, 256 requested ranks in permuted order, U shrinking and coherent destination construction. Frozen/current comparison; no independent fused transfer baseline is claimed. |
| Numeric edits | Boundary movement and suffix resize are separate; actual source-qualified damage, +1/-1 pairs with each intermediate mutation made observable, C1/C8 independent chains. Independent packed SIMD controls. |
| Native packets | G16 decode/reduction selects and consumes a payload word; direct native sink vs actual Ikea scoped CPS, C1/C8. Independent scalar oracle validates results but is not a timed fused packet baseline. |
| Loom | Actual reused Ring, manual phase scheduler and direct paths; C1/8/16/32, hints/no hints, point/range, mixed counts, one/524288 blocks. Preparation, payload consumption, frame/scheduler storage, setup and drain are retained. Ready work is kept inline. |

Construction/packing/transfer timings are **whole-block time**, also normalized
per rank. Eight consecutive blocks are not eight interleaved per-slot chains;
do not call their ns/rank normalization per-slot latency. Point/read/edit chains
are genuinely dependent or interleaved as labeled. Scalar query preparation is
inside complete consumers. Restore, independent validation and allocation are
outside hot primitive timings. Real owner histories separately qualify allocation
and lifetime; they are functional evidence, not full owner throughput.

Each principal reader/write/edit cell has nine samples: three repetitions in each
of three process invocations. Native packet normalization can merge semantically
identical placement cases, retaining the actual sample count. Cold cells have
three repetitions of a longer stream. Arm order is counterbalanced where the
driver supports it; cold before/current process order is fixed and remains a
limitation. Small differences should not be read as robust universal rankings.
No failed or losing samples were deleted from a qualified cell.

## Read, throughput and memory findings

For the M4095 grouped-U warm value consumer, current Sizes vs Relative Ends is:

| CPU / ISA | Sizes C1 latency | Relative C1 latency | Sizes C8 ns/request | Relative C8 ns/request |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX2 | 10.23 | 4.95 | 2.41 | 2.24 |
| Zen 5 AVX-512 | 10.29 | 5.06 | 2.44 | 2.07 |
| Granite AVX2 | 10.49 | 6.99 | 3.34 | 2.82 |
| Granite AVX-512 | 10.54 | 6.99 | 3.20 | 2.82 |
| Neoverse V2 NEON | 10.87 | 7.58 | 3.62 | 3.13 |

Absolute Ends is faster for point access in this case, but needs 1058 B geometry
versus 614 B for the checkpointed pair. Relative Ends is not free: the independent
fused point path is still roughly 0.6–1.0 ns faster in several profiles. The
shared same-codec comparisons, inverse results and partial blocks remain in the
scorecard rather than being hidden behind cross-codec wins.

Across the 1800 paired existing-codec full-block read cells, the unweighted median
current/frozen ratio is about 0.99; 150 cells improve by over 10%, and 24 regress
by over 10%. This count is a coverage diagnostic, **not a workload-weighted overall
speedup**. A visible counterexample is NEON M65535/B65535 at 64 blocks, no U:
Sizes typed reverse+value C8 goes 14.91 → 18.46 ns/request. Narrow full-prefix
construction removes chunk branches but is not uniformly better than the old
short-circuit inverse. Relative Ends is also slower than Sizes in some NEON
inverse-throughput cases. Consumers must measure inverse work, not extrapolate
from the point path. Tight extent can make Absolute Ends both smaller and faster.

The [Loom slice](evidence/portfolio/scorecard.md#large-population-actual-loom-consumers)
shows the substantial independent-throughput opportunity separately from serial
latency. Curated Sizes C1 is 266–321 ns/request in the large population; Ring C32
with hints is 27–31 ns/request. The same-concurrency direct path is 82–131 ns;
manual C32 is 23–36 ns. Ring loses to manual on the x86 profiles and wins this
particular NEON comparison. No-hint Ring is much slower. These numbers justify
prepared local dependency phases and caller-controlled scheduling; they do not
justify yielding on ready hot work or claiming Ring removes dependent latency.

Byte tables distinguish encoded metadata from harness backing and payload
capacity. U is absent when full; otherwise new widths follow their own bound.
At 17 assigned M255/B65280 Relative Ends ranks with U, grouped metadata is 166 B
versus planar 614 B; at 256 both are 838 B. Directory capacity is reserved up
front, local groups grow with assignment. Neither packed bytes nor malloc span
lengths establish physical page cost in an Orbital owner.

The final writer comparison also has losses. Across 900 paired existing-codec
append/packing/transfer cells, the unweighted median ratio is 0.94, with 345 cells
improving over 10% and 32 regressing over 10%. This is again coverage, not a
weighted workload score. Granite AVX-512 Absolute Ends without U at M255 takes
4.21 → 5.26 ns/rank for scalar append; NEON Absolute Ends without U at M255 packs
at 2.23 → 2.70 ns/rank. Carried construction is the recommended run path, not a
reason to hide the scalar cost. Relative Ends scalar append can pay an origin
reload that its carried encoder avoids. Numeric edits likewise retain losses,
including NEON Relative Ends resize C8 at 25.03 vs 16.82 ns/pair for the specialized
fused control. The complete tables qualify each choice rather than asserting
universal fused parity.

The boundary-specific fix closes the largest unforced seam gap: final Sizes
boundary C1 is 16.04/16.02 ns on Zen 5 AVX2/AVX-512 versus independently fused
16.17/15.87 ns, and 21.78 vs 21.85 ns on NEON. This follows the same arithmetic
and actual damage contract; it removes unnecessary endpoint preparation.

## Hardware and residence limits

Clang21.1.8, release O3, both loop/SLP vectorizers disabled. AVX2 uses x86-64-v3;
AVX-512 uses znver5 or graniterapids; NEON uses armv8-a+simd tuned for neoverse-v2.
Both ISA availability and tuning are recorded in compile commands. Actual hosts:
AMD EPYC9R45 (c8a.large), Intel Xeon6975P-C (c8i.large), Graviton4/Neoverse V2
(c8g.large). This campaign does not establish V3/SVE2 performance.

Warm traces repeatedly consume one or 64 small initialized block prefixes. The
harness also reserves full payload-capacity arrays; their untouched or inactive
capacity must not be called active cache footprint. The large Loom fixture's
nominal arrays total 800 MiB, including global ends and Relative Ends controls
charged to both arms. Its 262147-request point trace touches a smaller subset.
Granite exposes a 480 MiB LLC: allocated bytes exceeding that number do not prove
DRAM residence. Available counters and time identify memory-sensitive behavior,
not a trustworthy per-request cache-level classification. Accordingly the report
calls this a **large-population** case. It makes no universal DRAM/L2 residence
claim and keeps capacity, active requests, timing and RSS concepts distinct.

Complete linked assembly accompanies the worker bundles. It identified narrow
address fields, added geometry bases and the cursor reference cycle. The selected
[prefix codegen/MCA diagnostic](evidence/portfolio/codegen/README.md) confirms
explicit NEON vector prefixes/carries/comparisons without scalar carry round
trips. Its 5-cycle backend reciprocal-throughput estimate is not a measured
complete operation or dependent latency, and was not used to replace live results.

## Correctness, damage and actual retained readers

Local release checks and executable Boundlist/StreamPack guides pass, with all
22 public/author headers compiled independently. The final ASan/UBSan run covers
the five behavior suites and actual material histories. Native workers repeat
all five suites and material histories in each ISA profile.

- 41,472 independent boundary/resize cases include G16 crossings, carrier maxima,
  refusal without stores/effects and actual preserving vector footprints.
- 1,056 cross-encoding value histories plus fixed/full/immortal combinations use
  independent R/U/end models. Literal schemas and value bytes check old/new
  decoding, exact legacy re-encoding and conversion without modifying old bytes.
  An independent mixed-carrier planar fixture checks the partial prefix formula.
- Search covers ties, zero groups, mixed signed/wide queries, exact unaligned and
  guarded tails, custom domains, range closure, eligibility holes and wide origins.
  Prepared population completion survives destruction of its temporary typed
  dispatch by borrowing the original named source.
- 432 actual Loom/Orbital histories validate source-qualified effects, retained
  old/fresh/late readers, readable padded tails, packing, transfer and remaps.
- Shared-payload histories use real range Holds, not copied writer handles.
  Five captures expose a final initialized payload extent of 5317 B without
  copying prior payload prefixes. Copied private metadata is 6392/4600/4712 B for
  Absolute Ends/Sizes/Relative Ends; the sum of exposed prefixes is shared backing,
  not another copied-payload total. Live tail reclamation releases at least one
  actual 4 KiB page after the late reader drops while an early reader remains
  valid. Counts include metadata/holder allocations; they are not payload-only RSS.

Effects describe issued byte spans on the original source. They do not supply
read grants, beforeimages or publication. Owners retain one coherent geometry,
U, eligibility, payload and backing version through a wait. The shared-payload
fixture counts its extra Parts/holders; it is a valid integration, not a prescribed
allocation architecture. Engine continues to own complete directory policy.

## Reproduction and retention

[portfolio-inputs.json](portfolio-inputs.json) identifies the exact source digests
and worker cohorts. Read/packet/Loom source is the corrected geometry-first
capture; the final write/edit follow-up changes only author write orchestration
and formatting, preserving read-only bodies. Use the executed capture when
reproducing: intermediate U-first descriptors must not be decoded as final bytes.

```sh
python3 workbench/tools/worker.py fetch JOB
python3 workbench/spikes/boundlist/portfolio-report.py
python3 workbench/spikes/boundlist/portfolio-scorecard.py
python3 workbench/spikes/boundlist/design-check.py
```

Prefix Linux commands with `orb -m ubuntu` from macOS. Recover baseline headers
with `u-control.py --revision b6e4ebd5`, inject them at `portfolio-control`, and run
`portfolio-final.sh`; `portfolio-writes.sh` is the bounded write/edit follow-up.
Inputs/host identities and exact raw command lines are retained beside each
worker archive in S3. `provenance.json` records both cohorts and actual host data;
CSV exports retain all qualified cells. Full binaries, source snapshots, build
logs and assembly remain in S3/ignored storage instead of Git.

The early native2 jobs include interruption/timeouts and are diagnostic only.
The first geometry-first replay (`20261020T213558Z-5908f815`,
`20261020T213629Z-f310a151`, `20261020T213629Z-2441189c`) was cancelled after the
literal fixture exposed the partial planar length error; none contributes a
qualified measurement. The corrected replay and final bounded write/edit jobs
completed successfully. Initial cursor losses are retained compactly in
`cursor-diagnostic.csv` from the corrected read-cohort jobs. The intermediate
reference-cycle fix was separately qualified by jobs 20261020T215556Z-4b16ab46,
20261020T215556Z-9196212e and 20261020T215556Z-3d534a4a; the final write/edit
cohort supersedes those measurements. The local diagnostic runners were moved
to ignored `build/retired/boundlist-portfolio-20261020/` after matching their
SHA-256 entries in the recoverable final source capture.

Earlier diagnostic captures: native2 `1ed48448…` (20261020T202246Z-2405e82b /
24e8345d / c2a2701d), U-first 44 B `410bed5e…` (20261020T205256Z-b71ccae7),
and `f5ab60df…` (20261020T211214Z-5c3dda3b / 87d93479).
`portfolio-native.sh` and `portfolio-followup.sh` are recovered from their matching
captures, not treated as competing maintained qualification recipes.

Remaining measured headroom is explicit: NEON inverse throughput, some scalar
append/packing cells, remaining numeric edit seam costs and native CPS overhead.
There is no claim of parity with an independently fused implementation in every
cell. The retained mechanisms earn their place through complete consumers and
coherent composition; optional formats do not become compulsory for all owners.
