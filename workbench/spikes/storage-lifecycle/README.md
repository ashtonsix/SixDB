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
freshly verified eviction of archived closed worker collections. Reclamation
was stopped after the measured physical gain exceeded 50 GB. Provisional
working-tree changes to default source collection and shared artifact input
fetching are mitigations to inspect, not proof that this problem is solved.

## Findings and test subjects

The [lifecycle audit](AUDIT.md) locates the actual admission bypasses; the
[initial review](REVIEW.md) records further correctness failures. Independent
[growth](GROWTH-FAILURES.md) and [OS/process](OS-FAILURES.md) harnesses reproduced
the old failures and tested the first byte-store candidate. Entirely fresh
[workflow](adversarial/FRESH-WORKFLOW.md) and
[failure-path](adversarial/FRESH-BREAKER.md) reviewers then checked it again.
Their combined result was insufficient for deployment: the workflow reviewer
found nested source duplication, abandoned recovery archives, reader lifetime
and obsolete metadata failures that a narrower passing suite did not establish.

The revised [immutable-store prototype](store.py) has scoped consumers, separate
persistent pins, verified unique-to-archive transition and recoverable metadata
retirement. A further independent [reader/metadata review](adversarial/READER-METADATA-V2.md)
found and retained more failures, then checked their repairs on frozen source
`216733a3`: ten cases including descriptor-closing readers, wrapper death,
forked readers, overlapping aliases and changing inputs completed without a
new observed failure. This is a regular-file recovery backend and experimental
CLI, not a replacement already connected to normal worker/build operations.

The [persistent workspace probe](QUOTA-WORKSPACE.md) established that a fixed-size
image can enforce byte/inode exhaustion on this host and preserve contents across
remount. A pathname allocation walk alone misses unlinked open files and
filesystem metadata. The physical boundary and ordinary archive/consumer paths
must therefore be composed and tested before a replacement claim. The existing
worker, dataset, capture and mutable-build paths remain rollout work.

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
When a composed candidate appears ready, start new subagents with clean context
and ask them to defeat it. A previous review's conclusions are not their oracle.
Exercise repeated mixed jobs, simultaneous admissions, edits/pins, interrupted
transfers and publication, recovery corruption, unsafe paths, stale ownership,
actual filesystem exhaustion where safely available, and restart reconciliation.
Use tiny isolated fixtures and bounded private filesystem mounts; no real cache
or unique evidence is disposable test material. No cloud workers or benchmark
builds are needed. Retain failing cases and limits alongside the eventual result.

Status: open. No replacement or durability claim has been established yet.

## Public workflow qualification and current direction

The [actual-tool baseline](PUBLIC-WORKFLOWS.md) uses unchanged public commands,
a local external-service fixture and real Clang/CMake compilation. Twelve of
25 external checks expose growth beyond the advertised budgets. It is the
rollout regression workload; passing the standalone store cannot replace it.

Fresh composed reviewers found [capacity and recovery failures](COMPOSED-CAPACITY-V5.md)
and [shared-input/legacy compatibility failures](adversarial/COMPOSED-WORKFLOW-V5.md).
The capacity breach occurred even with correctly serialized locks: Btrfs kept a
pre-lock directory descriptor's old enumeration ceiling. Every capacity listing
now opens a fresh file description relative to the held directory inode. A
native replay admitted one 16 MiB image and refused the competing one under a
17 MiB limit. The archive repairs retain bounded recent manifest proofs and
support the actual legacy member counts. New reviewers must check these again.

The [persistent visible volume](VOLUME.md) addresses a practical failure of the
private workspace: normal IDE/build consumers need stable paths after a command
ends. It shares the image admission code, retains a kernel capacity boundary and
was tested only in a private host-like namespace with ordinary peer processes.
Public tools are being connected to this boundary. No live build path has been
silently overlaid or migrated; the existing nonempty trees require verified
curation/migration and preservation of active incremental work.

[Qualification records](qualification-records.json) recover frozen source cuts,
compact execution records and earlier counterexamples from S3. `index.json` in
that bundle maps paths to hashes. It contains no filesystem images or full
benchmark inputs. The old failure cases remain useful even as production code
moves out of the spike. Ashton explicitly requires fresh reviewers after fixes,
including actual public workflows; that work remains active.
