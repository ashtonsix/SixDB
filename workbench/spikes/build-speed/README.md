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
