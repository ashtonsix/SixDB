# Borrowed inline values versus external slot IDs

These measurements describe the original research candidate. Its borrowed-inline
read API is [promoted into Ikea](GRADUATION.md) without changing the layout or existing
getter; slot-ID-backed values remain research. Promotion qualification is separate
from these captured timings.
The [scorecard](evidence/zen5/tables.md) retains every tested implementation and
all hit/miss patterns for its selected panels; [individual repetitions](evidence/zen5/)
and the linked original captures support the numbers.

## The necessary correction to the first cold comparison

The first slot adapter prefetched the bucket home, matched the key, loaded its
ID synchronously, then prefetched/yielded before consuming the external value.
That leaves a cold dependent ID load on the critical path. Those numbers cannot
answer whether properly staged slot lookup is competitive.

The corrected path is:

```
match key -> prefetch ID cell -> yield
          -> load ID, prefetch payload -> yield
          -> consume actual payload
```

`Model::locate_id` returns the retained address of the ID cell without reading
it. `resolve_id` reads it in the next visit. Both the manual pipeline and the
actual native Loom Ring use sixteen outstanding queries; both include the
complete consumed byte window and an unaligned final line. Abseil, Boost and
F14 external-ID controls receive exactly the same staging. Their native inline
maps also retain ordinary direct and payload-only deferred consumers.

The old payload-only results remain as a diagnosis of the missing stage, not
as evidence against slot-backed values. The final cold ranking uses the new
paired build, including its ordinary native controls.

## Corrected cold result and recommendation

The corrected W32/V64 run has 2,673,868 entries, three seeds and two repetitions
per seed. Each row consumes all 64 payload bytes. The following are actual
consumer choices from the same captured native build; the complete scorecard
also shows each rejected treatment. Auto remains enabled for Ikea.

| Consumer | Hit | Random50 | Miss |
| --- | --- | --- | --- |
| Ikea getter, direct PF32 | 24.22 | 22.07 | 6.79 |
| Borrowed inline, Manual16 PF32 | 13.42 | 12.78 | 7.95 |
| Borrowed inline, Loom16 PF32 | 15.18 | 15.88 | 9.70 |
| Ikea ID8, original index, three-stage Manual16 | 21.95 | 22.72 | 8.18 |
| Ikea ID8, original index, three-stage Loom16 | 24.38 | 25.66 | 10.32 |
| Ikea ID4, original index, three-stage Loom16 | 33.37 | 34.38 | 22.68 |
| Ikea ID4, less occupied index, three-stage Manual16 | 20.61 | 20.83 | 6.10 |
| Ikea ID4, less occupied index, three-stage Loom16 | 21.08 | 21.68 | 7.23 |
| Abseil inline, native direct | 28.28 | 24.56 | 12.41 |
| Boost inline, native direct | 34.32 | 26.99 | 11.16 |
| F14Fast native, direct PF32 | 31.92 | 24.01 | 6.58 |
| F14Value inline, direct PF32 | 30.02 | 24.03 | 12.29 |

With the original ID8 index, the missing stage improves Loom hits from 42.80
to 24.38 ns. The less occupied ID4 index improves from 40.85 to 21.08 ns. That
ID4 candidate beats all four native peer maps on cold hits here, while using
82.82 backing bytes/live entry. Abseil uses 108.24, Boost 101.57, F14Fast 80.55
and F14Value 95.69. Its random50 result also beats those native controls.

The original ID4 geometry is a poor cold choice despite its smaller footprint.
Its 160-byte bucket stride alternates cache-line alignment; a bucket-home
prefetch can leave part of the key region unprefetched. The less occupied b18
index has 32 rows and a 192-byte stride, restoring cache-line alignment as well
as reducing overflow. Those effects are combined in this capacity experiment;
the evidence does not isolate their individual contributions.

**Use borrowed inline values for the strongest measured wide-value read path.**
Expose their address while retaining the owner, then defer payload consumption
when cold. The three-stage ID option is competitive with the peer maps; it is
useful when independent payload ownership or cheap index growth matters enough
to pay its remaining read cost. Prefer the less occupied, aligned ID4 geometry
at this V64 load point. Keep slot backing an explicit composition choice: it
does not justify replacing all inline maps, and the scalar write results below
remain a separate weakness.

The first16-byte consumer and W40/V64 extension reach the same ordering between
borrowed inline and fully staged Ikea IDs. The full scorecard contains the
32/128-byte cases and all external-ID peer controls. Those peer ID cells are
usually fetched with their colocated keys; adding an ID yield generally helps
them much less than Ikea's separate key and value regions.

The fixed three-stage consumer is unnecessary work for an all-miss stream.
Ordinary direct PF32 remains available and is measured separately: the less
occupied ID4 index misses in 5.09 ns in this paired build. This spike qualifies
the mechanisms; it does not implement an automatic choice of execution depth.

