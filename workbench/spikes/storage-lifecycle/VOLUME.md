# One persistent visible storage volume

October 25, 2026. Author-tested fixture only; not mounted globally, configured as
the default, or migrated over a live build tree. Independent public-workflow
validation remains necessary.

The [lifecycle audit](AUDIT.md) found ordinary producers bypassing retention
targets. The [workspace experiment](WORKSPACE.md) supplied a real filesystem
capacity limit, but private command namespaces hid retained headers/build paths
from ordinary tools and IDE readers. The composed v5 review also found admission
using a stale pre-lock Btrfs directory handle. The shared admission core now
enumerates a newly opened, inode-anchored directory handle under flock. A native
replay admits one fresh 16 MiB image and refuses the competing name under a
17 MiB pool budget; the original failing replay is retained.

`volume.py` reuses that image admission/allocation code and changes the **mount
lifetime**. One `shared.img` stays mounted at a stable path in the caller's host
mount namespace after the helper exits. Ordinary tools and readers use it
directly. No per-command PID namespace, Landlock, descriptor filter, daemon,
timer, scanner or copy-back operation is needed for this tool-owned storage
boundary. It does not confine arbitrary writes elsewhere on the device.

## Callable/public integration contract

```python
from pathlib import Path
import volume

root: Path = volume.ensure(central_pool, total_budget_bytes, image_capacity_bytes)
# Build output, worker launch/results, shared datasets, artifacts, input/CAS
# metadata, archives, logs, and temporary files must all use paths under root.
```

`ensure(pool, budget, size)` returns the same absolute `POOL/mount/work` Path
across processes, checkouts and helper calls. `describe(...)` performs the same
operation and returns bounded diagnostics. The CLI equivalent is:

```sh
python3 volume.py --pool /absolute/central/pool --budget BYTES --size BYTES
```

Run Linux commands with `orb -m ubuntu` from the Mac. An ordinary user calls the
helper; it uses passwordless `sudo -n` only for manager files and mount work.
The same caller uid/gid owns `work/`. The canonical code is intended to move into
the actual tooling package; there is no hard-coded spike location or checkout
identity in this API.

One configuration **outside individual checkouts** must select the central pool,
finite budget and finite image capacity for all producers. The helper itself
does not invent a second configuration source or independently choose per-tool
pools. It accepts exactly one `shared.img` in that pool. Existing capacity and
budget are immutable; no caller silently resizes an image. Capacity is a finite
4 KiB multiple of at least 16 MiB, bounded by the platform's signed file-offset
range, with no fixed 128 MiB production ceiling. Private workspace fixture CLI
limits remain unchanged.

The budget must cover image capacity plus the 1 MiB manager allowance. Visible
backing-file/manager allocation is checked before first mount. Initial backing
admission checks available guest-filesystem space for unallocated image capacity
plus a default 64 MiB outer headroom reserve (`describe`/CLI can configure that
reserve explicitly). The image is actually fallocated before formatting; ext4
format uses `nodiscard`, so formatting does not silently release the allocation.
Sparse logical maximum sizes alone do not count as preallocated backing.

Build/IDE integration can use stable paths such as
`root/checkouts/<canonical-id>/build`. A fresh checkout may link its ordinary
`build` path there. Existing nonempty live build directories require verified
owner-coordinated migration; this prototype overlays none. Source remains outside
the image. No whole tree is copied back to make generated files visible.

## Ownership, validation and failure behavior

The central pool is root-owned mode 0711. Image, lock and immutable budget header
are root-owned mode 0600. `work/` on ext4 is ordinary-user-owned mode 0700. The
underlying **unmounted** mountpoint is root-owned and non-writable by the user;
if the image is absent, a tool cannot recreate `work/` there and spill into the
ordinary backing directory.

One flock serializes image admission, allocation, mount validation and initial
directory setup. A fresh directory FD relative to the held pool inode supplies
each inventory. The shared `workspace.prepare` implementation owns O_EXCL regular
`.new` creation, allocation, format, fsync/rename publication and interrupted
unacknowledged staging reconciliation. Accepted images are never reformatted.
Unknown entries and unexpected modes/types/capacities refuse with data retained.
The manager has only a fixed lock/header/image/mount layout; no per-job manager
file pile grows outside the filesystem.

