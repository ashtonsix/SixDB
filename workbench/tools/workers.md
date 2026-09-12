# Run a script on a temporary worker

From the repository root on Linux (prefix with `orb -m ubuntu` from the Mac):

```sh
python3 workbench/tools/worker.py run workbench/tools/worker-smoke.sh
python3 workbench/tools/worker.py run workbench/spikes/your-study/cloud.sh \
  --machine zen5 --env CASE=small -- --repetitions 3
```

The second script is illustrative. `run` captures current sources, including
uncommitted work, executes the script and collects results in
`build/workers/JOB/results/`. It uses Spot and reuses a compatible idle worker.
After collection, the worker stays ready for five minutes, then shuts down.
No commit, SSH key or job description file is needed.

If exact-type Spot capacity is unavailable, `--capacity spot-or-on-demand`
permits fallback; `--capacity on-demand` requests it directly. Larger working
sets, compiler memory and topology questions may need `--instance-type TYPE`.

| Machine | Default instance |
| --- | --- |
| `zen5` | `c8a.medium`: 1 vCPU, 2 GiB |
| `granite-rapids` | `c8i.large`: 1 visible vCPU with SMT disabled, 4 GiB; billed as a large |
| `neoverse-v2` | `c8g.medium`: 1 vCPU, 2 GiB |

Hardware receipts establish the measured host. Choosing a machine does not
change the study's `SIXDB_MARCH` or `SIXDB_TUNE` settings.

## Writing the script

A repository shell script starts at the captured repository root:

```sh
#!/bin/bash
set -euo pipefail
cmake --preset dev -DSIXDB_SPIKES=your-study
cmake --build --preset dev --target your_study_bench -j "$SIXDB_BUILD_JOBS"
taskset -c "$SIXDB_CPU" build/clang/dev/workbench/spikes/your-study/your_study_bench \
  "$@" > "$SIXDB_RESULTS/samples.json"
```

An existing runner can do the build, checking and measurement instead; direct
wanted output beneath `$SIXDB_RESULTS`. Arguments after `--` pass literally.
Repeated `--env NAME=VALUE` settings are recorded, so use them for configuration,
not secrets. Local AWS credentials are not copied to the worker.

| Variable | Meaning |
| --- | --- |
| `SIXDB_RESULTS` | Output to collect, including nested experiment runs |
| `SIXDB_SOURCE` | Captured repository and initial working directory |
| `SIXDB_CPU` | First allowed CPU, or an explicitly supplied allowed CPU |
| `SIXDB_BUILD_JOBS` | Defaults to `1`; build commands opt into it |
| `SIXDB_DATA_CACHE` | Shared prepared inputs outside collected results |
| `SIXDB_JOB`, `SIXDB_SOURCE_COMMIT` | Job ID and original local HEAD |
| `SIXDB_WORKER_ID`, `SIXDB_WORKER_REUSED` | Instance session and `0`/`1` reuse indicator |
| `SIXDB_RESULTS_S3` | Mutable `live/` prefix for optional script checkpoints |

Setup installs pinned Clang, LLVM tools, CMake, Ninja, Git, Python/PyYAML and a C++
standard library. Script-specific dependencies belong in the script. Reuse keeps
installed packages, unchanged build inputs and prepared datasets. `--fresh` asks
for a new environment; add `--idle-seconds 0` to terminate it after this job.
For longer edit/review loops use `--idle-seconds 600`; a narrowly missed reuse
window can prompt this suggestion in the console.

For an explicit [source capture](README.md#captured-experiment-runs), use
`run SCRIPT --source PATH`. The script is relative to that checkout; job records
stay with the current controller. Study/setup bytes and controller runtime hashes
are recorded separately. The worker's synthetic Git commit differs from original
HEAD. Builds and datasets are caches; only `$SIXDB_RESULTS` is collected.

## Watching and collecting

```sh
python3 workbench/tools/worker.py run workbench/tools/worker-smoke.sh --detach
python3 workbench/tools/worker.py status JOB
python3 workbench/tools/worker.py wait JOB    # resume observation and collect
python3 workbench/tools/worker.py logs JOB
python3 workbench/tools/worker.py fetch JOB  # collect or repair local output
python3 workbench/tools/worker.py cancel JOB
python3 workbench/tools/worker.py list
```

Ctrl-C detaches. Cleanup continues without the controller after boot; `wait`
recovers missing local job records from S3. `cancel` only terminates a worker
still owned by that job. Failed jobs are not automatically resubmitted, and an
uncertain reuse claim stops rather than dispatching twice. A worker that never
boots needs controller cleanup through `wait` or `cancel`.

Completion requires a verified uploaded bundle; nonzero script exits remain
failures with output available. Vanished workers or failed uploads are incomplete.
Scripts can opt into `--sync-seconds N` for partial-output recovery; by default
uploads happen after measurement. `logs JOB --console` shows instance-wide boot
diagnostics, possibly including earlier jobs on a reused worker.

Spot workers poll interruption notices every five seconds and attempt early
collection when warned; output can still be lost. On-demand with sync disabled
has no such observer. Keep this context with timing comparisons. Fresh instances
do not establish cold hardware caches; the benchmark controls cache residence.

Ordinary fetch keeps measurements and metadata locally. Use `fetch JOB --list`,
`--file PATH` or `--full` to recover compiler output and prepared inputs on demand;
[local storage](storage.md) owns those details and repair behavior.
[Retention](artifacts.md) selects evidence for Git from the collected study.

## Configuration reference

`worker.py run --help` lists overrides for deadline, lifetime, disk, setup and
sync; [worker.json](worker.json) owns defaults and pinned Ubuntu AMIs. Use
`worker.py --config SETTINGS.json run SCRIPT` for a small reusable JSON overlay.
`worker.py plan` resolves settings with read-only AWS calls.

By default, the first run creates a reusable IAM profile and a security group
with no inbound rules in the default VPC. The role reads the artifact/input
stores and writes under `sixdb/`; it cannot delete objects or manage EC2.
An overlay can supply `vpc_id`, `subnets`, `public_ip`, `security_group_id` and
`instance_profile` instead. Subnets need outbound package/S3 access; custom
profiles need access to shared input stores as well as the chosen result bucket.
[Offline checks](tests/README.md) cover helper development.
