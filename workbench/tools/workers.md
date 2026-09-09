# Run a script on a temporary worker

From the repository root on Linux (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/tools/worker.py run workbench/tools/worker-smoke.sh
python3 workbench/tools/worker.py run workbench/spikes/your-study/cloud.sh \
  --machine zen5 --capacity on-demand --env CASE=small -- --repetitions 3
```

The second script is illustrative. A script receives the current source,
including uncommitted and non-ignored new files, on one disposable EC2 instance.
The command waits, downloads verified results to `build/workers/JOB/results/`,
and requests termination. The worker also shuts itself down, so collection
and cleanup continue if the controlling session disconnects. No commit, SSH key,
open inbound port, job file, or study framework is needed.

The first submission creates a `sixdb-worker` IAM role/profile and a security
group with no inbound rules in the default VPC. The role reads the existing
artifact bucket and writes only under `sixdb/`; it cannot delete objects or
manage EC2. These resources are reused. Source/job objects use
`s3://calico-fleet-artifacts/sixdb/workers/JOB/`; final bundles use the existing
content-addressed artifact store. Calico's workers, networking, and images are
left alone.

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
| `SIXDB_RESULTS_S3` | Job's mutable `live/` prefix, for optional script checkpoints |

Arguments after `--` are passed literally. Repeat `--env NAME=VALUE` for settings.
These values are recorded with the job; they are configuration, not a secret
store. No local AWS credentials are copied to the instance.

The default setup installs the pinned Clang 21.1.8, LLVM tools, CMake, Ninja,
Git, Python/PyYAML and a C++ standard library. Other dependencies belong in the
script. `--setup minimal` installs only the bootstrap tools; it is useful for
shell-only jobs or an explicitly selected prepared AMI. Package versions,
compiler output, hardware, source hashes, and logs accompany the results.

The source archive excludes `build/` and spike evidence, using the same source
selection as local experiments. A synthetic Git commit on the worker lets
existing runners use Git normally. Its hash differs from local HEAD;
`job.json` records the original commit and the actual captured source digest.

## Configuration without a job schema

| Option | Default / effect |
| --- | --- |
| `--machine zen5` | `c8a.medium`: one vCPU, 2 GiB |
| `--machine granite-rapids` | `c8i.large`: one core, 4 GiB; SMT disabled for one visible vCPU, still billed as a large |
| `--machine neoverse-v2` | `c8g.medium`: one vCPU, 2 GiB |
| `--instance-type TYPE`, `--ami ID` | Explicit capacity or image override; architecture is checked |
| `--capacity spot` | Default; tries subnets offering that exact instance type |
| `--capacity on-demand` | Uses On-Demand capacity |
| `--capacity spot-or-on-demand` | Explicit fallback after Spot capacity failures; preserves instance type |
| `--deadline SECONDS` | Default 3600, including boot/setup, script, and collection allowance |
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

## Leave it running and come back

```sh
python3 workbench/tools/worker.py run workbench/tools/worker-smoke.sh --detach
python3 workbench/tools/worker.py status JOB
python3 workbench/tools/worker.py wait JOB
python3 workbench/tools/worker.py logs JOB
python3 workbench/tools/worker.py fetch JOB
python3 workbench/tools/worker.py cancel JOB
python3 workbench/tools/worker.py list
```

`wait` resumes observation and fetches results; it never reruns the script.
It can reconstruct a missing local job record from S3. Ctrl-C detaches the
controller; `cancel` explicitly terminates only that job's tagged instance.
Capacity/transport retries preserve an EC2 client token where launch outcome
is uncertain. A failed experiment is never automatically resubmitted.

Successful scripts, failed scripts, and timeouts all attempt collection.
Completion is published only after a full bundle is uploaded and downloaded
for checksum verification. A script's nonzero exit remains a failure, with its
logs and outputs available. Failed uploads or a vanished worker are reported
as incomplete, with any live output recoverable separately. `logs` shows the
last uploaded script log, or EC2 boot-console output before that is available.
It is not a live terminal.

Background S3 sync is off by default to avoid disturbing measurement on one
CPU. Spot workers poll the interruption endpoint every five seconds and try
to terminate the script and collect early when warned. Interruption can still
lose output; use `--sync-seconds` or script checkpoints when partial progress
matters. This trades extra background work for recovery. On-Demand with sync
disabled has no interruption observer. The boot-time OS shutdown timer bounds
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

To keep findings in Git, apply the usual [retention command](artifacts.md) to a
recovered study run, or keep the worker's small `artifact.json` reference for
a generic validation bundle. Do not commit the fetched directory. Cloud jobs
upload automatically because their machines are temporary; local experiment
retention remains an independent choice. Bundles currently use the existing
single-PUT artifact helper, with its 5 GB limit.

`python3 workbench/tools/check_worker.py` checks launch retries, job-scoped
cleanup, literal arguments, real-script success/failure/timeout, archive safety,
and upload failure without allocating cloud resources.

The [live validation references](worker-validation.json) retain an On-Demand
Zen5 build, a Spot script failure with zone fallback, and a Granite Rapids
build with shared-input recovery. All three instances were observed terminated.
ARM boot/runtime and an actual AWS interruption have not been exercised here;
the timeout/interruption paths also have local lifecycle checks.
