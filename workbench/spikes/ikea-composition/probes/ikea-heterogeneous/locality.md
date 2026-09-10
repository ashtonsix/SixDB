# Metadata read footprints

[Back to the spike](README.md). This 2026-09-10 enumeration covers **capacity
256**, a 64-byte-aligned metadata base, and a fresh cursor for each point or
range. It follows the [metadata geometry](metadata_format.h),
[native readers](metadata_native.h) and [cursor refill rule](range_kernel.h).
The body allocation and plain query are separate resources.

`direct32` reads only the requested four-byte records. `local16` and `scan128`
eagerly reconstruct every sixteen-entry frame visited by the range, including
unrequested predecessors and future entries. The frame is reused until the
cursor crosses its next checkpoint; an empty range reads no metadata.

## Exact payload intervals

All intervals are half-open byte offsets from the metadata base. For point `i`,
write `q=floor(i/16)` for its frame. The direct read is `[4i,4i+4)`.
Local refill reads checkpoint `[32q,32q+2)`, population packet
`[32q+2,32q+20)`, and lengths `[32q+20,32q+32)`: their union is one 32-byte packet.

Scan refill reads:

- checkpoint `[2q,2q+2)`;
- population packet `[32+18q,50+18q)`;
- one or two sixteen-byte length fragments starting at
  `320 + 96*floor(q/8) + 16*(q%2) + 32c`.

The length chunk set `c` is respectively `{0}`, `{0,1}`, `{1,2}`, or `{2}`
for `floor((q%8)/2)` equal to 0, 1, 2 or 3. These are the existing ScanPack6
branch-local reads. Individual LocalPack6 instructions may overlap; the byte
unions above count each payload byte once, independent of that overlap.

## Every point and four ranges

Means give equal weight to all 256 possible point indices. Lines below are
distinct 64-byte lines containing the read bytes, not measured misses.

| Fresh point read | Unique bytes, mean / max | Lines, mean / max | Violations of two adjacent lines |
| --- | ---: | ---: | ---: |
| `direct32` | 4 / 4 | 1 / 1 | 0 / 256 |
| `local16` | 32 / 32 | 1 / 1 | 0 / 256 |
| `scan128` | 44 / 52 | 3.375 / 5 | 256 / 256 |

For Scan, 16 points touch two lines, 144 touch three, 80 touch four, and 16
touch five. Even its two-line cases have a gap between the lines.

Each range is written `first/count`; results are **unique bytes / unique lines /
byte-envelope length**. The envelope runs from the first read byte through the
last, including gaps. Repeated fragment reads do not enlarge these unions.

| Range | `direct32` | `local16` | `scan128` |
| --- | --- | --- | --- |
| 0/256 | 1024 / 16 / 1024 | 512 / 8 / 512 | 512 / 8 / 512 |
| 3/37 | 148 / 3 / 148 | 96 / 2 / 96 | 108 / 3 / 368 |
| 15/18 | 72 / 3 / 72 | 96 / 2 / 96 | 108 / 3 / 368 |
| 255/1 | 4 / 1 / 4 | 32 / 1 / 32 | 36 / 3 / 482 |

The full-range unions are `[0,1024)` for direct and `[0,512)` for both packed
layouts. Ranges 3/37 and 15/18 visit the same packed frames 0–2: Local reads
`[0,96)` on lines `{0,1}`; Scan reads `[0,6)`, `[32,86)`, `[320,368)` on
`{0,1,5}`. Direct reads `[12,160)` and `[60,132)`, respectively, both on `{0,1,2}`.
For 255/1, direct reads `[1020,1024)` on line 15; Local reads `[480,512)` on
line 7; Scan reads `[30,32)`, `[302,320)`, `[496,512)` on `{0,4,7}`.

## Individually local children do not prove a local parent

Points 80–95 refill Scan frame 5. Its checkpoint reads `[10,12)` on line 0,
population packet `[122,140)` on lines `{1,2}`, and length fragments
`[368,384)`, `[400,416)` on lines `{5,6}`. Every child individually satisfies
the two-adjacent-line condition, but the enclosing read touches five lines
`{0,1,2,5,6}`. Moving the children into separate planes changed the parent
obligation despite preserving their legal native reads.

This is a failure of a two-adjacent-line promise for this metadata placement,
not proof that its measured workload must be slower. Nor does a passing metadata
read establish a bound for metadata plus decoded-body inputs and query bytes.
The enclosing operation must account for each resource and its admitted extent.

## Reproduce and interpret

```sh
orb -m ubuntu python3 workbench/spikes/ikea-composition/probes/ikea-heterogeneous/footprint.py
# Optional structured output includes the exact witness child intervals.
orb -m ubuntu python3 workbench/spikes/ikea-composition/probes/ikea-heterogeneous/footprint.py --json
```

The standard-library script enumerates the source-prescribed native refill
intervals, checks allocation bounds and complete coverage, and checks every
child separately from the enclosing witness. It does not compile or inspect a
particular target binary; captured disassembly establishes what that compiler
actually emitted. It should be updated if the cursor or native read strategy
changes.

Logical lengths identify encoded BEC bytes; they do not describe these metadata
read unions or the decoder's accessible window. The requested result needs
populations and resolved starts; packed reconstruction additionally depends on
preceding lengths, and these readers fetch complete frames. No claim of minimal
required-field traffic follows from their access widths. Constants, dispatch or
ownership state, compiler spills, speculation, prefetching and issued-byte totals
are outside this census. In particular, 512 allocated metadata bytes do not
imply one local lookup, and neither line geometry nor footprint alone proves
cache residence or hardware traffic.
