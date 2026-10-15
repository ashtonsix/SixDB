# Whole-table list growth and owner cost

A whole-table list avoids allocating an underfilled bucket hierarchy. Its packed
planes contain fingerprints, suffix bytes and values/tags, while the caller owns
count, seed and Orbital allocation. The question here is whether private growth
should extend that Allocation or replace it, at identical capacities and layout.
This does not select the list-to-hierarchy threshold or the entire small-table
representation.

## Growth contract and comparison

Replacement allocates and initializes another packed list, copies its occupied
planes, then retires the old allocation. The private-grow control calls Orbital's
existing `grow`, rebinds, initializes newly exposed bytes and moves planes from
last to first with `memmove`. Occupied ranks and key mapping stay unchanged.
There is no allocator wrapper or altered reservation policy.

Growing in place can overwrite old initialized bytes. Before `grow` can retire
the old identity/address, the observer receives the complete old initialized
span. A failed grow can leave that conservative observation but must leave all
logical bytes unchanged. Effect capacity is admitted beforehand. This requires
exclusive private storage; a Hold preserves lifetime/address, not permission to
modify published bytes. Retained published versions use independent cloned bodies.

Independent checks cover all eight domains, sets, wide maps/multimaps, all
capacity boundaries, overlapping plane moves, Held relocation refusal followed
by retry, old-byte observation, cloning, erase/reuse and effect refusal.
All-domain ObjectStore retained-publication and list-to-hierarchy checks pass.
The growth/refusal/clone check also passes ASan+UBSan with always-on stack
use-after-return instrumentation against the same frozen source.
These are full-copy publication controls, not a byte-damage COW implementation.

The previous small-table harness used a counted Abseil allocator. The current
`small_batch.cpp` uses ordinary uninstrumented Abseil with ordinary/exact-width
keys, and inventories storage outside timing. Each timed sample repeats 64 whole
construct/build/write/read/destruct histories. It reports nanoseconds per
complete history, not ns per request or serial latency. This measures warm
allocator reuse. The independent oracle and per-lane checks stay outside timing;
complete timed answers/counts are checked afterwards.

Native V2 and Zen5 each complete 32 processes, 32,256 measured rows, all eight
key widths, sets/maps/multimaps, scalar/4/8 operations, active masks and ordered
duplicates. Seven repetitions in each process order retain both baselines.
All 17 behavior checks pass. All 432 complete read roots per host have no
ordinary calls; packet roots retain the cold `fprintf`/`abort` assertion path.
Source manifest: `4fe250bfc6d4d7adfb5b6c1b450b6f2881ecffa9bb8c54674755b8b78fc38ae0`.
The allocator is held at the same pre-repair bytes for this policy attribution.

## Results

Median complete-lifetime ns across both process orders (each order first takes
its seven-repetition median); ordinary Abseil uses the grow-control process's
matched sample. The initial population is followed by sixteen absent-key inserts
and the same duplicate/masked writes and read trace. An initial population of one
is therefore not a singleton-only lifetime. Multimap rows below use the ordinary
initial singleton history, unless marked extended.

| Host / K / kind / initial keys / packet | Replacement ns | Private grow ns | Ordinary Abseil ns |
| --- | ---: | ---: | ---: |
| Zen5 / 8 / set / 1 / 1 | 650.00 | 448.45 | 653.12 |
| Zen5 / 40 / set / 16 / 1 | 1,098.22 | 897.59 | 846.42 |
| Zen5 / 40 / map / 128 / 1 | 3,907.16 | 3,552.77 | 2,817.61 |
| Zen5 / 40 / map / 240 / 1 | 7,547.59 | 7,194.29 | 4,485.91 |
| Zen5 / 40 / multimap / 128 / 1 | 8,941.73 | 8,584.62 | 5,091.46 |
| Zen5 / 64 / map / 128 / 8 | 3,670.58 | 3,326.59 | 2,675.88 |
| V2 / 8 / set / 1 / 1 | 1,308.76 | 945.20 | 904.67 |
| V2 / 40 / set / 16 / 1 | 1,848.66 | 1,492.10 | 1,182.96 |
| V2 / 40 / map / 128 / 1 | 6,845.24 | 6,393.75 | 4,191.49 |
| V2 / 40 / map / 240 / 1 | 14,113.25 | 13,732.39 | 6,655.15 |
| V2 / 40 / multimap / 128 / 1 | 16,831.96 | 16,330.11 | 7,735.63 |
| V2 / 64 / map / 128 / 8 | 7,780.67 | 7,357.55 | 4,094.70 |

