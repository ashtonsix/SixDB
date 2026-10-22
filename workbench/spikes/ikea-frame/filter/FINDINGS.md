# Filter evidence must earn its bytes and refinement work

The [timing-order audit](../ORDER-AUDIT.md) supersedes close rankings from this
historical cyclic-order cohort. Its calibrated rerun preserves the large gains
and the important losing cases; use that paired cohort for current timing claims.

Logical H16 planes make the two examined Jena filters about 3.4–4.8× faster
than the encoded owner operation. They reach the native FOR array's scan cost,
but duplicate two bytes per original row. Exact block bounds are much smaller
and about 2.3× faster than that owner operation. Neither result selects a
universal Frame layout: RegionID and ResolutionWidth are strong counterexamples
to always adding logical heads, and the current quantised window bounds lose.

These are complete consumed COUNT queries over unchanged real encoded bodies,
with original-value oracles. The implementation supports separated SeriesPack
H8/H16 planes with independent logical interpretation; it does not make a
prediction residual's leading bits mean a logical predicate.

## Complete median-query result

Local Apple/OrbStack AArch64 VM, Clang 21.1.8, O3/native ISA/generic tuning,
automatic vectorisation enabled, vCPU7. Two processes reverse input and arm
order; 32 rotated trials per process give 64 samples per C++ cell. Each complete
answer is consumed and checked. All blocks selected, `COUNT(value >= population
median)`, median ns per original value:

| Column | Encoded owner | Direct FOR H0 | FOR H16 guarded | Logical H16 | Block bounds | Window q8 | Native FOR | Polars FOR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OS | .152 | .147 | .147¹ | .069 | .151 | 2.257 | .167 | .062 |
| ResolutionWidth | .183 | .189 | .178¹ | .301 | .180 | 2.593 | .175 | .101 |
| EventTime | .249 | .205 | .233 | .208 | .230 | .924 | .163 | .135 |
| UserID | .136 | .019 | .014 | .062 | .005 | .017 | .245 | .265 |
| URLHash | .330 | .473 | .445 | .274 | .334 | 1.305 | .258 | .238 |
| ConnectTiming | .043 | .026 | .018 | .0024 | .0026 | .0061 | .0001² | .0005² |
| RegionID | .235 | .238 | .254 | 1.935 | .246 | 2.987 | .200 | .129 |
| WatchID | .446 | .566 | .295 | .265 | .440 | 1.127 | .247 | .253 |
| Jena pressure | .855 | .848 | .859 | .179 | .368 | .430 | .175 | .221 |
| Jena temperature | .586 | .581 | .596 | .171 | .256 | .347 | .170 | .137 |

¹ No H16-eligible FOR/run bodies for this query: headless fallback, not a
two-byte-head gain. ² Median zero is all-hit; global FOR bounds answer from
metadata. These timer-dominated O(1) cases are not scan rates. The tiny 256-row
fault case is retained in the samples, but is unsuitable for scan rankings.

Actual Polars 1.42.1 runs single-threaded on the same CPU and original values,
with 14 alternating samples per query. Native FOR controls subtract the global
minimum, use the smallest native unsigned lane and retain the actual maximum
for the same out-of-domain shortcuts. Preparation is outside all query timers.
Polars covers all-mask queries; C++ also covers alternating original blocks.
No cost-free gather is assumed for a Polars coarse-selection comparison.

The [complete samples](../evidence/interval-consumers-local/filter/summary.json)
also retain minimum, quartile and U64MAX queries, H8, ordinary/guarded heads,
early working-u64 cuts, native u64 and both coarse masks. Close VM rankings are
not fleet policy. The earlier [head cohort](../evidence/heads-points-local/artifact.json)
is historical; its timings are not pooled with the changed owner fallback,
geometry and logical-evidence consumers here.

## What to use, and what to reject

- **Exact logical block bounds are a useful small baseline.** They settle
  clustered UserID almost entirely and substantially reduce predicted Jena
  work. UserID is *locally clustered*, not globally sorted: there are 28 backward
  adjacent transitions among 2,999,808 rows. An earlier version of this document
  incorrectly called it sorted. No ordered-array shortcut applies to these
  measured inputs. The implementation has a separately qualified ordered
  control, enabled only after proving global order.
- **Logical H16 earns further Jena query speed, at a substantial storage cost.**
  OS's whole logical H8 plane also costs only .069 ns/value here; H16 adds no
  useful gain. H8 is too coarse for several other columns: pressure .527,
  temperature .419, ResolutionWidth 2.541 and RegionID 3.302 ns/value. The
  unresolved equality bin matters as much as the final hit rate.
