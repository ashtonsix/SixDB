# First-pass evidence, 2026-09-13

[Findings](../findings.md) interprets the results. This directory retains whole
useful comparisons, including every candidate, seed and repetition in the
reported screens. Full captured source, binaries, build receipts and worker
logs are recoverable through each native export's `artifact.json` with the
[artifact helper](../../../tools/artifacts.md). Timings, untimed accounting and
modeled counts are deliberately identified separately.

## Captured native screens

| Export | Worker job | Scope |
| --- | --- | --- |
| [consumers-zen5](consumers-zen5/summary.md) | `20260913T145528Z-1dd6efa4` | Prefix and bucket, c8a.large Spot |
| [consumers-gnr](consumers-gnr/summary.md) | `20260913T145641Z-fbac20ae` | Same consumers, c8i.large Spot |
| [spatial-zen5](spatial-zen5/summary.md) | `20260913T150318Z-923f3eac` | Spatial, reused c8a.large Spot |
| [spatial-gnr](spatial-gnr/summary.md) | `20260913T152403Z-9e96fc9e` | Spatial, c8i.large on-demand after two Spot interruptions |

Both consumer runs use source digest
`8a02f1e4062ad7b8fd344c01a98a6d4a61c17bfe2f1e2e7fffb71749a618fb98`.
The spatial capture is
`97282eb63b8004f07b66561c6d909bd46cf7c3275cf9e574b6c4474d47a1cba1`.
These frozen captures were based on opening commit `8b86a42` with explicit
overlays of this spike's native files; unrelated active Ikea/Bec256 work was
excluded. The full bundles preserve the exact source rather than depending on
the shared checkout remaining unchanged.

Linux Clang 21.1.8, C++23, release optimization with assertions, no LTO/unity,
common `x86-64-v3` ISA and per-host `zen5` / `granite-rapids` compiler tuning were
used. Each runner pins one visible CPU and executes cases sequentially. Cloud
workers have 4GiB RAM; Zen exposes two cores, one hardware thread each, and GNR exposes
one CPU after disabling SMT. No competing experiment was deliberately dispatched
to the workers. This is not a guarantee about host interference or an estimate
of between-instance variation.

Both report 64B lines. Consumer workers report Zen 5 L2=1MiB/L3=8MiB and
Granite Rapids L2=2MiB/L3=480MiB. These are exposed geometry, not measured usable
residency. Hardware JSON retains topology and affinity. Native consumers use
repeated warmed random traces; neither their allocation size nor a nominal
cache capacity labels a sample as pure DRAM. Host/cache/tune differences prevent
interpreting cross-host ratios as an isolated microarchitectural effect.

Consumer coverage per host: eight prefix layouts × twelve operations and
336 bucket cases, each at 4,096 and 262,144 rows/keys, two seeds and three
repetitions: 5,184 timing rows. `summary.csv` preserves each seed and min/median/
max of its three repetitions. Bucket stderr records table occupancy, blocks,
exact keys and false candidates outside timing. Prefix/bucket CSV
`conditional_visits` is per query batch; it is not cache traffic. Allocation
counts actual encoded byte buffers, excluding query/oracle and prepared C++
objects. Case definitions explain ordinary versus raw recipe costs.

Spatial coverage per host: three organizations × two footprints × two phases ×
three access conditions × three K values × two seeds × two repetitions = 432
timing rows. Large logical bytes are 1,073,740,800; split core bytes are about
683MiB. Phase zero is the aligned comparison; phase32 is a deliberate shifted
placement diagnostic. Large full passes greatly exceed the requested 3ms
minimum. `placement.txt` retains first-touch/page/NUMA receipts;
`samples.csv` retains actual duration, checksums, operations and modeled demand.
No PMU traffic or causal hardware-prefetch measurement is present.

The initial GNR spatial job `20260913T150422Z-d6f9ad6f` was interrupted by Spot
reclamation after 17 seconds and is excluded from comparisons. Its partial
archive was fetched and verified under `build/workers/` and remains recoverable
by worker ID; archive SHA-256 is
`bf411e4f8d2bfcb64ddac022b487b482f15598046d4422ab97c1845f3ef68e24`.
The first retry, `20260913T151418Z-4654dcc4`, was also interrupted during its last
large case. It is excluded rather than spliced into the final run. Its fetched
partial archive SHA-256 is
`288cf919c7c630fb780d9c59226bfb6a55af056e593d68a4ec8a43f37d69dc47`.
The final retry uses unchanged source/workload parameters on a small on-demand
worker with a 1,200-second deadline and no idle window.
It completed in 478.6 script seconds; collection and termination are verified.
The Zen 5 spatial script took 403.0 seconds. Both completed runs contain the
whole 432-sample matrix; no samples were combined across interrupted workers.
All 128 mapping snapshots on each completed host show RSS equal to committed
size, 4KiB pages, no anonymous huge pages and all mapped pages on guest N0.

## Derived studies

- [Paired consumer contrasts](comparisons/summary.md) compare candidate and
  baseline within the same workload, host and seed. CSV retains both absolute
  timings and allocated bytes; provenance hashes the source summaries and script.
- [Boolean refinement](refinement-counts/provenance.json) contains 96 exactly
  verified configurations. These are logical work counts and synthetic aligned
  address unions, with no native timings or signature false-positive model.
- [Palette/shortlist evidence](../selection/evidence/retained-final/summary.md)
  reuses historical TuplePack `final-v2` and `final-zen5` captures in place.
  Full candidate identities, held-out classes and source hashes accompany
  the derived output. This is a finite measured subset, not new timing or
  independent-container generalization.

## Reproduction and validation

Run Linux commands through `orb -m ubuntu` from macOS. Re-analysis should write
to an ignored directory or a copy of a native export so that original hashed
receipts stay immutable:

```sh
python3 workbench/spikes/layout-analyser/selection/check.py
python3 workbench/spikes/layout-analyser/selection/study.py \
  --output build/layout-analyser/selection-replay
python3 workbench/spikes/layout-analyser/refinement.py \
  --output build/layout-analyser/refinement-replay
python3 workbench/spikes/layout-analyser/compare.py \
  --output build/layout-analyser/comparison-replay
```

The captured native runners own source capture, incremental build workspaces,
compiler/binary identity, correctness checks and receipts:

```sh
python3 workbench/spikes/layout-analyser/run.py --march x86-64-v3 --tune zen5
python3 workbench/spikes/layout-analyser/spatial/run.py --profile screen \
  --large-mib 1024 --march x86-64-v3 --tune zen5 --phases 0,32 --reps 2 --ms 3
```

Use the actual host's tune and ISA; these commands do not provision machines.
The fleet wrappers are `cloud.sh` and `spatial/run.sh`. Recreate old results from
the recorded capture rather than silently substituting the latest checkout.

Opening verification passed all 12 TuplePack reference checks and replayed the
retained rankings. New native checks validate exact logical output, admitted
wire agreement and preserving writes for all prefix/bucket candidates. Local
AddressSanitizer/UndefinedBehaviorSanitizer checks passed before capture.
Spatial native checks cover 3,240 combinations; independent byte enumeration
checks 64B/128B demand, and Python checks CSV accounting, invalid geometry and
placement receipts. Each completed cloud run repeats its native checks, and
every spatial timed pass agrees with the generated logical oracle. The
[spatial local record](../spatial/LOCAL.md) preserves its validation context.

The offline selection check verifies full-subset coverage, valid selected
plans and held-out-cost isolation. The refinement script independently checks
exact match sets in every configuration. Complete comparison/evidence hashes
are verified before committing this empirical pass. No production Engine
implementation or interface is being tested or selected.
