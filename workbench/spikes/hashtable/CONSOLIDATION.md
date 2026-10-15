# Consolidating private tables across allocation owners

> Earlier release/lifetime conclusions below are provisional because validation allocated between merge and release. The [measurement correction](BENCHMARK-VALIDATION.md) describes the corrected fixture; source retention and full-byte correctness checks remain valid.

Same-seed worker tables still own independent storage. A multimap's block/rank
identifier is meaningful only with its bag owner; copying that identifier into
another table does not transfer the allocation or its Pool authority. This
experiment constructs a private destination from immutable source tables by
copying logical keys and complete opaque values. Sets form a union, maps use an
explicit nonthrowing combiner, and multimaps retain every duplicate value.

Performance shapes the first implementation. Replaying every bag as scalar
inserts would repeatedly hash and locate its key. `consolidation.h` instead
consumes bounded contiguous groups, with one destination lookup and reservation
per group. It preserves inline values when the group fits; spill combines the
old inline prefix and incoming group without a large concatenated temporary.
External rows update only their descriptor when it changes. Native byte copying
uses explicit NEON, AVX2 or AVX-512 and exact scalar tails; the ordinary one-value
mutation paths are unchanged. Existing index, TuplePack field, SeriesPack
selector and StreamPack allocation/slot operations retain their observations.

The source cursor stores its scan position. Sets and maps commit directly inside
the ready row callback. Only a partial multimap bag also stores a decoded row,
key facts, local link ID and position within that link. It retains no raw payload
span across calls. Every resumed bag group rebinds its source window. The caller
must keep the source owner and interpretation immutable and alive. This cursor
is neither a retention token nor permission to read an unretained version. A
callback's span expires when it returns; it may mutate a disjoint destination,
but cannot mutate either source.

Index headers/records, multimap values and empty bag links consume work budget.
A set/map record includes its atomic destination attempt in that one work unit;
the earlier control charged a separate consume stage. A positive budget progresses
even inside a long bag. At most 64 values form one group;
this is a transfer control, independent of the eight-key query/batch interface.
A refused group commits no logical values and stays at the cursor. Previously
accepted groups are never replayed. Reservation refusal/cancellation may retain
extra physical capacity under the existing owner contract. Destination collision
walks, allocation and retirement are not hard real-time bounded by this source
budget. Cancellation discards a private partially built destination; it does not
undo already accepted writes in a destination supplied by the caller.

`consolidation_access` is a spike-local friend of the two experimental owners.
It keeps the new composition out of established scalar mutation bodies and
avoids declaring a public interface before measuring the complete operation.
If the boundary earns adoption, its logical scan/append operations belong with
the owning container; layout fields and bag IDs need not become public API.

`consolidation.cpp` compares one-value groups, grouped transfer, ordinary Abseil,
and exact-width Abseil from the first candidate. The baseline uses an efficient
direct traversal and one key lookup per multimap bag, with contiguous vector
append. It is not made to reproduce linked traversal or claimed as a resumable
Worker implementation. Source construction, merge, complete output scan and
release have separate timers, alongside their lifetime sum. Every timed history
then checks full bytes and multiplicity against an independently constructed
ordered-map oracle outside timing. Merge ns/input is reciprocal throughput;
output scan time is not dependent point-read latency.

The first panel covers compact lists at 128 keys/source and, outside K8, a
1,024-key/source hierarchy. Sources are disjoint or half overlapping, with
singleton, eight-value and mixed/long-bag populations. Both sources and the
destination coexist. Reported logical bytes include active owner state; backing
is the destination's live allocation capacity. Coexistence is sampled at ready,
not a transient high-water mark, Pool metadata or RSS. Abseil backing is unknown.
These warmed histories do not establish a cache-residence tier.

This first comparison deliberately uses different seeds for the two sources and
destination (17, 41 and 127), so every transferred key follows the general
decode/re-encode path. The hierarchy fixes primary/secondary units at 192/96 B,
with A=8 for narrow values and A=10 for wide values, and B=A-2. These are controls,
not selected merge formats. They overreserve narrow maps and charge empty bucket
visits. A fair recommendation still needs the existing smaller coupled layouts
and a matching-seed/geometry transfer that keeps encoded primary keys until a
downstream lookup actually needs reconstruction. The older word-map merger
already demonstrates that opportunity; generic storage must not lose it.

