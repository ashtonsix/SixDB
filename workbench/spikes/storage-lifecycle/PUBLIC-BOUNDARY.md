# Independent public build boundary review v13

Passed on the current frozen subject, with no product failure observed. The review was planned independently before reading storage-specific outcomes. It ran only a private native Btrfs subvolume (`0/287`, 32 MiB limit) and a promptly removed owned Mac directory. No canonical source edits, platform settings, caches, services, live quota groups, mounts, images, network or cloud compute were used.

## Consequential checks

- A real raw Mac-hosted linked Git worktree of the native fixture refused root `cmake --preset dev` before compiler identification, ABI probes, or root `.tmp` creation. CMake itself unavoidably left `CMakeCache.txt` (2,434 bytes), `CMakeFiles/cmake.check_cache` (85 bytes), and the containing `clang/dev`, `CMakeFiles`, and `pkgRedirects` directories: **2,519 regular-file bytes** plus directory metadata. The check is early, not zero-output.
- Actual `storage.py ROOT` after that failed configure refused the nonempty output and preserved it byte-for-byte. A separate untouched Mac linked worktree initialized first, linked `build` into the owned quota, and configured, built and executed the real Ikea intervals ordinary example with pinned Clang 21.1.8 through the same logical `build/clang/dev` path. Common native Git metadata was identical across initialization.
- A native primary fixture and a raw new ordinary native linked worktree configured/built/executed **before any initializer**. After the Mac workflow and subsequent native initialization, the original native object and executable retained inode, mtime and SHA-256; Ninja reported `no work to do`.
- Missing explicit configuration, bad saved quota identity and removal of the owned quota limit all refused before `.tmp`/compiler artifacts. The owned limit was restored immediately. A deliberately unconfigured ephemeral workflow retained compatibility and built/executed on Mac; this is intentionally outside the configured guarantee.
- A real local-cloned primary repo with foreign Mac Git metadata refused initialization before creating `build`, preserving every Git file identity/hash. A native common repo whose `worktrees` entry linked to owned foreign Mac storage refused and preserved the symlink inode/target and foreign sentinel. Git metadata was never relocated.
- From the initialized Mac worktree, the actual `experiment.Run` completed and executed the real example, actual `artifacts.py preview` selected a 749-byte export without network, and actual `dev.py` refreshed the File API compilation databases. The root editor database was a symlink into the owned native quota.

## Subject and limits

Root CMake, root cmake support files, current top-level Python tools and `sixdb_storage` sources were frozen byte-for-byte into a real committed fixture repo. Root CMake SHA-256: `e0c34edcde8c429f785a8afb3d7eefb2b1d1c93072b067eda7b0ff49eee8d713`. Storage SHA-256: `0e7ef8bfd546e638235f517f707b45c85b8c5dff1b69088e3a66911d3531fb64`. Native quota SHA-256: `47302e09325977d05174758e9ebff435e49e9acf422b4b102b9c004d604cb3d3`. `frozen-files.json` retains the complete source manifest; the canonical files still matched at review completion: `True`.

The fixture used the actual Ikea intervals example, search implementation and Ikea headers, with reduced module declarations to fit all worktrees/builds under 32 MiB. Root CMake/settings/compiler behavior and public tools were real; this review does not claim full module configuration/build coverage, remote artifact upload/recovery, physical disk exhaustion, or atomicity against privileged quota changes.

Peak observed regular Mac file bytes were 3,879,151; peak observed allocated Mac bytes including directory/symlink blocks were 5,017,600. Retained review evidence is roughly 140 KiB, keeping the reviewed external footprint below 12 MiB. Native final kernel counters and all command return codes are in `results.json`.

## Reproduction and cleanup

Run `orb -m ubuntu python3 -B /home/ashtonsix/sixdb/build/reviews/storage-20261025/fresh-boundary-v13/review.py` from this workspace. It allocates fresh unique owned paths, freezes the current checkout, and records hashes and commands.

The first cleanup attempt saw delayed Btrfs qgroup retirement (`EBUSY`) after successful subvolume deletion; waiting for the exact deleted root ID confirmed automatic removal. The retained executable harness now waits for that exact ID before checking/removing its qgroup. A complete second run passed with cleanup integrated. The final Mac directory, exact owned subvolume and qgroup `0/287` are absent. `cleanup-proof.json` records the identity check and disappearance; no review fixture remains.
