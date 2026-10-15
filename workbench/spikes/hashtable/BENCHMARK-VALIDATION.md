# Validation must not perturb lifetime measurements

An untimed validator can still change a measured phase. The growth-cycle fixture
allocated and freed ordered maps and per-key vectors between construction and
release. On the local paired K40/W2 map control, ordinary Abseil release fell
from about 33.8 to 2.5 ns/key when that validation became allocation-free; its
build fell from about 41.4 to 15.2 ns/key. Exact-key Abseil release fell from
29.5 to 0.07 ns/key. These are local attribution results, not native design wins.
The destructor really was inside the intended timer: native disassembly rules
out accidentally timing the oracle itself. The intervening allocation traffic
changes the heap state and subsequent histories.

Both attribution controls retain the same preallocated expected fixture. Both
check every key, every value byte and duplicate multiplicity. The corrected
control resets preallocated counters and matches the independently expected
values. No validation allocation or free occurs while the measured owner lives.
Native Zen 5 and V2 paired probes use the same ten configurations: sets,
K40/W2 maps/multimaps, and K64/W64 maps/multimaps, three seeds and five repeats.

## Scope of the correction

- `growth_cycle.cpp`: corrected by default; the allocating validator is retained
  only as an explicit attribution control. V5 local and V6 native archives keep
  their original sources. Their lifetime ratios must not select growth policy.
- `value_width_bench.cpp`: previously allocated validators after build, update,
  erase and reuse. Independent expected phase states and seen counters now exist
  before construction; complete scans and per-key checks remain. Prior geometry,
  capacity-family and bag-storage lifetime conclusions need corrected reruns.
- `consolidation.cpp`: previously allocated the complete oracle between merge
  and release. It now prepares the oracle before any measured history and resets
  it in place. Earlier release/lifetime conclusions need corrected reruns.
- `small.cpp`: phase validators now preallocate before construction. The separate
  `small_batch.cpp` repeated-lifetime timer already contains no allocating
  validation between construction and release; do not invalidate that study by
  association.
- `worker_epochs.cpp`: inventory materializes output vectors and a coordinator
  oracle before shutdown. Its partial-admission correctness and sanitizer checks
  stand. The corrected fixture now preallocates snapshot storage before owner
  construction and materializes the comparison only after shutdown. That
  snapshot capacity is reported separately as inspection_bytes. Corrected
  local, sanitizer and all-three-platform native panels pass; worker-epochs.md
  owns the remaining throughput losses and memory gains.

Logical bytes, admitted slots, migration counts and independently checked contents
remain observations. Cold read-window timings are a separate study and do not
inherit this lifetime error. Read timings in affected phase fixtures retain their
original warm-up context; a corrected rerun is needed for a combined scorecard.
No baseline is switched to a custom allocator and no production allocation layer
is introduced. `benchmark_oracle.h` is only reusable measurement-fixture code.

The paired local experiment completes30processes/630rows/126qualifiedcells,
source digest c4a71134d07f7946833b28ec63be40b3ced6ef9834ece6dd0b6fcb060b2774f4.
Native probe source is242cdd4066f58c9b21aa8b90adfc0f502a008ed2908bfceadef2d824ec84bb16:
Zen5 `20261015T070126Z-99e8ae4e`; V2 `20261015T070120Z-6fe87e29`.
Both completed:30processes/630rows/126qualifiedcells and ten static read audits per host. Their retained summaries and verified full recovery bundles are in `evidence/growth-oracle-{zen5,neon}`.

## Native corrected comparison

Warm6144-key singleton histories, medium14-slot family, doubling at90% primary
capacity; median across three fixture-seed medians, five repeats each. Build
includes actual promotion/reencoding; release is separate. Read columns are
ns/query, build/release ns/key, memory is final logical bytes including reported
owner metadata. This is not a DRAM, total-RSS or selected-format scorecard.
The exact baseline uses exact-width keys and opaque values.

