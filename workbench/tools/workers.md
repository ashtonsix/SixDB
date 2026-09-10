# Run a script on a temporary worker

From the repository root on Linux (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/tools/worker.py run workbench/tools/worker-smoke.sh
python3 workbench/tools/worker.py run workbench/spikes/your-study/cloud.sh \
  --machine zen5 --capacity on-demand --env CASE=small -- --repetitions 3
```

The second script is illustrative. A script receives the current source,
including uncommitted and non-ignored new files, on one EC2 worker at a time.
The command waits and downloads verified results to `build/workers/JOB/results/`.
After collection the worker stays ready for another compatible job for five
minutes, then shuts itself down. Collection and cleanup continue if the
controlling session disconnects. No commit, SSH key,
open inbound port, job file, or study framework is needed.

## Writing the script

A repository shell script runs with the repository as its working directory.
For example:

```sh
#!/bin/bash
set -euo pipefail
cmake --preset dev -DSIXDB_SPIKES=your-study
cmake --build --preset dev --target your_study_bench -j "$SIXDB_BUILD_JOBS"
taskset -c "$SIXDB_CPU" build/clang/dev/workbench/spikes/your-study/your_study_bench \
  "$@" > "$SIXDB_RESULTS/samples.json"
```

Use the [study's runner](../spikes/README.md#building-a-study) instead when it
already handles configuration, checking and measurement; direct its output
under `$SIXDB_RESULTS`. An existing runner that chooses its own allowed CPU can
continue doing so. The worker supplies:

| Variable | Meaning |
| --- | --- |
| `SIXDB_RESULTS` | Files to collect, including any nested experiment runs |
| `SIXDB_SOURCE` | Captured repository directory; also the initial working directory |
| `SIXDB_CPU` | First allowed CPU, or an explicitly supplied allowed CPU |
| `SIXDB_BUILD_JOBS` | Defaults to `1`; build commands opt into using it |
| `SIXDB_DATA_CACHE` | Prepared-data cache outside the collected results |
| `SIXDB_JOB`, `SIXDB_SOURCE_COMMIT` | Worker job ID and original local HEAD |
| `SIXDB_WORKER_ID`, `SIXDB_WORKER_REUSED` | Instance session ID and `0`/`1` reuse indicator |
| `SIXDB_RESULTS_S3` | Job's mutable `live/` prefix, for optional script checkpoints |

Arguments after `--` are passed literally. Repeat `--env NAME=VALUE` for settings.
These values are recorded with the job; they are configuration, not a secret
store. No local AWS credentials are copied to the instance.

The default setup installs the pinned Clang 21.1.8, LLVM tools, CMake, Ninja,
Git, Python/PyYAML and a C++ standard library. Other dependencies belong in the
script. `--setup minimal` installs only the bootstrap tools; it is useful for
shell-only jobs or an explicitly selected prepared AMI. Package versions,
compiler output, hardware, source hashes, and logs accompany the results.
Compatible reuse skips completed toolchain setup. Packages installed by earlier
scripts also persist; use `--fresh` when the experiment needs a new OS environment.

Source selection matches [local experiments](README.md#captured-experiment-runs).
A synthetic Git commit lets runners use Git; its hash differs from local HEAD.
`job.json` records the original commit and the actual captured source digest.
Each job replaces the source snapshot at a stable path, preserving unchanged
mtimes, `build/`, and the separate dataset cache. These caches are not collected;
put wanted outputs under `SIXDB_RESULTS`.

## Configuration

| Option | Default / effect |
| --- | --- |
| `--machine zen5` | `c8a.medium`: one vCPU, 2 GiB |
| `--machine granite-rapids` | `c8i.large`: one core, 4 GiB; SMT disabled for one visible vCPU, still billed as a large |
| `--machine neoverse-v2` | `c8g.medium`: one vCPU, 2 GiB |
| `--instance-type TYPE`, `--ami ID` | Explicit capacity or image override; architecture is checked |
| `--capacity spot` | Default; tries subnets offering that exact instance type |
| `--capacity on-demand` | Uses On-Demand capacity |
| `--capacity spot-or-on-demand` | Explicit fallback after Spot capacity failures; preserves instance type |
| `--deadline SECONDS` | Per-job deadline, default 3600; includes dispatch, initial boot/setup, script and collection |
| `--idle-seconds N` | Default 300 after collection; zero ends the worker after this job |
| `--max-age N` | Maximum instance age, default 14400 seconds; a new job must fit its remaining lifetime |
| `--fresh` | Launch a new instance even if a compatible worker is idle |
| `--disk-gb N` | Default 24 GiB encrypted gp3 root disk, deleted on termination |
| `--sync-seconds N` | Default 0; opt into periodic live output uploads |

The instance sizes come from [AWS's specifications](https://docs.aws.amazon.com/ec2/latest/instancetypes/co.html).
One vCPU is a useful starting point for microbenchmarks. Larger working sets,
compiler memory, multiple threads, or cache/topology questions can justify a
larger type. Inspect the recorded host rather than inferring its cache or clock
from another instance size. `SIXDB_TUNE` and ISA flags remain choices in the
study's CMake configuration; choosing a machine does not silently change them.

[worker.json](worker.json) holds repository defaults and pinned Ubuntu 24.04
AMIs. CLI options override them. For reusable personal or study settings, use
`worker.py --config path/to/settings.json run SCRIPT`; a small JSON overlay can
set any default, `env`, `vpc_id`, `subnets` (subnet IDs), `public_ip`,
`security_group_id`, or `instance_profile`. Explicit network/profile IDs bypass
their automatic creation.
The subnet must have outbound access to package repositories and S3. AMI IDs
are region-specific. `worker.py plan` resolves settings and hardware using
read-only AWS calls before allocating anything.

The first submission otherwise creates a reusable `sixdb-worker` IAM role/profile
and a security group with no inbound rules in the default VPC. The role reads
the artifact bucket and writes under `sixdb/`; it cannot delete objects or manage
EC2. Source/job objects use `s3://calico-fleet-artifacts/sixdb/workers/JOB/`;
final bundles use the [artifact store](artifacts.md).

## Reuse between jobs

Ordinary `run` first looks for an idle worker with matching hardware/topology,
AMI, disk, network/profile, setup and worker-runtime versions. It respects the
requested capacity and allowed subnets. Otherwise it launches normally. No
session name or reservation step is required. For example:

```sh
python3 workbench/tools/worker.py run workbench/spikes/your-study/cloud.sh
# Edit the study; a compatible worker can keep its toolchain, datasets and builds.
python3 workbench/tools/worker.py run workbench/spikes/your-study/cloud.sh
# A completely disposable run:
python3 workbench/tools/worker.py run workbench/spikes/your-study/cloud.sh --fresh --idle-seconds 0
```

Use `--idle-seconds 600` for a longer edit/review loop. A launch that narrowly
misses a compatible worker's idle expiry may suggest this in the console.

Job/host receipts record worker identity, reuse count, and setup reuse. The
benchmark still controls warmup and cache residence; `--fresh` gives a new
instance, not a hardware-cache guarantee. A collected nonzero script exit can
leave a reusable worker. Setup failures, timeouts, interruptions, and failed
collection retire it. `--fresh` can itself leave an idle worker; add
`--idle-seconds 0` to end it after collection.

## Leave it running and come back

```sh
python3 workbench/tools/worker.py run workbench/tools/worker-smoke.sh --detach
python3 workbench/tools/worker.py status JOB
python3 workbench/tools/worker.py wait JOB
python3 workbench/tools/worker.py logs JOB
python3 workbench/tools/worker.py logs JOB --console
python3 workbench/tools/worker.py fetch JOB
python3 workbench/tools/worker.py cancel JOB
python3 workbench/tools/worker.py list
```

`wait` resumes observation and fetches results, reconstructing missing local job
records from S3. Ctrl-C detaches the controller. `cancel` terminates a worker only
while it still belongs to that job, including its subsequent idle window. It
cannot terminate a worker already claimed by another job. `status` includes the current session state;
`list` shows session IDs, current jobs and idle/busy state.
A failed experiment is never automatically resubmitted. An uncertain reuse claim
stops dispatch rather than risk running a job twice.

Scripts and timeouts attempt collection; completion requires a verified uploaded
bundle. Nonzero exits remain failures with logs and outputs available. Failed
uploads or vanished workers are incomplete; any live output is recoverable separately.

`logs` shows the last uploaded script log, normally available during collection.
Use `--sync-seconds N` for earlier uploads. `logs JOB --console` requests
instance-wide [boot diagnostics](https://docs.aws.amazon.com/cli/latest/reference/ec2/get-console-output.html),
which may include earlier jobs on a reused worker. Neither is a live terminal.

Background S3 sync is off by default to avoid disturbing measurement on one
CPU. Spot workers poll the interruption endpoint every five seconds and try
to terminate the script and collect early when warned. Interruption can still
lose output; use `--sync-seconds` or script checkpoints when partial progress
matters. This trades extra background work for recovery. On-Demand with sync
disabled has no interruption observer. Boot-time and per-job OS shutdown timers bound
a detached worker; an OS that never boots needs controller cleanup (`wait` or
`cancel`). [EC2 shutdown behavior](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/Using_ChangingInstanceInitiatedShutdownBehavior.html)
and [Spot interruption behavior](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/spot-interruptions.html)
explain those boundaries.

Shared prepared inputs recorded by nested `Run.input()` calls are published
independently and restored on fetch. Keep the script's completed runs under
`SIXDB_RESULTS` and use the default shared dataset cache. Missing dependencies
are reported while preserving raw output. The datasets retain their existing
shared store even if a custom worker result bucket is selected; a custom
instance profile then needs access to both stores. Automatic profiles include
both and use a separate name for another result bucket.

Use [retention](artifacts.md) on a recovered study run to keep selected evidence
in Git, or keep the worker's small `artifact.json` for a generic bundle. Do not
commit the fetched directory. Bundles have the artifact helper's 5 GB limit.

For tool development, see the [offline checks](README.md#changing-a-helper) and
[reuse smoke script](worker-reuse-smoke.sh). The initial
[lifecycle](worker-validation.json) and [reuse](worker-reuse-validation.json)
receipts retain their tested conditions and limits.
