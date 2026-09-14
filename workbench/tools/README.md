# Research tools

Commands run from the repository root on Linux; prefix with `orb -m ubuntu`
from the Mac. Use the helpers independently or start from an existing runner.

| Task | Tool and guide |
| --- | --- |
| Run scripts on EC2 | [Workers](workers.md), [worker groups](worker-groups.md); Spot and compatible reuse by default |
| Freeze sources while editing | [Capture example below](#captured-experiment-runs) |
| Reuse input data | [Datasets](../datasets/README.md): `datasets.py get NAME` |
| Replay binaries or summarize measurements | [Benchmark tools](../benchmarks/README.md#replay-and-summarize): `replay.py`, `evidence.py` |
| Browse or share results | [Results explorer](../results/README.md): `results.py --serve` |
| Retain or recover evidence | [Artifacts](artifacts.md): `artifacts.py`; [local storage](storage.md): `worker.py cache` |
| Diagnose compilation or editor issues | [Compile probes](compilation.md), [editor setup](editors.md) |

## Captured experiment runs

An ordinary worker run captures current sources itself. For several machines
or a variant based on earlier bytes, capture explicitly and use current tools:

```sh
python3 workbench/tools/capture.py create build/captures/base
python3 workbench/tools/capture.py create build/captures/variant \
  --base build/captures/base --replace ikea/examples/seriespack/ordinary.cpp
python3 workbench/tools/worker.py run workbench/benchmarks/seriespack/run.sh \
  --source build/captures/variant --machine zen5
```

`--replace PATH` takes that live file or directory; `PATH=INPUT` maps a different
input into the capture. `capture.py create --help` covers deletion and hash checks.
The adjacent `.capture.json` identifies the result; `capture.py verify PATH`
checks it later. Builds, spike evidence and Python caches are excluded.

For a local runner, `experiment.Run(..., workspace=...)` captures sources and
preserves incremental builds. Build from `run.source_root` into `run.build_dir`;
runs sharing a workspace serialize. `run.input()` records a prepared dataset;
`run.compact()` names [selected evidence](artifacts.md#make-selection-repeatable).
The [aggregate runner](../spikes/aggregate-maintenance/run.py) measures timings;
the [regexp runner](../spikes/regexp-lowering/run.py) studies counts.

## Changing a helper

A separate worktree keeps current tools available to other tasks. Discover the
relevant [offline check](tests/README.md) with `python3 workbench/tools/check.py --list`;
run it with `check.py NAME`. For documentation edits, `check_docs.py` offers link
and catalog hints. Neither tool is a required experiment step.
