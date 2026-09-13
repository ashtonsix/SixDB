# Run the runtime-map experiment

The [study](../README.md) owns cases and interpretation. This runner captures
source once, builds independent ISA profiles in an incremental workspace, checks
each executable, and runs cases/repetitions sequentially on one allowed CPU.
Live edits can continue while the captured run executes.

From the repository root on Linux (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/spikes/tuple-layout/runtime/run.py --profiles neon
# One Zen worker, with both profiles run serially from the same source:
python3 workbench/tools/worker.py run workbench/spikes/tuple-layout/runtime/cloud.sh \
  --machine zen5 -- --profiles avx2 avx512 --tune zen5
```

The worker uses Spot and compatible instance reuse by default. For a V2 worker,
select `--machine neoverse-v2` with `--profiles neon --tune neoverse-v2`.
Local tuning defaults to `generic`. ISA profiles use `armv8-a+simd`,
`x86-64-v3` and `znver5` respectively; AVX-512 therefore needs a compatible
host, not merely any x86 machine. `--tune` is separately recorded.

Use `--filter REGEX`, `--repetitions N` and `--min-time 0.05s` to select a run.
`--min-time 1x --repetitions 1` is an execution smoke check, not performance
evidence. `--cpu` defaults to the lowest allowed local CPU; workers supply it.
`--jobs` defaults to one. `--workspace` selects a reusable captured build and
`--output` a new result directory. Builds never share paths between profiles
or tuning choices; a workspace lock serializes runs that reuse them.

Each captured profile contains:

- `description.json`: the executable's exact `--describe` output, including
  ordered maps and canonical recipe controls. Its schema belongs to the study.
- `samples.csv`: every iteration repetition and counter, without derived
  aggregate rows. Times retain Google Benchmark's per-iteration units; no
  blanket division by 256 is applied to preparation cases or other operations.
- `context.json`: description, source and binary identities, flags, commands,
  build reuse, CPU and benchmark context. Preparation/reuse counters come from
  the benchmark; reusing a build directory does not imply reusing a prepared plan.
- Full benchmark JSON, checker output, executables/library, compile/link
  commands, CMake/Ninja files and build logs for diagnosis and replay.

The run root has a source archive and the shared `run.json` receipt. Failures
leave available evidence there with failed status. Nothing uploads on a local
run; workers collect their nested run automatically. For selected results, use
[retention](../../../tools/artifacts.md) on the printed run directory (on a worker,
the recovered `results/tuple-runtime` directory). The default compact selection keeps
samples, context and small check/size outputs; descriptions, source, binaries and
full logs remain in the recoverable bundle. Select runs and comparison families
that support a retained finding. The [evidence guide](../evidence/README.md)
identifies which historical samples remain in Git and how to recover the others.
Failed runs require the artifact
helper's generic `put`, rather than a successful compact export.


The [viability round](../viability.md) adds `fusion/`, `scan/`, `small/` and
`mixture/` families. Scalar candidate descriptions are embedded from
[point-subset.tsv](point-subset.tsv) at configuration; they never read a changing
live file during a captured run. [point-subset.json](point-subset.json) records
the 28-ID selection. Regenerate it with the [analyser reference](../../layout-analyser/tuplepack-reference/README.md) `select` and `enumerate`
commands. `extra-checks.txt` retains the additional native scan/scalar checks.

`report.py` exports measured scalar costs for the analyser; `mixed_report.py`
compares additive predictions and measured plan choices within the same declared
56-plan universe. `inspect.py BINARY --match REGEX` reports matching demangled
function sizes and stack accesses. Static stack observations are not PMU traffic.