| Host | Case | Arm | Build ns/key | Release ns/key | Independent hit ns | Dependent hit ns | Logical B |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| Zen5 | K40 set | hierarchy | 13.49 | 0.04 | 3.69 | 6.38 | 38,398 |
| Zen5 | K40 set | Abseil | 9.50 | 0.55 | 2.32 | 6.22 | 73,760 |
| Zen5 | K40 set | exact-key Abseil | 9.42 | 0.03 | 5.00 | 7.75 | 49,186 |
| Zen5 | K40 W2 map | hierarchy | 14.56 | 0.05 | 4.46 | 15.29 | 55,788 |
| Zen5 | K40 W2 map | Abseil | 12.51 | 1.48 | 2.71 | 13.53 | 139,288 |
| Zen5 | K40 W2 map | exact-key Abseil | 9.83 | 0.02 | 7.59 | 13.23 | 65,568 |
| Zen5 | K40 W2 multimap | hierarchy | 17.43 | 0.04 | 5.93 | 18.52 | 172,226 |
| Zen5 | K40 W2 multimap | Abseil | 18.24 | 3.76 | 4.85 | 14.13 | 270,344 |
| Zen5 | K40 W2 multimap | exact-key Abseil | 18.90 | 3.75 | 9.43 | 14.58 | 270,344 |
| Zen5 | K64 W64 map | hierarchy | 22.76 | 0.12 | 8.11 | 20.34 | 562,922 |
| Zen5 | K64 W64 map | Abseil | 32.58 | 4.44 | 3.93 | 16.74 | 597,984 |
| Zen5 | K64 W64 map | exact-key Abseil | 32.38 | 4.32 | 4.00 | 17.03 | 597,983 |
| Zen5 | K64 W64 multimap | hierarchy | 80.46 | 2.21 | 13.48 | 28.42 | 1,763,218 |
| Zen5 | K64 W64 multimap | Abseil | 37.32 | 7.02 | 6.27 | 17.46 | 663,512 |
| Zen5 | K64 W64 multimap | exact-key Abseil | 35.00 | 7.73 | 6.29 | 17.55 | 663,512 |
| V2 | K40 set | hierarchy | 17.87 | 0.09 | 4.46 | 8.11 | 38,398 |
| V2 | K40 set | Abseil | 15.00 | 1.24 | 2.75 | 7.83 | 73,752 |
| V2 | K40 set | exact-key Abseil | 14.92 | 0.04 | 3.51 | 7.92 | 49,178 |
| V2 | K40 W2 map | hierarchy | 19.76 | 0.14 | 5.54 | 21.82 | 55,788 |
| V2 | K40 W2 map | Abseil | 21.19 | 3.13 | 3.22 | 17.62 | 139,280 |
| V2 | K40 W2 map | exact-key Abseil | 16.00 | 0.07 | 3.75 | 18.10 | 65,560 |
| V2 | K40 W2 multimap | hierarchy | 23.61 | 0.21 | 6.48 | 26.68 | 172,226 |
| V2 | K40 W2 multimap | Abseil | 33.11 | 8.07 | 5.95 | 20.82 | 270,336 |
| V2 | K40 W2 multimap | exact-key Abseil | 30.85 | 7.31 | 6.42 | 21.60 | 270,336 |
| V2 | K64 W64 map | hierarchy | 32.13 | 0.32 | 9.80 | 29.35 | 562,922 |
| V2 | K64 W64 map | Abseil | 55.35 | 12.84 | 5.05 | 22.45 | 597,976 |
| V2 | K64 W64 map | exact-key Abseil | 50.87 | 12.54 | 5.78 | 24.24 | 597,975 |
| V2 | K64 W64 multimap | hierarchy | 123.69 | 7.90 | 17.61 | 40.70 | 1,763,218 |
| V2 | K64 W64 multimap | Abseil | 66.18 | 17.36 | 6.76 | 24.44 | 663,504 |
| V2 | K64 W64 multimap | exact-key Abseil | 59.43 | 16.56 | 6.75 | 24.44 | 663,504 |

The correction removes false broad lifetime wins. Narrow set/map build is not
universally ahead; wide-map build still wins here, while wide multimap memory
and build remain major losses. Logical-memory wins for narrow domains survive
the correction, including against the exact-key baseline. The cold Loom
comparison remains in READ-WINDOWS.md, rather than being combined with these
warm timings from a different workload.
