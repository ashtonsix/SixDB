# Retained dirty-buffer evidence

- [filter32](filter32/summary.md): 85 selected comparisons, five repetitions,
  32 KiB filters. Source digest
  `5ea6476db662c87b74af8efc483f553de814a6032f32235659664ad1561b4aba`.
- [filter8-and-controls](filter8-and-controls/summary.md): 30 selected four-writer
  comparisons, five repetitions, 8 KiB filters and two no-filter controls.
  Source digest
  `ce1d9200d47ed04c1db4ba1c301921cae30532dc94585006208b81ee7044f058`.
- [validation](validation/README.md): sanitizer and failed-analysis evidence.

Each run keeps individual repetitions in `samples.csv`, accounting, generated
tables, compact provenance, and an S3 bundle reference in Git. Full bundles
contain original JSON, receipts, exact source archives, flags, logs, and
binaries. Uploads and downloaded/restored bundles were SHA-256 verified before
replacing the full Git copies. Tables regenerate with unchanged numerical
values; CSV line endings are normalized.

The second source adds no-filter comparands; existing mechanisms and allocation
layout are unchanged. Both runs isolate append allocation metadata from
benchmark controls. Source archives retain the measured versions, independently
of subsequent documentation and workflow changes.

Regenerate without downloading:

```sh
python3 workbench/spikes/aggregate-maintenance/dirty-buffer/analyze.py \
  workbench/spikes/aggregate-maintenance/dirty-buffer/evidence/local-arm-20260907/filter8-and-controls
```

Restore the full run:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/aggregate-maintenance/dirty-buffer/evidence/local-arm-20260907/filter8-and-controls \
  build/recovered/dirty-buffer-filter8
```

From macOS, prefix with `orb -m ubuntu`; substitute `filter32` for that run.
See [retention and recovery](../../../../../tools/artifacts.md). Original local
runs remain under `build/experiments/aggregate-maintenance/dirty-buffer/`.
