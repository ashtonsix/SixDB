# Cached unit stride: rejected native control

The cached `stride()==1` predicate candidate is rejected. Keep the R4
two-pointer general reader and its separately prepared predicate. Caching a
flag in predicate padding preserves their observed 16/24-byte sizes and saves
one instruction per selected full-tight filter window, but complete native
commands expose substantial preparation and one-shot losses. The candidate
header was never applied to live Ikea.

The [compact receipt](../evidence/interface-cohesion-v2-20261023/predicate-native.json)
and [all selected cases](../evidence/interface-cohesion-v2-20261023/predicate-native-cases.csv)
record this decision. This comparison uses exact R4 as its baseline, with only
`ikea/tuplepack/filter.h` changed in the candidate. It is separate from the
earlier f153-versus-R4 comparison and later writer API corrections.

## Complete commands and units

[blocks.cpp](blocks.cpp) uses 64 named blocks and eight immutable
`read_plan<64,16>` values. Widths one through eight occupy different bit shifts
inside a byte. Each width has seven 16-row blocks and one exact 13-row tail.
All routes visit the same blocks and execute the same queries in the same order:

| Route | Timed work at each visited block |
| --- | --- |
| `retained` | Use its retained prepared predicate |
| `reprepare` | Prepare a predicate from its retained reader, then issue queries |
| `oneshot` | Use `reader.filter` convenience for each query |
| `fresh` | Select the shared law/plan by width, attach placement, bind a reader and prepare a predicate, then issue queries |

Plans, byte allocations and fixture construction precede timing. Fresh binding
uses named local views/readers whose lifetimes cover every query on that block.
The fixture still physically retains per-block view/reader slots in all routes;
fresh measures admission and binding cost, not a minimum resident descriptor
design. Only retained keeps the prepared-predicate allocation during timing.

Queries per block are one, eight or 64. A quarter of commands use one of three
sparse activity patterns; the rest use full activity. In-domain right-hand
sides change with the traversal epoch. Tight, stride-two and mixed placement
combine with clustered, two-block-burst and fixed shuffled width traces:
108 cases per profile. Each trace visits every block exactly once, with eight
blocks per width and respectively seven, 31 and 58 width transitions.

Before timing, a bit-by-bit scalar oracle checks every timed activity shape
and every in-domain right-hand side through all four routes, then checks the
ordered traversal fold at all three reuse counts. `--check-only` covers all
nine placement/trace fixtures. One order-sensitive checksum consumes each
timed traversal; there are no added per-value assembly barriers.

Google Benchmark's `cpu_time` is **CPU ns per 64-block traversal**.
`ns_per_block` divides it by 64; `ns_per_query` divides it by
`64 * queries_per_block`. These are checked inverse-rate counters, not extra
timers. The separate maintained filter comparison reports **wall ns per
16-row window**. The reader validates these units and work counts; it never
pools them into one performance score.

Observed structural costs are the same in both arms:

| Counter | NEON / V2 | AVX2 / Zen 5 | AVX-512 / Zen 5 |
| --- | ---: | ---: | ---: |
| Shared inline plan array, bytes | 14,848 | 22,528 | 14,848 |
| Fixture object, bytes | 26,048 | 33,728 | 26,048 |
| Retained predicate allocation, bytes | 1,536 | 1,536 | 1,536 |

The fixture includes the inline plan array; those two counters are not
additive. Other routes report zero retained-predicate allocation. Exact payload
grants are 1,000 bytes tight, 1,936 strided and 1,456 mixed. Allocation capacities
happen to equal those grants in this cohort, but views receive vector size,
never capacity. Counts and sizes are observations on these builds, not portable
ABI contracts.

## Native protocol and results

All builds and independent checks complete before timing on Neoverse V2 NEON
and Zen 5 AVX2/AVX-512. Every profile checks all three executable arms, including
the 108 block cases and maintained filter oracles. Trials run pinned to one
CPU, with five recorded orders:

```text
before, after, duplicate
after, duplicate, before
duplicate, before, after
duplicate, after, before
after, before, duplicate
```

Duplicate is a byte-for-byte copy of the before ELF, with no separate
compilation. All timed arms use the same `argv[0]`, working directory, user
arguments and result pathname. The [wrapper check](../evidence/interface-cohesion-v2-20261023/predicate-wrapper-smoke.json)
verifies argument preservation. This does not disable ASLR or equalize every
loader/environment effect. Duplicate/before is a measured negative control,
never a divisor or correction factor.

Each filter trial uses 300,000 iterations. Each block case receives at least
0.1 seconds with one Google Benchmark repetition per trial. The retained CSV
contains all five samples, medians, ranges, median absolute deviations, paired
ratios and ratios of medians. The following ranges use **median paired
after/before ratios**, expressed as percentage changes; positive means slower.

| Profile | Retained, all 27 cases | Reprepare, nine q1 cases | One-shot, all 27 cases | Fresh, all 27 cases |
| --- | ---: | ---: | ---: | ---: |
| NEON / V2 | −0.9% to +0.2% | +8.1% to +11.9% | +24.8% to +98.5% | −1.0% to +0.8% |
| AVX2 / Zen 5 | −0.9% to +2.9% | +13.0% to +18.4% | −1.8% to +1.9% | approximately 0% to +1.5% |
| AVX-512 / Zen 5 | −2.8% to +0.6% | +13.6% to +22.1% | −0.8% to +1.9% | −1.4% to +0.6% |

