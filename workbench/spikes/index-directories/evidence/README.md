# Selected index-directory evidence

The selection keeps matched controls, all repetitions and counters for the
executable storage, composed, placement, directory-pressure, Ring and byte-key
families. It supports the negative update-cost finding and the cross-platform
read/write tradeoffs in [FINDINGS.md](../FINDINGS.md). Source snapshots, build
receipts, binaries and omitted logs are recovered through each `artifact.json`.
This is selected experimental evidence, not a production qualification record.
Earlier layout/capture cuts retain the pressure and, where cited, Ring panels;
their other successful families are recoverable from the archives. Packed cuts
retain the complete varied matrix. Root cuts retain only the new pressure panel.

| Selection | Worker job | Purpose |
| --- | --- | --- |
| [first-zen5](first-zen5/) | `20261020T030658Z-d2ae6a7c` | First broad-copy composed failure and storage controls, before later implementation changes. |
| [layout-zen5](layout-zen5/) | `20261020T074051Z-c195f879` | Paged/branched directory and summary-grain comparison before capture-helper cleanup. |
| [capture-zen5](capture-zen5/) | `20261020T074700Z-3305b465` | Same layouts after caller cleanup, with bare Object capture attribution. |
| [capture-intel](capture-intel/) | `20261020T075133Z-c9a1826f` | Same captured caller on Granite Rapids with native flags. |
| [capture-neon](capture-neon/) | `20261020T074711Z-4e633fac` | Same captured caller on Neoverse V2. |
| [prefix-zen5](prefix-zen5/) | `20261020T075725Z-4a9735fe` | First many-prefix pressure case exposing the Object-resolution failure. |
| [resolution-before-zen5](resolution-before-zen5/) | `20261020T080328Z-7b647891` | Frozen attribution control before the ObjectId hash repair. |
| [resolution-after-zen5](resolution-after-zen5/) | `20261020T080539Z-7f3ef9dc` | Same consumer with only the repaired directory header substituted. |
| [packed-zen5](packed-zen5/) | `20261020T081334Z-648eb352` | Occupied-page packing and typed hash geometry. |
| [packed-intel](packed-intel/) | `20261020T081340Z-b6ca11a2` | Same code on Granite Rapids. |
| [packed-neon](packed-neon/) | `20261020T081345Z-6a192887` | Same code on Neoverse V2. |
| [root-zen5](root-zen5/) | `20261020T082118Z-7dff8748` | Root-row chunking loss with matched unchanged-root controls. |
| [root-intel](root-intel/) | `20261020T082123Z-b7613397` | Same root control on Granite Rapids. |
| [root-neon](root-neon/) | `20261020T082129Z-75ce1480` | Same root control on Neoverse V2. |

The capture trio uses the code frozen by worker group
`20261020T074653Z-102689a1`. Intel was rerun from that group's exact captured
source after the initial Intel job `20261020T074705Z-32e021da` used Zen 5 flags;
that initial job is excluded from the native-target comparison. The final Intel
job explicitly sets `DIRECTORY_TUNE=granite-rapids`.

The packed trio uses worker group `20261020T081328Z-040469c9`; the root trio
uses `20261020T082111Z-e45fea12`. All members of each group share its frozen
source, and Intel's environment is inside its member configuration. Root jobs
run pressure only; packed jobs check and measure all six drivers.

The resolution pair uses the same reused Zen 5 host. Public `capture.py` derived
`index-directory-prefix-repaired` from `index-directory-prefix-old` by replacing
only `orbital/include/orbital/detail/object/directory.h`. Source digests are
`19b24b87e51102ce5dfef1ce3ddf2d010027f98f6d36fe3af41fa3bfadf99f56`
and `2d169ad7ac4f2f2e4d9ea197a40bd2210206bb7937664328153e5407de007259`.
The repaired header SHA-256 is
`c8fd01e101a2015cee22a8892d98dfadb0dc7b79e63125baa90fa2a0443be471`,
the file committed by Orbital LEAD in `4b4751a5`. Frozen-source receipts in the
archives establish actual bytes; current HEAD is not a substitute.

Native worker settings: Clang 21.1.8, Release `-O3 -g1`, both vectorizers enabled,
one pinned CPU, 16,384 requested operations, five trials and 32 placement writes.
Pressure/staged drivers cap their own operations where documented in source;
pressure has 16 writes and composed has 64. Zen 5 uses `znver5`/`zen5`, Intel
`graniterapids`/`granite-rapids`, ARM `armv8-a+simd`/`neoverse-v2`. Host receipts
record actual instances. The Zen 5 c8a.large and ARM c8g.large expose two vCPUs;
timed clients use one. Intel c8i.large has SMT disabled and one visible vCPU.
The per-run hardware output and CMake cache are available in the full archive.

Local source cuts are diagnostic history in ignored
`build/experiments/index-directories/`, not additional headline evidence. The
original local captured pressure run `20261020T074614.330436Z` passed after caller
cleanup. Later source cut `20261020T081230.615211Z` passed all six drivers with
occupied-page packing and geometry controls. All six checks also passed after
root chunking with `-O1 -g1 -fsanitize=address,undefined -fno-omit-frame-pointer`
in `build/clang/index-directories-sanitize` (9.7 seconds). Final pressure cut
`20261020T082833.907864Z` adds the 4,097-prefix retained-root shrink check.
That extended check also passed ASan/UBSan (8.8 seconds).
Those checks are semantic/lifetime evidence, not timing measurements.

Use `workbench/tools/artifacts.py fetch PATH NEW_DIRECTORY` on any selected
directory to recover its complete verified archive. Raw `measurements/*.csv`
retain trial rows and logical counters; the adjacent `*-summary.csv` is
median/min/max over those trials. Value-update times are burst averages with a
retained reader. They do not estimate service p99 or steady-state throughput.
