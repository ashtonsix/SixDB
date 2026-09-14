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
For coordinated hosts or several architecture targets, use [worker groups](worker-groups.md).

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
| `SIXDB_DEVICES` | [Requested data-disk identities](#data-disks), when configured |
| `SIXDB_RESULTS_S3` | Mutable `live/` prefix for optional script checkpoints |

Setup installs pinned Clang, LLVM tools, CMake, Ninja, Git, Python/PyYAML and a C++
standard library. Script-specific dependencies belong in the script. Reuse keeps
installed packages, unchanged build inputs and prepared datasets. `--fresh` asks
for a new environment; add `--idle-seconds 0` to terminate it after this job.
For longer edit/review loops use `--idle-seconds 600`; a narrowly missed reuse
window can prompt this suggestion in the console. The next job's entire deadline
must also fit the remaining `--max-age` lifetime.

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
uploads happen after measurement. `logs JOB --file setup.log` shows toolchain
setup output; status preserves the phase where failure occurred. `logs JOB
--console` shows instance-wide boot diagnostics, possibly including earlier jobs
on a reused worker.

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

## Data disks

Storage experiments can request new disposable EBS disks in a settings overlay
(or in a group member's `config`):

```json
{
  "data_volumes": [
    {"name": "bulk", "type": "gp3", "size_gib": 32},
    {"name": "wal", "type": "io2", "size_gib": 32, "iops": 3000}
  ]
}
```

Names are distinct letters, digits, underscores or hyphens. gp3 defaults to
3000 IOPS and 125 MiB/s; `iops` and `throughput_mib_s` override these. io2 requires
`iops` and has no throughput setting. AWS checks current size/performance limits.
Attachments are assigned away from the AMI's mappings. These encrypted volumes
are created with the instance and deleted when it terminates.

For local disks, set `"instance_store_count": 1` and select a supporting instance
type. This requests `ephemeral0` on legacy hosts such as I2. On NVMe hosts, AWS
attaches all local disks automatically; the count is a minimum and the manifest
includes all identified non-root local disks. See [AWS instance-store attachment
semantics](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/add-instance-store-volumes.html).

Before the script runs, `$SIXDB_DEVICES` points to `$SIXDB_RESULTS/devices.json`:

- `format: 1`, with `ebs` keyed by logical name. Entries include `device`,
  `volume_id`, `attachment`, `size_bytes`, `model`, `serial` and `mountpoints`,
  plus requested `type`, `iops` and gp3 `throughput_mib_s`.
- `instance_store` is a separate list with `device`, `size_bytes`, `model`,
  `serial`, `mountpoints` and `identity`; legacy mappings also have `name`.
- `root_devices` lists excluded whole disks, including root/boot ancestry.

Named EBS discovery currently requires Nitro. The worker installs Ubuntu's
`amazon-ec2-utils` and uses `ebsnvme-id` to match volume IDs and launch attachment
names; [NVMe enumeration order is not stable](https://docs.aws.amazon.com/ebs/latest/userguide/identify-nvme-ebs-device.html).
Local NVMe identification uses model/serial; legacy mappings use IMDS and the
corresponding non-root whole disk. Missing or ambiguous identities fail the job
before its script starts.

The helper does not format, mount or write disks. The study owns initialization,
filesystem/raw access and evidence of persistence semantics; this manifest makes
no PLP claim. Compatible reuse preserves data-disk contents and mounts. Use
`--fresh` when a new disk is part of the experiment; changed disk requirements
select a different worker profile. `check.py worker_devices` checks discovery
and launch behavior offline.