## Hot reads and small values

The existing small-value path stays in place for V <= 8. At W32/b11 and 13,926
entries, predictable full-eight-byte hits are 1.407 ns for the maintained getter
and 1.412 ns for the borrowed candidate's retained getter. Automatic all-hit is
1.462 / 1.465 ns; all-miss is 1.165 / 1.164 ns. Random50 automatic is 3.607 / 3.695
ns in this complete-consumer loop, versus explicit U at 2.327 / 2.360 ns. This
repeats the same baseline and candidate in one build; it does not claim to
reproduce the older consumer's absolute automatic-controller timing.

For 64-byte values at W32/b9, 6,963 entries and 85% primary occupancy, the main
three-seed hot panel is:

| Complete-value consumer | Hit | Random50 | Miss | Alternating50 | Runs64 |
| --- | --- | --- | --- | --- | --- |
| Maintained getter, Auto | 2.83 | 9.59 | 3.63 | 3.10 | 3.42 |
| Borrowed inline, Auto | 2.61 | 6.14 | 3.61 | 2.89 | 3.22 |
| Ikea ID4, Auto | 2.94 | 6.11 | 3.39 | 3.12 | 3.29 |
| Abseil | 2.83 | 11.83 | 9.89 | 6.08 | 6.75 |
| Boost | 2.70 | 6.67 | 1.21 | 2.19 | 2.14 |
| F14Fast | 3.02 | 6.70 | 1.53 | 2.38 | 2.42 |
| F14Value | 2.65 | 6.75 | 1.45 | 2.16 | 2.20 |

Timings throughout are ns/operation, lower is better. The executable consumes
all eight limbs, not merely a returned pointer. The borrowed adapter also makes
a branchless primary-match path available for wide values: the random50 gain is
not attributable solely to eliminating a copy.

A separate fixed-population capacity ablation spends capacity on the ID index
without reserving unused wide payloads. Its W32/b10 ID4 map is 42.5% occupied,
uses 82.86 backing bytes/live entry versus inline's 85.08, and measures 2.45 ns
hits, 4.16 random50, 1.16 misses, 1.93 alternating50 and 1.84 runs64. This is a
useful hot option; a compact ID index need not inherit an inline wide table's
occupancy policy. The paired controls and rejected ID8 option remain in the
scorecard.

## Writes, growth and memory

Borrowing does not alter full insertion, replacement, erase or rebuild. The
exclusive borrowed one-limb update also does not automatically beat `modify`:
the compiler already removes unnecessary copies on successful primary updates.

For the main cold W32/V64 fixture, these are ordinary scalar operations after
cache conditioning, with caller-prepared capacity. No arm uses a staged write
pipeline. Upsert50 alternates fresh and existing outcomes, with shuffled existing
keys; its historical raw `random50` label does not mean randomized outcomes. Replacement writes every payload byte; increment changes one limb.

| Implementation | Replace | Increment8 | Insert | Upsert50 | Erase | Refill | Grow / live entry |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Maintained getter/modify | 35.24 | 37.44 | 48.30 | 47.61 | 26.83 | 47.65 | 55.04 |
| Borrowed inline | 35.00 | 37.29 | 48.82 | 46.76 | 26.48 | 47.48 | 54.94 |
| Ikea ID8 | 47.27 | 49.21 | 68.98 | 79.23 | 64.23 | 50.99 | 13.94 |
| Ikea ID4 | 38.91 | 44.93 | 70.23 | 69.10 | 53.46 | 53.77 | 10.60 |
| Abseil | 38.21 | 36.64 | 43.17 | 50.75 | 53.81 | 53.92 | 60.06 |
| Boost | 40.52 | 37.48 | 37.54 | 48.52 | 41.59 | 36.97 | 47.68 |
| F14Fast | 49.52 | 49.31 | 49.39 | 64.34 | 104.69 | 38.41 | 49.37 |
| F14Value | 46.88 | 46.38 | 52.49 | 57.59 | 63.12 | 50.98 | 57.06 |

The separate value store makes index growth cheap because the payload stays
put. At 128 bytes, cold ID4 growth is 4.59 ns/live entry versus 63.61 for inline
Ikea, 93.24 Abseil, 75.09 Boost and 61.74 F14Fast. This growth does not compact or
relocate the external store; store expansion is separate work. The store grows
geometrically if reservation is exceeded, retaining measured holes/free IDs.

The auxiliary external-ID peer write rows in the full scorecard use `find`
then `emplace` on fresh keys. They diagnose those prototype adapters, not the
best possible peer ID-store write integration. The native inline controls above
use direct existing assignment or `insert_or_assign`.

