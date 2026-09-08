# First trie-remapping probe

2026-09-08. The first spike is concluded; its broader design questions remain
open in the [closeout](README.md).

**Local remapping remains promising, but the useful adaptation
boundary depends on payload layout and access pattern as well as trie shape.**
The experiment supports keeping natural-key routing available and exploring
both fully ordered gaps and order between blocks with flexible slots inside.
It does not select a production representation or a density threshold.

This is a resident, single-threaded access-method model on an Apple ARM64 Linux
VM, pinned to CPU 0, using Clang 21.1.8, C++23, `-O3`, generic tuning, and
sequential Google Benchmark repetitions. It compares whole-region arms; mixed
natural/remapped boundary discovery and actual dual-arity physical-trie
navigation are still unimplemented. See the [probe contract](probe.md) for
representation, timer, oracle, and accounting details.

## What was measured

Nine variants cover natural radix routing with three terminal cutoffs, packed
remapped blocks, fully ordered gaps, gaps with packed-rank columns, block-local
disorder, a separate payload pool, and an append-aware gap policy. Remapped
blocks have 256 positions and initially at most 192 rows. Natural containers
reserve 25% growth slack, capped by their representation limit.

The retained [evidence](evidence/local-arm-20260908/README.md) comprises an
862-comparison broad screen with five repetitions, a 260-comparison confirmation
with seven, and a 164-comparison gap-policy follow-up with seven. Comparisons
overlap between runs. The broad screen is archived in full; the two follow-ups
also retain compact repetitions and accounting in Git. Every run has measured
source, commands, binaries, and full results in a verified S3 bundle.

Below, **churn ns** means one delete/insert pair plus two point queries, a
16-row projected scan every eighth pair, and a reference lookup every fourth
pair where configured. `deps` has sixteen 64-bit columns, four sets of physical
references, and per-block plus collection count/sum summaries. It is a concrete
mixed cycle, not an insert latency or a full database transaction. Times are
medians; the small differences should not drive a design choice.

## Density alone does not identify a transition

Broad-screen churn ns per cycle, with occupancy measured over 65,536 natural
key positions:

| Occupancy | Key-only natural | Key-only gapped | `deps` natural | `deps` gapped |
| --- | ---: | ---: | ---: | ---: |
| 1% | 188 | 163 | 504 | 284 |
| 5% | 145 | 181 | 392 | 336 |
| 20% | 181 | 219 | 603 | 399 |
| 50% | 220 | 267 | 1,126 | 535 |
| 90% | 267 | 290 | 2,180 | 757 |

**Both 5% and 20% are viable natural-trie regimes here.** With keys alone,
natural wins those churn comparisons. With wide columns and dependencies,
gaps win. As natural byte terminals fill, packed column ranks make mutations
move more data. High natural-key density can therefore coexist with a reason
to change payload placement.

This sweep changes population. Holding population at 4,096, broad-screen
key-only natural churn changes from 167 ns over 8,192 positions to 395 ns over
1,048,576; gapped changes from 183 to 183 ns. The natural shape matters as well
as row count. This pair still does not hold footprint or cache residency fixed.

Logical point lookup pulls in the other direction. In the confirmation's
`deps` profile, natural versus gapped takes 29 versus 56 ns at 5%, and 30 versus
73 ns at 20%. Those requests are half hits, half misses; hits read the full row.
Exact suffix lookup and remapped routing must earn back that additional cost.
An adaptive trigger should consider access mix, shape, column width, and recent
maintenance, with conversion payback and hysteresis tested separately.

## Gaps in keys must reach the payload

For 512 cycles at 5% occupancy, the broad screen records:

| Placement, `deps` | Churn ns | Counted payload bytes moved | Reference cells repaired |
| --- | ---: | ---: | ---: |
| Packed ordered rows | 1,416 | 12,720,896 | 397,528 |
| Gapped keys, packed-rank columns | 1,116 | 12,720,896 | 2,160 |
| Gapped keys and columns | 336 | 69,120 | 2,160 |
| Block-local disorder | 364 | 0 | 0 |

The confirmation repeats the ranked/gapped split at 1,119 versus 332 ns.
Keeping physical labels stable saves locator repair, but does not save column
shifts when columns remain aligned to occupied rank. The gap benefit requires
payload gaps or another way to keep payload positions stable. Zero movement
here means no counted relocation in this finite history; all arms still write
new values and maintain search metadata.

## Full order is sensitive to gap policy

Gap-policy follow-up, `deps`, churn ns:

| History | Natural | Uniform gaps | Tail-aware gaps | Block-local disorder | Separate pool |
| --- | ---: | ---: | ---: | ---: | ---: |
| 5% uniform | 386 | 326 | 328 | 355 | 340 |
| 20% uniform | 569 | 401 | 408 | 449 | 447 |
| Shared-prefix collisions | 835 | 342 | 343 | 373 | 356 |
| Fixed hotspot | 544 | 613 | 615 | 496 | 363 |
| Appends | 469 | 1,623 | 477 | 448 | 300 |
| Moving hotspot | 488 | 624 | 623 | 494 | 360 |

