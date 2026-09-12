# TuplePack spike evidence

Measurements refer to the source and binaries recorded by each capture. The
[module measurements](../module-evidence/README.md) are a separate comparison.

| Captures | Available in Git | Purpose |
| --- | --- | --- |
| `final-zen5`, `final-v2` | All sample repetitions, counters, context and checks | Final physical/composition/scan results and complete analyser cost inputs |
| `mixed-zen5`, `mixed-v2` | All sample repetitions, counters, context and checks | Earlier mixed-workload counterexample, including every candidate needed to reproduce measured minima and regret |
| `runtime-first-zen5`, `runtime-first-neon` | All sample repetitions, counters and context | Runtime-map baseline, including controls and comparisons beyond the displayed table rows |
| `neon-initial` | Original CSV, receipt and validation | Initial local screen; no separate archive reference |
| `fusion-*-normalized`, `viability-zen5`, `viability-v2`, `single-write-v2` | Context, checks, size observations and archive references | Intermediate timing sweeps are recoverable; final captures cover the later operating point |
| Correctness and ABI captures | Small check outputs, identities and selected assembly | Specific correctness and calling-convention observations |

Physical-description dumps live in each full archive, including the exact maps
and controls used for that measurement. They are not inputs to the offline timing
reports. Each curated `provenance.json` keeps the original source/run identities
and records excluded files in `archived_files`, with their hashes, archive
references and member names. Selecting fewer Git files is not a new measurement.

## Recover a report or a description

Run from the repository root on Linux; prefix with `orb -m ubuntu` on the Mac.
Choose a new destination under `build/`:

```sh
python3 workbench/spikes/tuple-layout/runtime/recover.py \
  workbench/spikes/tuple-layout/evidence/viability-v2 \
  build/recovered/tuple-viability-v2
python3 workbench/spikes/tuple-layout/runtime/report.py \
  build/recovered/tuple-viability-v2/evidence
```

[The wrapper](../runtime/recover.py) uses the shared retention tools to download
and verify the complete archive and its original run receipt. It reconstructs
the captured compact selection under `evidence/`, including archived CSVs and
their provenance, so existing reports accept the result. Full logs, binaries and
`source.tar.gz` are under `run/`. No worker or new measurement is involved.

Use the same command with `single-write-v2` to inspect that intermediate writer
capture, or with a final/mixed/runtime-first capture to recover its exact controls.
For only one description, the shared fetcher avoids expanding the whole run:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/tuple-layout/evidence/final-v2 \
  build/recovered/tuple-final-v2-description --file neon/description.json
```

This verifies the archive hash and extracts the selected member; it does not
construct a complete report input. See [retention and recovery](../../../tools/artifacts.md)
for rebuilding from captured source or inspecting measured binaries.
