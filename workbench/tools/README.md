# Research tools

Start with the task at hand. These helpers can be used independently; an
experiment can still be one script. Commands run from the repository root on
Linux; prefix with `orb -m ubuntu` from the Mac.

| I want to… | Start here |
| --- | --- |
| Run an existing script on the right CPU | [Workers](workers.md): `worker.py run SCRIPT --machine zen5`; Spot and compatible instance reuse are the defaults |
| Freeze sources while continuing to edit | [Capture example below](#captured-experiment-runs); `capture.py` also derives small variants |
| Write a local experiment runner | [experiment.py](experiment.py), used by the [aggregate runner](../spikes/aggregate-maintenance/run.py) for timings and the [regexp runner](../spikes/regexp-lowering/run.py) for counts |
| Rerun measured binaries without rebuilding | [Replay example](../benchmarks/README.md#replay-and-summarize): `replay.py` restores selected files and runs pinned sequential trials |
| Turn measurements into a compact table | [Summary examples](../benchmarks/README.md#replay-and-summarize): `evidence.py` reads raw or compact repetitions |
| Browse results or share an interactive snapshot | [Results explorer](../results/README.md): `results.py --serve` discovers retained evidence |
| Keep results, or recover selected files | [Retention and recovery](artifacts.md): `artifacts.py`; recover into `build/recovered/NAME` |
| Reuse input data | [Dataset catalog](../datasets/README.md): `datasets.py` prepares shared inputs; adapters live beside their datasets |
| Diagnose an expensive compilation | [Compile probes](compilation.md): replay selected TUs serially with timing, process RSS and failure evidence |
| Enable a study in the editor | [Editor guide](editors.md): `dev.py --add NAME` or `--add-benchmark NAME` |
| Check documentation links and headings | `python3 workbench/tools/check_docs.py`; advisory navigation hints |

The command entry points stay here. `experiment.py` and `evidence.py` also
provide small Python APIs; `worker_runtime.py` and `worker_pool.py` are worker
implementation details. Helper regression checks live under [tests/](tests/README.md),
separate from tools used to conduct research. Component-specific workloads and
applicability belong in their [benchmark suite](../benchmarks/README.md).

## Captured experiment runs

For a comparison across machines, capture once and run ordinary build/worker
commands from that checkout. Derive a small variant without taking unrelated
live edits:

```sh
python3 workbench/tools/capture.py create build/captures/base
python3 workbench/tools/capture.py create build/captures/variant \
  --base build/captures/base --replace ikea/examples/seriespack/ordinary.cpp
```

`--replace PATH` takes that live file; `PATH=INPUT` can map an ignored prototype
instead. Directories replace the subtree, `--drop PATH` omits a source, and
`--expect PATH=SHA256` checks an agreed input. The adjacent `.capture.json` records
the result; `capture.py verify PATH` checks it later. Captures exclude builds,
spike evidence and Python bytecode caches.

For an authored runner, `experiment.Run(..., workspace=...)` records sources,
commands and receipts while preserving incremental builds at stable paths.
Build from `run.source_root` into `run.build_dir`; runs sharing that workspace
serialize. `run.input()` records a prepared dataset; `run.compact()` selects
evidence and its offline regeneration command. The runners above are complete
examples; execution and analysis stay with the study.

## Changing a helper

Use a separate worktree while other tasks rely on the current tools; prepared
inputs can share `SIXDB_DATA_CACHE`. Discover and run the relevant offline check
with `python3 workbench/tools/check.py --list` and `check.py NAME`.
[Check prerequisites](tests/README.md) describe the few that compile fixtures.
