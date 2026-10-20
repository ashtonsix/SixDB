# Hashtable benchmarks

This suite compares the maintained Ikea integer set/map with actual Abseil
`flat_hash_set` and `flat_hash_map`. Start with the
[broad maintenance scorecard](ACCUMULATION.md) for absolute timings, memory
counts, wins and losses. It predates the October 20 eight-bit tag promotion;
the [overflow follow-up](../../spikes/hashtable-swiss-probes/RESULTS.md) measures
that change and the rejected wider groups on Zen 5. [Usage](../../../ikea/docs/hashtable/usage.md) and
[rationale](../../../ikea/docs/hashtable/rationale.md) describe the container.
Owning multimaps and K-way folding remain [research](../../spikes/hashtable/README.md).

The recurring suite measures independent read throughput, mutation throughput,
growth and ordered accumulation. It does not measure dependent serial lookup
latency or run a Loom ring. Those are separate, explicitly labeled historical
experiments; [where to find them](#latency-loom-and-cache-residence).

## Find the result that answers your question

| Question | Read |
| --- | --- |
| How does the maintained implementation compare with Abseil? | [Broad core and accumulation results](ACCUMULATION.md), with complete CSV scorecards; the October 20 tag follow-up is separate below |
| Did the original extraction preserve the selected candidate? | [Original promotion](QUALIFICATION.md), a dated paired comparison |
| What does one RMW/native mutable batch cost? | [RMW experiment](RMW.md), including binding and initial-value preparation |
| Does the Zen 5 recipe transfer to Zen 4? | [Generation comparison](ZEN-GENERATIONS.md); it does not establish a new Zen 4 profile |
| What changed with eight-bit overflow tags, and should probes be wider? | [October 20 overflow qualification](../../spikes/hashtable-swiss-probes/RESULTS.md), with actual Boost/Abseil controls; 16-byte groups and 87.5% retained |
| Where are raw runs, sources, compiler flags and losing cells? | [Evidence and recovery](evidence/README.md) |

Historical reports retain their own measurements and source identities. Their
numbers are not interchangeable with the broad scorecard, particularly after
the RMW harness correction. A code-preservation comparison against a captured
Ikea candidate is also a different question from competitiveness against Abseil.

## What a number means

`W` is the meaningful key width; `b` is log₂ of the primary bucket count;
`K = W − b` is the stored quotient width. `V` is value bytes, with zero for sets.
Thus a K14/b10 case has **24-bit keys**, and K30/b10 has **40-bit keys**.
`E` names the physical quotient encoding selected for the CPU/ISA/value size.

| Reported quantity | Included work and interpretation |
| --- | --- |
| `independent`, ns/query | Complete randomized lookup loop with independent keys, hit accounting and all value limbs consumed; reciprocal throughput, not a dependent point latency |
| `prefetch32`, ns/query | Same useful result, with a 32-request software-prefetch distance; prime and tail included |
| `build`, ns/entry | Owner creation/reservation and insertion of precomputed key/value inputs |
| `upsert` / `erase`, ns/operation | Complete replacement/new-key or erase loop; fixture and full state checks outside timing |
| `modify` / `modify_or_insert`, ns/update | RMW consumer, including initial-value preparation for upsert; map8 increments a uint64, map64 updates every byte |
| `grow`, ns/source entry | New independent owner, with source preserved; the maintained Abseil control copies and reserves |
| Accumulation `prepare_ns` | Destination copy or allocation, source-zero transfer when needed, and capacity reservation, per incoming entry |
| Accumulation `merge_ns` | Fixed-backing ordered fold of sources 1..M−1, per incoming entry |
| Accumulation `total_ns` | Preparation plus fold measured in that trial, using the same denominator |

Each accumulation source has the same population. “Incoming” excludes source
zero, which initializes the destination; it includes duplicate entries in later
sources. Both controls know the exact union cardinality, and Ikea's spill bound
is computed outside timing. This measures prepared accumulation, not an unknown-size
stream with online sizing. Each control prewalks its own incoming storage outside
timing, and AB/BA order alternates. The default reducer is an ordered,
non-associative byte fold; copy/add reducers are not qualified by that panel.

All times favor lower values. Full scorecards retain medians, median absolute
deviations (MADs), sample counts and losses. An Ikea/Abseil time ratio below one
favors Ikea; a median across cells is not a workload-weighted speedup. Medians of
preparation and fold need not add to the median total.

### Memory accounting

Core `bytes` uses Ikea buffer capacities plus its concrete owner object, and
Abseil's `AllocatedByteSize` for allocated table storage. It excludes input and
oracle buffers, allocator-wide overhead, descriptor control blocks and process
RSS. A growth row reports the destination, not peak coexistence with the source.

Accumulation reports destination `primary_bytes` and `overflow_bytes` separately;
these use assigned Ikea backing and Abseil allocated storage, excluding source
owners and fixture/oracle workspace. Do not silently combine these counters with
the core's capacity-plus-owner accounting. The read utility additionally exposes
Ikea assigned, accessible and reserved bytes through its storage policy.

Abseil uses standard integer keys: uint16 up to W16, uint32 up to W32, otherwise
uint64. The maintained suite does not use an exact-width custom Abseil key.
Both sides store the same complete fixed-size byte value and use matching
release/ISA/tuning settings. [abseil.cmake](abseil.cmake) pins the dependency
archive and applies those settings to its compiled helpers too. The retained
inline threshold and function alignment are benchmark settings, not consumer
requirements imposed by Ikea.

## Coverage of a recurring run

[native.sh](native.sh) builds module/header checks and five focused ASan/UBSan
targets before timing. [run-native.py](run-native.py) then runs three seeds and
four repetitions with alternating implementation order:

- K14/K30, b10, sets/map8/map64: 0/50% misses, direct and prefetch32 reads;
  construction/destruction; 50/85% nominal occupancy and 0/50/100% new-key writes;
  both writer strategies; map RMW; concrete and descriptor growth with old readers.
- K30, b18, the same value sizes: reads after scanning a 512 MiB conditioning
  buffer, with 0/50% misses.
- Native-ISA accumulation: 54 basic and 48 wide cells per CPU, with two/eight
  sources and 0/50/100% overlap. Basic uses K14/b10, K30/b10 and K30/b15;
  wide uses K31/b10, K47/b10, K47/b17 and K55/b9.

Native x86 and bare AVX2 are separate builds; ARM uses NEON. Accumulation runs
only in the native build. This grid is not every supported key width, value
size, reducer or operation. Functional width coverage and measured profile
provenance serve different purposes.

### Latency, Loom and cache residence

Zero conditioning means the consumer was warmed before timing. A large owner
and a conditioning scan do not prove that each query reached DRAM. Use the
[residency tools](../../tools/residency.md) and recorded machine topology when
making a residence claim.

The [research evidence index](../../spikes/hashtable/README.md#retained-observations)
retains complete cold Loom procedures, dependent chains, useful companion work,
warm losses and observed mutation comparisons. These are measurements of their
recorded research owners and consumers, not qualifications of the current
set/map API. In particular, a multi-request ring's ns/query is throughput; it
does not replace a one-chain dependent-latency measurement. Recover older serial
panels through the [retired-study index](../../notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026).

## Run and audit the complete suite

Use pinned Linux Clang 21.1.8. Prefix Linux commands with `orb -m ubuntu` in the
macOS/OrbStack workspace. For a Zen 5 worker:

```sh
python3 workbench/tools/worker.py run workbench/benchmarks/hashtable/native.sh \
  --machine zen5 --instance-type c8a.2xlarge --deadline 5400 \
  --env HASHTABLE_CPU=zen5 --env SIXDB_BUILD_JOBS=2 --detach
```

Use the worker's returned job ID with `worker.py wait JOB` to observe completion
and collect results. Then audit the collected directory:

```sh
python3 workbench/benchmarks/hashtable/native-results.py build/workers/JOB/results \
  --output build/hashtable-native-summary.json
```

Select machine/CPU `granite-rapids` or `neoverse-v2` for the other native targets;
the [worker guide](../../tools/workers.md) owns available instance choices.
The auditor checks the expected grid, completion marker, flags and finite
measurements, and retains every cell. A successful submission or observation
timeout is not a completed benchmark.

The maintenance cut optionally compares a separate SHA-verified source capture
with `HASHTABLE_COMPARE_CAPTURE=1`. The `comparison_input` entry in
[archive.json](archive.json) identifies the complete external baseline archive,
its hash and its required path inside an isolated source capture. Copy that
archive unchanged to the recorded capture path; `native.sh` verifies and unpacks
it. The archive of old reference headers is a different input. Ordinary recurring
runs need neither. To reaudit the recorded maintenance result, supply the
receipt's RMW supplement with `native-results.py --rmw PATH`. New runs use the
corrected mixer directly.

## Build a focused read comparison

This example is for a compatible Zen 5 host. Change ISA/tuning/profile together
for another deployment. CMake sets the compiled key geometry; CPU/ISA options
select the runtime construction profile, not hardware discovery.

```sh
cmake -S . -B build/hashtable-bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_BENCHMARKS=hashtable -DSIXDB_SPIKES= \
  -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 \
  -DIKEA_HASHTABLE_W=24 -DIKEA_HASHTABLE_B=10
cmake --build build/hashtable-bench --target ikea_hashtable_reads -j2
build/hashtable-bench/workbench/benchmarks/hashtable/ikea_hashtable_reads \
  --operation check --cpu zen5 --isa avx512
taskset -c CPU build/hashtable-bench/workbench/benchmarks/hashtable/ikea_hashtable_reads \
  --operation bench --cpu zen5 --isa avx512 --seed 713 --repetitions 5 \
  --requests 1048576 --miss-percent 0 --conditioning-bytes 0
```

Replace `CPU` with an available logical CPU. The utility compares sets, map8
and map64, using 85% nominal primary capacity capped at half the key domain.
Requests are randomized with replacement; absent inputs come from a disjoint
generated range. Check mode verifies a 4,097-request tail. Its output is raw CSV,
distinct from the complete recurring-run auditor's input.