Grouping removes repeated per-value admission but still performs one key lookup
per group. Abseil retains a located bag through the complete contiguous append.
A private destination can potentially retain an owner-qualified location through
several groups, provided resume rebinds storage and validates the destination
generation; a raw pointer across suspension would not satisfy that boundary.
This and destination pre-sizing are architectural alternatives to evaluate before
micro-tuning the general transfer loop. Neither has been measured here yet.

The group boundary also follows each physical source link. An eight-value bag
built by scalar appends can occupy a four-value link followed by a partially used
eight-value link; grouping cannot turn those into one contiguous eight-value
window. The current destination consequently repeats link-level admission and
can inherit this slack. A logical whole-bag transfer with destination reservation
sized from the bag's length is another candidate, alongside contiguous source
storage. Obtaining that length, holding exclusive preparation across yields and
publishing only initialized values all have costs and lifetime obligations. This
is a design alternative, not an asserted free vector-copy speedup.

The fixture uses three actual Orbital Pools on one adopted Lane. Checks retire
both source Pools before reading and mutating the destination. They also cover
partial-bag yield/refusal, exhausted effect capacity, actual allocation pressure,
retry and recovery, linked/vector/buffer bags, and exact copy windows abutting
guard pages. Sources remain readable throughout preparation. This establishes
the consolidation core, not remote Worker handoff or ObjectStore publication.
Those consumers must retain sources through final copy, publish the destination
under the existing version contract and charge source retirement separately.

The first complete local panel passes six boundary checks and 960 timing rows,
with complete-output checks after every history. Source scan fusion removes the
outlined scan call observed in the first disassembly; it does not establish a
timing win. At half overlap, grain 64 and eight values per key, K40/W2 grouped
transfer is 10.41 ns/input value versus 22.97 with one-value groups and 3.02 for
Abseil. K64/W64 is 23.98 / 33.66 / 7.22 ns. K8 sets take 9.64 versus 11.03 ns for
Abseil, but their complete table lifetime remains slightly slower. These are
OrbStack diagnostics, not native performance claims or evidence that grouping
closes the multimap gap. Timed rows average several histories.

## Native general-merge comparison

The first native panel completes on Zen 5 AVX2, Neoverse V2 NEON and Granite
Rapids AVX-512. Each host supplies 12 processes, 3,200 timing rows and six boundary
checks. Two process orders and five repetitions measure 16 histories per row.
Source and destination seeds differ; fixed geometry is the control described
above. For half overlap and grain 64, median merge reciprocal throughput is:

| Case | Zen ours / Abseil ns | V2 ours / Abseil ns | Granite ours / Abseil ns |
| --- | ---: | ---: | ---: |
| K8 set, 128 keys/source | 6.82 / 10.64 | 10.82 / 11.94 | 8.83 / 12.22 |
| K40/W2 map, 1,024 keys/source | 13.86 / 11.11 | 20.25 / 12.72 | 18.29 / 12.40 |
| K64/W64 map, 1,024 keys/source | 30.67 / 11.15 | 43.79 / 14.18 | 44.65 / 12.96 |
| K40/W2 multimap, eight values/key | 8.53 / 2.78 | 11.95 / 3.60 | 10.92 / 3.30 |
| K64/W64 multimap, eight values/key | 18.10 / 6.14 | 33.06 / 10.62 | 29.79 / 8.14 |

Units are input keys for sets/maps and input values for multimaps. These are
grouped transfer results, not dependent lookup latency. One-value groups cost
19.82 / 30.59 / 25.55 ns for K40/W2 multimaps and 27.79 / 46.47 / 43.83 ns for
K64/W64 (Zen/V2/Granite). Grouping removes substantial redundant work but remains
well behind Abseil. The efficient baseline's direct traversal is not a resumable
Worker implementation.

Memory is a separate result. On x86, the destination K8 set uses 304 logical B
versus Abseil 545 B; the fixed K40/W2 map uses 55,592 versus 34,760 B, or 16,381 B for
exact-key Abseil. Wide maps use 245,314 versus 149,112 B. K40/W2 multimaps use 114,352
versus 83,816 B; K64/W64 use 1,957,728 versus 1,214,024 B. The source/destination
coexistence columns expose the additional live owners. These are logical bytes,
not Pool metadata, peaks or isolated RSS. The map capacity deficit is a reason to
change the destination plan before interpreting any small merge-loop tuning.

