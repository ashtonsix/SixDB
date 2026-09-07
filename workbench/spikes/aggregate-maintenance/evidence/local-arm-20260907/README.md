# Retained local ARM evidence

- [screen/summary.md](screen/summary.md): 14 cases, four methods, three repetitions.
- [confirm/summary.md](confirm/summary.md): five selected cases, five longer repetitions.
- [validation](validation/README.md): oracle, sanitizer, build/editor and workflow checks.

Each measured run keeps `samples.csv` (every individual repetition),
`accounting.csv`, generated tables, `provenance.json`, and `artifact.json` in
Git. Full bundles in S3 contain the original raw JSON, receipts, source
archives, compilation databases/caches, command logs, and binaries. Both runs
have measured source digest
`f6ef8f254f3d33d36cfbb00698b691b5958dfadad2704cbbcc80f0d3e6d57a65`.

These measurements predate relocation from `workbench/prototypes/aggregate-deltas/`
and `workbench/design/aggregate-summaries/`. Recorded commands and source
archives preserve their original contents. The compact numerical tables
regenerate identically with the current analyzer; CSV line endings are normalized.

From the repository root, regeneration requires no download:

```sh
python3 workbench/spikes/aggregate-maintenance/analyze.py \
  workbench/spikes/aggregate-maintenance/evidence/local-arm-20260907/confirm
```

Restore a complete run, including the exact measured source:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/aggregate-maintenance/evidence/local-arm-20260907/confirm \
  build/recovered/aggregate-confirm
```

From macOS, prefix with `orb -m ubuntu`. Substitute `screen` for that run;
validation has its own artifact reference. Uploads and full downloads were
SHA-256 verified before Git's full bundles were replaced. Original local
runs remain under `build/experiments/aggregate-deltas/`; they are not required
in another checkout. See [retention and recovery](../../../../tools/artifacts.md).