Private growth improves more than 5% in 78/128 scalar histories on Zen5 and
69/128 on V2, with no regression above 5% in either. For N8 the counts are 89/128
and 85/128, again with no regression above 5%. N4 has three Zen5 regressions above
5%, none on V2. These are finite measured histories, not an all-input guarantee.
Select private growth for the exclusively owned list. Granite qualification
below confirms the broad benefit while preserving its negative cases. This
does not fix large-list scanning or bag-storage cost.

Stored requested bytes, current allocation capacity and owner metadata are
unchanged. For example K40 sets/maps with 128 initial keys use 728/1,752 logical
bytes including their 88-byte owner, versus ordinary Abseil 2,336/4,376 B on
Zen5 and 2,328/4,368 B on V2. Tiny tables still pay that owner and an allocation. Abseil
backing allocation size and process RSS are not inferred from requested bytes.
The retained raw histories include initial and final inventory; complete allocator
high-water or transient peak memory is not measured by this panel.

[Zen5 evidence](evidence/small-growth-zen5/panel/summary.csv) and
[V2 evidence](evidence/small-growth-neon/panel/summary.csv) retain complete
comparisons, including larger-list and multimap losses. Their artifact references
recover source, binaries, assembly, all repetitions and command contexts.

## Owner-state control

`compact_state.cpp` revives a pre-existing isolated owner-state experiment rather
than adding a second design. It removes unused seed facts and derives set/map
value counts. Its full/compact copies share operations; the current `small_table`
is an additional bridge control, not an identical implementation of those copies.
All eight domains, every legal hash address width, independent inverse/value
oracles, retained clones and refusal checks now pass locally.

In the local ABI, set/map owners shrink from 88 to 48 B for K8 and to 64 B for
wider domains; multimap owners shrink from 96 to 64/80 B. Packed list bytes and
allocation policy do not change. Ordinary Abseil's owner remains 16 B. Local
lifetimes show no consistent speedup and retain significant singleton allocation
cost. Native checks and arrays-of-owners measurements now confirm the reductions
and the limited timing effect below. This is a memory/composition lead, not a
selected production owner or hash family.


The native compact-state jobs `20261015T014651Z-ee4c3901` (Zen5) and
`20261015T014651Z-3cfc928b` (V2) each pass all eight checks and produce 30,240
rows: all key widths/kinds, 0/1/4/16/64 keys, arrays of 1/64 owners, five arms,
nine repetitions and both key-process orders. Source manifest
`50f17b914d859121099695c9ac1791f52b5eddb856054652ece2650555dd40c1`.
All 120 serial consumer roots per host fuse lookup; remaining calls are clocks
and cold failure reporting. No alternative hash mapping is introduced.

| Host / K40 case | Full-state copy | Compact-state copy | Ordinary Abseil | Unit |
| --- | ---: | ---: | ---: | --- |
| Zen5 singleton set, 64 owners | 89.06 | 87.81 | 10.16 | ns/owner lifetime |
| V2 singleton set, 64 owners | 148.39 | 146.73 | 17.39 | ns/owner lifetime |
| Zen5 16-key map, 64 owners | 773.60 | 782.91 | 470.63 | ns/owner lifetime |
| V2 16-key map, 64 owners | 1,353.80 | 1,320.18 | 699.06 | ns/owner lifetime |
| Zen5 64-key map, dependent reads | 5.864 | 5.840 | 6.094 | ns/lookup |
| V2 64-key map, dependent reads | 10.419 | 10.417 | 9.482 | ns/lookup |
| Zen5 16-key multimap, 64 owners | 3,606.30 | 3,551.99 | 2,053.86 | ns/owner lifetime |
| V2 16-key multimap, 64 owners | 6,935.98 | 6,892.31 | 2,757.06 | ns/owner lifetime |

Lifetimes include build, two update sweeps, hit/miss consumption and release;
multimaps start with three values per key and retain all duplicates. Dependent
reads use the pre-update answer graph. These are different workloads from the
batch-growth panel above and must not be divided into each other.

Against the copied full-state control, median serial ratios are 0.999 on Zen5
and 1.000 on V2 across 96 cases; each host has one >5% regression. Arrays-of-64
lifetimes have median ratios 0.990/0.989, with 28/21 improvements and 2/0
regressions above 5% among 120 cases. This supports deleting redundant stored
state as a memory simplification; it does not justify treating it as the remedy
for tiny-table performance. The original-owner bridge is retained in the
[Zen5](evidence/compact-state-zen5/artifact.json) and
[V2](evidence/compact-state-neon/artifact.json) recoverable evidence, alongside
ordinary and exact-width Abseil, every repetition and disassembly.

