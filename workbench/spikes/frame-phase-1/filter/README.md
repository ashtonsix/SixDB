# Separated heads in Frame predicates

Ikea LEAD owns this consumer experiment. Ashton requires Frame to carry the
SeriesPack option for separated leading one/two-byte planes, and asks where in
the transform chain those planes remain both informative and independently
interpretable. This is part of Frame research, not a maintained Frame API.

The [prior guarded-filter investigation](../../packed-filters/EARLY-GRADUATION.md)
provides the starting point: existing head8 plus an expensive suffix can avoid
substantial work; mixed windows and narrow suffixes can lose throughput to
unconditional SIMD. Physical splitting and the stopping branch must be tested
separately. Therefore this consumer compares ordinary and guarded operations on
**identical split bytes**, with headless direct filtering as another control.
It reuses maintained SeriesPack packing, comparison and composition throughout.
The [findings](FINDINGS.md) retain complete-consumer measurements, actual Polars
controls and the remaining losses; separated heads are not a universal default.

## Transform cuts and predicate meaning

The first image experiment supports packed FOR and run-value bodies. After FOR,
the head covers the high bits of the **actual residual width K**, not the high
bits of a zero-extended u64. A logical threshold translates using the descriptor's
base alone: `x >= q` becomes `residual >= q-base`, with explicit whole-block and
out-of-domain handling. The suffix is not needed to interpret the head. Run
predicates retain original run extents and count selected lengths rather than
expanding a row mask.

The contrasting early cut separates heads from 64-bit working values before
FOR. It has an independent logical interpretation but can waste most of its
head entropy on the shared prefix. This is a deliberately named u64 working
domain control, not a claim that every source schema declares 64 useful bits.

Prediction residual heads do not mean reconstructed-value heads. A late split
after prediction needs a prefix/checkpoint dependency, a separate logical-value
summary, or a different ordering of the transforms. Dictionary codes likewise
require the dictionary predicate law; arbitrary codes are not ordered values.
Those alternatives remain open. Other kinds reuse the owner's COUNT, keeping
the FOR law's applicability explicit. Dictionary images use the complete
dictionary-aware owner program; residual heads do not inherit logical ordering
from arbitrary codes.

SeriesPack already represents H=8 and H=16. Its maintained opt-in guard is H=8;
the private H=16 experiment composes that guard twice, using the second head as
the inner eight-bit head. Either can resolve the predicate before the remaining
payload. K=8/16 all-head cases have no nonexistent suffix to read. TuplePack has
independent field placement and projection/filter composition; it has no matching
dedicated leading-head format option. Its physical grouping remains a separate
cross-column experiment rather than another head flag here.

The next private arms keep those encoded bodies unchanged and add logical-value
evidence: part-wide H8/H16 heads after a part FOR, and conservative q8 min/max
bounds for each sixteen-row window. The heads' predicate meaning depends only
on a part base/width; the bounds have independent logical block minima/ranges.
Only unresolved rows need exact refinement. The current simple fallback decodes
an unresolved block once, then checks its unresolved rows. That dependency cost
is part of the experiment rather than a claim of selective residual decoding.
Exact logical block min/max is a smaller control: it can already skip many
prediction blocks, so finer evidence must beat it as well as ordinary decoding.
Retained-byte accounting includes C++ object/vector capacities, addressing and
padding; allocator bookkeeping and shared executable code remain separate.

This reuses the [head-cut model](../encoding/head_cuts.py), including its RegionID
counterexample to part-wide precision, and Calico's
[fine summary](../../../../calico/frame/include/frame/fine_summary.h) distinction
between valid evidence and a required exact fallback. The earlier
[row-signature findings](../../row-filter-signatures/FINDINGS.md) show that cheap
resident native predicates can beat progressive evidence even with excellent
rejection. Therefore complete same-body consumers and strong native controls
remain mandatory comparisons for this question. All sidecar bytes, including
padding/addressing metadata and duplicated information, are charged. No Calico
format, fixed summary-storage gate or new Frame default is inherited.

## Checks and limits

`frame_filter_check` compares masks with original integers at every width 1–64,
H=0/8/16 where legal, all five predicates, ordinary/guarded execution, out-of-domain
thresholds and aligned/unaligned/tail windows. Whole-image COUNT comparisons cover
all six body kinds, immutable run geometry, dictionary editions, the two transform cuts and
all/none/alternating/clustered coarse block selections. Protected planes check
that either head can reject without reading the suffix; deliberately unresolved
predicates must fault. These are architectural-access checks, not proof about
speculative cache traffic.

The complete count body dispatches once per block. Packed bodies popcount masks;
run bodies use native length-weighted masks for complete sixteen-value windows
and exact bounded tails. Generic five-predicate window masks remain separate.

The migration instrument borrows the original image for fallback kinds and the
decode control, retaining converted split planes separately. Report those extra
bytes and conversion work; it does **not** establish final resident memory or
an in-place migration format. There is no default head policy or performance
claim from these semantic checks. Warm/cold complete consumers and native controls
must establish whether useful filtering pays for placement, guards and retained
metadata. Predicate entropy and unresolved windows matter separately from final
result selectivity.

```sh
cmake --build build/clang/frame-checkpoints --target frame_filter_check --parallel 1
build/clang/frame-checkpoints/workbench/spikes/frame-phase-1/filter/frame_filter_check \
  build/spikes/ikea-frame/cols/OS.u64 \
  build/spikes/ikea-frame/cols/ResolutionWidth.u64
```

Prefix Linux commands with `orb -m ubuntu` from macOS. Width-specialised kernels
compile once in eight-width slices per head layout to bound compile memory.
