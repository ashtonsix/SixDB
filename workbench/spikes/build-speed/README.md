# Build speed, compile memory and output size

Question: why are SixDB builds slow, where does compile memory peak, and what
makes outputs large? Measured 2026-10-22 at `1b9866b7` on a 16-vCPU, 32 GiB
Graviton4 worker (`c8g.4xlarge`), Clang 21.1.8, dev preset (`-O2 -g`), building
`all` plus every Ikea/Loom/Orbital/Engine check and Ikea example (293 edges;
`orbital_design_check` excluded because it fails to compile at this commit).
Prior reading: [Calico build lessons](../../notebook/build-iteration.md) (broad
inline graphs cost both front and back end) and the
[compile probe](../../tools/compilation.md), whose per-TU method this extends.

## Findings

**Clean build:** 106 s wall, 1,440 CPU-s (`-j16`). Ikea tests are 85% of CPU;
product libraries are 3%. The longest TU,
`ikea/test/seriespack/read/composition.cpp` (79 s), bounds wall time together
with the eight `ikea/test/filters/widths_*.cpp` shards (36–70 s each). Links are
under 2 s. No-op: 0.01 s.

**Back end, not parsing:** 77% of compile time is back end (`-ftime-trace`).
Header parsing is 8%. In the slowest shard, `AlwaysInlinerPass` alone is 11 s:
each shard instantiates ~160 `series_case` bodies and force-inlines the kernels
into them. Debug-info passes (DEBUG_VALUE, assignment tracking, debug-variable
analysis) total ~120 CPU-s across the build.

**Memory:** heavy test TUs peak at 2.3–2.9 GiB RSS each (widths shards, tiles,
read/composition). Eight together exceed 16 GiB (the observed worker OOM). The
local 8 GiB OrbStack VM with Ninja's default `-j` swaps: its existing dev log
shows the same shards at 120–254 s versus 51–70 s here. A 1-vCPU 2 GiB default
worker is below a single heavy TU's peak.

**Incremental:** touching a core SeriesPack/TuplePack header rebuilds 114–121
edges, 79–94 s wall here; ~75 TUs (1,170 of 1,440 CPU-s) depend on
`seriespack/view.h`. `orbital/types.h`: 8 s; `loom/execution.h`: 0.8 s;
`ikea/src/seriespack/readers.cpp`: 21 s.

**Output size:** debug sections are 91% of output bytes. `ikea_filters_check` is
160 MiB (10 MiB text); `ikea_seriespack_check` 84 MiB. All product library text
totals 0.8 MiB; test text comes from combinatorial instantiation.

## Flag variants (per TU, relative to `-O2 -g`)

| Variant | Wall | Peak RSS | Debug bytes |
| --- | --- | --- | --- |
| `-gline-tables-only` | 0.60–0.75 | 0.47–0.58 | 0.22–0.44 |
| `-g0` | 0.54–0.70 | 0.40–0.50 | 0 |
| no assignment tracking | 0.82–0.96 | 0.69–0.86 | ~1 |
| `-O1 -g` | 0.82–0.95 | ~1 | ~1 |
| `-Og` | 0.81–0.96 | ~1 | ~1 |
| `-O0 -g` | 0.33–0.88 | up to 1.38 | larger; text 4–12x |

Whole build with `-O2 -gline-tables-only`: 74 s wall (−37%), 1,097 CPU-s
(−29%), build tree 359 MB vs 894 MB. `-O1 -g`: 106 s vs 117 s. Text sizes are
unchanged by debug level. Check runtime is ~1.7 s in all three.

## Adopted

`RelWithDebInfo` now defaults to `-gline-tables-only` (`SIXDB_DEBUG_INFO=lines`;
`full` restores `-g`). `sixdb_heavy_compile()` puts `ikea_filters_check`,
`ikea_seriespack_check` and `ikea_tuplepack_packets_check` in a Ninja pool of
host MiB ÷ 1792 (lines) or ÷ 3072 (full) jobs; `SIXDB_HEAVY_COMPILE_JOBS`
overrides it. The pool is per target, so those targets' light TUs queue too.

