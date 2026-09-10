# First heterogeneous campaign — 2026-09-10

The directories trade space, address reconstruction and locality. At 256 BEC
blocks, both packed directories use 512 bytes versus 1024 for `direct32`.
With BEC bodies and query held identical, their fully inlined full-range time
is 6–14% higher than the direct layout across this campaign. The BEC region cut
roughly halves selected function text, but its time cost depends on target and
metadata layout. These are repeated resident controls; cache residence is
unestablished and **no cold random-access result is claimed**.

## Work and measurement

[bench.cpp](bench.cpp) measures decode → intersection with a separate plain
query → count. Each workload has eight complete 65,536-position windows, each
with 256 BEC bodies. Two deterministic synthetic workloads cover a population
sweep and random half-density. [prepare_data.py](prepare_data.py) selects up to
eight whole windows without replacement from each of the twelve shared
RealRoaring archive members; all twelve supplied eight. Empty/full 256-position
cells inside a selected window are preserved. The retained prepared input has
selected-window lineage and source hashes. These small deterministic samples
do not characterize every row or real query workload.

Each window is encoded once. The three directories share that body owner and
query; all six arms perform the same requested logical work. Range first/count
is `0/256`, `3/37`, `15/18` or `255/1`. Preparation, full-source admission,
encoding and the independent byte-wise intersection oracle are outside timing.
The current consumer always invokes BEC decode, including zero/full bitsets;
metadata-driven skipping is not measured.

One allowed CPU is pinned and checked around every repetition. A common pass
count for all six arms of a dataset/range is calibrated with `direct32-inline`
to at least 10 ms. Each arm gets a warm pass, then five sequential repetitions.
Arms run in fixed order, without concurrent cases. The tables use per-arm medians;
raw repetitions show spread. This is one campaign per machine, not a randomized
order study or a confidence interval. Hardware clocks/cache placement and
longer-run variability have not been separately controlled.

All three runs have 1,680 timing rows: 14 datasets × 4 ranges × 6 arms × 5
repetitions. Checksums, pass counts, body/query extents and logical work agree
across arms. All timed rows report zero major/minor page faults. Cycle and
instruction groups were available, with running time equal to enabled time;
raw counts are retained. Page faults and cycle counters do not establish cache
residence or traffic. The harness cost of invoking prepared ranges is included.

`metadata_bytes` counts the selected directory allocation's logical extent;
`body_bytes` counts dense logical BEC payload; `body_suffix_bytes` separately
counts the 64-readable-byte owner suffix per window. `query_bytes` counts the
whole query allocation for each window. These fields are not measured bytes
transferred or total heap working set. Construction catalogs, original inputs,
other coexisting directory variants, prepared-range objects, lookup tables and
allocation rounding are outside that accounting.

## Timings

Population-sweep (`structural`) median **ns per full 256-block range**, with
inline/split performing identical decode/AND/count work:

| Machine | Metadata | Inline | Split | Split / inline |
| --- | --- | ---: | ---: | ---: |
| Zen 5 | direct32 | 6758.20 | 7678.07 | 1.136 |
| Zen 5 | local16 | 7425.37 | 7787.34 | 1.049 |
| Zen 5 | scan128 | 7349.72 | 7739.28 | 1.053 |
| Granite Rapids | direct32 | 6533.01 | 6787.63 | 1.039 |
| Granite Rapids | local16 | 7094.90 | 7116.47 | 1.003 |
| Granite Rapids | scan128 | 6969.79 | 7109.79 | 1.020 |
| Neoverse V2 | direct32 | 18660.90 | 19995.10 | 1.071 |
| Neoverse V2 | local16 | 21241.60 | 21139.90 | 0.995 |
| Neoverse V2 | scan128 | 21137.00 | 21043.20 | 0.996 |

Across all fourteen datasets, full-range **split/inline** ratios span the
following values. These are ranges of individual dataset median ratios, not
pooled throughput or timing uncertainty:

| Machine | direct32 | local16 | scan128 |
| --- | --- | --- | --- |
| Zen 5 | 1.132–1.138 | 1.049–1.057 | 1.053–1.058 |
| Granite Rapids | 1.035–1.042 | 1.002–1.009 | 1.015–1.025 |
| Neoverse V2 | 1.068–1.073 | 0.994–1.001 | 0.991–0.998 |