The existing fast spill/external mutation settings were **off during source
construction** in this first control. Grouped merge has its own descriptor patch
path, but the complete multimap lifecycle cannot stand in for the best integrated
candidate. The paired `patches` panel compares those already-qualified
construction bodies with the original control. The phase timers keep build,
merge, complete scan and release separate. In the core panel, K40/W2 map complete
lifetime is 42.05/61.38/58.47 microseconds versus Abseil 69.97/87.29/82.50; it wins
despite slower merge and excessive destination bytes. K8 set lifetime wins on
x86 (3.55 versus 4.67 microseconds Zen; 4.83 versus 5.41 Granite) and loses on V2
(5.94 versus 5.48). A merge-throughput headline alone would miss those trade-offs.
The compact-list body does not use those hierarchy patch settings; compact
histories, including all K8 histories, are deliberately unchanged controls.

That paired panel is now complete on all three native hosts, with six checks and
4,800 timing rows per host. In the same half-overlap/eight-value/grain64 hierarchy
case, source construction and complete lifetime improve as follows. Times are
microseconds for the complete history, not per value:

| Case / target | Build, control → patched | Lifetime, control → patched | Matched Abseil lifetime |
| --- | ---: | ---: | ---: |
| K40/W2, Zen | 321.28 → 212.18 | 501.16 → 377.84 | 191.82 |
| K40/W2, V2 | 720.73 → 308.08 | 957.63 → 543.88 | 292.76 |
| K40/W2, Granite | 367.88 → 263.69 | 579.28 → 473.78 | 269.37 |
| K64/W64, Zen | 550.68 → 465.17 | 1,150.20 → 1,067.01 | 749.41 |
| K64/W64, V2 | 1,255.54 → 896.81 | 2,311.81 → 1,943.20 | 1,223.72 |
| K64/W64, Granite | 755.22 → 609.48 | 1,687.71 → 1,543.02 | 929.68 |

Logical bytes are unchanged. The patched general-merge rates remain 8.49/11.87/
10.82 ns/input value for K40/W2 and 18.05/32.10/30.12 for K64/W64 (Zen/V2/Granite),
against Abseil 2.75/3.60/3.32 and 6.13/10.71/8.28 respectively. The construction
repair improves the complete candidate but does not resolve merge or memory.
Variations in unchanged compact bodies and baselines remain in the raw results;
they must not be attributed to these hierarchy patches.
[Zen](evidence/consolidation-patches-zen5/artifact.json),
[V2](evidence/consolidation-patches-neon/artifact.json) and
[Granite](evidence/consolidation-patches-granite/artifact.json) retain both settings
and both baselines. The correction reinforces the need to assemble qualified
components into one coherent candidate before selecting formats.

[Zen evidence](evidence/consolidation-zen5/artifact.json),
[V2 evidence](evidence/consolidation-neon/artifact.json) and
[Granite evidence](evidence/consolidation-granite/artifact.json) retain every
baseline, overlap, grain and fanout. Their report recipes reproduce the full
tables; full source/binaries/disassembly remain in the verified archives. The
52-case release panel and four ASan/UBSan boundaries pass, including actual
allocation pressure, exact-copy guard pages and source-Pool retirement. Codegen
inspection finds no outlined source-scan call in any of the 18 native binaries.
This is not a profile or proof that other outlined cold paths are free.

Qualification and source receipts are recorded in STATUS.md. The initial compile
failed on harness key conversion and a nonexistent small-owner validator; it
provided no timings. Matching-seed transfer is measured below. Coupled destination
sizing, remote Worker lifetime and ObjectStore publication remain open.

## Matching primary permutations

The new comparison adds two arms with identical source/destination seeds:
`grouped_same_seed` still decodes/re-encodes every key, while
`encoded_same_seed` carries primary bucket, fingerprint and suffix facts.
Comparing these two isolates the transfer algorithm from changed placement.
The different-seed controls and both efficient Abseil baselines remain present.

The common record scan supplies its key interpretation to the composition;
there is no second traversal implementation. The destination normalizes encoded
or full-key input into one mutation body. A primary hit or insertion with no
downstream search avoids key reconstruction. Full-key reconstruction and secondary
hashing are lazy and cached within an attempted mutation. Source secondary and
overflow records keep the general full-key fallback. Existing descendants must
still be searched after deletions create room in the primary bucket.

