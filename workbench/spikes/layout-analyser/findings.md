# First empirical findings

These are the retained 2026-09-13 comparisons. The
[2026-09-14 synthesis](design.md) and [source dossier](prior-art.md) now connect
them to the original research and later Ikea work. New worked traces there are
logical examples, not additional measurements; captured results below are unchanged.

2026-09-13. This pass makes the decision space more concrete without specifying
an Engine module. A layout's byte width is too weak a summary: conditional
refinement, physical plane placement, operation recipe and request concurrency
each change a verdict in these bounded comparisons. Training a useful menu also
does not ensure that a cheap selector chooses well from it.

The [evidence guide](evidence/README.md) identifies captures, complete samples,
reproduction and limits. Native timings are elapsed time for a whole consumer
loop, including its bookkeeping. Logical counters and modeled line demand are
separate observations. Ranges below describe measured samples/seeds, not
confidence intervals or variation across a fleet.

## Relocating a byte changes the dependency, not just the stride

[The prefix consumer](prefix/README.md) compares the two equivalent-record
families from the [original fixtures](examples.md): 55 or 57 other fixed bytes plus a string. This first
fixture gives every string exactly 16 bytes. It measures a known-row scalar
projection, equality, full reconstruction and preserving scalar writes.
It uses ordinary retained TuplePack operations for the scalar projections and
writes; equality and reconstruction are explicit byte consumers.

The 262,144-row results below are candidate/baseline time ratios. Each range
spans the two matched seed ratios, each formed from three repetition medians.
[The generated contrast table](evidence/comparisons/summary.md) and CSV preserve
other operations and the 4,096-row controls.

| Change and requested operation | Zen 5 | Granite Rapids |
| --- | ---: | ---: |
| D65 → E64prefix7, first mismatch at byte 7 | 2.69–2.72× | 2.91–3.04× |
| A63 → B64prefix9, first mismatch at byte 8 | 0.37–0.40× | 0.41–0.43× |
| D65 → E64prefix7, authored mixed equality trace | 1.24–1.49× | 1.50–1.52× |
| A63 → C64pad, all fields | 0.96–0.98× | 0.94–0.96× |
| A63 → separate prefix, first mismatch at byte 7 | 0.74–0.79× | 0.66–0.67× |
| A63 → separate prefix, full string plus rare byte | 1.27–1.35× | 1.30–1.32× |

Byte numbers are zero-based. Moving the eighth prefix byte out of a 65B core
achieves 64B stride but forces tail refinement on byte-7 mismatches. Moving a
ninth byte into a 63B core avoids tail refinement for byte-8 mismatches. The
conditional-visit counters independently confirm these changes. These are
authored collision distributions, not estimates of production strings.

A separate dense prefix plane helps the rejection path but hurts complete
reconstruction in this fixture. Padding helps the all-fields path by a few
percent; it does not deliver the approximately twofold gain that full-slot
64B-line geometry might tempt one to expect. The scalar projection touches only
its actual bytes. Full strings also need tail data. Their costs cannot be
inferred from the fixed-record width alone.

F64side retains an eight-byte prefix and relocates the rare byte. It provides
another legal alternative, but this screen accesses that byte on every
full-string/rare request; it does not yet establish a sparse optional/patch
policy. Length distributions, prefix entropy, collation, NULLs and coordinated
partial updates remain consequential extensions. The next useful fixture would
make the refinement/rare-byte demand come from a declared distribution while
preserving the same logical values across alternatives.

## Bucket slack is not automatically useful fingerprint capacity

[The bucket consumer](bucket/README.md) fixes logical key count, with 20-bit
exact keys, 24-bit values, Local fingerprint tiles and an eight-byte metadata
tuple. It constructs real head buckets and overflow ropes. Bucket capacity N is
8, 9 or 16 slots; fingerprint width b is 7, 8 or 12 bits. N changes head count
and occupancy; b changes fingerprint work and false candidates.
Two nominal head loads and a skew case accompany hit, miss, mixed and existing
value-update traces. Construction and overflow growth are outside timing.

