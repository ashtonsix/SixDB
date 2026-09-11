# V2 result and bounded production selection

The retained V2 run supports coalescing eight adjacent Local8 tiles when the
carrier is one byte and the payload width is 3–7. Production now selects that
64-value region for dense encode and decode. Remaining pairs and single tiles
keep their exact existing access bounds. Individual tiles, strided regions and
wider carriers do not acquire this selection.

The diagnostic ran on Neoverse V2, pinned CPU0, with Clang 21.1.8, O3,
`-march=armv8-a -mtune=neoverse-v2`, 8,192 values, 50ms minimum time and five
sequential repetitions. All 74 cases and sanitizer/guard checks passed. Its
production library was verified byte-identical to the retained baseline.

| Local width | Native encode | Coalesced64 encode | Predecessor256 encode | Native decode | Coalesced64 decode | Predecessor256 decode |
|---|---:|---:|---:|---:|---:|---:|
| 3 | .20978 | .17280 | .17277 | .20091 | .17409 | .17247 |
| 4 | .18450 | .16863 | .16677 | .18470 | .17323 | .17325 |
| 5 | .21017 | .17699 | .17661 | .20092 | .16933 | .16746 |
| 6 | .20960 | .17291 | .17286 | .20090 | .16740 | .16548 |
| 7 | .24576 | .18057 | .17342 | .21931 | .16730 | .16910 |

Numbers are median CPU ns/value, with shared input/output buffers for all arms.
The selection reduces encode time by 8.6–26.5% and decode time by 6.2–23.7%.
Grouping four existing pairs without coalescing does not recover the encode
loss. The concrete change is that the admitted wire region uses complete vector
loads/stores, including its exact last eight-byte chunk when present, instead
of repeating short packet accesses.

Four coalesced regions per iteration improve encode another 1.2–3.2% in this
run; decode changes by −0.4–1.1%. The production selection retains the smaller
region. In particular Local7 encode remains about 4.1% behind the immediate
predecessor in this diagnostic; that residual is recorded rather than described
as exact parity.

The striped arms expose a separate loop-grain result. Scan4 encode falls from
.01357 to .01061 ns/value at a fixed 256-value region, matching its predecessor;
decode falls from .01701 to .01580. Scan6 encode falls from .01840 to .01710,
also matching its predecessor, while decode increases from .01910 to .01988.
Those measurements do not select a striped production change in this Local
coalescing patch.

## Integration evidence

The matched before/candidate compilation changes only `native_neon.h`; the
copied `native.cpp` and every other included header are identical. Both native
objects use the V2 flags above. The candidate adds 4,268 bytes of `.text`
(0.33%) and 496 bytes of read-only data, with no new dispatch table. The ten
selected dense functions have no helper calls or stack accesses, including
their exact remainder handling.

The final production header SHA256 is
`370a96764d012a6b8bf290ff229c1952c11c39069c5e09b2f069caae5cdc9948`.
The matched `native.cpp` hash is
`869f0504a0e0febaba99347fc01058779d903aaeb136356960919f8353aaa0df`;
`detail/physical.h` is
`41129fd803584a6e663c39f98306f558c3a8a215164160a511244c01697f32fa`,
including the independent regular-striped point correction. Later range-path
changes require a new integrated capture; these timings do not measure them.

Validation passed:

- 396 payload families and 489,112 checks, normal and ASan/UBSan. Dense checks
  cover every tile count from 0 through 33, all four carrier sizes, source-bit
  bases across the 64/72-value boundary, high-bit projection, all byte offsets,
  exact allocations and both guard-page ends.
- The independent public oracle: 206 descriptions, 2,472 placements, 39,075
  reads, 18,540 mutations and 206 append scenarios.
- A focused extension of that oracle: 780 dense/strided placements with heads
  0/8/16 and lengths around 64/128/192/256, 14,040 reads and 7,350 mutations.
- 19,776 guarded public encode cases, all 206 descriptions and four carriers,
  checked and bound paths, including 1,025-value arrays and exact source/output
  extents.

The bulky V2 receipt is under
`build/workspaces/seriespack-neon-coalescing-20260910/build/workers/20260910T162456Z-ba3adcd5/results/neon-coalescing/`.
Matched source snapshots, compile commands, code/data sizes, selected assembly,
the focused public-oracle source and validation binaries are under
`build/seriespack-neon-coalescing-production/`. The diagnostic's original five
source files remain unchanged from its hardware capture.
