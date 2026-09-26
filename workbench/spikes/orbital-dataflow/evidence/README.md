# Selected dataflow evidence

2026-09-26, Ubuntu Python 3.13.7, standard library only. These are exact logical
computations, finite semantic histories and a synthetic byte/tick resource
model. No wall-clock benchmark or production durability/latency result is claimed.

| File | Selection and assumptions |
| --- | --- |
| [exchanges.json](exchanges.json) | All 26 authored join comparisons, nine queue comparisons and algebra counterexamples. Complete-input build-size/heavy-key knowledge; synthetic byte/work counts; result validation is outside modeled build memory. |
| [progress.json](progress.json) | Six finite-cut join histories, seven recursive-change comparisons, progress/coverage, recovery and cancellation/retention examples. Includes deliberately invalid controls. Source closure, atomic accepted transitions, checks and sink deduplication are supplied where stated. |
| [resources.json](resources.json) | All 21 selected policy/capacity/cancellation cases, including rejection and incomplete work. Complete counters plus first/last eight events and lifecycle events; full trace hashes permit regeneration without retaining repeated chunk events. Synthetic service times, single worker/writer, supplied durability/decision facts. |
| [lineage.json](lineage.json) | All 12 reuse/reconstruction examples, including unavailable inputs and wrong live-alias recovery. Exact fixed pure workload, bounded cache/checkpoint bytes; no timing or real storage/host failure. |
| [manifest.json](manifest.json) | SHA-256 identities of all executable/check sources and the four evidence artifacts, recorded only after successful checks with unchanged source hashes. |

The independent oracle tests also cover seeded/random and exhaustive small
inputs. Their routine successes are reproducible from source and seeds rather
than retained individually. All chosen comparative cases remain visible,
including failures, refusal, stalls and negative controls. Claims and excluded
costs stay in the owning guide.

From the repository root:

```sh
# Run focused checks, check hashes and exactly reproduce retained JSON.
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/check.py
# Verify source/artifact hashes without running work.
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/check.py --verify-only
```

To intentionally regenerate the selected evidence after changing this study,
`check.py --retain` runs every generator/check, verifies source stability and
replaces the four JSON files and manifest. It does not fetch data, provision
machines or publish outside the repository.

Individual generators accept `--output PATH`:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/exchange_probe.py --output build/orbital-dataflow/exchanges.json
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/progress_probe.py --output build/orbital-dataflow/progress.json
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/resource_probe.py --output build/orbital-dataflow/resources.json
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/lineage_probe.py --output build/orbital-dataflow/lineage.json
```

The resource simulator's `run(case)` retains its full authored event trace in
memory; the CLI selects nonrepetitive trace evidence. Probe sources and their
SHA-256 identities provide the reconstruction recipe. There are no external
datasets, omitted large binary inputs or shared-simulator dependencies to recover.
