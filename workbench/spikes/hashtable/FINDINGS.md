# Initial findings

These are pre-table experiments. They establish neither Abseil parity nor a
winning bucket capacity. The [plan](PLAN.md) retains the complete comparison scope.

## Permutations and secondary pressure

`model.py` independently round-trips all 8/16-bit keys and boundary/random wider
keys, checking every address/fingerprint/suffix split: **156,196 permutation
round trips**. It also enumerates 1,728 illustrative byte-rounded geometries and
144 placement cases. Geometry counts include an explicitly provisional five-byte
header; they are not complete table/allocator memory figures.

For keys 0 and 2^55 under a fixed golden-ratio 64-bit multiplier, randomized XOR
seeding does not remove strong correlations in the high bucket bits:

| Bucket bits | Fixed multiplier collision fraction | Independent random odd multiplier | Ideal independent assignments |
| --- | ---: | ---: | ---: |
| 2 | 83.50% | 25.10% | 25.00% |
| 3 | 67.41% | 12.44% | 12.50% |
| 4 | 34.43% | 6.22% | 6.25% |

Each cell uses 100,000 deterministic seed trials. This rejects that fixed
multiplier/XOR family as an unquestioned choice. The random-multiplier family remains a
candidate; these samples do not establish adaptive security or an Iceberg theorem's assumptions.

At 16 slots, 1,024 primary buckets and 64 secondary buckets, 15,564 uniform keys
(95% of primary slot capacity) leave about 1,190–1,267 keys outside primary buckets
in the tested seeds/families. Secondary storage has only 1,024 slots: 197–262 keys
reach chains. Secondary ratio and growth occupancy need joint selection; simply
adding a small secondary array does not make chains negligible. Structured
sequences behave differently, so neither uniform nor sequential alone suffices.

[Selected model receipt](evidence/model-screen.json) retains the counterexample
and uniform case. Regenerate the full logical model with:

```sh
orb -m ubuntu python3 workbench/spikes/hashtable/model.py --output build/hashtable/model.json
```

`state_model.py` separately stores only fingerprint/suffix/value, reconstructing
keys from their primary, secondary or chain owner. An independent dictionary
checks 38,400 mutation states across all eight key widths, including 192 reseeded
rebuilds, all three erase tiers and the zero-fingerprint `a=w` boundary. A targeted
case updates displaced keys after deleting the sole primary entry: neither may
be inserted again into the vacancy. Complete draining checks that both descendant
counters return to zero without promotion. This is a logical algorithm check,
not native storage, chain allocation, multimap or concurrency validation.
The mutation model uses the unmixed random-odd family and deliberately small
capacities 1/4 to force transitions. It does not model finite counter overflow
or batch admission refusal. Independent review also injected duplicate records,
count drift, a lost swap entry and a wrong bucket owner; the oracle rejected each.

[State-model receipt](evidence/state-model.json); reproduce with:

```sh
orb -m ubuntu python3 workbench/spikes/hashtable/state_model.py \
  --output workbench/spikes/hashtable/evidence/state-model.json
```

## First native composition consumer

`bucket.h` uses actual SeriesPack Local `read16` and TuplePack `native::read_body`
over admitted byte windows. Schema controls are prepared once. This has no
per-bucket retained views, allocator facade or copied production implementation.
It compares four recipes: decode/filter versus direct bit-plane equality, each
with generic TuplePack controls versus a fixed raw field-load control. The latter
is an attribution control, not the strongest possible complete table.
An added third TuplePack recipe now calls its existing fixed-transfer body after
once-per-schema shape admission. It retains the prepared offset/shift/mask and
exact windows, isolating the cost of general shape dispatch without copying a
codec. The experiment currently names an internal body because the existing
public author endpoint requires a bound view; a raw-address sibling is the
smallest prospective composition change. Its native captures are separate from
the initial four-recipe results below.