Reprepare q64 recovers some cost: its nine cases range from −3.9% to −1.7% on
NEON, −3.6% to −2.4% on AVX2, and −1.3% to +1.2% on AVX-512. Those benefits
at high reuse do not offset the preparation and one-shot failures of a global
interface change. All 27 ARM one-shot cases lose about 25% or more.

Representative complete commands, **CPU ns/query**, show the absolute cost:

| Case | Profile | Before median | Candidate median | Duplicate median | Paired candidate ratio |
| --- | --- | ---: | ---: | ---: | ---: |
| One-shot, clustered, strided, q1 | NEON | 18.45 | 36.62 | 18.41 | 1.985 |
| Reprepare, clustered, tight, q1 | NEON | 28.38 | 31.67 | 28.38 | 1.116 |
| Reprepare, clustered, tight, q1 | AVX2 | 17.06 | 20.21 | 17.05 | 1.184 |
| Reprepare, clustered, tight, q1 | AVX-512 | 14.29 | 17.33 | 14.31 | 1.213 |

The first case loses in every paired trial: ratios 1.984–1.990. Its duplicate
paired median is 0.998. Across all 108 block cases, duplicate paired medians
range from 0.993–1.001 on NEON, 0.989–1.006 on AVX2 and 0.992–1.010 on AVX-512.
No duplicate block case reaches a 5% median loss. Display precision does not
claim hundredth-nanosecond measurement accuracy.

The separate 360-case maintained filter comparison has zero, five and twelve
candidate cases with median paired losses of at least 5% on NEON, AVX2 and
AVX-512, respectively; duplicates have none. Worst paired ratios are 1.024,
1.084 and 1.147. Some x86 losses occur in shared decoded-value routes that
bypass the prepared predicate. Those are actual executable outcomes, but
cannot be attributed directly to cached-flag preparation in those bodies;
linked code placement and other execution effects remain possible.

## What the actual ARM caller establishes

The [linked receipt](../evidence/interface-cohesion-v2-20261023/predicate-blocks-linked-arm.json)
checks the actual ELF symbol extents and instruction bytes. The common checked
full-tight filter falls from 54 to 53 instructions and retains one vector load,
one `CMHS` comparison and the same mask-reduction constant. These counts describe
that selected path, not the strided path or a latency prediction.

The query loops include one direct call to that outlined checked filter:

| Route | Before instructions | Candidate instructions |
| --- | ---: | ---: |
| Retained | 26 | 26 |
| Reprepare | 27 | 25 |
| One-shot | 47 | 52 |
| Fresh | 25 | 25 |

The candidate newly outlines the reprepare traversal; the baseline keeps it
inside the benchmark caller. Thus the shorter query loop also has a changed
call/frame boundary. Both fresh traversals remain outlined. Their 64-block
backedges include law/plan selection, stride/overflow checks, exact-extent
division, plan/view compatibility and one-code admission, followed by a local
48-byte view and 24-byte predicate. Admission was not hoisted out of traversal;
there is no allocation, plan construction or full plan-control copy there.
Query loops, weighted sums, epoch updates and ordered rotate/XOR folds survive.

One-shot repeats admission, code/pointer copying and candidate stride-flag
acquisition on every query. ARM copying also changes: the baseline stores the
pointer pair once and copies code separately at offset 16. The candidate stores
the pair in temporary and consumed locations, then copies four bytes from
temporary offset 47 to consumed offset 15. That copy includes the view pointer's
last byte; the checked filter immediately loads the full eight-byte pointer.
The exact addresses and instructions are retained in the receipt.

This is extra emitted work and a concrete overlapping-store forwarding hazard
on the candidate path. The comparison does not establish its causal share of
the timing loss, and it does not explain x86 outcomes. Padding size, a saved
metadata load or a shorter selected path alone cannot establish a faster
complete command.

## Recovery and limits

[predicate-native.py](predicate-native.py), its [shell entry](predicate-native.sh),
and [predicate-results.py](predicate-results.py) retain preparation, execution
and reading. The compact receipt references both durable worker artifacts,
their exact one-header source delta and common block source. Recover the
results, then read or validate them without rebuilding:

```sh
python3 workbench/spikes/ikea-composition/handle-boundaries/predicate-results.py \
  neon=NEON_RESULTS zen=ZEN_RESULTS --output OUTPUT
python3 workbench/spikes/ikea-composition/handle-boundaries/blocks-linked.py \
  --results NEON_RESULTS --output OUTPUT/blocks-linked.json
```

[blocks-linked.py](blocks-linked.py) admits only the recorded ARM pair and
checks 10,356 literal instructions across the selected function symbols. It
replays annotated paths; it does not infer source semantics or timing causes.

This is warmed read-command evidence, with shared plans prepared outside
timing. It does not cover writes/effects, owner publication, cold memory,
content-proof acquisition or a complete Frame consumer. The deterministic
width traces do not establish independent streams, unpredictable hardware
behavior or a catalogue crossover; the corrected
[distinct-stream result](../../filter-composition/MASK-GRADUATION.md#harness-correction)
remains the relevant negative control for that inference.