Admission requires the same key/value domain, kind and primary permutation.
Bucket byte units, inline budget, secondary geometry and secondary seed may
differ: the source interprets its own secondary records, and the destination
routes their complete keys. A checked cross-layout transfer changes all four
while preserving complete bytes and bag multiplicity. The encoded fact is a
temporary 32-byte value, not a retained pointer, persisted descriptor or
transferable bag reference. Saving hash work need not outweigh its temporary
state/control cost on every width or platform. Selection remains measured.

Checks include matching and mismatching mapping admission, deliberately crowded
primary/secondary/overflow source tiers, one-unit budgets, refusal/retry, exact
per-key accepted multiplicity, independent full-byte results and source-Pool
retirement. Allocation-pressure recovery also runs through the encoded path.
The ordinary migration and packet consumers exercise the shared scan/mutation
changes separately. Native qualification and its receipts are recorded in STATUS.

The first codegen control left seven outlined general-source scan calls in three
local binaries; fusion was restored before the native comparison. A subsequent
inspection found two instantiated encoded/general mutation bodies and per-record
`accept` calls. The next control normalizes the facts into one body and fuses
the forwarding layer into the budgeted scan. Those are implementation changes
to measure, not claims of algorithmic superiority or excuses for excess capacity.

The following ready-row control removes another unnecessary boundary: sets and
maps commit in the record scan's callback instead of copying a pending row and
returning to a second consume stage. They retain only the record-scan position.
Multimaps still need an owned partial-bag continuation. This also restores a
contiguous per-bucket scan within each budgeted visit. One record now consumes
one work unit for sets/maps; an equal numeric budget is not an equal scheduling
quantum to the old two-stage control. Complete ns/input comparisons keep logical
work fixed. Actual Worker fairness/latency still needs its own budget selection.

This is a composition lesson beyond merge: retain state only for an operation
that can actually be partial across suspension. Fixed-size ready operations can
share a bounded caller interface with variable-size operations without sharing
the latter's pending-row copies or internal stages. The source must remain
immutable, and refusal must leave the row unconsumed, for that fusion to be valid.

The ready-row comparison completes on all three native hosts. At half overlap,
grain64 and1,024keys/source (eight values/key for multimaps), each cell below is
**same-seed general / same-seed encoded / ordinary Abseil**, in ns/input:

| Case | Zen5 | Neoverse V2 | Granite Rapids |
| --- | ---: | ---: | ---: |
| K40 set | 7.16 / 12.53 / 10.73 | 12.55 / 11.39 / 12.25 | 10.07 / 16.28 / 12.23 |
| K40/W2 map | 8.75 / 14.17 / 11.04 | 13.90 / 12.69 / 12.71 | 10.84 / 18.77 / 12.52 |
| K64/W64 map | 19.15 / 23.40 / 11.04 | 32.46 / 31.03 / 14.14 | 29.69 / 33.38 / 13.04 |
| K40/W2 multimap | 8.41 / 8.32 / 2.78 | 12.05 / 12.32 / 3.61 | 10.80 / 10.88 / 3.34 |
| K64/W64 multimap | 17.91 / 17.26 / 6.37 | 36.75 / 35.12 / 11.32 | 29.56 / 29.54 / 8.57 |

Ready-row fusion is useful; the encoded representation does **not** earn universal
selection. The local ARM diagnostic would have predicted the wrong x86 choice.
The native set/map bodies have no outlined scan/accept forwarding calls. The
encoded Zen map cursor is still larger and contains inverse-permutation helper
calls on downstream paths; multimap scan wrappers remain outlined. Those codegen
observations are not a profile attributing the full timing difference. They do
not justify another broad architecture decision from the number of hash operations.

Logical memory remains the fixed-geometry deficit above: K40 set/map55,592B,
same-seed wide map242,962B and eight-value multimaps114,352/1,957,728B on x86.
No lookup-latency improvement is implied by these merge timings. The scoped cold
read results in STAGED.md remain a distinct strength. All52domain/width checks,
four shared consumers and four sanitizer boundaries pass for this source.
[Zen](evidence/consolidation-ready-zen5/artifact.json),
[V2](evidence/consolidation-ready-neon/artifact.json) and
[Granite](evidence/consolidation-ready-granite/artifact.json) retain all six arms,
both orders, small/large tables, overlaps, fanouts and budgets. The first encoded
control is a separate negative result; it must not be conflated with this source.

