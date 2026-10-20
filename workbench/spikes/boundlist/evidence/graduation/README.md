# Boundlist production evidence

The [scorecard](scorecard.md) presents readable absolute-ns slices. The
[implementation report](../../GRADUATION.md) explains adoption, limits and source
contracts. `context.json` identifies each job, immutable source capture, machine,
raw archive and hardware receipt; `*-inputs.json` hashes the exact selected raw
files. Raw repetitions, full sources, binaries, disassembly, RSS/host receipts and
checks are recoverable through those worker artifacts.

## Selection

- `search.csv`: final scalar/real-batch prepared and complete point consumers,
  native u16/u32/u64, N256/4096/16384, positive/duplicate/skewed geometry, including
  independent fused scans, ordinary search, numeric bindings and phase controls.
- `ordered.csv` / `ordered128.csv`: complete point/range extrema consumers,
  duplicate/population/one-versus-scattered lists. The 128-bit comparison predates
  the numeric factory and remains comparison-only, without a metric requirement.
- `upper.csv`: closed-edge consumers with unique values and runs17/80. It uses
  prepared queries plus 8B selected children and a full 1024-query cycle; its
  envelope differs from the generated-query/hash point panel.
- `promotion.csv`: final physical reverse/range and logical point consumers,
  frozen before bodies and independent fused controls. `checkpoint_candidates.csv`
  retains the decisive pre-adoption exact/padded comparison, including scatter.
- `cold_adoption.csv`: actual typed sidecar phases in compact 64B frames,
  direct serial/eight-way and actual Loom16. `cold.csv` keeps the broader
  independent/manual/no-hint scheduler controls. `frames_regular.csv` and
  `frames_compact.csv` retain matched frame/selector experiments rather than
  crediting a smaller owner frame to the kernel.
- `reads.csv` / `mutation.csv`: complete composition and original-source effect
  consumers, retained alongside frozen mutation bodies and independently fused
  readers. The rejected flattened adapter and losing cells remain visible.
- `model_alias.csv`: failed 4-slot adaptive correction for quantile-aliased
  duplicate runs; it explains the minimum bracket choice.
- `preparation.csv`: one 4096-construction cache-hot loop mean per case and binding
  bytes, separately from query timing. It is not repeated/cold-source setup.
- `calls.csv`: complete linked-caller audits, including all 132 actual Ring
  callbacks per cold profile. Binaries are SHA256-checked against worker archives;
  no ordinary kernel helper calls remain. Assertions are excluded by the audit.

Median/min/max/repetitions come from 3 or 5 trials, as recorded. Latency and
reciprocal throughput are separate dimensions; batch ns is per query, not batch
completion latency. Metadata, child, allocated capacity, binding, frame and
scheduler bytes retain their own columns. The large cold Width operand bound is
288MiB; combined 544MiB includes an unused Direct alternative. Granite reports
480MiB L3, so these cells are large random consumers, not proven DRAM residence.

## Reproduce and recover

On Linux, recover the worker jobs in `context.json`, then run:

```sh
python3 workbench/spikes/boundlist/consolidate.py workbench/spikes/boundlist/graduation-inputs.json build/boundlist/graduation
python3 workbench/spikes/boundlist/supplements.py workbench/spikes/boundlist/graduation-inputs.json build/boundlist/graduation
python3 workbench/spikes/boundlist/scorecard.py build/boundlist/graduation
```

Prefix commands with `orb -m ubuntu` from the Mac. Supplements fetches any evicted
binaries through the existing verified worker archive before auditing them.
The retained artifact's report recipe regenerates the scorecard from its CSVs.
The aggregate full bundle additionally preserves the reporting scripts; exact
measured C++ sources stay in each worker archive. No failed/two-repetition phase
cells from the initial cold trial-order omission are included.
