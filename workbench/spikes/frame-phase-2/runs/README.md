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

## What this says about the split

The choices form a frontier rather than a winner. Bytes go down with sizes,
G64 and outliers; point and SUM cost go up with each. That makes the boundary
form, checkpoint spacing, outlier budget and placement weighted choices:
Engine supplies the bytes/point/scan weights, and Ikea fits within them. A
point-heavy part keeps u8 ends. A cold or bandwidth-bound part takes sizes,
G64 and outliers. These are warm timings, so the bandwidth side of that
trade is still unmeasured.

## Next

- Remove the glue: sum `s·v` plus `Σv` instead of adding one to each size,
  and take a patch-free path for blocks without outliers.
- Retime once SeriesPack's short reads are faster, and flushed, where the
  bytes saved should pay.
- The same stages for the other boundary consumers: block body offsets
  within a group, and NULL or compaction ranks.
