# Does centring the 12-bit residual earn a wire change?

The current striped 10/20-bit payloads already put one 32-byte residual stripe
between two equal body halves. Widths 14/15 alternate body and residual chunks.
The remaining mixed case, width 12, keeps `body64 || tail32`. This probe compares
it with `body32 || tail32 || body32` using the same Ikea read/write bodies and
ordinary prepared operations; only the two within-tile offset functions differ.

**Retain the current 12-bit wire.** The middle arrangement improves logical
locality but has no convincing overall runtime advantage in this comparison.
It would simplify only a small offset helper, not remove duplicate kernels.
The production helper now shares the equal-half formula for widths 10 and 20;
that algebraic cleanup changes no bytes. A provisional versioned implementation
of centred12 was withdrawn before adoption. All persisted formats remain v1.

## Geometry versus runtime

At tile phases 0/32, both 12-bit layouts require at most two adjacent 64-byte
lines for a point or aligned 16-row group. Across tight 96-byte tiles, centring
raises the one-line share from 25% to 50%. With 128-byte tile strides, every tile
starts at phase 0; centring raises the one-line share from 0% to 50%. These count
required byte locations, not cache misses, memory transactions or prefetches.

Five sequential repetitions on each target give these median centred/body-first
ratios. Point/group times include the ordinary bound call and consumption; scan
times include materialization, and writes include byte-coverage effects.

| Case | Zen5 | Neoverse V2 |
| --- | ---: | ---: |
| 12KiB tight, point | 1.000 | 1.001 |
| 1.5MiB tight, point | 1.072 | 1.043 |
| 96MiB tight, point | 1.025 | 1.050 |
| 96MiB tight, aligned16 | 0.982 | 0.988 |
| 128MiB padded, aligned16 | 0.960 | 0.979 |
| 12KiB tight, scan | 1.000 | 0.999 |
| 96MiB tight, scan | 1.035 | 0.997 |
| 8,192-row tight write | 1.056 | 0.994 |
| 8,192-row padded write | 1.197 | 1.074 |

The notable isolated win is the small padded Zen point case (0.857); it does
not generalize to larger sources. Small differences may include executable
placement or noise. We have not attributed the remaining ratios to particular
instructions or cache behavior. The geometric improvement alone is insufficient
evidence for a new default.

Random reads advance through 1,048,576 generated query positions instead of
repeating a tiny hot subset of the large allocation. Each timed batch contains
8,192 queries. Allocation size does not certify cache residence; no hardware
cache-miss or prefetch counters were collected. Scans write u16 results; range
mutation repeatedly replaces the same 8,192-row range and measures issued
stores/effects, not a changing-data distribution. Preparation is outside timing.
All input rows are checked against an independent value formula; written values
are checked after measurement. Each layout has identical logical values, byte
extent and stride.

## Reproduce and recover

```sh
orb -m ubuntu python3 workbench/spikes/ikea-composition/placement/geometry.py
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/ikea-composition/placement/run.sh --machine zen5
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/ikea-composition/placement/run.sh --machine neoverse-v2 \
  --env PLACEMENT_PROFILE=neon
```

The probe's `centred12` type is experimental and must not be serialized as the
production format. Its loads/stores and operation bodies come from Ikea.
[Zen evidence](evidence/zen5/artifact.json) and
[V2 evidence](evidence/neoverse-v2/artifact.json) retain all 40 cases and five
repetitions, including counterexamples. The archives also retain source, binary,
toolchain/build receipts and hardware context. Captured source:
`cb3a769d5a5fb32e87b8008cfce619f96ae33531c02c21f3c0a5e58412e9f3c1`.
Recover with the [artifact tools](../../../tools/artifacts.md).
