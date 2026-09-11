# V2 byte-gather result and narrow selection

The shared-buffer V2 diagnostic supports grouping the existing eight-value
TBL4 gathers into 64-value dense regions for Local payload width 8 with u64
input carriers. Production now selects that region and retains exact existing
single-tile remainders. It changes neither the byte-body wire representation
nor other payload widths, carriers, decode paths or the separate head traversal.
The preceding Local3–7/u8 coalescing selection remains intact.

This is stronger evidence than the original cross-fixture comparison. The
original bulk screen measured .11494/.08032 ns/value for bound SeriesPack and
Calico, a 1.431× ratio. This diagnostic gives both implementations the same
source values, source addresses and destination addresses. At 8,192 values it
measures .11461/.08010, again 1.431×. Raw native8 is .11242, so removing the bound
interface leaves most of the gap. Grouping the same gather operation makes a
substantial difference within that shared fixture.

The run used Neoverse V2, pinned CPU0, Clang 21.1.8, O3,
`-march=armv8-a -mtune=neoverse-v2`, 50ms minimum time and five sequential
repetitions. All 30 benchmark cases and 8,260 sanitizer/exact/guard checks passed;
the baseline library was verified against the retained capture.

| Arm | 256 values | 8,192 values | 65,536 values |
|---|---:|---:|---:|
| Bound Local8/H0 | .11336 | .11461 | .15220 |
| Bound all-head K8/H8 | .12565 | .11513 | .15375 |
| Raw native8 | .11446 | .11242 | .15241 |
| Gather32 D | .08868 | .09041 | .11468 |
| Gather32 Q | .08191 | .09006 | .11461 |
| Gather64 D | .08707 | .08650 | .11172 |
| Gather64 Q | .08369 | .08722 | .11198 |
| Gather256 D | .09088 | .10471 | .12367 |
| Gather256 Q | .08822 | .08400 | .11729 |
| Calico | .08242 | .08010 | .11333 |

Numbers are median CPU ns/value. Source footprints are 2 KiB, 64 KiB and
512 KiB; output footprints are 256 B, 8 KiB and 64 KiB. Those footprint names do
not assert cache residency. D/Q describes the authored store width; Clang can
combine adjacent D stores.

Gather64 D reduces time versus raw native8 by 23.9%, 23.1% and 26.7% across the
three sizes. Gather32 Q wins at the smallest size, while Gather64 D wins at the
two larger sizes. Gather256 Q gains another 2.9% at 8,192 values but loses to
Gather64 D at the other sizes; both 256-value forms also spill vector data. The
64-value region is therefore the bounded production choice. It remains about
8% slower than Calico at 8,192 values in the diagnostic; no exact-parity claim
follows from this selection.

## What this selection does not resolve

The all-head K8/H8 path still has its separately measured loss. It uses the
same eight-value gather mechanism in a different traversal, and changing the
payload encoder does not change that code. A minimal follow-up can reuse the
validated payload entry for a whole 64-value prefix when the head shift is zero
and the source carrier is u64, preserving existing short-region and scalar
remainders. That proposal was handed to the owner of `native.cpp`; this receipt
does not claim that the all-head path has been corrected or measured after it.

Nor does this test establish a new grouping policy for wider bodies, arbitrary
head shifts, or multiple head planes. The existing 256-value alternatives remain
diagnostic controls.

## Production validation

The final header SHA256 is
`24ca61295359664381426fcc0e4e9c08bb4b9204ebb34d8a44eb58ca4abdaef2`.
Matched native objects use identical copies of `native.cpp` and all other
headers, with the Local3–7 coalescing candidate as the before state. Total
`.text` changes by −12 bytes and read-only data by −16 bytes; unwind data grows
112 bytes. This is effectively unchanged aggregate code/data size. The actual
public encoder's 64-value loop contains eight TBL4 gathers and no helper calls
or stack accesses; its partial final tile retains the existing cold staging.

Checks passed:

- Normal and ASan/UBSan payload tests: 400 families, 513,320 checks. The added
  width-8 cases cover every tile count 0–33, all carriers, every u64 source bit
  across 64/72-value regions, high-bit projection, unaligned byte destinations,
  exact allocations and both guard-page ends.
- Independent public oracle: 206 descriptions, 2,472 placements, 39,075 reads,
  18,540 mutations and 206 append scenarios.
- Width-8 payloads with heads 0/8/16 at lengths around 64/128/192/256: 156
  dense/strided placements, 2,808 reads and 1,470 mutations.
- Guarded public encode: 19,776 cases across 206 descriptions and four
  carriers, checked/bound paths, including 1,025-value arrays.

A short local OrbStack run also passed the actual combined bound-encoder
benchmark's independent checks. Its timing was noisy and adds no V2 performance
claim. A final integrated V2 capture is needed for the production-bound timings,
including the separately developed arbitrary-range and head-traversal changes.

The bulky V2 receipt is under
`build/workspaces/seriespack-neon-byte-grain-20260910/build/workers/20260910T162724Z-fcb97c2d/results/neon_byte_grain/`.
Matched source snapshots, compiler commands, size receipt, focused public test,
assembly and validation binaries are under
`build/seriespack-neon-byte-production/`. The diagnostic's original four source
files remain unchanged from the hardware capture.
