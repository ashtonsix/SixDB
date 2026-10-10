# Finite trim membership

Selected Allocation now keeps two page bitmaps in its control mapping: immutable
trim membership and helper success. They replace the reserved span/result vectors.
The helper emits maximal runs across word boundaries; a failed discard or
protection keeps the whole run unavailable and charged until retry. Owner
statistics use separate scratch. Group width, heat policy and full-batch jobs
are unchanged. The obsolete compact-span API was removed.

At 256 identities on the 4 KiB AArch64 guest, requested Group metadata falls
from **7,384 to 6,008 B** for palette A and **8,400 to 6,072 B** for B. The new
160/288 B planes fit the existing 4,096 B mapping. These figures exclude
Arena/Pool setup, the first 8 B owner-vector capacity, Object/Representation and
caller state. They are not RSS or the complete collection budget. At experimental
width 1,024, A's rounded mapping grows from 8 to 12 KiB; production remains 256.

Thirteen native checks and 26 paired consequence histories pass. Maintained
`orbital/test/finite.cpp` checks actual bitmap admission, a literal [62,67) run,
unused tail bits and complete refusal when a later selected page is active.
The retained probe also checks partial syscall failure, exact retries, charges,
owner statistics and compatible allocation while the helper is paused. Seven
ordinary scenarios preserve actual job, syscall-run and selected-page counts.
Traversal estimates in the probe are explicitly source-derived.

Six quick AB/BA rounds without concurrent builds give these median complete
trim times. Setup and final close are outside the interval; waits and owner
replies are inside. The plain binaries contain no fault/counter wrappers.

| Case | Span inventory | Bitmaps |
| --- | ---: | ---: |
| Fragmented A | 0.683 ms | 0.743 ms |
| Fragmented B | 1.148 ms | 1.156 ms |
| Contiguous | 0.154 ms | 0.139 ms |
| Coalesced prefix | 0.167 ms | 0.163 ms |
| Mixed | 1.963 ms | 1.847 ms |
| Retained floor | 1.051 ms | 1.042 ms |
| Empty Group retirement | 0.651 ms | 0.645 ms |

This selects the memory saving with mixed timing feedback, including the
fragmented-A negative. It establishes neither a general speedup nor a precise
regression magnitude on isolated hardware. An earlier concurrent-build screen
was substantially noisier and did not determine the choice. Raw wall, owner CPU
and process CPU samples remain in [feedback.json](feedback.json). No new
sanitizer or target-machine claim accompanies this change.

## Replay

The [source record](source.json) identifies the exact baseline commit, selected
commit lookup and postimages. The selected commit is the first commit adding
`source.json`; its exact Git lookup is recorded there. Create an ignored working directory with `baseline/`
and `projection/` trees archived from those refs, and copy this directory's
`probes/` beside them. From the Linux guest run
`python3 probes/run.py --phase comparison --rounds 6`; use `orb -m ubuntu` from
macOS. The helper uses pinned Clang 21.1.8 and preserves its incremental build.
Use `--phase qualification` for consequence checks and census without timings.
Do not run the timing comparison concurrently with native builds.
