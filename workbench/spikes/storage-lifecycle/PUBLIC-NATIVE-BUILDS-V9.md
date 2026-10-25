# Fresh native build reliability review v9

October 25, 2026. Independent review of ordinary builds, Git worktrees, editor
configuration, captures and native quota failure paths. No capacity breach or
accepted-data loss was observed in this scope. This does not qualify the input,
transfer or recovery-store workflows, or machine restart persistence.

The review plan was recorded before reading prior storage outcome reports.
The starting evidence was the current [storage lifecycle findings](README.md),
[build guide](../../../BUILDING.md), [storage guide](../../tools/storage.md),
and [editor guide](../../tools/editors.md). The old pathname/retention mechanism
missed open-unlinked files and filesystem metadata, and a passing standalone
store did not establish ordinary build coverage. This review therefore used the
current real root CMake files and pinned compiler under the kernel limit, rather
than treating helper admission or a pathname byte count as the oracle.

## Frozen subject and isolation

Execution records, plan, harness, complete selected-source hashes and cleanup
proof are in ignored
`build/reviews/storage-20261025/fresh-native-builds-v9/`.

Important frozen public hashes:

| File | SHA-256 |
| --- | --- |
| `CMakeLists.txt` | `f863dc5e0b4c88397bf1df941f7dd8bf3f224f5410d1f5e0a22656fac4bb3c20` |
| `workbench/tools/storage.py` | `750b6898133e0cfdafe47d014a6527a5c02009f7e915ad67a88b0632300ba1f7` |
| `workbench/tools/nativequota.py` | `47302e09325977d05174758e9ebff435e49e9acf422b4b102b9c004d604cb3d3` |
| `workbench/tools/dev.py` | `8010484050bcef13f090b18fe27f40421fda48896e63db80bcd4d374f626f600` |
| `workbench/tools/capture.py` | `e7916e27a121f14944212c9dd02d53018b682ca5503e3f50fd2bf70b4382051f` |
| `workbench/tools/local_cache.py` | `39035ccaf316d90f7ad510b80cc372b144db2845b83d4efeced5231d57af2097` |

The fixture was a new owned native Btrfs subvolume, ID 270, on filesystem
`bc5102ea-4ba3-45e3-895f-babc8dbae424`, with a 32 MiB level-zero squota hard
limit. Its format-1 configuration selected that exact identity, its own scope,
and its own `hub`. An independent Git repository inside it held 601 selected
files: actual root CMake/presets, module source/headers/tests, necessary Workbench
CMake inputs and public tools. No product ROOT was the live checkout. The frozen
fixture hash-map digest was
`f180425cbcfea32b0d1557301b1c3a792d915314e31751ac1095177dc82a8210`.

The first fixture omitted `workbench/CMakeLists.txt`; its resulting configure
failure was a harness error. The file was copied unchanged, committed, and the
fresh-worktree experiment was restarted before running initialization. This is
explicit in the command record and is not a product counterexample.

During the review, the live `local_cache.py` changed to
`28cdd071b3d97574a69f4b8e473cc71370d0cfa24c105a337b6a01dd30f2370c`:
its prune branch now includes the configured reserve in external admission.
This review tested status and refusal, without entering that prune branch. All
other listed public source hashes remained unchanged. Input/cache-prune
qualification belongs with the other workflow review.

## Actual workflow observations

| Operation | Observed result |
| --- | --- |
| Plain `git worktree add --detach`, then `cmake --preset dev` and build `ikea_example_intervals_ordinary` | Passed before any storage initializer. Real Clang 21.1.8 compiled/linked; executable ran. The hub remained absent. |
| Immediate ordinary incremental rebuild | Ninja reported no work; existing object inode/mtime identities were equal. |
| Initialize an already populated checkout | Existing build directory, accepted unique result, `.git`, Git index/HEAD, CMake cache and object inode/content identities were equal before/after. The following build remained a no-op. |
| Compiler/linker output with an unusable shell TMPDIR | Actual strace showed build-relative object temporaries, dependency files, Ninja metadata and linked executable. Generated commands explicitly set TMPDIR/TMP/TEMP to the build `.tmp`. No observed write escaped to `/tmp`. |
| `dev.py --sync`, repeated sync, and Mac-facing home alias | Passed; root compilation database was a link to the bounded build database. Alias commands selected native-home paths by identity. |
| Foreground `dev.py --watch` | Discovered a new spike, refreshed its compilation database, and its target built/ran. Removing the spike and syncing removed its selection. The watcher process was terminated and joined. No service was installed. |
| Public capture create/verify | Passed with 599 selected source files; current outputs were excluded. |
| Real build and public capture running concurrently | Both completed; resulting capture verified. |
| `local_cache.py` and `storage.py --status` | Reported the configured native qgroup/capacity and fresh room. |