The 262,144-key, 90%-target-load uniform mixed hit/miss comparison gives these
paired ratios with **the same scalar Local fingerprint + ordinary TuplePack
recipe** on both sides:

| Change | Zen 5 | Granite Rapids |
| --- | ---: | ---: |
| N8, b7: pad stride 63 → 64 | 0.966–0.968× | 0.850–0.851× |
| N8, stride 64: b7 → b8 | 0.805–0.808× | 0.744–0.748× |
| N9, b7: pad stride 76 → 96 | 1.005–1.007× | 1.074–1.090× |
| N16, b8: pad stride 120 → 128 | 1.003–1.016× | 1.094–1.096× |
| N16, stride 128: b8 → b12 | 1.280–1.291× | 1.299–1.303× |
| N9, b7: combined → separate fingerprint plane | 1.102–1.103× | 0.949–0.964× |

Spending N8's one slack byte on eight-bit fingerprints helps here. Spending
N16's eight slack bytes on twelve-bit fingerprints loses. In one retained miss
batch, wider N16 fingerprints reduce exact false-key checks from 14,802 to 967
over 262,144 queries, with identical blocks visited. That count saving does not
pay for the complete recipe's additional work when exact keys are this cheap.
The b7→b8 change also changes extraction geometry, so its gain cannot be assigned
solely to fewer false matches.

Recipe choice materially changes the result. For the **ordinary SeriesPack +
TuplePack** separate N16 layout, widening b8→b12 is 4–6% slower on Zen 5 and
12–13% slower on Granite Rapids in the same mixed trace. This is a smaller
penalty than the scalar Local reader, while the separate plane also has a larger
footprint at b12. Ordinary SeriesPack decodes a complete small range; the scalar
reader may stop extracting after an exact hit. These are executable consumer
alternatives, not interchangeable isolated costs.

