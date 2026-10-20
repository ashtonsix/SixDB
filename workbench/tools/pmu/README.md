# Linux PMU diagnostics

Counter support for Workbench consumers, independent of database modules and
peer libraries. Build just the availability probe with the pinned toolchain:

```sh
cmake -S . -B build/tools/pmu -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/tools/pmu --target sixdb_pmu_probe -j1
python3 workbench/tools/pmu/probe.py \
  build/tools/pmu/workbench/tools/pmu/sixdb_pmu_probe \
  --cpu 0 --output build/pmu-check
```

Choose an allowed CPU. Commands run from the repository root on Linux; prefix
with `orb -m ubuntu` on the Mac. A worker can run the same tool:

```sh
python3 workbench/tools/worker.py run workbench/tools/pmu/run.sh --machine zen5
```

Use `--machine neoverse-v2` for ARM. The probe needs no architecture-specific
optimization flags or LMDB installation. `SIXDB_MARCH` and `SIXDB_TUNE` remain
explicit overrides. `run.sh` also compares noninteractive sudo access; it changes
no perf policy. Repeat `--event NAME:TYPE:CONFIG` after the worker's `--` to try
raw events individually. The collector keeps the measured binary, its hash,
sysfs catalog, raw stdout/stderr, per-process exit codes and measurement status.
Exit zero can still mean counters are unavailable: read `probe.json`.

A CMake consumer links `workbench::pmu`, includes `<pmu/pmu.h>` and uses
`workbench::pmu::Session`. The implementation compiles once per build tree.
Create a session before timing, then start/stop it once on the constructing
thread. It counts only that thread's user-space execution, with no helper-thread
inheritance. The required cycles/instructions pair shares a fresh group; event
IDs bind counts to events. Optional failures, enabled/running coverage,
multiplexing and unavailable/error states survive in JSON. Counts are unscaled;
missing values remain null. Off makes no perf syscall.

The short probe establishes availability. Use the separate
[memory sensitivity controls](../residency.md) to qualify an event over real
memory accesses before using it in an experiment. Workloads still own their
measurement boundaries. Historical Orbital captures retain their original PMU
source and identities; the move does not revise old measurements.
