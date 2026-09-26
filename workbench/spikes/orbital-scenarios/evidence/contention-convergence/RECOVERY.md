# Contention convergence evidence

This selection supports [the recommendation](../../CONVERGENCE.md). It keeps
every new semantic history and every locality cohort, including expected invalid
controls and costly conservative cases. The older 1,783-run study is retained
separately in [contention-comparison](../contention-comparison/RECOVERY.md).

- `dynamic-sql.json`: 13 computed application histories and their results.
- `envelope-audit.json`: 42 histories, authority counterexamples, overlapping
  admission search, semantic MAX comparison and the retention boundary.
- `composition.json`: 10 exact-byte, local schedule and publication-dependency
  histories. Verification acceptance is modeled, not implemented.
- `envelope-locality.json`: all 9 runs and their cohort outcomes/counters.
  Repeated input payloads are omitted here and retained in the full bundle.
- `study.json`: source hashes, raw-file hashes, selection and exact replay commands.

Numbers are logical counts or synthetic ticks/service costs. There are no
measured database latency/throughput results or distributed recovery proofs.

The self-contained replay snapshot passed its 22 checks, and its 9 locality runs
reproduced every raw output byte. The current spike's full 109 checks also passed.
Earlier comparison source identities were verified unchanged. These are checks
of this finite experiment, not evidence that every protocol interleaving works.

The retention tool uploaded and download-verified the bundle referenced by
`artifact.json`: 155,782 compressed bytes and 28 files. Recover from the repository
root into a new ignored directory:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/orbital-scenarios/evidence/contention-convergence \
  build/recovered/orbital-convergence
```

The recovered `replay/` directory contains the complete source/import set and
raw inputs/results. Run `study.json`'s `regenerate_from_replay_root` commands
there using Linux Python. The two checks, application/audit/composition programs
and locality runner need only the standard library. Prefix commands with
`orb -m ubuntu` when running from macOS.

The selection itself is reproduced by:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/convergence_export.py \
  --input build/orbital-convergence \
  --output build/orbital-convergence/evidence
```

All source hashes must still match the raw results; the exporter refuses an
inconsistent snapshot. Artifact retention adds transport/recovery provenance
but does not infer scientific validity from successful upload.
