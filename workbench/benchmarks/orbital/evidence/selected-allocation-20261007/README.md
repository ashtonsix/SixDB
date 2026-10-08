# Selected allocation composition: completed synchronous and same-CPU preparation comparison

C2 shows scalar headroom across most of the retained workload cells. Eight prepared Pages sharing the caller CPU did not remove the four insertion p99 deficits. This closes the measured allocation-stage comparison; those gaps are composition leads for stage 2.

Both complete panels have 1,710 qualified cases: 19 workloads × three initial populations × five backends × throughput/latency × three trials. All 3,420 case identities match across panels (input, trace, final state, request count, seed and execution/setup policy). Owner headroom covers 57 workload/population cells against each actual LMDB reference.

Against `lmdb-read-batch64`, both panels meet/exceed throughput in 56/57 cells, p50 in 56/57, and p99 in 53/57. Against one-transaction-per-request `lmdb`, synchronous throughput is 56/57 and prepared throughput 57/57; both have p50 56/57 and p99 53/57. These are trial medians, with tiny margins retained rather than promoted into general performance claims.

## Large-population margins

4,194,304 initial records. Ratios are owner throughput / actual LMDB read-batch64 throughput; latency headroom is LMDB minus owner (positive leaves room). LMDB writes use one transaction per request in both reference lifetimes.

| Workload | Sync rate | Prepared rate | Sync p50 µs | Prepared p50 µs | Sync p99 µs | Prepared p99 µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| append | 25.708 | 25.129 | 1.550 | 1.530 | 3.490 | 3.540 |
| delete | 3.907 | 3.986 | 2.200 | 2.230 | 4.720 | 4.550 |
| dependent-16 | 1.203 | 1.152 | 1.760 | 1.510 | 1.430 | 1.170 |
| insert-random | 1.845 | 1.816 | 2.570 | 2.550 | -3.070 | -3.290 |
| insert-scan | 1.188 | 1.175 | 1.720 | 1.720 | -3.421 | -3.500 |
| order-entry | 3.485 | 3.583 | 4.170 | 4.179 | 7.480 | 7.530 |
| order-status | 1.029 | 1.063 | 0.020 | 0.000 | 0.380 | 0.430 |
| point-hit | 0.933 | 0.950 | -0.090 | -0.090 | 0.241 | 0.270 |
| point-miss | 1.043 | 1.039 | 0.040 | 0.000 | 0.280 | 0.240 |
| point-rmw | 3.901 | 3.989 | 2.350 | 2.360 | 4.800 | 4.910 |
| point-update | 3.807 | 3.899 | 2.370 | 2.390 | 4.670 | 4.920 |
| scan-16 | 1.028 | 1.021 | 0.020 | 0.020 | 0.470 | 0.540 |
| stock-range | 1.082 | 1.106 | 0.140 | 0.150 | 0.460 | 0.540 |
| ycsb-a | 3.968 | 4.038 | 1.790 | 1.780 | 4.000 | 3.740 |
| ycsb-b | 1.614 | 1.495 | 0.090 | 0.110 | 2.650 | 2.891 |
| ycsb-c | 1.175 | 1.170 | 0.080 | 0.090 | 0.490 | 0.530 |
| ycsb-d | 1.801 | 1.741 | 0.110 | 0.100 | 0.850 | 0.860 |
| ycsb-e | 1.391 | 1.400 | 0.180 | 0.170 | 0.610 | 0.580 |
| ycsb-f | 4.012 | 3.935 | 1.780 | 1.810 | 4.020 | 4.240 |

## Actual preparation behavior and resource costs

Caller and producer were requested and observed on CPU 0 throughout; no extra CPU resource was added. At the large population, random insertion has a median 464 ready hits and 15,920 stockouts out of 16,384 Page demands (97.17% stockouts). Insert-scan has 549 ready hits and 15,835 stockouts (96.65%). At 262K rows, the corresponding median is 24 ready hits / 1,000 stockouts out of 1,024 demands. These are actual measured flags, joined to exact request ordinals and latencies, not readiness inferred from an untimed replay.

Large random-insert request p99 changes from 10.710 to 10.960 µs; insert-scan from 10.701 to 10.960 µs. Medium insert p99 changes from 6.420/6.460 to 6.660/6.640 µs. The same-CPU treatment therefore has no measured tail repair in these panels.

Ready-only large random-insert requests have a median-of-trial p50 of 5.98 µs and conditional p99 of 46.0 µs; stockout requests have 9.45 µs / 14.47 µs. Insert-scan is 6.07 / 45.79 µs for ready-only and 9.43 / 14.53 µs for stockouts. Ready-only groups contain roughly 456–552 requests per trial, stockouts roughly 15.8–15.9K. Classification does not isolate the cause of those long samples or prove scheduler interference; retain both groups for the next composition.

Whole-process throughput CPU per completed request changes from 2.136 to 2.187 µs for large random insertion and 2.337 to 2.387 µs for insert-scan. The prepared producer contributes about 19.4 and 22.1 ns/request, including service/wake and measured drain. These means are separate from request p50/p99. Prepared startup is outside the request clock: large insertion median wall startup is about 107–121 µs depending on workload/mode; the complete trial table keeps wall/process/producer costs separately.