There is a concrete capability boundary: a global SeriesPack view has a constant
tile stride, whereas two compact Local tiles repeated inside each bucket need
a grouped tile address calculation. Ikea confirmed that current composition
does not supply this placement. Separate planes and N8 combined views are
admitted. Individual views for aligned 128B buckets are another possible recipe,
not measured here. The [capability note](bucket/README.md#the-repeated-two-tile-placement-gap)
retains the rejected dense cases and avoids turning an unavailable recipe into
a padding requirement.

These results support jointly considering capacity, fingerprint width,
placement and recipe. They do not select a SixDB hashmap policy. In particular,
more expensive keys, narrower/wider values, separate key/value planes and timed
construction could change the frontier. The hardware contexts also differ in
cache exposure and compiler tuning; cross-host differences are not an isolated
test of prefetch behavior.

The same measured menu can consume an authored space cap. For the mixed read
trace above, its observed space/time frontier has two points when both ordinary
TuplePack recipes are allowed: N16/b8 with a separate ordinary SeriesPack plane
uses 2,799,744 encoded bytes at 36.06ns on Zen 5 / 44.13ns on GNR; N8/b8 combined
with the scalar fingerprint recipe uses 3,028,992 bytes at 33.25ns / 43.23ns.
A 2.8-million-byte cap selects the former; a 3.1-million-byte cap admits the
latter's lower observed time. These are medians of seed medians and an observed
finite-menu frontier, not statistical dominance or a production memory budget.
Prepared objects and transition costs are not included. This exercises cost
consumption while leaving the value of memory to the caller.

## Boolean evidence needs both useful grouping and physical separation

[The refinement model](refinement.py) preserves 65,536 rows and four exact
Boolean fields at one byte each. Independent fields have probability 0.1 of
being true. A second order sorts the same multiset by complete field state.
After each plane, exact Boolean bounds certify matches/rejections; only
unresolved rows drive later group visits. All field bytes in a visited group
are charged. Independent physical planes and four-byte interleaved rows use
the same logical test and values. This is a model, with no CPU timings.

For `(A OR C) AND (B OR D)`, 64-row groups give:

| Row order and grouping | Loaded field bytes | Distinct modeled 64B lines, separate / interleaved | Row refinement steps |
| --- | ---: | ---: | ---: |
| IID, all four together | 262,144 | 4,096 / 4,096 | 65,536 |
| IID, AC then BD | 262,144 | 4,096 / 4,096 | 78,194 |
| Clustered, AC then BD | 156,800 | 2,450 / 4,096 | 78,194 |
| Clustered, AB then CD | 261,248 | 4,082 / 4,096 | 130,403 |
| Clustered, A then C then B then D | 155,520 | 2,430 / 4,096 | 155,090 |

All cases return the same 2,424 matches. AC/BD exposes useful early rejection,
but with scattered unresolved rows every 64-row group still gets visited. The
same unresolved-row count becomes useful when clustered. Interleaving eliminates
the modeled cold-line saving even when later logical bytes can be omitted.
Splitting into four planes saves little more than AC/BD in the clustered case
while nearly doubling row refinement steps. Those steps are work counts, not
assumed equal-duration operations.

The second expression, `(A AND B) OR C`, also checks certified true rows and an
irrelevant D field. All 96 configurations agree with independent full Boolean
evaluation. [Complete counts](evidence/refinement-counts/counts.csv) retain
16/64/256-row grains. This does not reproduce probabilistic 32-bit signature
quality or measure SIMD execution. It isolates why predicate shape, group
occupancy and plane placement must survive into an eventual cost experiment;
pairwise affinity and aggregate selectivity discard needed information.

## Spatial placement is a contextual consumer question

The [spatial fixture](spatial/README.md) consumes the same 64B core and 32B
extension as split planes, dense 96B rows or 128B-stride rows. It varies no
extension / one eighth / all, K=1/8/32 independent dependent-chain positions,
and base phases 0/32. One random cycle visits every row per full pass. The
large arm contains just under 1GiB of logical rows; 128-row controls expose
arithmetic and loop costs. Useful extra computation and software prefetch are
implemented but disabled in this first screen.

On Zen 5, aligned large K=1 takes about 149–151ns/row across the layouts and
extension demands. At K=8, core-only split/dense/padded medians are
28.30/29.03/28.73ns; full-row medians are 42.92/40.41/40.06ns. At K=32, the
one-in-eight extension arm is 34.71/32.63/33.35ns. Thus a split organization that
helps core-only requests loses with extension demand. The K=32 sparse case also
shows dense96 ahead despite greater modeled per-operation line demand than the
aligned padded128 case. Reported min/max and shifted-base arms remain separate
in [the complete summary](evidence/spatial-zen5/summary.md).

The completed [Granite Rapids comparison](evidence/spatial-gnr/summary.md) uses
the same logical data and parameters on a new c8i.large worker. Selected aligned
large medians are:

| Extension demand / K | Split64+32, ns | Dense96, ns | Padded128, ns |
| --- | ---: | ---: | ---: |
| Core only / 1 | 176.52 | 185.79 | 190.25 |
| Full row / 1 | 190.76 | 186.86 | 191.74 |
| Core only / 8 | 27.53 | 29.56 | 29.60 |
| One eighth / 8 | 36.81 | 32.46 | 31.96 |
| Full row / 8 | 43.59 | 40.39 | 40.68 |
| One eighth / 32 | 37.72 | 32.51 | 31.63 |

The broad split/core-only versus adjacent/extension tradeoff survives. The
smaller dense/padded differences depend on context: phase0 K32 sparse requests
favor dense by 2.0–3.1% in matched Zen 5 samples, while dense takes 2.2–3.6%
longer than padded on GNR. On GNR, shifting the base to phase32 changes
that same K32 sparse comparison to 32.51ns dense versus 33.91ns padded. Keep the
actual phase and K with the observations; these are within-instance contrasts,
not proof that an architecture name determines a winner. Dense96 remains close
to padded128 on full reads while using about 25% fewer allocated bytes.

These observations justify preserving contextual alternatives; they do not
identify a prefetch mechanism. Whole-loop instructions, independent requests,
cache/TLB behavior and different footprints all contribute. K is the number of
independent positions, not a measured number of outstanding misses. The large
capacity and hot-control difference establish a different timing regime, not
pure DRAM residency. Granite Rapids is a second hardware context, not a
no-prefetch control.

Loom's read-only audit found no invalidating failure in the Zen 5 run: all
checksums, available artifact hashes and 128 placement snapshots agreed. Each
snapshot showed fully resident guest mappings on N0, 4KiB pages and no anonymous
huge pages. Those receipts exclude a silent page-size/guest-NUMA mismatch at
the sampled points, while translation remains a real difference: aligned full
split rows model two pages/op, dense96 about 1.016 and padded128 one. Demand
overlap can also hide extension latency on the K1 next-row critical path.
K32 does not improve on K8 here; its full-row arms are roughly 5% slower.
Neither a hardware queue size nor a globally best layout follows from fixing K.

The final GNR capture also passes all 432 sample/checksum/duration checks and
128 fully resident 4KiB guest-N0 mapping snapshots. Its exposed LLC is 480MiB,
so the smallest large core plane is only 1.42 times that capacity, compared with
85.3 times on this Zen 5 instance. That difference further limits causal
cross-host interpretation. Two interrupted Spot attempts are retained as failed
attempt provenance, excluded from the completed on-demand comparison.

## Good palettes and good selection are different questions

[The selection study](selection/README.md) reuses the existing TuplePack final
captures: 28 measured layouts × two uniform recipes, three machine/ISA contexts,
and three write mixtures. It introduces no native timing. Shortlist budgets count
layouts; palette budgets count executable plans. Complete-plan probes select
finalists rather than summing isolated operation coefficients.
Here a probe replays a retained complete-plan observation and charges a
hypothetical probe count; no fresh online fitting run was performed. Observed
regret is `100 × (chosen time / best time among the 56 measured plans − 1)`
for the same context.

A diverse two-layout shortlist, followed by four complete-plan probes, has
0.60% median / 1.53% maximum observed regret across nine contexts against that
finite measured universe. A co-access-first single layout has up to 25.88%
regret; an isolated-cost-first single layout, after probing both recipe families,
has up to 9.93%. These results are restricted to the already measured 28-layout
subset, not the full 2,816-layout search.

Holding out one workload mixture and training on the other two exposes a
different failure: growing the palette from one to two plans increases the
worst fast-selection regret from 13.55% to 44.92%. The fast selector uses the
nearest training mixture's ordering. The larger menu contains useful alternatives
but the selector chooses badly. Probing all plans locally reduces the maximum
to 7.10% for two plans and 3.89% for four plans. These probe counts do not establish
production selection latency or amortization.

The 44.92% outlier is the retained Neoverse V2/NEON 50%-write case, borrowing the
90%-write training class's ordering after training on 5% and 90%. Its word-recipe
choice loses to a byte-recipe plan already in the palette. With four palette
plans, local probes reduce this particular case to 1.16% regret. The failure is
therefore not just an omitted layout; recipe selection matters to reuse too.

The held-out costs do not influence training or fast selection; a perturbation
check verifies that separation. They are still workload classes on the same
captured rows/binary, not independent containers, key regions or historical
windows. The next meaningful reuse experiment needs those independent units
and a declared acquisition cost. This pass supports a palette-plus-bounded-fit
research direction without showing that a collection-wide trainer generalizes.

## What this changes in the investigation

The useful next work follows specific uncertainties: distributions that produce
prefix/optional refinement; expensive-key bucket consumers; a bounded spatial
timing/concurrency counterexample; and independent units for palette acquisition
and selection. Each can compare complete alternatives before introducing a
general search framework.

An eventual micro-analysis input needs enough semantics to preserve exact
reconstruction, legal operations and conditional demand. It also needs an
executable recipe and explicit workload/hardware context for each measured cost.
The experiments do not decide the representation of that input, an equivalence
graph, a target profile API or the relevant Engine module. Macro cost production,
publication, historical migrations and interference remain dependent on owners
whose mechanisms are not yet defined. Existing cost-consumption and migration
arithmetic are useful controls, not evidence that those owner costs are solved.
