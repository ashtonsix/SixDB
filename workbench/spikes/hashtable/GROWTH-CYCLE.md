# Capacity through actual growth

> The original V5/V6 lifetime timings remain withheld after the [validation correction](BENCHMARK-VALIDATION.md). Corrected native campaigns now complete on all three hosts; the section below owns their conclusions.

A good fresh-table byte count does not select a format. Larger growth factors
save migrations but leave more empty capacity; the same allocation can win near
the end of its growth cycle and lose near the beginning. This comparison uses
the capacity family's actual packed owners and bounded migration, beginning at
an empty compact list. It is a single-writer attribution experiment; the Worker
coordinator and retained-version mutations have separate comparisons.

The caller admits at most 64 pending updates. The compact list promotes only
when insertion of an actual new key would exceed 256; existing-key updates and
multimap duplicates stay compact. A capacity refusal on an existing key is not
misinterpreted as promotion. Hierarchy growth uses current occupancy plus the
pending upper bound. The owner receives no final cardinality. K8 stays in the
compact list. Larger domains promote into at least 512 primary slots,
then double or quadruple bucket count. Medium/large slot families solve their
primary and secondary units again at each level. The first comparison uses a
90% primary-capacity trigger; 80% and 100% controls expose the trade-off with
linked overflow. These are experimental controls, not production presets.

Sets, maps and multimaps use the same transition. Multimap histories include
singleton and four-value keys; pending duplicates can make hierarchy admission
conservative. The first V2/V4 fixture also used that bound at the list boundary;
the corrected fixture preserves the whole <=256-key regime instead.
Each new epoch receives a fresh seed pair from the caller's deterministic test
fixture. Migration reencodes keys and transfers bag ownership only at finish.
The measured path requires exclusive private ownership; it does not authorize
mutating a published version. Runtime format admission remains unresolved: these
compile-time levels keep the experiment bounded and are not a proposed production
owner with one alternative per possible growth level.

Ordinary Abseil and exact-width-key Abseil grow naturally beside the candidate.
Values are opaque byte arrays on both sides, including the exact-key baseline;
this can avoid padding present in earlier integer-valued baseline rows. Both
baselines use ordinary allocators and `InlinedVector<value,1>` for multimaps.

Construction, the migration subset, release, independent-read reciprocal cost
and dependent-read serial cost are separate columns. Reads consume every value
byte; dependent traversal checks its result against an independent successor
cycle. The caller key/digest/successor arrays are charged separately. The local
fixture is warm and does not establish any native cache-residence claim.

An untimed replay records logical/backing/owner bytes after every chunk and
before/after transitions, plus coexistence of source and replacement at the end
of each index migration. Timed runs must reproduce the same migration and scan
counts and satisfy the complete-byte/multiplicity oracle. Sample-weighted mean
bytes are an observation at chunk endpoints, not an integral of every allocation.
`observed_coexist_*` includes observed index coexistence, not a complete peak:
short-lived list/bag allocation overlap and allocator retained storage are not
captured. Abseil's internal rehash coexistence and backing are unknown (`-1`).
Neither measure is RSS. The recursive fixture also reports the peak simultaneous
C++ control-object sizes separately; these are not measured stack frame bytes or
a proposal for persistent owner storage.

The first local build measured narrow maps before building the other kinds.
It already shows the expected memory trade-off: at 6,144 K40/W2 entries, doubling
the medium family finishes at 55,788 logical bytes, versus 108,840 after fourfold
growth, ordinary Abseil 139,280 and the packed-key baseline 65,560. This is a
useful lifetime/format question, not evidence for choosing a threshold yet.
Native qualification, trigger controls, long-lived churn, retained versions and
the actual Worker merge remain part of the broader task.

The local threshold panel completes588processes/7,317timing rows across three
fixture seeds and three larger populations. The corrected compact boundary
then completes102processes/1,242rows, including240/256/257keys across all eight
domains. Four boundary families under ASan/UBSan/always-stack-UAR complete14
processes/66rows. At K40/W2 multimap fanout4,240 and256 keys remain compact at
5,712 logical bytes;257 keys migrate once. Full-value/multiplicity oracles and
independent sampled-memory reports pass throughout. These local timings have
material run-to-run variability; use native qualification for performance policy.

Native source V6 is `growth-cycle-source-20261015-v6`, digest
`079fdb09572e2b053d99c4a71bf7f87287f10f460e8d07e96125997fe47ff0c2`.
Its static audit identifies the complete timed read function for both baselines
and each candidate level. Six local case families pass with no forwarding calls
inside those consumers, alongside72timing rows. The one measurement-function
call is outside its timers. This is not a dynamic profile or mutation-call audit.