Uniform spacing repeatedly spends gaps behind an append position. A small
policy change extends consecutively at the right edge and packs that block
toward the front when only internal gaps remain. It sees only the current
insertion. Over 2,048 append cycles it reduces counted payload relocation from
33,292,160 to 1,185,536 bytes and reference repairs from 1,040,380 to 37,048.
It performs 33 redistributions and the same 14 splits. Churn improves 3.4×,
to approximately the natural arm's cost.

That rescues full order for this history; it does not solve interior hotspots.
The moving hotspot still favours avoiding payload relocation. Block-local
disorder moves 294,912 payload bytes and repairs 14,484 reference cells there,
versus 4,904,960 and 158,600 for uniform gaps. A block split moves some rows,
but ordinary insertion can choose any free slot inside its logical range.

Order between blocks is a useful candidate constraint. After the mutation
history, projected scans of up to 256 rows take 164/201 ns at 5%/20% with
block-local disorder, versus 316/377 ns with gaps and 187/253 ns with the
separate pool. The small ordering permutation can be cheaper to traverse than
occupancy gaps while preserving more locality than an unrestricted pool.
These scans sum one column; wide row reconstruction and cold gathers remain
open. Before/after query sets differ, so their timing difference is not a
controlled estimate of fragmentation alone.

## Larger natural terminals are an essential control

For 4,096 keys sharing sixteen high 16-bit prefixes, the final follow-up gives:

| Representation, `deps` | Initial terminals/blocks | Initial owned KiB | Point ns | Churn ns | Post-history scan ns |
| --- | ---: | ---: | ---: | ---: | ---: |
| Natural, 64-row general cutoff | 2,583 | 2,716 | 50 | 835 | 1,808 |
| Natural, 1,024-row general cutoff | 16 | 875 | 54 | 1,382 | 104 |
| Gapped | 22 | 949 | 58 | 342 | 328 |
| Block-local disorder | 22 | 971 | 72 | 373 | 171 |

Simply terminating sooner removes most tiny nodes and gives excellent projected
scans. It also enlarges the packed columns shifted by mutations: 17,143,040
counted payload bytes versus 70,528 for the smaller natural terminals. Remapping
is one way to combine compact routing with insertion room; it should continue
to compete against improved natural terminals and independent column layouts.
The model stores full keys and IDs even where a Calico-like keyset could omit
them, so these footprints do not establish production memory superiority.

## Identity, summaries, and conversion

Stable IDs replace four physical-reference repairs with one locator-table
repair per moved row. In the moving-hotspot gapped arm this changes 158,600
repairs to 39,650, and churn from 624 to 606 ns; the columns still move.
The append-aware arm changes 37,048 repairs to 9,262, with little timing change.
Stable identity is useful for containing repair fan-out, but this experiment
does not show it removes movement cost or always improves elapsed time.

The reference arrays already know every repair address and are relatively
compact. Real secondary indexes may scatter those cells and require a search.
Conversely, the probe's natural physical references resolve by repeating natural
lookup. In the confirmation, valid remapped references read a wide row in about
43–46 ns, versus 65–86 ns for natural in these 4,096-row cases. This excludes
secondary-key search. Read-path costs belong beside the repair counts.

Count/sum totals follow materialized logical blocks and the collection. Pure
block relabelling keeps the block total; splitting redistributes it. Thus a
physical code change need not rewrite summaries for nonexistent trie strata.
Logical range summaries over arbitrary physical pools, pending aggregate
deltas, min/max, and retained editions still need explicit contracts. Changing
the dependency profile also changes allocation/cache layout; subtracting profile
times would not isolate summary or reference overhead reliably.

Confirmation rebuilds of the gapped, blocked, and separate-pool arms take about
40–65 ns per row for these 4,096-row cases: roughly 0.16–0.27 ms. This includes
materializing the source and constructing the target and its dependencies while
the source stays live. Publication, reclamation, concurrent readers, WAL, and
replication are excluded; temporary conversion space is not a measured peak.
It is an initial conversion-cost observation, not an adaptation payback claim.

## What this changes, and what remains open

The next useful unit is a region that can choose natural routing and payload
placement with explicit conversion cost. Keep fully ordered gaps, block-local
disorder, and larger natural terminals as active candidates. More permissive
ordering is supported by this probe, but unrestricted pooling carries scan and
identity trade-offs that need larger, colder working sets.

The most discriminating follow-ups are a mixed natural/remapped structure that
charges boundary discovery and actual physical navigation; multi-byte/variable
keys and real 16-bit containers; and wider cache-residency sweeps on Zen5 with
compressed columns and scattered secondary references. Online switching needs
drifting histories, reverse conversion, operation tail latency, retained readers,
and durable publication. No decision on 65,536-position segment geometry,
multiple occupants of a truncated slot, or stable-locator generations follows
from this model.

Correctness checks passed for every registered method and profile with counters
on/off and 0%/25% natural growth slack, including empty lifecycles, splits,
ordered-map/image/scan oracles, reference resolution, and block/collection totals.
The final run also passed ASan/UBSan. Seven repetitions are repeated executions
of one deterministic history, not seven workload seeds. Final-run median
max-to-min spread is 2.3%, with outliers; this is no operation-tail or statistical
confidence claim. The evidence notes identify superseded query-domain results
so they are not silently reused.
