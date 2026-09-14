# Experiments with several workers

Use `worker_group.py` when scripts need several hosts, or when comparing the same
source on several architectures. Each participant remains an ordinary
[worker job](workers.md), with its own results and recoverable S3 archive.
Groups default to Spot and compatible reuse, like single-worker runs.

For example, save this as `build/group.json`:

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
python3 workbench/tools/worker_group.py cancel build/worker-groups/ID/group.json
```

Omit `--detach` to collect after launching. `--source PATH` captures a chosen
checkout once; `--output PATH` chooses a new group directory. Common `script`,
`args` and `config` can be overridden per member. Configuration uses the existing
worker settings; member environment entries merge with the common environment.
A group uses one region and result bucket. Its local `group.json` keeps names,
resolved settings, preallocated job IDs, collection outcomes and cleanup state.
Keep it when moving or retaining the experiment; individual jobs also recover
through `worker.py wait JOB` and the ordinary artifact tools.

## Private networking and experiment coordination

An optional `network` entry creates one temporary security group with only
self-referencing ingress, for example:

```json
"network": {
  "vpc_id": "vpc-...",
  "tcp_ports": [[43000, 43001]],
  "udp_ports": [[43002, 43002]],
  "icmp": true
}
```

Members can select subnets with `config.subnets: ["subnet-..."]`. Private-group
workers are fresh and terminate after collection, so their temporary network
can be removed. Without `network`, normal worker reuse and idle settings apply;
an externally supplied `security_group_id` remains its owner's responsibility.

Scripts receive `SIXDB_GROUP_MEMBER` and `SIXDB_GROUP_URI`. The latter is a unique
prefix under `sixdb/` for optional study-owned rendezvous. Existing scripts can
map it into another environment variable with `"PEERS": "{group_uri}"` in
`config.env`. Neither a group nor its launch completion establishes application
readiness. Peer discovery, barriers, phase ordering and readiness timeouts stay
with the experiment; avoid control traffic during measurements.

The [AZ-latency launcher](../spikes/az-latency/launch.py) is the worked example:
it chooses the six AZs and supplies network and script settings. The shared helper
owns capture, submission, observation and cleanup. The study's `node.py` owns its
rendezvous and timed phases. Worker deadlines still include setup, peer waits and
collection; keep server scripts alive until their peers finish.

## Interrupted and partial groups

Use the recorded receipt to resume; `run` always starts a new group. Job IDs are
saved before submission, so an uncertain response is observed through that same
job. A failed launch stops further submissions and prints the recovery path;
`cancel` also works for a partially launched group. It waits for any active
submission to finish before cancelling the recorded jobs.

`wait` retries observation errors until each job's deadline plus recovery grace.
An observer exception does not turn into an early cancellation. Ctrl-C detaches
observation after in-flight requests return, leaving jobs recoverable. Collection
outcomes are saved independently before cleanup; cleanup attempts every member
even if another fails, and network cleanup can be retried. An explicit `cancel`
aborts the group and may forgo unfinished output. Successful ordinary groups
leave their warm workers available; private groups close their dedicated workers
and remove their owned network. Status and receipts preserve unresolved errors.

`check.py worker_group` exercises these failure paths offline.
For an optional live lifecycle check, use `tests/group-smoke.sh` as the script
with `config.setup: "minimal"`; it needs no compiler or measurement workload.