## Corrected native growth comparison

The allocation-free fixture completes303processes/6,300rows/1,260qualifiedcells
and34complete-read static audits on each host. Three seeds,3072/6144/7680keys,
medium/large slot families, doubling/quadrupling,90%default and targeted80/100%
controls, plus all-eight-domain240/256/257 boundary checks. Source
242cdd4066f58c9b21aa8b90adfc0f502a008ed2908bfceadef2d824ec84bb16.
Zen20261015T070834Z-726ff8dc, V2...-b79e0606, Granite20261015T071241Z-5a06e088.
`evidence/growth-cycle-{zen5,neon,granite}` keeps summaries, inputs and complete
source/binary/raw recovery. The failed initial Granite launch used an invalid
CMake tune name and measured nothing; its corrected retry uses the same source.

**Advance with doubling and90% as the working growth choice.** Quadrupling can
reduce migration work, but its retained empty capacity defeats the memory
objective in important phases. Growing at80% can double bytes before necessary;
waiting until100% incurs extra crowded insertion cost without reducing final
capacity in the larger populations. This chooses the next integrated candidate;
long churn and adversarial distributions still need their own evidence.

K40/W2 map,6144keys,90%primary-capacity trigger. Costs are median across three
seed medians, five repeats per seed. Read columns are ns/query; build includes
promotion and reencoding, release is ns/key. Bytes include reported owner
metadata, not total backing/RSS. Both Abseil arms grow naturally.

| Host | Arm | Build ns/key | Release ns/key | Independent hit ns | Dependent hit ns | Final logical B | Chunk-sampled mean B |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Zen5 | medium/double | 13.27 | 0.05 | 4.42 | 15.32 | 55,788 | 35,537 |
| Zen5 | large/double | 12.28 | 0.02 | 4.24 | 15.77 | 49,960 | 33,572 |
| Zen5 | medium/quadruple | 10.56 | 0.02 | 4.17 | 15.81 | 108,840 | 60,560 |
| Zen5 | Abseil | 13.00 | 1.25 | 2.71 | 13.53 | 139,288 | 85,183 |
| Zen5 | exact-key Abseil | 9.80 | 0.02 | 7.59 | 13.28 | 65,568 | 40,107 |
| V2 | medium/double | 19.60 | 0.10 | 5.53 | 21.89 | 55,788 | 35,537 |
| V2 | large/double | 22.16 | 0.08 | 8.33 | 21.20 | 49,960 | 33,572 |
| V2 | medium/quadruple | 16.52 | 0.07 | 5.40 | 23.14 | 108,840 | 60,560 |
| V2 | Abseil | 21.02 | 2.88 | 3.25 | 17.63 | 139,280 | 85,175 |
| V2 | exact-key Abseil | 16.06 | 0.06 | 3.73 | 18.14 | 65,560 | 40,099 |
| Granite | medium/double | 15.76 | 0.08 | 5.75 | 23.35 | 55,788 | 35,537 |
| Granite | large/double | 15.50 | 0.04 | 5.79 | 24.41 | 49,960 | 33,572 |
| Granite | medium/quadruple | 13.32 | 0.04 | 5.42 | 24.44 | 108,840 | 60,560 |
| Granite | Abseil | 16.51 | 1.61 | 4.36 | 20.54 | 139,288 | 85,183 |
| Granite | exact-key Abseil | 11.93 | 0.04 | 11.49 | 21.09 | 65,568 | 40,107 |

The28-slot family is useful on x86 narrow containers; V2 usually favors the
14-slot family for complete operations. A single bucket family does not win
all kinds/widths. For wide maps, the cold located-value result in READ-WINDOWS.md
matters more than a small warm-hit difference here. Keep that execution choice
paired with the selected stored format rather than transplanting the fastest
number from a different cohort.

The full summary retains substantial negatives. At6144singleton K64/W64 multimap
keys, medium/double needs1,763,218logical B versus663,512 on x86 Abseil, and its
build is77.59vs37.80ns/key on Zen5,82.01vs53.60 on Granite,123.54vs67.54 on V2.
This control has no inline W64 value: changing the inline budget and corresponding
bucket capacity is a structural next comparison, not a reason to optimize the
external singleton chain in isolation. All three hosts retain narrow set/map
throughput losses beside their memory and selected build wins.
