# Experiments with several workers

Use `worker_group.py` for named hosts sharing one source capture. Each remains an
ordinary [worker job](workers.md), with its own deadline, results and recoverable
archive. Common `script`, `args` and `config` can be overridden per member;
environment entries merge. Spot and compatible reuse are the defaults.

Save a spec such as this in `build/group.json`:

```json
{
  "script": "workbench/tools/worker-smoke.sh",
  "members": {
    "zen": {"config": {"machine": "zen5"}},
    "arm": {"config": {"machine": "neoverse-v2"}}
  }
}
```

From the repository root on Linux (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/tools/worker_group.py run build/group.json --detach
python3 workbench/tools/worker_group.py status build/worker-groups/ID/group.json
python3 workbench/tools/worker_group.py wait build/worker-groups/ID/group.json
```

Omit `--detach` to collect after launching. `--source PATH` captures another
checkout once; `--output PATH` chooses a new receipt directory. All members use
one region and result bucket. `status` joins lifecycle, last study progress and
its age, collection, instance state and remaining reuse time; `--json` gives the
underlying records. A last reported phase is a checkpoint, not a heartbeat.

## Startup and progress

Group membership and job IDs are published **before the first submission**.
Scripts receive `SIXDB_GROUP_MEMBER` and a unique `SIXDB_GROUP_URI`; each cohort
has its own rendezvous even on reused machines. Existing studies can use these
with their own protocol. For lightweight startup, import `GroupContext` with
`$SIXDB_SOURCE/workbench/tools` on `PYTHONPATH`:

```python
from worker_context import GroupContext
ctx = GroupContext.from_env()
ctx.progress("preparing")
# Prepare data, start the server and check its listening endpoint here.
ctx.ready({"address": address})
peers = ctx.wait_ready(["server"], timeout=300)
ctx.progress("measuring", case="batch-8", completed=3, total=12)
# Run the native measurement; publish another checkpoint after it returns.
```

Readiness means what the script has actually established. `wait_ready` checks
required peers' worker status, so setup failure ends the wait even if the failed
peer never imported the helper. A completed server cannot satisfy live readiness.
`publish(name, value)` and `wait_values(name, members)` instead exchange durable
values that may outlive their producer. A controller can use
`Group(receipt).publish(name, value)`; scripts wait for member `"controller"`.
This accommodates controller-side network setup without implying launch = ready.

The [runnable group smoke](tests/group-smoke.sh) starts private HTTP endpoints,
checks every peer's identity and keeps servers alive until all peers finish.
It also demonstrates retained setup across cohorts. For that script use
`config.setup: "minimal"` and private TCP port 43841 as below.

Publish progress between measured phases: `case`, `waiting_for`, `completed`,
`total`, `last_completed`, and `log` are useful optional fields. Upload failure
leaves a local diagnostic and does not fail a measurement; required rendezvous
writes do fail visibly. Reads distinguish missing keys from permission errors
and bound transient retries. There is no background progress observer. The helper
is for startup and coarse phase boundaries, not a distributed timing barrier or
continuous health monitor. Scripts still own subprocess deadlines and hang recovery.

## Private networking with repeatable setup

Add `network` when members need inbound communication:

```json
"network": {
  "scope": "my-study-dev",
  "vpc_id": "vpc-...",
  "tcp_ports": [[43841, 43841]],
  "udp_ports": [[43002, 43002]]
}
```

A named scope keeps one SixDB-owned security group with self-only ingress and
allows ordinary exclusive warm-worker reuse. Repeating the spec creates new jobs
on compatible idle hosts; it never resumes an old measurement. Region, VPC and
scope identify the network. Its normalized port/ICMP policy must match; changed
policy or unexpected ingress is reported, not merged into an active network.
`icmp: true` allows ICMP. Members can choose `config.subnets: ["subnet-..."]`.

Without `scope`, the network is temporary: workers are fresh, terminate after
collection, and the helper removes the network. Without `network`, ordinary
worker reuse applies; independent storage/architecture comparisons need no
private group. An externally supplied `security_group_id` remains owner-managed.

Use `config.idle_seconds` for edit/review time and `max_age_seconds` for the whole
instance lifetime. The **full next job deadline** must still fit that lifetime;
an increased idle window alone cannot make an almost-expired host reusable.
`fresh: true` remains available. Reuse preserves packages, builds, datasets and
machine state: the recipe must revalidate/reset relevant disks, mounts, MTU,
SRD and sysctls. A scope makes no clean-state guarantee.

Create, dispatch and cleanup share a scope lock across checkouts/worktrees on the
same Linux controller. Use distinct scope names for concurrent controllers on
different hosts. After the scope's hosts expire or are explicitly cancelled:

```sh
python3 workbench/tools/worker_group.py cleanup-network build/worker-groups/ID/group.json
```

Cleanup preserves a network with attached hosts or unresolved ENI dependencies
and reports the pending cause. It does not cancel other cohorts to free a scope.

## Partial progress and recovery

```sh
python3 workbench/tools/worker_group.py fetch RECEIPT --member zen
python3 workbench/tools/worker_group.py fetch RECEIPT --partial
python3 workbench/tools/worker_group.py logs RECEIPT arm --file setup.log
python3 workbench/tools/worker_group.py references RECEIPT --output build/members.json
python3 workbench/tools/worker_group.py recover ID --output build/recovered-group
python3 workbench/tools/worker_group.py cancel RECEIPT
```

`fetch` collects published outputs without waiting for or stopping other members;
`--partial` also recovers uploaded live checkpoints. The named references export
joins source identity, artifact references, collection and resource observations.
Failed measurements can have complete diagnostic archives; partial live output
is not an immutable archive. Keep selected references with the study's evidence.

`recover` restores lost membership from S3 without submitting anything. Older
groups need their original receipt. Use `wait` to resume observation: job IDs
precede dispatch, and ambiguous submissions are never retried as new jobs. A
failed launch stops further submissions; each study chooses which peers it
requires. Independent members are not cancelled because another failed.
Ctrl-C detaches observation. Explicit `cancel` aborts the recorded jobs, subject
to their current ownership, and may forgo unfinished output. Named scopes remain.

## Keep cohort time useful

Separate native measurement time from setup, coordination, serialization,
analysis and recovery/readback time. A worker deadline covers all of them plus
collection. `host.json`, resolved `job.json` hardware and study-selected settings
provide context: usable CPUs are not the application's thread count, advertised
network peaks are not measured throughput, and disk identity does not prove PLP.

For adaptive experiments, emit small decision statistics alongside raw samples.
Do only the analysis needed to choose the next case while the cohort waits;
full quantiles, compression and reports can often run after collection. Retain
required correctness/readback checks before releasing their resources. The
[CFT study](../spikes/cft-commit-latency/README.md) illustrates why these costs
matter; its protocol, arrival schedules and selection criteria remain study-owned.