The corresponding packed-inline/direct-inline ratios are 1.088–1.099 Local and
1.084–1.091 Scan on Zen 5; 1.080–1.091 Local and 1.061–1.076 Scan on Granite
Rapids; 1.138–1.142 Local and 1.132–1.136 Scan on V2. The directory byte saving
therefore has a visible resident computation cost. It might repay that cost in
a larger working set, but this campaign cannot establish that.

The structural workload's final-point (`255/1`) inlined times are:

| Machine | direct32 ns | local16 ns | scan128 ns |
| --- | ---: | ---: | ---: |
| Zen 5 | 43.31 | 58.70 | 57.97 |
| Granite Rapids | 52.15 | 64.36 | 62.60 |
| Neoverse V2 | 87.89 | 110.46 | 103.67 |

This point still repeats over eight windows; it is **not cold**. Eager metadata16
refills do more work than direct point loads, even before considering the
[metadata byte/line geometry](locality.md). All partial-range results and raw
repetitions are in the target summaries below.

## What the boundary actually did

The first nominal-inline implementation was outlined by Clang on all targets.
It therefore failed to test the intended cut. Those runs are retained as
compiler-boundary controls, and their timings are excluded from the tables
above. The corrected source forces the curated region and author loop inline;
actual benchmark ELFs now have no calls in the inline readers and only the two
explicit BEC calls in split readers. No LTO or custom calling convention is used.

[Boundary inspection](notes/boundaries.md) separates metadata frame preservation,
BEC internal spills and scalar ABI operands. All packed split readers save and
reload the 64-byte native metadata frame around pair calls. Full inlining keeps
that frame off the stack on x86, but V2 still spills it and adds other vector
pressure. These observations help explain why the same cut has different costs;
they do not assign all elapsed-time differences to one spill.

## Validation, retained evidence and recovery

The captured runner builds and runs the existing bitset and integer provider
checks plus the combined caller, in one source configuration. Each final run
passes 70,417 bitset codec cases, 1,024 algebra cases, 14,336 integer codec cases
and 3,670,016 integer point reads. Combined checks cover 22,240 metadata entries,
254,370 ranges and 13,071 structural BEC pairs. They include all BEC populations,
exact body/query guards, partial capacities, association/framing failures,
retained owner lifetime and unrequested query prefixes. ASan/UBSan also passes
on the local AArch64 host. This is correctness evidence, not native-host timing.

| Capture | Compact evidence | Role |
| --- | --- | --- |
| 00:39:04 Zen 5 | [summary and repetitions](evidence/zen5/summary.md) | Corrected inline/split timing and all checks |
| 00:39:05 Granite Rapids | [summary and repetitions](evidence/granite-rapids/summary.md) | Corrected inline/split timing and all checks |
| 00:39:04 Neoverse V2 | [summary and repetitions](evidence/neoverse-v2/summary.md) | Corrected inline/split timing and all checks |
| 00:38:32 native ASan/UBSan | [combined checks](evidence/native-sanitize/combined-checks.txt) | Corrected source with both provider checks |
| Initial Zen 5 | [provenance](evidence/outlined-control-zen5/provenance.json) | Compiler-outlined control; full source/ELF recovered from bundle |
| Initial Granite Rapids | [provenance](evidence/outlined-control-granite-rapids/provenance.json) | Same |
| Initial Neoverse V2 | [provenance](evidence/outlined-control-neoverse-v2/provenance.json) | Same |

Each directory has a verified `artifact.json` and exact source identity.
Full source, executable, assembly, compiler commands, raw timing and input
lineage can be recovered through [artifact tools](../../tools/artifacts.md).
The seven exports were retained after verified upload/download; large outputs
and datasets are outside Git. Reproduce new measurements with the commands in
[README](README.md), or regenerate existing summaries without remeasurement:

```sh
python3 workbench/spikes/ikea-heterogeneous/report.py workbench/spikes/ikea-heterogeneous/evidence/zen5
python3 workbench/tools/artifacts.py verify workbench/spikes/ikea-heterogeneous
```
