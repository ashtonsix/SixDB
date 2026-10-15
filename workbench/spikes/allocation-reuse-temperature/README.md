# Allocation reuse and temperature advice

**Use cheap reuse selection and keep temperature advice for reclamation.**
The tested HOT → WARM → COLD queues lost on ordinary allocation traces.
A subsequent ablation supported removing hint ranking from both backends.
[Findings](FINDINGS.md) explain the decisions and comparison limits.

Windows now take the first compatible, confirmed prepared extent requiring no
extra charge; fallback still minimizes additional charge. Finite Groups retain
their ready-candidate cache and search prepared homes before unprepared homes.
Release temperature and timestamps remain useful for cold-first trim and stale
advice. Neither is proof of cache residence.

The current-API [reuse and page-retention comparison](slack/README.md) separates
accessible capacity from resident RAM, prices closer-fit searches and examines
stable address reuse with tail-page reclamation.

`allocator.cpp` exercises actual Orbital allocation through Loom. It includes
partial Groups with rotating classes that miss the candidate cache, and Window
traces with size bursts, retained live allocations and mixed release hints.
`--touch-bytes 64|full` separates one-line bookkeeping work from full-payload
memory traffic. Data references and retained-live checks run outside timing.
`probe.cpp` separately isolates the earlier queue mechanisms.

## Reproduce the comparison

The variants describe the historical allocator. Recover its captured source
rather than applying them to today's simplified API:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/allocation-reuse-temperature/evidence/zen5-ablation \
  build/recovered/reuse
python3 workbench/spikes/allocation-reuse-temperature/run.py \
  --source build/recovered/reuse/comparison/source-baseline \
  --output build/reuse-comparison --cpu 0 \
  --policies baseline,window-first-ready,finite-two-pass,combined \
  --touch-bytes 64,full --trials 8 --rounds 8 --operations 65536
```

Use Linux and an allowed CPU. The runner freezes each variant, qualifies it,
and runs fresh processes sequentially with balanced policy order. The
[worker recipe](cloud.sh) runs the same comparison; set
`SIXDB_REUSE_DIAGNOSTICS=1` for separate untimed selection-path counts.
The old queue probe is opt-in through `SIXDB_REUSE_POLICY_PROBE=1`.

[Selected samples](evidence/zen5-ablation/comparison/samples.csv) retain all
matching policies, trials, widths and VM counters. Regenerate trial medians,
observed ranges and inventory summaries without downloading compiler output:

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/allocation-reuse-temperature/evidence/zen5-ablation \
  build/reports/reuse
```

The [API/layout replay](evidence/zen5-cleanup/README.md) separately compares the
promoted cleanup to the exact measured combined binary. Both archives recover
sources, binaries and full checks. The earlier
[cascade](evidence/zen5-cascade/artifact.json) and
[candidate-cache](evidence/zen5-cached/artifact.json) campaigns retain the rejected
alternatives. [Recycling](../orbital-views/evidence/recycling/README.md) established
retained reuse and background reclamation, not an aging policy.