- **Do not adopt the current q8 window-bounds candidate.** It loses broadly,
  even where exact block bounds help. It branches across 16 windows and an
  unresolved window still requires decoding its block once. Finer nominal
  pruning has not paid for this consumer's traversal and reconstruction. This
  rejects this implementation/program, not every possible smaller-region bound.
- **A native/plain choice remains necessary.** Logical H16 on RegionID is over
  8× slower than headless, and the native-array controls still beat encoded
  consumers on several columns. EventTime's default bodies are largely packed;
  evidence that helps predicted Jena does not automatically help EventTime.

The regular residual-head image now reuses the owner's complete block COUNT for
fallback kinds, instead of decoding all values merely to count them. This
removes the earlier ConnectTiming seam. Complete packed/run consumers still
pay descriptor lookup, width dispatch and run weighting; logical heads pay
their own loads and any exact reconstruction. Linked assembly retains these
costs and guarded suffix decisions. No branch or cache counter was inferred
from timing alone.

## Memory and preparation

These are deliberately extra evidence over the original image, not a compact
replacement representation. Every unresolved logical-head block decodes once;
only tied rows contribute through the exact fallback. Exact block bounds can
reuse the owner's code-aware block operation. Dictionary editions remain the
body's own immutable interpretation dependency.

Actual retained capacities, including vector/object storage, bytes:

| Column | Original with bindings | Extra logical H16 | Extra block bounds | Native FOR |
| --- | ---: | ---: | ---: | ---: |
| OS | 4,849,936 | 6,001,232 | 187,520 | 2,999,928 |
| ResolutionWidth | 5,546,238 | 6,001,232 | 187,520 | 5,999,736 |
| RegionID | 6,115,492 | 6,001,232 | 187,520 | 11,999,352 |
| UserID | 10,792,684 | 6,001,232 | 187,520 | 23,998,584 |
| Jena pressure | 184,978 | 131,176 | 4,128 | 131,192 |
| Jena temperature | 151,968 | 131,176 | 4,128 | 131,192 |

Logical H8 adds 3,001,424 bytes to each large column, 65,640 to each Jena image.
Window q8 costs 56 bytes/block plus its object: 656,240/14,368 bytes for those
populations. Block bounds cost 16 bytes/block plus the object. All retained
capacity and two construction observations are in
[setup and queries](../evidence/interval-consumers-local/filter/setup-and-queries.json).
Construction observations are not a paired encoding benchmark. The experiment
retains original decoder bindings; later no-binding/borrowed-body work is not
measured here. Neither a memory advantage over native FOR nor maintenance under
writes follows from the query gains.

## Placement, access obligations and correctness

After FOR, descriptor base/width translate comparisons before the suffix is
read. Early u64 heads can be dominated by common leading bits. The earlier
EventTime trace found all working-u64 windows tied, versus only 5,177/124,283
late H8 windows and 43/124,129 late H16 windows. Those are descriptive logical
traces, not measured traffic. Logical part heads instead carry their own
base/width and can filter independently of prediction, exceptions or dictionaries;
their wider part domain can lose useful entropy.

753,760 exact checks cover all 64 widths, five predicates, H0/H8/H16,
ordinary/guarded operations, weighted runs, all six examined kinds,
prediction exceptions, sorted/frequency dictionaries, multiple/partial parts,
original coarse coordinates and real-column counterexamples. Protected tests
exercise rejected suffixes, the exact seventeen-byte run-end tail, none masks
with inaccessible metadata, resolved evidence with an inaccessible encoded
column, and deliberately unresolved queries that fault. The owner fixture
separately protects actual payload pages and matching dictionary editions;
all ten injected illegal reads fault. These prove architectural access
obligations, not speculative cache traffic or cold-throughput gains.

The accepted source capture is
`ed97e4bd447bc82e4f4df380002ef2ce85b6d9385cc233f57a3b6a8b641d2df0`,
with encoding core f760ca39 and maintained Intervals. Exact commands,
input/source/ELF hashes, raw results, source, binaries and linked assembly are
recoverable from the [verified bundle](../evidence/interval-consumers-local/artifact.json)
and [reproduction notes](../evidence/interval-consumers-local/REPRODUCE.md).
Later borrowed-block and monotone-delta COUNT changes are a distinct source
epoch. Native fleet/cold/Loom measurements, physical maintenance of evidence
and complete lifecycle costs remain open before selecting a policy.