The explicit NEON/x86 equality kernels build candidate masks in registers.
Both compiler vectorizers are disabled. For 1–7-bit fingerprints, direct equality
can intersect encoded bit planes without transposing them to byte values first.
That predicate is currently experimental matched-wire code; if it earns a place,
it belongs as a reusable SeriesPack native operation rather than a second codec
inside hashtable. At 8 bits both recipes currently use the same SeriesPack decoder;
more aggressive mask representations and direct contiguous loads remain open.

Native checks cover all 1–8-bit fingerprints, every occupancy, all 64 byte phases,
exact spans ending at a guard page, every matching/inactive lane, false candidates
and suffix widths 0/1/7/8/9/15/16/17/23/24/31/32/33/39/40/41/47/48/49/55/56.
The complete bucket's final suffix/value also ends at a guard page. Dirty unused high bits
test TuplePack masking. Encoded expectations come from separate scalar fixture
construction. Timed hit queries consume complete values; serial next-addresses
come from returned values, and expected traversals use the original permutation.
Timed values encode both next bucket and slot, making the checksum sensitive to
a wrong-slot return as well as a wrong next address.

The probe's 16 fully occupied slots are a bounded comparison fixture, **not a
selected table capacity**. It does not yet time misses, writes, overflow, hashing,
growth, publication or Loom. Independent query order is an odd-stride permutation;
the dependent chain order is shuffled. The 16/4,096-bucket sizes test different
footprints; neither alone establishes a cache-level residence claim.

The final native captures cover Neoverse V2 NEON, Granite Rapids AVX2/AVX-512 and
Zen 5 AVX2/AVX-512. All native checks passed. Five repeats of every tested recipe,
size and traversal are retained as median/min/max in
[all 320 result cells](evidence/bucket-summary.csv); the
[receipts](evidence/bucket-receipts.json) identify captured source, hardware,
checksums and recoverable raw artifacts.

For the 16-bucket, 24-bit-suffix case, the speedup from direct plane equality over
ordinary SeriesPack decode/equality **with the same generic TuplePack path** is:

| Native target | 3-bit serial | 3-bit independent | 7-bit serial | 7-bit independent |
| --- | ---: | ---: | ---: | ---: |
| Neoverse V2 / NEON | 2.70× | 1.53× | 2.32× | 1.00× |
| Granite Rapids / AVX2 | 1.45× | 1.44× | 1.32× | 1.03× |
| Granite Rapids / AVX-512 | 1.49× | 1.45× | 1.37× | 1.05× |
| Zen 5 / AVX2 | 1.88× | 1.73× | 1.43× | 1.09× |
| Zen 5 / AVX-512 | 1.96× | 1.76× | 1.50× | 1.12× |

These are within-probe ratios, **not speedups over Abseil**. In the Neoverse
7-bit cell the actual serial medians are 27.52 versus 11.88 ns, while independent
medians are 8.77 versus 8.80 ns. This is a concrete reason to retain both metrics.
Eight-bit fingerprint recipes are identical and supply a useful noise control.
The narrow widths also change false-candidate density; this table does not
justify choosing one fingerprint width over another.

There is a separate TuplePack composition opportunity: with the same 8-bit
fingerprint path, fixed raw field loads reduce the representative independent
median from 5.68 to 3.04 ns on Neoverse, 4.26 to 2.08 ns on Granite AVX2, and
3.20 to 1.66 ns on Zen 5 AVX2. Those direct loads do not yet provide a reusable
primitive author contract. That motivated testing shape admission while
preserving the existing codec and exact windows.

The follow-up does exactly that, calling the existing TuplePack fixed-transfer
body with once-admitted byte extent. Its [480 result cells](evidence/admitted-transfer/bucket-summary.csv)
and [receipts](evidence/admitted-transfer/bucket-receipts.json) retain all three
TuplePack recipes and the same fingerprint controls. All five native checks and
local Release/ASan/UBSan checks pass. Representative 16-bucket, 8-bit-fingerprint,
24-bit-suffix medians are:

| Native target | Generic independent ns | Admitted independent ns | Raw independent ns | Generic → admitted serial ns |
| --- | ---: | ---: | ---: | ---: |
| Neoverse V2 / NEON | 5.70 | 4.04 | 3.04 | 15.23 → 14.84 |
| Granite Rapids / AVX2 | 4.14 | 2.71 | 2.08 | 9.54 → 9.05 |
| Granite Rapids / AVX-512 | 4.15 | 2.71 | 2.05 | 10.31 → 9.84 |
| Zen 5 / AVX2 | 3.18 | 2.08 | 1.65 | 6.18 → 5.90 |
| Zen 5 / AVX-512 | 3.33 | 2.11 | 1.64 | 6.62 → 6.34 |

Thus removing dispatch helps independent throughput substantially but dependent
latency only modestly. Runtime offset/shift/mask still cost instructions/registers.
A small raw-address sibling of TuplePack's existing admitted author endpoint is
supported by these results; raw-load parity is not yet established. Eight-bit
fingerprints remain the common case while `w-a >= 8`, so narrow-plane wins must
not divert the next full-table comparison from its ordinary byte-fingerprint path.

Assembly inspection confirms explicit vector comparison, candidate extraction,
exact suffix loads, and loaded-value dependence into the next bucket address.
The generic consumer hoists many controls into its prologue; its large frame and
function size are not all per-lookup work. Remaining shape dispatch, register
pressure and transfer instructions must be attributed separately. These warmed
probes have no useful memory-stall profile yet; a PMU campaign and full-table
baseline comparison remain outstanding.

The [Neoverse V2 llvm-mca model](evidence/mca-neoverse-v2.json) gives 3.7 versus 8.2
cycles block reciprocal throughput for the 3-bit plane predicate versus decode/
compare. At 7 bits it gives 8.3 versus 8.2: lower dependent latency is a separate
question from throughput. These are model outputs, not hardware timings. Whole
function repetition introduces ABI register recurrences, so its total-cycle
number must not be reported as real serial lookup latency.

Build and inspect the bounded experiment:

```sh
orb -m ubuntu cmake -S . -B build/hashtable/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2
orb -m ubuntu cmake --build build/hashtable/native --target hashtable_bucket hashtable_codegen -j2
orb -m ubuntu build/hashtable/native/workbench/spikes/hashtable/hashtable_bucket check
orb -m ubuntu taskset -c0 build/hashtable/native/workbench/spikes/hashtable/hashtable_bucket 16 1000000
orb -m ubuntu python3 workbench/spikes/hashtable/codegen.py --build build/hashtable/native \
  --output build/hashtable/codegen --cpu neoverse-v2 --triple aarch64-linux-gnu
```

`native.sh` runs the same checks and bounded comparisons through the existing
worker tooling. Intel/AMD each use explicit AVX2 and AVX-512 builds; Neoverse uses
NEON. Native bucket attribution is available above; complete-table comparisons
remain outstanding.

## Spatial-prefetch qualification

Loom found that conditional stimulus helpers in the old memory-characterisation
probe and runtime calibrator loaded before their trailing fence. Speculation
could therefore warm control targets and contaminate adjacent-fetch conclusions.
Both helpers now fence at entry. Loom delivered the corrected native diagnostic,
cold snapshot fields and bounded missing-field calibration within the requested
hour. Old adjacent/training observations are quarantined from production imports.
The owning [controlled results](../memory-characterisation/ADJACENT.md) retain
3,981,312 checked trials: Zen 5 shows forward +1 benefit at both starting parities
under demand and suitably delayed T0 histories, but not T1; Granite Rapids varies
across repetitions and stays unknown in the broad diagnostic. Far/cross-page
controls show no benefit under these histories. These observations do not
establish an L2 trigger, exact fetch depth or permission to omit the second hint.

The [Loom architecture account](../../../loom/docs/hardware.md) describes cold
snapshot access and the narrower startup history. ARM calibration currently
leaves adjacency unknown. No address heat oracle has been implemented. None of
these facts has yet been used here to select a bucket capacity or final layout.