## Integration into the real owner

The real `small_table` now stores the compact seed representation, derives set/map
value counts and selects private list growth. Packet execution binds transient
full hash facts, preserving the exact existing mapping and explicit SIMD lowering.
The alternate selector experiment retains its original state. The copied owner
prototype stays a diagnostic control; it is not a second implementation to graduate.

`small-integrated-local` passes 23 checks: all-domain compact mapping/inversion/
packet boundaries, complete growth/refusal/retained bytes, ObjectStore publication
and list promotion, plus K8/40/64 packet owners under both state and growth controls.
Its 24-process lifetime panel covers seven repetitions in both process orders and
64 histories per timer. Owner metadata reductions are present in complete tables,
including multimap bag owners. Local timing has no consistent broad speed claim;
these OrbStack diagnostics do not replace native qualification. The frozen source
manifest is `bba3b80460f9f3bcddfaf37736b2d64909b7d480143b65e7354489995234b46b`.

Native all-domain comparisons of the integrated owner against full-state and
replacement-growth controls complete on Zen5 `20261015T021815Z-1c6b3207`,
V2 `20261015T021815Z-370d1196`, and Granite `20261015T021815Z-3f766a57`.
Each completes 64 processes, 64,512 raw rows, 33 checks and 864 complete read
roots with only cold failure calls. The native source is the integrated owner
capture above, preceding the later opaque-combiner change.

Compact/private growth versus full-state/replacement has a median complete-history
ratio of 0.931 on Zen5, 0.929 on V2 and 0.931 on Granite, across 384 matched cases
per host. More than 5% improvements occur in 240/264/242 cases; regressions in
5/0/3. Compact state alone, holding private growth fixed, has median ratios
0.998/0.997/1.002, with 17/1/31 regressions above 5%. It remains a memory
simplification, not an independent broad speedup. Keep the negative cases.

Complete lifetime ns, medians across process orders:

| Host / K / kind / initial keys / packet | Old full/replacement | Full/private grow | Compact/private grow | Ordinary Abseil |
| --- | ---: | ---: | ---: | ---: |
| Zen5 / 8 / set / 1 / 1 | 665.71 | 451.95 | 509.07 | 664.77 |
| V2 / 8 / set / 1 / 1 | 1,303.44 | 936.30 | 952.46 | 903.54 |
| Granite / 8 / set / 1 / 1 | 1,027.66 | 685.70 | 722.57 | 897.07 |
| Zen5 / 40 / map / 128 / 1 | 3,780.83 | 3,508.78 | 3,587.54 | 2,910.66 |
| V2 / 40 / map / 128 / 1 | 6,871.91 | 6,409.16 | 6,510.84 | 4,198.91 |
| Granite / 40 / map / 128 / 1 | 5,025.10 | 4,986.11 | 4,919.38 | 3,715.09 |
| Zen5 / 40 / multimap / 240 / 1 | 15,373.83 | 15,411.80 | 14,710.45 | 8,513.20 |
| V2 / 40 / multimap / 240 / 1 | 28,278.26 | 28,259.18 | 27,906.62 | 12,243.41 |
| Granite / 40 / multimap / 240 / 1 | 22,564.61 | 22,818.15 | 20,058.55 | 11,097.39 |

As above, these histories add sixteen keys after the initial population; the
one-key row is not a singleton-only lifetime. Logical/backing bytes retain their
separate inventories. [Zen5](evidence/small-integrated-zen5/panel/summary.csv),
[V2](evidence/small-integrated-neon/panel/summary.csv) and
[Granite](evidence/small-integrated-granite/panel/summary.csv) preserve every
matched median/range, with all raw repetitions, codegen and sources recoverable.


Granite AVX-512 `20261015T014737Z-8580964c` completes the same 32 processes,
32,256 rows, 17 behavior checks and 432 fused read roots. Private growth improves
more than 5% in 83/128 scalar histories, with three regressions; N4 improves
71 with none, and N8 improves 86 with none. The scalar median grow/replacement
ratio is 0.919. These exceptions remain in the
[complete comparison](evidence/small-growth-granite/panel/summary.csv).
For K40/128-key maps, scalar complete lifetime is 5,383.27→4,550.23 ns, versus
Abseil 3,679.98; the corresponding multimap is 11,433.27→11,493.66 versus
7,047.41. For K8 sets starting at one key and ending at seventeen, it is
1,003.95→734.16 versus916.45. The direction earns adoption as a growth policy,
with no claim that it repairs the representation's other losses or that every
history improves.