The scalar write weaknesses are real. At V64, inline insertion trails Boost by
about 30%; the initial slot adapters trail it much further. Prepared insertion
already avoids a second Ikea index lookup, but Ikea slot erase still does an
ID lookup followed by ordinary index erase. The ID/payload read pipeline does
not repair those write paths. At V128, cold inline insert is 90.06 ns versus
38.61 Abseil / 33.93 Boost; ID4 reduces it to 77.63 ns. Slots are not a general
write-performance cure.

| W32 / V64 steady backing | Hot bytes/live | Cold bytes/live |
| --- | --- | --- |
| Ikea inline / borrowed | 85.08 | 81.81 |
| Ikea ID8, original index | 79.08 | 77.19 |
| Ikea ID4, original index | 74.08 | 72.28 |
| Ikea ID4, doubled index bucket count | 82.86 | 82.82 |
| Abseil inline | 81.12 | 108.24 |
| Boost inline | 152.36 | 101.57 |
| F14Fast | 77.41 | 80.55 |
| F14Value | 143.53 | 95.69 |

These are requested backing allocations, including payload capacity and free-ID
storage, not RSS. Owner descriptors, allocator metadata, benchmark fixtures and
Loom setup are excluded. The hot table has 6,963 entries; cold has 2,673,868.
Peer reserve rounding is visible in their different backing costs. The doubled
cold ID4 index is 31.875% occupied rather than 42.5%, because its bucket capacity
also increases from 24 to 32.

Ikea growth retains the original source and constructs a separate image; peer
native rehash releases old backing. The scorecard reports this ownership
difference. Its timings are useful costs of these actual
operations, not a claim that their retention semantics are identical.

## Assembly and interpretation

The selected native disassembly already makes the maintained successful
64-byte primary read a ZMM load, without a mandatory stack round trip. The
borrowed overflow helper returns an address rather than a wide sret result,
letting the compiler retain more table state across the loop. The wide U path
also changes the primary-match branch. These are distinct from the cold benefit
of deferring the actual payload load.

The [native staged callback](evidence/zen5/staged-id8-loom.txt) confirms the
ID load follows the `id_pending` dispatch, followed by payload prefetch and
an actual Ring tail transfer. The key-match arm prefetches the ID cell and sets
that state without loading the ID. The second visit loads it at `0x5e2cf`, then
prefetches the payload at `0x5e2e1` before another tail transfer.

The [selected assembly/MCA inputs](evidence/zen5/mca-input/) retain the literal
primary paths used for inspection. Modeled reciprocal block costs are 7.33 cycles
for materialized P, 6.50 for borrowed P and 9.17 for borrowed U without overflow.
LLVM 21.1.8's `znver5` scheduling model uses a Zen 4 proxy. These are instruction
scheduling models, not measured cache, branch-prediction or MLP costs; the modeled
U block cost does not predict the random50 native result.

## Boundaries and verification

All native panels use Zen 5 EPYC 9R45 / AVX-512, pinned CPU affinity and Clang
21.1.8. They cover W32 values 8/16/32/64/128 and an additional W40/V64 cold map.
The non-8/64 payload widths reuse the maintained V64 encoding profile; this is
not a new per-width encoding search. Warm means a warmed trace. Cold means
CLFLUSHOPT/SFENCE over owned backing before each interval; lookups within the
interval can warm later accesses, so it does not mean every load reaches DRAM.

Every read interval checks hit count, matched keys and actual consumed payload
against generated expected answers. Writes and growth verify final membership
and every payload byte. Independent mutation histories, absent keys, tombstones,
slot reuse, tiny Swiss wrap, sticky overflow counts above 255, W64/bitmap
boundaries and partial pipeline batches have sanitizer coverage. Allocation
accounting must return to zero. Native sanitizer receipts are retained.

Sources are immutable during reads; borrowed ID/value addresses require the
owner to stay retained throughout deferred consumption. No arm pays MVCC costs.
The experiment does not qualify Engine's separate sixteen-entry frozen bitmap
groups or choose their representation. That consumer needs its own small-group
measurement before adopting one of these ordinary-table results.

The main Spot run completed 1,618 cases before interruption. Recovery verified
all completed CSV hashes/arguments and reused the captured original binaries
for the remaining 98 cases on another c8a.xlarge host. Each retained row records
its actual job identity; the final staging panel is a separate paired run on
one host. Original captures retain complete argv, source manifests, generated
F14 configuration, binaries and disassembly. The main matrix completes 1,716
process cases; the payload-only panel 816; the density ablation 351; and the
corrected staging panel 684. The retained selection excludes superseded cold
read variants while keeping their recovery references.

Steady backing is the qualified memory metric. The captured `peak_bytes` field
missed old/new overlap inside Ikea's base-buffer replacement, so it is retained
as raw diagnostic data and excluded from published memory comparisons. The
current adapter fixes that counter and checks 64 -> 128-byte growth against the
192-byte simultaneous-allocation expectation. This accounting repair does not
change the read paths, steady footprints or captured operation timings.
