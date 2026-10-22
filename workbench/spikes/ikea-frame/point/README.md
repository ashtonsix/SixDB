# Points into actual Frame bodies

Frame's point experiment consumes the shared private
[encoding material](../encoding/README.md), rather than replacing it with a new
codec or materialising all 256 rows per point. Ikea LEAD owns this consumer.
Ashton has separated the Boundlist/Intervals investigation and assigned it to
Ikea HELPER; its geometry algorithms and checkpoint-spacing work are independent.

The first reader reconstructs constants, packed FOR, runs, predicted deltas and
patched FOR. Runs use either the compact mutable Boundlist control or the
immutable u8 end-minus-one projection. Predicted points use the nearest stored
checkpoint and decode only the local residual prefix. Native every-16, packed
every-16, every-64 and absent checkpoints are read from each image's own options.
Retained images with different choices coexist. Packed checkpoint binding and
its resident storage are explicit preparation, not timed point work.

The starting evidence is the real Jena result in the encoding inventory: fewer
checkpoints shrink each body and change which blocks choose prediction. Weighted
SUM needs none of those checkpoint reads. Therefore compare point costs both on
fixed body choices and on complete images chosen by construction, with bytes,
analysis and reader preparation accounted separately. The [first measurements](FINDINGS.md)
now expose substantial point cost and avoidable prepared-state overhead.
Engine chooses among curated material options; no default or
public Frame API follows from this reader.

`frame_checkpoint_point_check` compares every point directly with original input
values, independently of `decode_block`, across all four checkpoint modes and
both run geometries. Its authored image exercises all five body kinds, signed
steps and high unsigned anchors. Coarse masks select original blocks without
renumbering rows. A protected descriptor proves that a none mask touches no
column material; deliberate selection in a child process faults as expected.
This proves architectural accesses, not speculative cache traffic.

The initial native check passed 70,057,984 point comparisons: the authored image,
all 42 Jena default-window columns, the 14-column sentinel block, and the original
ClickBench OS/UserID exports. All/none/alternating/clustered masks also agree.
Inputs and working-domain transformations retain the encoding inventory's
provenance; this is an exact u64 working-domain check, not a new signed/float
arithmetic claim. The shared material cut is `6cbd12d9`; the findings retain the
first performance/prepared-state comparisons without choosing a representation policy.

Build using the pinned Linux toolchain and spike selection, then run:

```sh
cmake --build build/clang/frame-checkpoints --target frame_checkpoint_point_check --parallel 1
build/clang/frame-checkpoints/workbench/spikes/ikea-frame/point/frame_checkpoint_point_check \
  build/spikes/ikea-frame/jena-0/p__mbar.u64
```

Omit input arguments for the authored/protected-memory checks. Files must contain
nonempty complete blocks of little-endian u64 working-domain values; partial
source rows are not silently discarded. For ongoing native work use the owning
build directory, never another peer's active Ninja workspace.
