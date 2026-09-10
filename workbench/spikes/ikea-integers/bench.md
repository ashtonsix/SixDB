# Narrow integer benchmark contract

`bench.cpp` measures the materialized 256-value endpoints through matched opaque
function calls. It compares selected and Calico kernels for each layout at widths
1–7, plus plain `uint8_t`. The capacity suite also includes ordinary contiguous
LSB-first bitpacking with scalar readers. That control has no bulk timing row;
its scalar encoder only prepares its input.

These measurements include address generation, endpoint call overhead and result
consumption. They are not an inline-kernel comparison. In particular, the opaque
call can dominate a small hot point read; the plain control exposes part of this
floor. The two layout providers encode their own inputs because their wire
bit assignments need not agree. Their logical values and access algorithms agree at
matching logical extents.

For width-56 u64 controls use the [separate experiment](wide56/README.md).
The 12-bit [composition comparison](composition/README.md#checks-and-measurement)
has its own workload and timing contract.

## Running

Use the captured [runner](run.py), with benchmark arguments after `--`. From
macOS, prefix these Linux commands with `orb -m ubuntu`.

```sh
# Build/check, then a short harness smoke test; not a stable timing campaign.
python3 workbench/spikes/ikea-integers/run.py -- --quick

# Resident candidate: the same logical record count in every representation.
python3 workbench/spikes/ikea-integers/run.py --target neoverse-v2 -- \
  --suite bulk --bulk-bytes 524288 --repetitions 3 --provider all

# Full capacity extent, one width first. Allocations are sequential.
python3 workbench/spikes/ikea-integers/run.py --target zen5 -- \
  --suite capacity --width 1 --max-power 30 --samples 1048576 --repetitions 3 --pmu

# Separate comparison: up to 512 MiB in each representation, differing counts.
python3 workbench/spikes/ikea-integers/run.py --target zen5 -- \
  --suite capacity --payload-bytes 536870912 --samples 1048576 --repetitions 3 --pmu

# Narrow a comparison without rebuilding or allocating the rest of the matrix.
ikea_integer_bench --suite capacity --width 7 --power 20 \
  --operation dependent --provider selected --layout scan --samples 32768
```

The runner's benchmark phase has three parts. Arguments after `--` apply only
to `ikea_integer_bench`. The 32/64-byte stripe-pair comparison also runs with its
own fixed workload, even with `--kernels-only`; the 12-bit composition benchmark
runs unless `--kernels-only` is supplied. `--check-only`, sanitizer and QEMU runs
skip all timings. Use the built executable directly for just one timing suite.

The cloud scripts are retained campaign recipes, not additional runners:

| Script | Arguments after the worker's `--` | Work |
| --- | --- | --- |
| [cloud.sh](cloud.sh) | All normal `run.py` arguments | Transparent runner entry point |
| [cloud-repair.sh](cloud-repair.sh) | Target, optional reader, optional `register-masks` | Focused Scan5/7 bulk and capacity captures, sequentially |
| [cloud-capacity.sh](cloud-capacity.sh) | Target | Separate equal-count and fixed-payload capacity campaigns |

Both campaign recipes use `--kernels-only`, so omit the 12-bit composition but
still run the stripe-pair comparison. Their exact flags are visible in these
short scripts. Captured source bundles preserve historical versions.

CPU selection uses `--cpu`, then `SIXDB_CPU`, then the first CPU in the allowed
affinity mask. The executable pins itself and checks its CPU immediately around
each timed interval. Cases and repetitions run sequentially. Standard output is
CSV; standard error contains progress and failure details. The runner captures
source/toolchain and machine receipts, `timings.csv`, and `benchmark.stderr`.

`run.py --scan-reader fragment-classes|constant-offsets` selects the ScanPack
materializing read endpoints at build time, before benchmark execution. It is a
reader implementation choice over the same physical bytes; bulk encode/decode
and the separately bound 12-bit composition are unchanged. The default is
`fragment-classes`. Run configuration and captured source identify this choice;
the CSV's `selected` provider means the choice in that run, not one universal
implementation. `report.py` never pools different run inputs.
The separate `--scan-register-masks` control changes only the x86 5/7-bit
encoders' mask materialization. It is off by default and does not change their
wire, read kernels, or any ARM kernel. Its measured tradeoffs are in
[measurements.md](measurements.md).

Defaults are all seven widths, all providers, all operations, three repetitions,
and 1,048,576 random operations per capacity row. The capacity powers are 10, 14,
18, 22, 26, and 27. Use `--max-power 30` explicitly for the largest extent;
`--min-power`, `--power-step`, and `--power` select narrower sweeps. The final
maximum power is included even if it is not on the step progression. Samples
must be a positive multiple of eight. `--quick` sets powers 10/13/16, one
repetition, 16,384 samples, 64 KiB bulk budget and eight bulk passes; explicit
options override this preset regardless of their position.

`--payload-bytes` selects the separate fixed-payload capacity comparison. It
accepts 256 bytes through 1 GiB and is mutually exclusive with explicit
`--power`, `--min-power`, `--max-power` and `--power-step`. With `--quick`, the
quick sample/repetition settings still apply, while its default power sweep is
replaced by the payload choice. A bulk-only suite or operation rejects a payload
budget; `--suite all` can still run its unchanged bulk comparison alongside the
fixed-payload capacity cases.

## Validation

The codec check covers 14,336 cases and 3,670,016 selected point reads, all
aligned groups, exact encode/decode bytes, every allocation offset modulo 64,
sentinels and guard pages. The composition adds all 4,096 input values, all
65,536 position masks, rejected placements/graphs, retained lifetime and poisoned
child-span checks. Native ARM ASan/UBSan, AVX2 under QEMU and all three hardware
targets pass. QEMU and development-host timings do not establish target speed.
The retained checks and hardware runs identify their exact sources separately;
historical validation is not presented as validation of a later kernel edit.
The final source passes both reader choices, with separate captured checks:

| Reader choice | Native ARM ASan/UBSan | AVX2 under QEMU |
| --- | --- | --- |
| Fragment classes | [checks](evidence/final-native-fragments/provenance.json) | [checks](evidence/final-avx2-fragments/provenance.json) |
| Constant offsets | [checks](evidence/final-native-offsets/provenance.json) | [checks](evidence/final-avx2-offsets/provenance.json) |

## Bulk working set

The common logical record count is `floor(bulk_bytes / 512) * 256`. Thus the
default 512 KiB budget means 262,144 values for every width and provider. Plain
input plus output consumes the whole budget; a packed representation consumes
`values + values*K/8` bytes. CSV records both buffers separately and their total
rounded allocation. This is a candidate working set for a private cache; the
budget alone does not establish L2 residence.

Unless `--bulk-passes` is supplied, a plain 256-byte copy calibration doubles the
pass count until a timed interval reaches 20 ms, capped at 1,048,576 passes. That
single pass count is shared by every width, layout, provider, and bulk operator
in the invocation. Each case gets one full warm pass before its sequential
repetitions. A bulk operation in the CSV means one 256-value endpoint call;
`ns_per_value` divides by 256. The output is checked over the whole logical
extent after every repetition, using a common logical checksum.

## Capacity access and checks

The extent is `2^power` logical values. One representation is allocated at a
time, aligned to 64 bytes and written in full before timing. At power 30 this is
128 MiB for one-bit packing and 1 GiB for plain bytes, with only small auxiliary
state alongside it. No full plain input is retained when a packed input is
measured; deterministic values are generated a cell at a time during setup.

The default mode is labelled `capacity_mode=fixed_logical` in the CSV. It keeps
the same logical count across all representations, which deliberately changes
the payload size. The optional `capacity_mode=fixed_payload` instead chooses the
largest power-of-two logical count whose representation fits the requested
budget. Each representation is independently allocated and released before the
next. Actual payload size can lie anywhere above half the budget through the
full budget because logical counts remain powers of two. It is a common byte
budget, not a promise of exactly equal allocation sizes.

For a 512 MiB request:

| Representation | Logical values | Actual payload |
| --- | ---: | ---: |
| Packed 1-bit | 2^32 | 512 MiB |
| Packed 2-bit | 2^31 | 512 MiB |
| Packed 3-bit | 2^30 | 384 MiB |
| Packed 4-bit | 2^30 | 512 MiB |
| Packed 5-bit | 2^29 | 320 MiB |
| Packed 6-bit | 2^29 | 384 MiB |
| Packed 7-bit | 2^29 | 448 MiB |
| Plain `uint8_t` | 2^29 | 512 MiB |

This mode admits logical powers through 33: one-bit packing can hold 2^33 values
in 1 GiB. The budget cap prevents a giant plain allocation; plain never exceeds
1 GiB. All extents, offsets, address masks and generator indices use 64-bit
arithmetic. `requested_payload_bytes` records the request, while
`logical_values`, `payload_bytes` and `allocation_bytes` record the actual case.
Bulk rows use `capacity_mode=not_applicable` and a zero requested payload.

* `dependent` returns a `uint8_t`, folds it into a full 64-bit mixing state, and
  uses that state for the next index. The returned narrow value does not limit
  the address domain. Both decoding and the state recurrence are on the
  dependency chain.
* `independent` generates batches of eight unrelated mixed counter addresses,
  then calls `get1` for each. Addresses have no dependency on decoded results.
  This measures the throughput available through the opaque call contract; it
  does not promise eight simultaneous hardware misses.
* `get16` uses the same independent generation, selecting groups aligned to
  sixteen logical values. Every result byte is consumed through two 64-bit sums.
  Its CSV operation is one group; `ns_per_value` divides by sixteen.

Each repetition and operation has a distinct deterministic seed. A width at a
matching logical extent uses the same logical values and seed across all its
representations. Fixed-payload arms with different logical counts use different
seeds and traces; their common generator still agrees on overlapping indices.
After timing,
the harness replays every access, checks every value against the independent
logical generator, and verifies the result sums, full dependency state, and
address sum against the timed result. The replay also hashes every logical
index. Equal seeds, trace hashes, and result fields across providers are an
inspectable cross-representation check. Dependent traces can differ across
widths because the decoded values differ.

Replay occurs **after** its measurement to avoid warming that same trace first.
Earlier setup, operations, or repetitions can still affect the cache. The
harness neither flushes caches nor proves a cold access, LLC miss, or DRAM
response. Every CSV row therefore says `residence=unestablished`. Use the
capacity sweep, measured machine topology and additional hardware evidence to
interpret a residence transition.

The two capacity modes answer different questions. Fixed-logical compares the
cost of accessing the same records as representation size changes. Fixed-payload
probes a larger randomized working set for narrow widths without increasing the
plain allocation by the same logical factor. Neither mode alone proves a DRAM
plateau or equal cache warmth. Compare multiple payload budgets and retain the
actual footprint, coverage and hardware-event evidence with the timing.

## Accounting and interpretation

Capacity coverage is the exact set of 64-byte payload lines containing the bits
logically required by the recorded trace. One-hot encodings establish the wire
permutation; a post-timing bitmap counts unique required lines and required line
visits. `coverage_kind=required_bytes_exact_trace` describes this logical census.
It does not observe the wider loads a kernel might issue, prefetching, hardware
cache misses, or coherence traffic. Such differences need assembly inspection
or suitable hardware events. Bulk rows instead use
`coverage_kind=full_sequential_extent` and cover the complete packed extent;
their trace hash and required-line-visit fields are zero sentinels because no
per-access census is taken.

`allocation_bytes` counts the rounded input/output payload allocations;
`input_bytes` and `output_bytes` report their logical byte sizes. Capacity
`output_bytes=0` means no heap output, with only a fixed 16-byte group scratch
buffer. `hot_metadata_bytes` is the endpoint descriptor size, not an estimate of
all stack or instruction-cache use. `trace_bytes=0` means no stored address
array: timed generation uses constant scalar state and eight temporary indices.
`audit_aux_bytes` separately counts the wire map and, for capacity replay, the
line map and line-coverage bitmap; these are not accessed in the timed interval.
Page size, payload alignment and minor/major faults during measurement are
reported. Prefaulting is not a claim that the process can never fault again.

With `--pmu`, a Linux perf group requests generic user-space cycles, instructions
and cache misses. Counts are raw, accompanied by enabled/running times; they are
not silently scaled. Permission, event or scheduling failures are reported in
`pmu_status` with `NA` counters. Generic cache misses alone do not establish which
cache level supplied an access. Timing uses `CLOCK_MONOTONIC_RAW` and does not
depend on PMU availability.

The all-arm Linux/Clang smoke campaign covered 448 rows. All value/replay checks
passed, and an external CSV check confirmed identical checksums, dependency
states, address sums and trace hashes for all six arms in each of the 63
capacity trace groups. These are harness checks, not performance conclusions.

The fixed-payload extension passed 844 smoke measurements: the original 448-row
campaign, 126 cases at 64 KiB, 252 at a 1,000-byte budget, and 18 at
the minimum 256-byte budget. Twelve invalid extent/filter combinations were
rejected before emitting CSV. The original campaign's work counts, seeds,
footprints, traces and result fields matched its previous output. Maximum
logical-count arithmetic is also checked at compile time; these smoke tests
did not allocate the largest payloads.

## Compact reporting

```sh
python3 workbench/spikes/ikea-integers/report.py path/to/timings.csv > summary.csv
python3 workbench/spikes/ikea-integers/report.py --validate-only path/to/timings.csv
```

The standard-library helper validates mode/footprint accounting and compares
checksums, states and trace hashes only for matching logical extents and seeds.
It reports each case's median/minimum/maximum time and selected/prior and
selected/plain comparisons as `baseline_ns / selected_ns`: values above one
favour selected. Missing baselines remain `NA`. Fixed-payload comparisons against
plain explicitly say when their logical counts differ; those traces are not
asserted equal. Repetition sets must match before a ratio is emitted. Input
files are treated as separate runs, so different targets and source revisions
are never pooled. Older CSVs without a mode field are accepted as the original
fixed-logical comparison. An intentional checksum corruption was rejected during
the helper's verification.
