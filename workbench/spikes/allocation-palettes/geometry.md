# Historical geometry screen

This screen preceded native timing and the selected implementation. Its recommendation
was superseded by the [native findings](README.md); the logical comparisons below
explain why backing alone was insufficient to select a palette.
The [descriptor study](../memory-descriptors/README.md) explains the addressing
scheme; [finite.cpp](../../../orbital/src/allocation/finite.cpp) owns current code.

## Geometry-only recommendation (superseded)

**Keep A/B as the general starting point on both page sizes. Add a narrow upper
palette for clustered reservation sizes; do not replace the whole range with 32
classes by default.** A/B remain the existing tables in [run.py](run.py).
The useful new table, `upper15`, is:

```text
1280 1408 1536 1664 1792 1920 2048 2176
2304 2432 2560 2688 2816 2944 3072
```

It keeps 15 classes, 16 homes per class and 16 identities per grid. A 2,050-byte
request reserves 2,176 rather than B's 2,560 bytes. Do not use its 1,280-byte
minimum for ordinary tiny requests. Preserve A's existing tiny-class packing:
on 64 KiB pages its five smallest complete class runs share one page.

| Profile | Choice supported by this spike |
| --- | --- |
| 16 KiB, broad or tiny mixture | Existing A/B; route and reuse deliberately before adding classes. |
| 16 KiB, clustered upper sizes | `upper15` where it improves the reservation; eligibility matters more than another globally finer table. |
| 64 KiB, broad or tiny mixture | Existing A/B and its tiny aperture; sparse extra class runs cost a whole 64 KiB granule. |
| 64 KiB, clustered upper sizes | `upper15`, with a narrower eligible population; stronger benefits when Groups fill with compatible sizes. |

The **31-class/8-home/8-identity-grid** full-range challenger is:

```text
16 32 48 64 96 128 192 256 320 384 448 512
640 768 896 1024 1152 1280 1408 1536 1664 1792
1920 2048 2176 2304 2432 2560 2688 2816 3072
```

Adding 2,944 makes 32 classes. That saves some rounding but `0xff` consumes the
last class's last slot: its maximum population is **224 per Group**, versus 256
with 31 classes. The 31-class form is the cleaner challenger if full-range finer
steps or staying in one palette through growth matters. It is not the selected
general default: smaller grids and more alternative class runs increased backing
in the broad traces. [recommend.py](recommend.py) owns these exact candidates,
a balanced tiny-focused table and the unchanged-A/B-table partition control.

## What the comparisons establish

All figures below are logical accessible backing, not measured RSS or timings.
Requests, IDs and lifetime events are identical between candidates. The fresh
population is 1,024 allocations; the concentrated case is 2,017–2,112 bytes,
aligned to 64 bytes. `upper-band` retains the baseline's fresh A/B choice unless
`upper15` offers a smaller capacity.

| Fresh population and selection | A/B: 16 KiB / 64 KiB | A/B + upper15: 16 KiB / 64 KiB |
| --- | ---: | ---: |
| Concentrated, unrestricted prepared reuse | 3.453 / 4.500 MiB | 3.188 / 4.250 MiB |
| Concentrated, strict preferred-palette placement | 3.453 / 4.500 MiB | 2.094 / 2.250 MiB |
| Uniform 16–3,072 bytes, unrestricted reuse | 8.031 / 16.812 MiB | 8.188 / 19.500 MiB |
| Uniform, strict preferred-palette placement | 7.891 / 16.562 MiB | 8.016 / 16.500 MiB |

Concentrated slot slack falls from **15.4% to 3.3%**. The strong strict-placement
result includes segregation into B and upper15 Groups, not just smaller slots.
Shifting the concentration down 64 bytes gives no improvement; shifting up gives
2.500→2.125 MiB at 16 KiB and 2.500→2.250 MiB at 64 KiB. This is useful evidence
for a conditional upper palette, not a universal platform optimum.

Even an entirely upper-size mixture is insufficient: uniform 1,153–3,072-byte
requests with strict placement increase backing from 5.969→7.875 MiB at 16 KiB
and 11.688→16.562 MiB at 64 KiB. [Counterexamples](evidence/counterexamples.json)
retain this population and the tiny-aperture witness below.

The controlled screen isolates three other decisions:

