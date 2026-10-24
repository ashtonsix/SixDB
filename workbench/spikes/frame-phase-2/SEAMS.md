# Cross-thread seams

The contracts the phase 2 threads share, 2026-10-24. Each row names who owns
it and where its evidence lives. A thread that needs a change here proposes it
to the owner rather than working around it. Anything not listed is still
thread-local.

| Seam | Contract | Owner | Evidence |
| --- | --- | --- | --- |
| Image | Descriptor columns in original block order: kind, width, base, body offset, run count. Bodies are front C's byte laws. Body placement (address order or grouped by kind) is a separate substrate choice per image. | Substrate (B) | [image.h](substrate/image.h), [shared image](substrate/README.md#the-shared-image-substrate-over-front-cs-leaves) |
| Leaf ABI | Header leaves in `bodies/` (`packed<layout, K>`, `runs<T>`, `lanes<T>`, `patched<K>`): `sum`, `count_ge`, `count_eq`, `mask_ge`, `masked_sum`, `point`, `unpack`, `max`, `min`, `first_ge`; run leaves take `n` after the body. Results are in the residual domain. `catalogue.h` and `sweeps.h` are measurement loops, not entry points. | Leaves (C) | [bodies/README.md](bodies/README.md) |
| Base terms | The substrate adds `base × count`, with the count taken from the final aggregate domain (after fine selection and validity), never 256, the predicate count or a run count. K ≤ 32 full-block sums fit u64; 64-bit run values return the wide type. | Substrate (B) | [LEAF-CONTRACTS.md](instruments/LEAF-CONTRACTS.md#partial-results-and-coordinate-laws) |
| Read envelope | Fast leaves read past their body by the halo front C tabulates per leaf and ISA (at most 63 bytes, or 15 lanes for run values). An image admits that once at binding as an initialised, immutable, read-isolated whole-image window: one suffix of the maximum halo, plus each next body readable to the same depth. Allocation padding alone does not grant it. Slices, protected neighbours or mutable bodies need an exact boundary route instead. Prefetch follows the logical body extent, not the halo. | C states halos; B admits them; A reviews | [bodies/README.md](bodies/README.md#read-and-write-envelopes), [LEAF-CONTRACTS.md](instruments/LEAF-CONTRACTS.md#keep-the-fast-leaf-make-its-physical-authority-explicit) |
| Selections | Coarse: one bit per block, tested before any descriptor or body address is formed; an empty block reaches no leaf. Fine: four u64 words for original rows 0–255 of the block. Fine input selection, A validity and B validity stay separate inputs; a run-ordinal mask is a different type. | Substrate (B) | [LEAF-CONTRACTS.md](instruments/LEAF-CONTRACTS.md#partial-results-and-coordinate-laws) |
| Run lookup | Default: the leaves' SIMD count of ends below the row (no search, no evidence). The 32-byte run-start bitmap is optional derivable evidence, bound to the same body edition, with the ends as fallback. Group run-end buffers were measured and lost 1.2–5× to per-block search, so they are not a candidate. | Leaves (C); Intervals thread for ikea::intervals search | [intervals-search](../intervals-search/README.md), [bodies/README.md](bodies/README.md) |
| Controls | Comparisons use front A's qualified controls: native narrow FOR (phase 1 bridge, corrected to vectorise), actual Polars, DuckDB's C API, TurboPFor and FastLanes at their pinned entry points, CRoaring and vqsort. A thread's own control is labelled as such until replaced. | Instruments (A) | [instruments/README.md](instruments/README.md) |
| Measurement | Zen 5 and Graviton 4 workers; warm and flushed conditions labelled as conditioning, not tiers; concurrent-stream runs state the instance and core count (a small guest is not a socket); every call checks an exact oracle; arms rotate. | Instruments (A) | [MECHANISMS.md](MECHANISMS.md#residence-tiers), [instruments/FINDINGS.md](instruments/FINDINGS.md) |
| Change support | Logical dependencies, the physical read/store envelope and issued-byte effects are three descriptions. Evidence planes are rebuilt or invalidated with the body edition they describe. | Instruments (A) | [PARTITION.md](PARTITION.md#change-support-inside-ikea), [change](change/README.md) |

## Open, with an owner

- **Placement** (B): grouped-by-kind bodies plus a kind index make run-heavy
  scans 2.5× faster, and on the measured columns they cost points and fused
  predicate → SUM nothing (within ±7%, warm and flushed, both hosts). Grouped
  placement is the working default; writes and appends, which move bodies,
  have not been priced against it.
- **Exact boundary route** (C with B): every leaf needs a variant that reads
  only its body, for sliced or protected images.
- **Substrate controls** (B): the substrate probes still use their own native
  controls; they move to front A's.
- **A second body family through the same ABI** (B with a future front):
  dictionary codes are the first test that the leaf ABI and descriptor columns
  generalise beyond integer bodies, before more fronts open.
