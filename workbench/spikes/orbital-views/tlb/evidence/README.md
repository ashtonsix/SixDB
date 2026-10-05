# Selected mapping-transition evidence

Job `20261005T114640Z-23ec48a5`, fresh Spot c8a.xlarge. The
[study](../README.md) explains the matrix, measured units and limits.
The six raw CSV files, their comparison, coverage check and host/kernel context
are selected. Each trial has 140 cells and each cell has 100 measured syscall
sequences. No useful remote workload or tail latency was measured.

At retention, `probe.cpp`, `run.sh` and `summarize.py` matched the worker's source
manifest. The [artifact](artifact.json) preserves captured sources, executed
binary and full output. [Provenance](provenance.json) covers selected bytes and
the regeneration command.

The earlier job `20261005T114136Z-6600b9dc` used private anonymous target pages.
Forking an independent helper made those pages COW and changed the protection
transition being compared. Its full worker archive remains recoverable; its
comparison is excluded here. This run uses shared anonymous target pages,
keeping backing shareability consistent across contexts. An independent
expected-byte check runs after each cell's measurement.

Rebuild the report without rerunning the experiment:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/orbital-views/tlb/evidence build/reports/orbital-views-tlb
```

Both worker instances were configured with `--idle-seconds 0`; their shutdown
was checked after collection.