- **Bit partition:** unchanged A/B capacities with 8 rather than 16 identities
  per grid usually increase backing. More classes are not the only cause.
- **Routing:** globally choosing the smallest aligned A/B class reduces broad
  uniform backing to 5.891/9.625 MiB with strict placement, without adding a class.
  It makes the tiny mixture worse. Adding upper15 to that routing increases the
  uniform result to 6.641/13.000 MiB. Keep routing distinct from table quality.
- **Co-packing:** first-fit packing whole small-class runs helps some populations
  but scatters A's tiny mixture. Five alternating sizes 16/32/48/64/96 occupy one
  64 KiB page with current A placement, versus three with that alternative.
  Packing every class into each grid also performed poorly in the first screen.

Do not select a universal 12.5% oversize bound from these results. Bounded reuse
sometimes saves Group creation and sometimes prevents useful page sharing. The
comparison includes unrestricted, strict and bounded eligibility, with newest
Groups searched in both prepared and missing-page passes. It omits the real
four-entry ready cache and measures no search latency.

**Lifetime and growth change the verdict.** Four rounds replacing half of a
fixed 1,024-row population acquire each successor before freeing its predecessor,
then trim after each round. At 64 KiB the concentrated strict-placement result
settles at 4.562 MiB for A/B versus 2.312 MiB with upper15. Uniform churn gives
13.812 MiB for unrestricted A/B, versus 13.500 MiB with strict upper15 placement;
at 16 KiB that upper15 policy is slightly worse. Tiny/log traces retain further
counterexamples in [churn.csv](evidence/churn.csv).

[private_growth.py](private_growth.py) keeps the original identity grid and old
Home occupied during a move. Growing 256→3,072 bytes by 16 bytes in a populated
B grid takes 14 moves/14,464 copied exposed bytes; upper31 takes 23/33,024.
Geometric **reservation** growth reduces both to four moves/3,840 bytes without
prematurely exposing the reserved extent. Alignment, the `0xff` hole and palette
range can force fallback; ordinary different-class growth cannot exhaust a
full 16-home destination using only the other 15 identities. These are modeled
counts, not elapsed-time or concurrency results.

## Qualification and reproduction

The first screen has 5,400 lifetime cells, 180 prepared-placement cells and 240
isolated growth cells. The refined screen adds 1,152 controlled cells, 240 churn
cells, 10,752 populated-grid growth cells and six counterexample cells. The 113 candidate geometries pass
605,792 native home-pattern, alignment, byte-decode and retained-home reclamation
checks in both Release and ASan/UBSan. Current A/B also match actual FiniteGroup
metadata, bind/resolve, preparation and release across all 7,680 homes.

The actual OrbStack Linux host has **4 KiB pages**. Larger-page checks use aligned
mappings and real Linux discard/protection calls at 16/64 KiB policy granules;
they do not qualify native large-page faults, TLB behavior, throughput or latency.
Modeled trim uses an independent occupied-byte-interval census. Metadata uses
native current record sizes and projections for redesigned tables, excluding
controller containers and malloc overhead. No production allocator was changed.

Run from the repository root, prefixing with `orb -m ubuntu` on the Mac:

```sh
bash workbench/spikes/allocation-palettes/run.sh
```

The runner builds only this probe and its Orbital dependency in private Release
and sanitizer directories. Full output stays in `build/allocation-palettes/results/`.
[retain.py](retain.py) retains the small comparisons and [context](evidence/context.json):
tables, seeds, source/raw hashes, compiler, record sizes and qualification.
[recommend.csv](evidence/recommend.csv) contains the refined fresh comparisons;
[private-growth.csv](evidence/private-growth.csv) contains the retained growth
controls. The older [screen](evidence/selected.csv), [placement](evidence/placement.csv)
and [isolated growth](evidence/growth.csv) record how the finer-step question began.

Related allocator work reinforces the population question: [jemalloc's generator](https://github.com/jemalloc/jemalloc/blob/dev/src/sc.c)
chooses slab size from page/class geometry; [TCMalloc](https://google.github.io/tcmalloc/design.html)
explains the reclamation trade-off of larger allocator pages (distinct from OS
pages); [snmalloc's variable slab design](https://github.com/microsoft/snmalloc/blob/main/docs/security/VariableSizedChunks.md)
varies backing population by class. Our inference is that poor fixed-population
packing is not evidence against finer classes in every allocator arrangement.
