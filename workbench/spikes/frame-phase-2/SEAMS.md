# Cross-thread seams

The contracts the phase 2 threads share, 2026-10-24. Each row names who owns
it and where its evidence lives. A thread that needs a change here proposes it
to the owner rather than working around it. Anything not listed is still
thread-local.

| Seam | Contract | Owner | Evidence |
| --- | --- | --- | --- |
| Image | Descriptor columns in original block order: kind, width, base, body offset, run count. Bodies are front C's byte laws. Body placement (address order or grouped by kind) is a separate substrate choice per image. | Substrate (B) | [image.h](substrate/image.h), [shared image](substrate/README.md#the-shared-image-substrate-over-front-cs-leaves) |
| Leaf ABI | Header leaves in `bodies/` (`packed<layout, K>`, `runs<T>`, `lanes<T>`, `patched<K>`): `sum`, `count_ge`, `count_eq`, `mask_ge`, `masked_sum`, `point`, `unpack`, `max`, `min`, `first_ge`; run leaves take `n` after the body. Results are in the residual domain. `catalogue.h` and `sweeps.h` are measurement loops, not entry points. | Leaves (C) | [bodies/README.md](bodies/README.md) |
| Base terms | The substrate adds `base × count`, with the count taken from the final selected valid domain (after fine selection and validity); 256 only when that domain is the whole block, and never the predicate count or a run count. K ≤ 32 full-block sums fit u64; 64-bit run values return the wide type. | Substrate (B) | [LEAF-CONTRACTS.md](instruments/LEAF-CONTRACTS.md#partial-results-and-coordinate-laws) |
| Read envelope | Fast leaves read past their body by the halo front C tabulates per leaf and ISA (at most 63 bytes, or 15 lanes for run values). An image admits that once at binding as an initialised, immutable, read-isolated whole-image window: one suffix of the maximum halo, plus each next body readable to the same depth. Allocation padding alone does not grant it. Slices, protected neighbours or mutable bodies need an exact boundary route instead. Prefetch follows the logical body extent, not the halo. Compaction also has a write envelope (whole-word plane ORs, first-touch plane clears, uninitialised slice tails in a partial output block). | C states halos; B admits them; A reviews | [bodies/README.md](bodies/README.md#read-and-write-envelopes), [LEAF-CONTRACTS.md](instruments/LEAF-CONTRACTS.md#keep-the-fast-leaf-make-its-physical-authority-explicit) |
| Selections | Coarse: one bit per block, tested before any descriptor or body address is formed; an empty block reaches no leaf. Fine: four u64 words for original rows 0–255 of the block. Fine input selection, A validity and B validity stay separate inputs; a run-ordinal mask is a different type. | Substrate (B) | [LEAF-CONTRACTS.md](instruments/LEAF-CONTRACTS.md#partial-results-and-coordinate-laws) |
| Run lookup | Default: the leaves' SIMD count of ends below the row while the ends fit about 64 bytes; above that, ikea::intervals `direct_source` (bisect to one 64-byte span, then count), which is the Intervals thread's new default (`cb6c5d19`): at about 6 runs it takes 1.1 / 6.2 / 7.4 ns on AVX-512 / AVX2 / NEON, and 1.0 / 1.0 / 2.1 ns with `author::with_readable_window<32>` under the read-envelope row, against 11–13 ns for phase 1's projected `run_end_source`. The run-start bitmap wins from about 50 runs per block. Decision (2026-10-24): Frame's run lookups go through ikea::intervals, not a Frame-local search. The shared image's point path calls `direct_source<u8>` with `with_readable_window<32>` (`image.h` `point_intervals`), and Intervals' own `run_end_source` drops its sub-16-run projection so other Frame callers get the same search. Front C's in-leaf count stays only inside fused leaves (masks, masked sums) where it shares loads with other work; the worker comparison (image-v4) favours the Intervals call on Zen 5 (OS 12.4 → 6.4 ns warm) and the leaf count on Graviton 4 (Intervals up to 2.9× slower cold), so the point path picks per ISA until the Intervals NEON path is examined. The 32-byte run-start bitmap is optional derivable evidence, bound to the same body edition, with the ends as fallback; on the shared image it is faster for masks and points (run masks 6.7 ns per block at any run count vs 15.5 from ends at 24 runs; points 6–10 vs 13–17 ns, Zen 5). Group run-end buffers were measured and lost 1.2–5× to per-block search, so they are not a candidate. | Leaves (C); Intervals thread for ikea::intervals search | [intervals-search](../intervals-search/README.md), [bodies/README.md](bodies/README.md) |
| Controls | Comparisons use front A's qualified controls: native narrow FOR and predicate(A) → SUM(B) through `frame_cells_controls` (`instruments/cells/cells.h`: `native_sum_once`, `native_pair_once`), actual Polars, DuckDB's C API, TurboPFor and FastLanes at their pinned entry points, CRoaring and vqsort. A thread's own control is labelled as such until replaced. | Instruments (A) | [instruments/README.md](instruments/README.md) |
| Measurement | Zen 5 and Graviton 4 workers; warm and flushed conditions labelled as conditioning, not tiers; concurrent-stream runs state the instance and core count (a small guest is not a socket). Every distinct input and arm is independently checked; timed repeated work consumes and checks exact results after all timers stop; treatment positions and ordered predecessors are counterbalanced; actual per-worker intervals distinguish common-window throughput from completed batches. | Instruments (A) | [MECHANISMS.md](MECHANISMS.md#residence-tiers), [instruments/FINDINGS.md](instruments/FINDINGS.md) |
| Change support | Logical dependencies, the physical read/store envelope and issued-byte effects are three descriptions. Evidence planes are rebuilt or invalidated with the body edition they describe. | Instruments (A) | [PARTITION.md](PARTITION.md#change-support-inside-ikea), [change](change/README.md) |

## Open, with an owner

- **Placement** (B): grouped-by-kind bodies plus a kind index make run-heavy
  scans 2.5× faster, and on the measured columns they cost points and fused
  predicate → SUM nothing (within ±7%, warm and flushed, both hosts). Grouped
  placement is the working default. Relocating rewrites decay it: 10% of
  blocks relocated costs cold scans 1.3–2.2×, points nothing, so compaction
  is a per-group policy triggered by the relocated share
  ([relocation](substrate/README.md#placement-under-relocating-writes)).
- **Exact boundary route** (C with B): front C is adding `with_exact_body()`
  in `bodies/body.h` (halo leaves read a zero-tailed copy; plane and lane
  leaves already read exactly). The substrate calls it only for the last body
  of a slice or next to a protected or mutable neighbour.
- **Per-code weights for coded SUM** (C with B): the dictionary test below
  shows the leaf ABI carries coded COUNT unchanged but has no leaf for
  Σ D[code]; SUM falls back to histograms or unpack-and-gather. Front C has
  taken it: `weighted_sum(body, const u64 *D)` and
  `masked_weighted_sum(body, sel, D)` returning u128 (runs: one gather per
  run; K ≤ 8: histogram dot D; K 9–16: decode to u16 lanes, then gather).
- **Substrate controls** (B): the substrate probes still use their own native
  controls; they move to front A's.
- **A second body family through the same ABI** (B): sorted part
  dictionaries over the unchanged shared image
  ([dict_measure.cpp](substrate/dict_measure.cpp)). COUNT translates the
  threshold to a code per part and calls the ordinary mask leaves, with no ABI
  change, and matches the integer image (URLHash 2.5× faster coded). SUM is
  the gap above: 6–25× slower than the integer image on Zen 5 (OS 50.1 vs
  4.0 ns per block, RegionID 108 vs 5.5). Coded SUM needs a per-code weight
  leaf, or block summaries, before a dictionary front opens.
  ([dictionary results](substrate/README.md#a-second-body-family-sorted-part-dictionaries))