Every owner prepared trial starts with eight ready Pages and finishes with zero preparation inventory, ready/inflight/repair work and ready-page backing (`ready_backing_bytes`). Peak inventory is eight; the private metadata bound remains 6,912 bytes. The Pool retains 65,536 bytes of idle backing until ordinary trim or Store teardown. Large insertion normally ends with eight inflight physical records, all settled before helper join. Eight successfully constructed unused Pages are discarded (46,080 pack bytes / 16 spans and 4,352 index bytes / eight spans); no constructor or admission failure is recorded. Pure reads demand no Pages and discard all eight startup Pages. Arena boundary charge also includes ordinary live storage; it is not a preparation-only peak or a post-Store zero-charge observation.

The scripts already ran the separate slot-16 finite/window/mixed lifecycle cohort: 18 qualified trials across the two jobs, each 64 cycles, real foreign CPU 1 and Arena helper CPU 1, with verified completion and zero after-trim and after-shutdown charge. This is a small lifecycle cohort, not budget exhaustion. Slots 1/64 remain unstarted follow-ups.

## Identities, recovery and limits

- Capture: `595e07faf800d3a749658934c6839ae513d019bb2141f6da7ff2cae552ab695e`.
- Measured allocation binary: `dbf3785e2de171721b5595f4c834778de0e3e65a11d6cde985bdb794e67df3d3`.
- Postcollection validator: `82417c9ec1f26d6136ad76e52e7d09361dae8d3fd9a759a1508073d58b85b6e9`; hardware capture/raw archives unchanged. The captured runner remains its original a8e6b4da version.
- Synchronous job `20261007T213247Z-9b3e59de`, archive `ac61a769e5e9a7e0c5da732340b0b12687bc7d78f083a82d6a2ae60a5254f8dd`.
- Prepared job `20261007T220604Z-25aff588`, archive `08e9485333915b2ce940f8b5716260fe1b0dd03936d8a12027a4a94fbc96af5a`.
- Same reused on-demand c8a.xlarge guest `i-09ad03d49ae94e37a`; verified collections, successful scripts, queue controller removed and instance confirmed terminated.

Fixed-seed, sequential three-trial comparisons; no randomized cross-panel ordering or trace diversity. Across panels, unchanged LMDB throughput controls vary about −2.9% to +2.4%, so similar-size changes are not attributed to preparation. These are closed-loop allocation-consumer requests; Engine/Shore/versioning/isolation/durability remain outside this stage. Cache load service is unobserved. Partial failed constructor writes are unquantified. The interrupted Spot attempt stays an immutable partial attempt and is not mixed into these complete medians.

[`comparison/cells.csv`](comparison/cells.csv) keeps every workload/population median and both references. [`comparison/resource-trials.csv`](comparison/resource-trials.csv) and [`comparison/conditioned-latency-trials.csv`](comparison/conditioned-latency-trials.csv) retain repetitions and actual flags/costs. Panel `cases.csv` retains all competitors/trials; duplicate JSON and full capture manifests are archive-only. Original request samples and binaries are recoverable from the two panel `artifact.json` references. `comparison/comparison-receipt.json` hashes derived inputs and identifies the analyzer. `plot.py RECOVERED_PANEL_DIRECTORY --output IGNORED_OUTPUT` regenerates the scalar PNG/SVG from recovered hashed inputs (Python/matplotlib).

## Stage 2 tooling

`workbench/benchmarks/orbital/stage2.py` prints or dispatches one selected recipe through the existing worker. It carries scalar/preparation controls, active width/conditioning sweeps, lifecycle slots and availability/sensitivity/composed counter diagnostics. It requires an explicit new source capture, defaults to no dispatch and leaves no automatic queue. Twenty-two recipe/machine/event configurations passed the actual worker parser, eleven scalar/staged configurations passed actual runner preflight before output creation, shell adapters parse, and three bad CLI configurations refuse. No new exploratory hardware job ran.

The extra-CPU preparation, complete current width sweep, lifecycle slots 1/64 and further counter qualification remain stage-2 leads. Existing request-event joins, actual preparation samples, versioned collector refusal checks, mapping/PMU receipts and memory probes are ready. A new composed source cut owns its own qualification; these C2 measurements do not assign performance to the later private closing cut.

## Recover archive-only inputs

Git retains findings, selected comparisons and context. Raw intervals, duplicated
JSON tables, full manifests and SVG companions are in the
[verified pre-curation archive](../20261008-before-curation-artifact.json);
[the curation record](../20261008-curation.json) names every removed file and hash.
Full worker archives remain referenced separately for original samples, sources
and binaries. Recover into ignored storage:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/benchmarks/orbital/evidence/20261008-before-curation-artifact.json \
  build/recovered/orbital-evidence-20261008
```

Render either recovered panel without changing it:

```sh
orb -m ubuntu python3 workbench/benchmarks/orbital/evidence/selected-allocation-20261007/plot.py \
  build/recovered/orbital-evidence-20261008/selected-allocation-20261007/all19-synchronous \
  --output build/reports/orbital-synchronous
```