The fresh-destination, three-Pool comparison also must not become the only worker
merge strategy. A scratch reduction can keep one worker as the destination and
fold other immutable worker tables into it, retaining that worker's original
Pool/Lane authority. That avoids copying the first source and creating a third
owner. The matching Abseil comparison must receive the same opportunity.
Disjoint routing additionally supplies a real absence proof; overlapping
aggregation needs duplicate combination. These are distinct candidates for the
actual Worker consumer, alongside out-of-place construction for retained readers.

The next paired comparison adds `keep_left` alongside `fresh` for **every** arm.
It folds only the second source into the first, with no new destination table and
no transfer of Pool authority. Both contenders retain their first table's existing
capacity and pay any growth needed by the merge. The unchanged source is retired
before further destination reuse in dedicated checks. Refusal/retry must neither
repeat already combined values nor lose a partial bag. This is a private-destination
operation; an old immutable reader of the destination would require preparation
of a writable version first.

`merge_ns_per_input` divides by values/keys actually transferred: one source for
`keep_left`, two for `fresh`. Compare total merge and lifetime ns to assess work
elimination. The kept fold also has a different hit/insertion mix because its
destination starts populated; a higher ns/transferred-input can coexist with less
total work. Ready coexistence counts two owners rather than three; it is still
not a peak-RSS measurement. The fixture retains the same Lane/Pool contexts
outside phase timing, so this comparison cannot claim removal of Worker/context
setup cost. Same-seed arms hold placement constant between modes. Different-seed
fresh and kept destinations have different seeds and are separate general controls.

The native fold comparison completes on all three hosts:14processes,
10,560timing rows and seven boundary-check executables per host. Across the88
same-seed general-transfer workload cells, keeping the first owner improves
complete lifetime by more than5% in every cell. Median kept/fresh lifetime is
0.815 on Zen5,0.828 on V2 and0.801 on Granite. This supports keeping an existing
**private** owner when its format/capacity remain suitable. It is not permission
to mutate a published retained version, and Abseil also benefits from this policy.

Half-overlap/grain64 results below keep the general-transfer implementation fixed;
all numbers are absolute ns. There are1,024keys/source, and multimap bags have
eight values/key. The merge column is total merge work, not per-input latency.

| Case / target | Our merge, fresh → kept | Our lifetime, fresh → kept | Abseil kept merge / lifetime |
| --- | ---: | ---: | ---: |
| K40/W2 map, Zen5 | 17,751 → 9,649 | 32,757 → 24,147 | 13,061 / 58,731 |
| K40/W2 map, V2 | 27,689 → 15,702 | 46,812 → 34,249 | 15,668 / 74,976 |
| K40/W2 map, Granite | 23,932 → 12,248 | 44,729 → 32,153 | 15,161 / 70,088 |
| K64/W64 map, Zen5 | 39,418 → 22,315 | 96,582 → 76,472 | 13,366 / 102,959 |
| K40/W2 multimap, Zen5 | 136,042 → 72,163 | 369,790 → 302,842 | 28,329 / 168,303 |
| K64/W64 multimap, Zen5 | 284,070 → 137,804 | 1,050,312 → 919,720 | 59,129 / 680,654 |

Memory improves through fewer coexisting owners, not a denser destination.
The K40/W2 map's ready coexistence falls166,776→111,184B, while its destination
stays55,592B; ordinary Abseil kept coexistence is69,520B on x86. K64/W64multimap
coexistence falls4,009,120→2,983,424B, while its destination remains1,957,728B;
Abseil kept coexistence is1,903,760B. These are logical inventories, not peakRSS.
The128-key K8 set remains a useful small-table contrast: kept Zen lifetime is
2,514ns versus Abseil3,576ns, destination304versus545B; on V2 its lifetime is
4,428versus4,220ns, despite the memory saving. Keep that small absolute lifetime
loss visible alongside the larger-container improvements. These histories do
not measure dependent point-lookup latency.

[Zen](evidence/consolidation-fold-zen5/artifact.json),
[V2](evidence/consolidation-fold-neon/artifact.json) and
[Granite](evidence/consolidation-fold-granite/artifact.json) retain both destination
policies, all six transfer/baseline arms and every measured fanout/overlap/grain.
All52domain/width cases, four shared consumers and four sanitizer boundaries
with timed-history oracles pass. The next material choice is destination geometry
and growth policy; wide-map and multimap transfer/storage losses still need work.