After failure tests, `git fsck --full` succeeded (one ordinary dangling empty
blob), the worktree list contained only the intended source and three successful
captures, the original capture verified, and the built executable still ran.

## Real exhaustion and recovery

An open file was unlinked while its descriptor remained live. Synchronous
131072-byte writes reached ENOSPC after 17,764,352 bytes. At refusal the qgroup
reported 33,538,048 referenced bytes against 33,554,432 capacity, with 16 KiB
available. This allocation was invisible to an ordinary pathname walk.

At that state, public capture failed while creating its Git worktree metadata;
no failed destination or receipt remained. Ninja/Clang failed to create their
build output with ENOSPC. The accepted unique result and original capture
receipt retained their inode/hash identities. Closing the descriptor released
the allocation; room returned to 17,833,984 bytes, and both build and capture
retries completed. The retry capture verified.

A separate metadata-only experiment created 47,977 zero-byte files before
ENOSPC. The qgroup rose from 16,318,464 to 33,488,896 referenced bytes despite
zero file payload bytes. Public capture refused at that state. Removing only
those owned files returned the qgroup to 16,318,464 bytes; ordinary build remained
useful. The kernel boundary covered both tested non-pathname allocation cases.

## Refusal and boundary checks

For the owned qgroup only, a 24 MiB saved budget/limit was first accepted, then
the live limit was raised to 32 MiB. Status, local-cache, editor-sync and capture
all refused the saved-budget mismatch. The owned limit was also temporarily
removed with no writer active; the same public commands refused. The original
32 MiB limit and configuration were restored. An explicitly missing config also
refused; no unconfigured fallback or refused capture was created.

A nonempty owned external output directory was preserved and refused by
`manage_directory`; direct `write_json` and `headroom` refused its foreign
identity. A new nested subvolume, ID 271, received its own 1 MiB limit and a small
accepted sentinel. The same checks refused it, and the sentinel survived.

A level-zero qgroup covers writes whose containing subvolume has that exact
identity. A child subvolume or a different mount is outside that boundary even
when its pathname is below the scope. Helpers must check the actual destination;
the test does not claim an arbitrary raw command directed into another
subvolume/mount is capped by the parent. Empty explicit external output paths
may be linked into the hub by the documented helper, whereas existing nonempty
output requires verified migration. Administrative quota removal can disable
raw-write enforcement; configured helpers detected the changed live limit.

## Operating judgment and remaining limits

The ordinary operating path is simple: native quota configured once, then the
existing CMake/Git/editor commands. Raw same-subvolume writes were already
bounded before initialization. No image, overlay, mount activation, per-command
reservation or storage service was needed by this path. Cleanup/freeing owned
bytes followed by retry restored progress. Existing nonempty incremental output
did not need migration when it already shared the configured native subvolume.

One concrete usability defect remains: `dev.py` lets quota/configuration
`ValueError` escape as a full Python traceback. Storage, capture and local-cache
public CLIs produced concise diagnostics for those same conditions. This did
not permit writes or fallback. The current storage/build documentation also
still describes the older retention-only behavior; documentation should state
the deployed native boundary and its child-subvolume/mount limitation.

This was a small AArch64 build using pinned Clang 21.1.8, not a complete all-target
build or compiler-version matrix. The trace qualifies the observed compile/link
invocation, not every possible custom build command. Foreground watcher behavior
was tested; service installation and machine restart were deliberately outside
this review. No live quota/config/cache/data was altered. Read-only inspection
found live root qgroup `0/263` with flags `1` and `max_referenced=206158430208`,
matching both the saved native configuration and
`orb config get machine.ubuntu.disk_bytes` (192 GiB).

Exact owned path/filesystem/qgroup identities were checked before cleanup.
Both owned subvolumes were deleted; deletion sync completed, and both qgroups
were absent afterward. The main owned qgroup was explicitly destroyed. The first
immediate child qgroup destroy returned busy while deletion was pending; after
sync it was already absent. No images, mounts, loop devices, services or network were
used. Retained records outside the fixture total less than 1 MiB.
