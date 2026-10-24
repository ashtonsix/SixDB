# Patched point reads for the bounds stage

The bounds stage ([../../bounds/README.md](../../bounds/README.md#representation-and-seams))
stores each block as lo, width K, a SeriesPack Local slot and exceptions
(position, x - lo), with exception slots holding field 0. Its control,
`compose.cpp`, found that letting exceptions grow (no per-exception penalty)
slows points 1.5–2.7× on Zen 5. This probe asks where that cost comes from
before Engine's penalty is set. SUM and COUNT need nothing new from
SeriesPack: lo shifts the existing range kernels' results (SUM + lo × rows)
and arguments (COUNT against t - lo), with exceptions corrected by their
own values, as `compose.cpp` already does.

[`points.cpp`](points.cpp) copies `compose.cpp` (the bounds thread keeps the
original) and times three point paths on the same blocks:
- **pair:** search the block's exceptions first, stored as compose.cpp's
  32-byte (u8, i128) pairs;
- **compact:** the same search over a u8 position column and a u64 value
  column, 9 bytes per exception;
- **field first:** read the field first; a non-zero field cannot be an
  exception, so only field-0 rows search the compact columns.

Jobs `20261024T145743Z-2d6ea28f` (Zen 5) and `20261024T145810Z-f9490bc3`
(Graviton 4); every point, SUM and COUNT is checked against the column.
[evidence/points-zen5.jsonl](evidence/points-zen5.jsonl),
[evidence/points-graviton4.jsonl](evidence/points-graviton4.jsonl).

| ns per point, Zen 5 | exceptions | pair | compact | field first |
| --- | --- | --- | --- | --- |
| ClientIP, guess, penalty 0 | 290k | 15.6 | 10.9 | 12.3 |
| ClientIP, guess, penalty 8 | 12k | 4.8 | 4.3 | 4.3 |
| OS, guess, penalty 0 | 212k | 34.9 | 25.6 | 21.1 |
| OS, today | 116k | 28.8 | 22.1 | 15.4 |
| RegionID, guess, penalty 0 | 331k | 39.8 | 27.8 | 18.8 |
| UserID, guess, penalty 0 | 817k | 49.0 | 34.3 | 27.7 |
| UserID, guess, penalty 8 | 38k | 14.2 | 12.9 | 14.0 |

## Findings

1. **Much of the measured exception cost is the control's storage, not the
   representation.** The 32-byte pairs make the exception table 3.5× the
   representation's own costed bytes. Compact columns cut points with no
   exception penalty by 20–30% on Zen 5 and 15–59% on Graviton 4 (OS 68 → 35,
   UserID 127 → 52).
2. **Reading the field first removes most lookups.** Exception slots store 0,
   so only field-0 rows search. With compact columns that saves another
   15–45% on columns whose zeros are rare (OS, RegionID, ResolutionWidth,
   URLHash). It changes little on ClientIP and UserID, where around 10% of
   rows are exceptions.
3. **A penalty is still needed, but a smaller one.** With both changes,
   penalty 2 on OS and RegionID reads points within 1–10% of today (Zen 5:
   15.6 vs 15.4, 16.0 vs 14.5), while still saving 1–5% of bytes. ClientIP
   and UserID still pay 1.2–1.3× at penalty 2 and 2–3× at penalty 0. The
   penalty should be set against field-first reads over compact exceptions;
   the pair storage overstates it.

These are representation and read-path choices for the bounds composition.
They need no SeriesPack change: SeriesPack's point and range operations are
used as they are.
