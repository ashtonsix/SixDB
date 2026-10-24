# Bounded research storage

Ashton requested this replacement spike on October 24, 2026 after repeated disk
exhaustion. The outcome is a replacement for the storage lifecycle in the
research tooling, tested by independent adversarial agents. A cleanup, passing
helper unit tests or an unenforced budget is not that outcome.

## Starting evidence

The [first research retrospective](../../notebook/research-experience.md) already
reported that shared input references replaced copies in each bundle. That
mechanism remains useful, but was not applied by subsequent artifact-based
runners. The current [storage guide](../../tools/storage.md) explicitly describes
retention budgets rather than quotas. The October 16 and October 23 recovery
checkpoints preserve earlier cleanup; neither proves controlled future growth.

On October 24, local worker files totaled 73,509,436,074 bytes against a nominal
10 GiB worker budget. 138 collected jobs contained about 40.98 GB allocated in
input columns; manifests identified about 40.67 GB of repeated content. Launch
and result source-package copies occupied another roughly 23 GB. Several Frame
runners fetched the same input artifact under SIXDB_RESULTS/in, so it was
rebundled and restored for every job. Healthy foreground operations checked a
5 GiB reserve but did not enforce retention budgets. Local build accounting was
about 180 hours old. There is no scheduled scanner on this device, by Ashton's
explicit choice; reinstalling the canceled timer is not a solution.

The immediate repair is recorded in ignored build/reviews/storage-20261024/.
It includes kernel-verified extent sharing without path/content changes and
freshly verified eviction of archived closed worker collections. Those actions
continue separately while this spike investigates replacement. Provisional
working-tree changes to default source collection and shared artifact input
fetching are mitigations to inspect, not proof that this problem is solved.

## What the replacement must establish

- Repeated ordinary jobs sharing inputs and source do not accumulate another
  complete physical copy per job. Source and data identity and recovery survive.
- A budget controls admission and actual retained/in-flight output, including
  temporary files, interrupted writers, metadata and worktrees in its scope.
  Accounting-only thresholds and a fixed free-space warning do not suffice.
- Concurrency cannot spend the same remaining capacity twice. A killed or failed
  operation cannot leave unbounded unaccounted staging or corrupt accepted data.
- Active incremental builds, pins, changed local files and unarchived unique
  evidence survive. If they exhaust capacity, the operation refuses safely and
  identifies the cause rather than pretending its budget is satisfied.
- Recovery bytes are checked, publication is durable, and damaged or malicious
  archives/receipts/paths cannot trigger loss outside the managed scope.
- The actual workflow is useful: ordinary collection, selected recovery, shared
  datasets, capture/build reuse, archived eviction and old-job compatibility.
  State what is controlled and what an arbitrary external writer can bypass.

The candidate design is still open. First audit the producer/consumer lifecycle
and reproduce the old failure; then test a replacement from the outside. Keep
state and operating work smaller than the discarded machinery.

## Adversarial validation

Independent subagents own failure discovery and external workload oracles.
Exercise repeated mixed jobs, simultaneous admissions, edits/pins, interrupted
transfers and publication, recovery corruption, unsafe paths, stale ownership,
actual filesystem exhaustion where safely available, and restart reconciliation.
Use tiny isolated fixtures and bounded private filesystem mounts; no real cache
or unique evidence is disposable test material. No cloud workers or benchmark
builds are needed. Retain failing cases and limits alongside the eventual result.

Status: open. No replacement or durability claim has been established yet.