`verify.sh`, clean build of the same target set with Ninja's default `-j`:

| Host | Case | Pool | Wall | CPU-s | Min available |
| --- | --- | --- | --- | --- | --- |
| 16 GiB, 8 vCPU | lines | 8 | 132 s | 1,280 | 6.1 GiB |
| 16 GiB, 8 vCPU | full | 5 | 248 s | 1,607 | 3.1 GiB |
| 16 GiB, 8 vCPU | full, unpooled | — | OOM-killed | — | 37 MiB |
| 32 GiB, 16 vCPU | lines | 17 | 75 s | 1,157 | 16.4 GiB |
| 32 GiB, 16 vCPU | lines, unpooled | — | 73 s | 1,183 | 15.5 GiB |
| 32 GiB, 16 vCPU | full | 10 | 147 s | 1,619 | 12.1 GiB |

The pool costs ~2 s with line tables on 32 GiB. With full debug info it is
deliberately conservative: the unpooled `-j16` baseline above took 106 s on the
same host, and the full-info pool could likely be widened toward ÷ 2.5 GiB. A
first, stricter sizing (÷ 2.5/3.5 GiB) took 88/165 s on 32 GiB. Checks passed
under both debug levels (hashtable width and two Loom example targets were not in the set). Worker jobs `20261022T020325Z-acc98bfe`,
`20261022T020354Z-1e87ee83` (first sizing), `20261022T021854Z-090e38c4` and
`20261022T021934Z-ba3733a3`.

`SIXDB_TEST_SCOPE=fast` (the default) instantiates the filter, SeriesPack and
hashtable width matrices at 33 of 64 widths (`ikea/test/scope.h`): 1–10, the
striped widths 12/14/20, and 8n−1, 8n, 8n+1. On the 32 GiB host a clean build
is 56 s and 992 CPU-s versus 79 s and 1,391 CPU-s for `full` (−29% CPU), and a
rebuild after touching `seriespack/view.h` is 46 s versus 70 s. Both scopes
passed every built check. Worker jobs `20261022T024330Z-1218c100` and
`20261022T024416Z-b14b723e`. The saving is below the share of width shards in
CPU because each shard's fixtures, includes and non-matrix checks remain.

Sanitizer and Release builds keep full `-g`, so the pool reads the configured
flags: ÷ 8 GiB when `-fsanitize=` is present, ÷ 1.75 GiB for line-table
RelWithDebInfo and ÷ 3 GiB otherwise. `pool-memory.sh` measured full-scope
peaks of 2.8 GiB per TU for Release `-O3 -g` and up to 10.9 GiB for Debug `-O1`
ASan/UBSan (`mutation/widths_9_16.cpp`; `filters/tiles.cpp` 8.8 GiB, most
others 4.6–5.1 GiB), job `20261022T034131Z-7a38d527`. With the flag-sized pool
(3 on 32 GiB) and default `-j`, `asan-verify.sh` built and passed the three
heavy checks in 18 min with at least 14.3 GiB available
(`20261022T040130Z-803c750d`); 10 unpooled jobs had exhausted 32 GiB.

## Reproduce

```sh
python3 workbench/tools/worker.py run workbench/spikes/build-speed/cloud.sh \
  --machine neoverse-v2 --instance-type c8g.4xlarge --disk-gb 80 --deadline 7200
python3 workbench/tools/worker.py run workbench/spikes/build-speed/variants.sh \
  --machine neoverse-v2 --instance-type c8g.4xlarge --disk-gb 120 --deadline 7200
```

`cloud.sh` records clean/no-op/incremental timings, Ninja deps, section sizes
(`sizes.py`), per-compile RSS (`rss.sh` launcher) and trace summaries
(`traces.py`). `variants.sh` recompiles costly TUs under flag variants
(`variants.py`) and runs whole builds for three flag sets. Results: worker jobs
`20261022T004816Z-724256f9` and `20261022T010120Z-be53bb78`. x86 was not measured.
`verify.sh` takes `CASES` (`name=cmake,args ...`) and samples `MemAvailable`.
