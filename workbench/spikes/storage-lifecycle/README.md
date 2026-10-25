# Bounded research storage

The replacement is installed on the OrbStack Ubuntu host. The operating guide is
[local storage](../../tools/storage.md); this study retains the causes, rejected
approach and independent qualification. Ashton requested it after repeated disk
exhaustion, then required fresh reviewers after repairs and actual public
workflows rather than a passing standalone prototype.

## What caused the recurrence

The first [research retrospective](../../notebook/research-experience.md) already
recommended shared input references. Later runners nevertheless fetched those
inputs into each job's results, repackaged them and restored another full copy.
The October 24 audit found about 40.7 GB of repeated input content and roughly
23 GB of launch/result source-package copies. Nominal 10/60 GiB retention
budgets did not control admission; accounting was about 180 hours old.

A pathname walk also misses open-but-unlinked files and filesystem metadata.
Cleanup alone could not establish a capacity boundary, and repeated interrupted
operations left separate pending copies. The immediate recovery preserved
[308 verified collection references](../../tools/evidence/storage-20261024/README.md)
and achieved the requested greater-than-50-GB whole-volume gain. That checkpoint
explicitly includes concurrent owner activity and does not claim every byte as
exclusive reclamation by this task.

## Replacement

- **One native limit.** OrbStack's existing Btrfs simple quota now caps this
  Ubuntu subvolume at 192 GiB, including its existing data. Ordinary native
  builds, captures, worktrees and configured temporary output share it.
  There is no storage timer, image, overlay, mount service or periodic scan.
- **One shared input store.** Artifact selections share immutable payload bytes;
  prepared data and local runner children share a dataset hub across checkouts.
  Process-owned pins protect active inputs. Streaming checks the complete source
  and every member without staging compressed or omitted payloads.
- **Owned pending work.** Upload, recovery, dataset and launch preparation reuse
  fixed pending slots. Interruption and different-name retries do not accumulate
  another unknown partial archive. Completed jobs, edits, pins and unique output
  remain accepted work, never disposable pending data.

Launch sources leave after their independently verified upload; collection omits
source packages, large compiler output and shared prepared bodies by default.
Explicit recovery retains their identities. Cache reclamation verifies recovery
again before removing matching bytes. Current Linux builds and Git metadata
kept their paths/inodes. Four older Mac-hosted build paths were copied, hash
checked and archived before their matching old copies were replaced by links
into the bounded store; see the [migration record](../../tools/evidence/storage-20261025/README.md).

CMake also checks the configured output boundary before compiler detection.
Fresh Mac-hosted worktrees must initialize first (the editor helper does this);
foreign output refuses before compilation. Initial CMake control files can
precede the check. Common Git metadata is validated in place, never relocated.

## Why the image/overlay candidate was dropped

The private image prototype enforced capacity, but composing it with ordinary
work exposed a worse operating model: old data exceeded the image, stable IDE
paths needed overlays, a full-inode restart needed reserved recovery resources,
and 128 registered roots involved 385 mounts. Fresh reviewers then found that
raw CMake in a new worktree bypassed setup and prepared-reference uploads crossed
filesystems unexpectedly. Root-controlled layer replacement was also insufficiently
identified. Passing isolated store tests had not established a working system.

The host already had consistent native quotas. Using the supported
[OrbStack machine disk limit](https://docs.orbstack.dev/machines/#resource-limits)
removed that image/migration/restart machinery. The old age scanner, timer
installer, duplicate pruning and unused prototype CLI/recovery APIs were retired.
The remaining custom code handles sharing, checked transfer and interruption;
it does not implement a second filesystem or a benchmark certification framework.

[Retired source, detailed reports and historical harnesses](retired-candidate.json)
are verified in S3, including an indexed hash map. Earlier
[qualification cuts](qualification-records.json) and the independently frozen
[public image-candidate record](image-candidate-records.json) preserve additional
counterexamples. These are recovery references, not installed alternative backends.

## Evidence and limits

[Final public workflows](PUBLIC-WORKFLOWS.md) use unchanged real worker runtime,
CLI run/resume/checkpoint/group cycles, repeated artifact/prepared-input recovery,
concurrency, interruption and genuine native quota exhaustion. Only external
AWS/HTTP services were offline stand-ins. [Native build review](PUBLIC-NATIVE-BUILDS-V9.md)
adds actual root CMake, Clang, editor/capture and metadata-pressure checks.
[Mac-worktree boundary review](PUBLIC-BOUNDARY.md) qualifies the final guard and
initialization path independently. Frozen source, executable harnesses, commands
and cleanup proofs are in [qualification records](final-qualification.json).
The subsequent ancestor-pin policy passed a fresh 18-check public lifecycle
review, including pins introduced midway through verification. Upload release,
collection and explicit eviction all preserved the affected local bytes.

The installed host was also restarted with an active real input consumer. The
platform limit persisted, stale process pins expired, build/Git inodes stayed
unchanged, the editor service restarted, and real public input/build checks
worked afterward. No real cloud workers were launched for this qualification.

The boundary is the configured level-zero Btrfs subvolume, not arbitrary new
filesystems or privileged changes. Explicit authored exports remain outside cache
storage. Live kernel reservations can conservatively delay admission; pressure
checks retain the refusal and any verified reclamation already completed.
Finite capacity can refuse useful work; it never justifies erasing unique or
active data. These tests establish the exercised workflows and limits, not a
claim about every application on the Mac or untested cloud infrastructure.
