# Retained evidence

`adjacent-{zen5,granite-rapids}-20261012/` retains every seed/repetition
distribution from the corrected adjacent-fetch matrix, including negative
controls, with reported hardware and actual mapping receipts. Its archive-based
report recipe recovers raw trials and regenerates classifications. The separate
`adjacent-runtime-*` exports check the final compiled table quarantine and real
bounded fallback. `adjacent-unfenced-20261012/` keeps the rejected control cut by
archive reference only. [Method and results](../ADJACENT.md) explain the selection
and conditional conclusions.

`adjacent-neoverse-v2-20261012/` keeps the corrected safe standalone ARM matrix;
its instruction/timer/stamp metadata is distinct from x86. The three
`platform-*-20261012/` exports retain actual typed native inventory and
conditioned Loom observation records, independently compared with Linux on each
host. These do not infer DRAM service from footprint or turn local candidates
into shared hardware constants. [Loom's account](../../../../loom/docs/hardware.md)
owns their architectural use and native inspection commands.

The `screen` and `repeat` directories retain complete sweep rows (including
controls and all repetitions), hardware/page/topology facts, initialization
cost, selected timer and actual mapping receipts, and optional filesystem /
transport diagnostics. `provenance.json` and `artifact.json` come from the
shared artifact helper. Full source, binaries and logs are recoverable from
those references. The source and measurements are immutable; generated lookup
and fleet tables live one level above and can be rebuilt offline.

`campaign.json` maps each instance example to its exact worker run. Most pairs
are two independent seeds on one worker. c6i and i4i share a CPU context and
are pooled only after their near-optimal sets and throughput agree. These are
not independently rebooted repetitions of every model. Source captures differ
as diagnostics were added; retained per-run source digests identify those
versions. MLP's encoded-chain kernel and units are common across the comparison.

`streams/` retains the ordered-versus-shuffled follow-up on Zen 5 and Granite
Rapids. `lookup-validation/` retains the actual returned profile and process
cost from live static hits on those machines. They are ordinary worker bundles,
so their small selection manifests use `selected_files_sha256`; full source
and binaries remain in the referenced archive. `local-unknown/` retains the
successful lookup-miss path and unresolved single-load controls on the ARM VM.

The superseded first prefetch-retention design and the failed NUMA binding
attempt are not lookup inputs. Their diagnostic corrections are explained in
METHOD/FINDINGS; the successful replacement comparisons are retained here.
The serialized-stream cliff is a one-seed observation and is not inserted as
an exact prefetch-table capacity.

Rebuild the lookup and fleet comparison from repository root on Linux:

```sh
python3 workbench/spikes/memory-characterisation/lookup.py
python3 workbench/spikes/memory-characterisation/check.py
python3 workbench/tools/artifacts.py verify workbench/spikes/memory-characterisation/evidence
```

Use `plot.py` with Matplotlib to recreate the static figures (rendered with
Matplotlib 3.11.2). `retain.py` repeats the selected export from locally collected
worker jobs. To recover an omitted file or the full run, use the normal
[artifact helper](../../../tools/artifacts.md), for example:

```sh
python3 workbench/tools/artifacts.py fetch workbench/spikes/memory-characterisation/evidence/c8a.large/screen/artifact.json build/recovered/memory-zen5
```