An existing mount must match the exact target in namespace mountinfo: ext4,
filesystem root `/`, rw/nodev/nosuid, and the expected loop device. The kernel's
LOOP_GET_STATUS64 must independently identify the exact backing image device and
inode, zero offset, full capacity, writable status and autoclear flag. Another
filesystem, wrong image, stacked target mount or ambiguous staging refuses.
An image still attached elsewhere is refused instead of being mounted read-write
again through a second loop device. A helper interrupted after mounting can be
reentered and validate/reuse that persistent mount.

The lock is released after setup. Unwrapped readers and concurrent ordinary
writers need no lifetime lease to remain within capacity: the kernel filesystem
accounts for in-flight writes, inode use and open-unlinked files. Category
retention preferences are advisory; active, pinned and unique bytes all consume
the same hard filesystem capacity. If they fill it, allocations fail rather
than deleting accepted data to satisfy a reported budget.

Repeat `ensure` does not create category, cache, temporary or bookkeeping files
on the inner filesystem. It validates the existing single `work/` directory,
whose parent is root-owned, and can return the cleanup path with **zero free
inodes** or data ENOSPC. Public tools must expose cleanup/exec paths that do not
initialize a new cache transaction before users can remove disposable output.
Ordinary shell/IDE access to the mounted path also remains available. Resizing,
unmounting for live users, filesystem repair and eviction are not hidden inside
`ensure`.

## Author fixture observations

One fresh private host mount namespace held the volume while independent
ordinary uid-501 peers called the actual helper and accessed paths directly.
It used one 32 MiB image, not separate per-operation namespaces or copies.

| Check | Result |
| --- | --- |
| Helper exits, later peer calls ensure | Same `work` path and mount ID; no remount |
| Independent ordinary build writer/IDE-like reader | Retained `build/unit.o` and marker visible without a use wrapper |
| Explicit unmount/remount of the same image | Actual `ensure` callable returns a Path; retained build file and marker survive |
| 16 MiB open-unlinked writer held by peer | Available space stayed reduced until final FD close, then recovered by at least 16 MiB |
| Empty-file pressure | 8,175 fixture files exhausted inodes; repeat ensure returned the stable root with free_inodes=0 |
| Native data pressure | Actual ENOSPC; marker survived and repeat ensure still returned the cleanup root |
| Image length/allocation | Exactly 33,554,432 logical and allocated bytes |
| Changed size/budget or a foreign stacked mount | Refused; accepted files remain readable after restoring only the fixture mount |
| Mount absent | Ordinary user cannot create a backing-directory fallback `work` path |
| Teardown | Explicit unmount succeeded; no loop association remained; fixture image removed after verification |

The small report and host fixture driver remain under ignored
`build/reviews/storage-20261024/volume-authorcheck/`. The original admission-race
replay and corrected actual-source replay remain under
`build/reviews/storage-20261024/workspace-lock-audit/`. These are author checks;
they do not replace fresh independent public-command testing.

## Limits carried into actual-tool validation

The fixed inner filesystem bounds output and its filesystem metadata. A charged
outer manager allowance and preallocation do **not** prove a byte-exact bound on
arbitrary Btrfs tree growth, retained external snapshots, thin VM backing or Mac
filesystem use. The guest-space headroom check is not a reservation against
other applications, and no whole-device physical quota is claimed. Deployment
must choose capacity/headroom using the real backing-device situation and keep
the central pool unique across checkouts.

No live root has been configured or mounted. Actual public tools still need all
owned output/temp/log/acquisition paths routed here; a single missed fallback
can recreate the old unbudgeted pile. Existing data/builds need explicit recovery
and migration. Actual VSCode and any Mac filesystem-projection visibility across
their real namespaces remain a rollout check; the fixture proves ordinary Linux
peer visibility within one shared host namespace. Unclean host restart/power
loss, corrupted images, hostile root interference and production-scale ext4
initialization are not established by this tiny fixture.
