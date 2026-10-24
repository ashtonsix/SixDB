# Run boundaries as composed stages

Ashton's composition (2026-10-24): Intervals supplies a block's run
geometry as sizes or relative ends, with the checkpoints moved into the
group's checkpoint column. The bounds stage
([notebook/pfor-bounds.md](../../../notebook/pfor-bounds.md)) pulls outlier
fields out, and SeriesPack packs the rest. Engine chooses the boundary form,
checkpoint spacing, outlier budget and placement. Phase 1's design already
named this path ([ENCODING.md §6–8](../../frame-phase-1/ENCODING.md#6-block-kind)),
and its measurements set the bar. Run geometry was 43–59% of bytes on
run-heavy columns, and a u8 ends-minus-one plane with no checkpoints beat
Boundlist's compact Sizes (OS 2.78 against 4.17 bits per value;
[encoding README](../../frame-phase-1/encoding/README.md)). Intervals found
that sparse relative ends widen their fields
([FINDINGS.md](../../intervals/FINDINGS.md)).

Each stage is a separate function over an existing module
([`read.cpp`](read.cpp)):

| Stage | Module | Choices |
| --- | --- | --- |
| boundaries | ikea::intervals forms | sizes or relative ends; checkpoint spacing G = 16 or 64 |
| bounds | the bounds note's exhaustive width search | none, or outliers as (u8 rank, u8 value) |
| pack | ikea::seriespack Local, one array per width | field placement at 8 rows (tight) or 16 rows (native groups) |
| read | SeriesPack `read`, then `intervals::sparse_sizes` | whole block, or routed through the checkpoint column to one region |

[`census.cpp`](census.cpp) counts bytes for more forms (relative ends,
Bulk). A run block is a 256-row block with 2–128 runs; run values are the
same in every form and are left out. Every form decodes back to the
column's run ends, and every timed answer is checked. Source `7141f210`;
jobs `20261024T034701Z-f92cc2e5` (Zen 5, AVX-512) and
`20261024T034734Z-3fc3b189` (Graviton 4). Warm only.
[evidence/read-v2-summary.txt](evidence/read-v2-summary.txt),
[evidence/census-v1.jsonl](evidence/census-v1.jsonl).

## Findings

| Zen 5, ratio to u8 ends | bits per row | point | SUM over runs |
| --- | --- | --- | --- |
| OS: sizes, G64, bounds | 0.69 | 15× | 2.3× |
| OS: sizes, G16, 16-row placement, routed | 0.90 | 6.8× | 1.5× |
| UserID: sizes, G64, bounds | 0.58 | 10× | 2.2× |
| UserID: sizes, G16, 16-row placement, routed | 0.82 | 4.0× | 1.4× |

u8 ends cost 3.2–5.2 ns per point and 26–35 ns per block of SUM on Zen 5,
and 8.8–9.0 ns and 35–50 ns on Graviton 4.

1. **The composition saves 31–43% of run geometry.** Sizes with G64
   checkpoints, packed by SeriesPack with outliers pulled out, take OS from
   1.12 to 0.77 bits per run-block row, RegionID from 1.57 to 0.93 and
   UserID from 1.69 to 0.98. Pulling outliers out is worth 8–20% on top of
   tight sizes. G64 saves 5–7% over G16.
2. **Relative ends and Bulk do not pay here.** Relative ends save only 2–8%
   at G16 and lose at G64, where fields widen as Intervals found. Bulk's
   striped tiles waste most of their bytes at 30–60 runs per block (4.3 bits
   per row against 0.9).
3. **Reads cost more today: points 4–15× and SUM 1.4–2.3× the u8 ends.**
   Sizes need a decode and a prefix before the search; ends are searched in
   place. A local profile puts about half the point cost in SeriesPack's
   short-range decode (16–128 fields per call) and most of the rest in glue:
   adding one, patching outliers, padding. The SeriesPack thread has the
   short-read finding.
4. **Placement and routing are the cheap speed levers.** Placing each
   block's fields on a 16-row boundary lets SeriesPack read native groups. It
   costs 0.07–0.1 bits per row and cuts point and SUM time by about a
   quarter. Routing a point through the checkpoint column to one 16-field
   region saves up to another 25% at G16 and nothing at G64.
5. **Outliers cost time as well as saving bytes.** Patching adds 8–12 ns per
   point and 6–9 ns per block of SUM.

## Group-continuous series, Bulk, and cold reads

Packing each block's sizes separately left Bulk's 64–256-field tiles mostly
empty (a run block has 2–128 runs; the mean is 36 on OS and 46–56 elsewhere).
The group form makes the run sizes of 16 consecutive run blocks one
SeriesPack series per group, with u16 group checkpoints every G runs and
each block's first run index. Bulk's tiles then fill, and SUM over runs
takes one long read per group. A cold condition sweeps 256 MB before each
timed call. Source `f73b4cef`; jobs `20261024T040921Z-f1ae8dc2` (Zen 5) and
`20261024T040957Z-de63b810` (Graviton 4);
[evidence/read-v3-summary.txt](evidence/read-v3-summary.txt).

| Zen 5 | bits per row | point ns | SUM ns per block | cold point / SUM |
| --- | --- | --- | --- | --- |
| OS: u8 ends | 1.12 | 3.0 | 26.4 | 5.5 / 26.4 |
| OS: group sizes, G64, compact, outliers | 0.82 | 50 | 17.8 | 53 / 20.4 |
| OS: group sizes, G64, Bulk, outliers | 0.85 | 44 | 17.7 | 48 / 18.3 |
| UserID: u8 ends | 1.69 | 4.8 | 34.1 | 8.0 / 34.6 |
| UserID: group sizes, G64, compact, outliers | 1.09 | 55 | 26.3 | 59 / 28.9 |
| UserID: group sizes, G64, Bulk, outliers | 1.15 | 52 | 26.6 | 56 / 26.8 |

6. **In group form, sizes beat u8 ends on SUM over runs as well as on
   bytes.** On Zen 5 SUM is 1.3–1.5× faster (OS 17.8 against 26.4 ns per
   block) with 27–36% fewer bytes. On Graviton 4 it is 1.1–1.2× faster. A long
   read hits SeriesPack's fast paths; the per-block form paid its short-read
   cost instead.
7. **Bulk fills its tiles in group form.** It costs 4–6% more bytes than
   compact (whole-tile rounding per group). In return SUM is 7–8% faster on
   Graviton 4 (parity on Zen 5) and points are 3–12% faster.
8. **Points get slower in group form** (30–70 ns): a G64 region decode plus
   the group checkpoint search. A point-heavy part keeps u8 ends, or per-block
   G16 sizes at 16-row placement (20–31 ns).
9. **Cold barely moves anything.** These structures are a few hundred KB to
   a few MB, and every form is compute-bound here. u8 ends points rise from
   3 to 5.5 ns on Zen 5; the composed forms rise by about 3–6 ns. The bytes
   saved should matter for bandwidth on far larger parts or under shared
   bandwidth, which this does not measure.

## Pipelines with and without RLE

Ashton doubted RLE is universally useful. [`pipelines.cpp`](pipelines.cpp)
sends every block through one of two pipelines and times whole columns:

- **No RLE:** FOR, then bounds, then SeriesPack at one preset (compact,
  bulk_x86 or bulk_arm).
- **RLE when smaller:** run sizes (with bounds and G16 checkpoints) plus run
  values (FOR), both packed by SeriesPack Local at 16-row placement. A block
  takes this form only where its bytes are smaller.

Constant blocks exit early in both. Source `376672e8`; jobs
`20261024T041722Z-7e863600` (Zen 5) and `20261024T041751Z-82582304`
(Graviton 4); [evidence/pipelines-v1-summary.txt](evidence/pipelines-v1-summary.txt).

| Zen 5, compact | blocks taking RLE | bits per value: no RLE / RLE | SUM ns per block | COUNT ≥ ns per block | point ns |
| --- | --- | --- | --- | --- | --- |
| OS | 98% | 7.04 / 2.50 | 19.1 / 47.1 | 29.2 / 43.6 | 28 / 41 |
| ResolutionWidth | 90% | 10.18 / 3.52 | 12.1 / 51.3 | 31.4 / 46.8 | 8.8 / 39 |
| ClientIP | 96% | 31.66 / 9.89 | 27.1 / 57.0 | 41.1 / 49.1 | 6.3 / 42 |
| UserID | 96% | 51.07 / 15.01 | 123.9 / 69.6 | 4.4 / 24.3 | 19 / 50 |
| URLHash | 16% | 64.28 / 59.41 | 276.8 / 245.1 | 294.0 / 255.8 | 103 / 105 |
| EventTime, WatchID | 0% | unchanged | unchanged | unchanged | unchanged |

10. **RLE is column-specific.** By bytes it takes 90–98% of blocks on the
    categorical and session-ordered columns, 16% on URLHash, and none on
    EventTime or WatchID. Where it applies it cuts bytes 2.1–3.4×. Where it
    doesn't, the stage costs nothing.
11. **Today RLE trades those bytes for time.** SUM is 2.1–4.2× slower and
    COUNT 1.2–1.7× slower on Zen 5 (1.2–3.4× on Graviton 4), and points are
    1.3–6.7× slower. UserID's COUNT is 5.5× slower, because unencoded blocks
    are settled wholesale by their base. The exception is UserID, where unencoded blocks need
    about 51 bits and RLE SUM is 1.8× faster. These RLE blocks read their
    sizes and values per block, the short-read path that finding 6 shows is
    slow. The group-continuous form took SUM over sizes from about 50 to 18 ns
    per block on OS, so much of the gap belongs to this composition, not to
    RLE itself.
12. **Bulk against Local matters on Graviton 4 behind no RLE.** OS SUM is 28
    ns per block under Bulk against 46 under compact, and COUNT 73 against
    91. On Zen 5 the presets are within a few percent of each other.
13. Packed-block points in this probe cost 19–34 ns on OS, RegionID and
    UserID (about 6 on EventTime and ClientIP): an outlier lookup and a virtual
    width call per point. That is this probe's glue, not SeriesPack's `get`.

### Group-continuous RLE

The RLE blocks of each 16-block group share one sizes series and one values
series (values relative to the group's smallest minimum), with u16 group
checkpoints. Source `088c8da7`; jobs `20261024T042821Z-bf601ac7` (Zen 5) and
`20261024T042855Z-84c42bbc` (Graviton 4);
[evidence/pipelines-v2-summary.txt](evidence/pipelines-v2-summary.txt).

| Zen 5, compact | bits per value: no RLE / group RLE | SUM ns per block | COUNT ≥ ns per block | point ns |
| --- | --- | --- | --- | --- |
| OS | 7.04 / 2.12 | 19.9 / 19.6 | 29.7 / 9.5 | 28 / 51 |
| RegionID | 10.39 / 4.71 | 25.1 / 28.4 | 32.5 / 14.2 | 35 / 58 |
| ResolutionWidth | 10.18 / 3.24 | 12.3 / 23.7 | 31.5 / 11.8 | 8.8 / 50 |
| ClientIP | 31.66 / 8.92 | 29.0 / 30.7 | 40.6 / 14.2 | 6.3 / 59 |
| UserID | 51.07 / 14.30 | 124.7 / 39.7 | 4.1 / 15.0 | 19 / 64 |

14. **In group form, RLE keeps its 2.2–3.6× byte saving and stops costing
    scans on Zen 5.** SUM is at parity with no RLE on OS, RegionID and
    ClientIP (within 0.9–1.1×) and 3.1× faster on UserID; COUNT ≥ is 2.2–3.1×
    faster, since a run's comparison covers all its rows. ResolutionWidth's
    SUM is 1.9× slower, because its 9% constant blocks make no RLE cheap.
    Bytes also drop a further 4–15% against per-block RLE.
15. **Graviton 4 still pays for RLE scans:** SUM 1.2–1.9× and COUNT 1.1–3×
    slower than no RLE under Bulk, except UserID (SUM 2.5× faster). The RLE
    COUNT loop (a conditional add of run sizes) is the suspect: on Zen 5 it
    takes 9–15 ns per block, on Graviton 4 86–193. That matches the
    Clang/NEON select pattern seen before, and is worth a typed rewrite before
    the frontier is read off.
16. **Points are RLE's cost in every form** (50–65 ns on Zen 5 against 6–35
    without RLE). A point-heavy part should skip RLE, or keep u8 run ends
    (3–5 ns per point).

## What this says about the split

RLE is a weighted stage, not a default. It pays in bytes only on columns
with runs, and today it costs scan time there, so Engine's bytes/scan
weights decide it per part. Both pipelines need optimising: the no-RLE path
under Bulk and Local, and the RLE path in group-continuous form, where on
Zen 5 it now matches or beats no RLE on scans with 2.2–3.6× fewer bytes.


The choices form a frontier rather than a winner. Bytes go down with sizes,
G64 and outliers; point and SUM cost go up with each. That makes the boundary
form, checkpoint spacing, outlier budget and placement weighted choices:
Engine supplies the bytes/point/scan weights, and Ikea fits within them. A
point-heavy part keeps u8 ends. A cold or bandwidth-bound part takes sizes,
G64 and outliers. These are warm timings, so the bandwidth side of that
trade is still unmeasured.

## Next

- Glue removed locally (SUM consumes sizes minus one, no re-padding under
  16-row placement): SUM with outliers drops about 5%, points unchanged. What
  remains is SeriesPack's short-range decode, which the SeriesPack thread owns.
- Retime once SeriesPack's short reads are faster, and flushed, where the
  bytes saved should pay.
- The same stages for the other boundary consumers: block body offsets
  within a group, and NULL or compaction ranks.
