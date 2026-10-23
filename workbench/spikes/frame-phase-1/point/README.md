# Points into actual Frame bodies

Frame's point experiment consumes the shared private
[encoding material](../encoding/README.md), rather than replacing it with a new
codec or materialising all 256 rows per point. Ikea LEAD owns this consumer.
Ashton has separated the Boundlist/Intervals investigation and assigned it to
Ikea HELPER; its geometry algorithms and checkpoint-spacing work are independent.

The reader reconstructs constants, packed FOR, runs, predicted deltas and
patched FOR, plus opt-in predicted deltas with exceptions. Runs use the immutable
u8 end-minus-one projection through maintained [Intervals](../../../../ikea/docs/intervals/usage.md).
The obsolete compact-run control is retired; its earlier measurements remain
source-qualified in the retained findings. Predicted points use the nearest stored
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

The [Intervals integration cohort](INTEGRATION.md) preserves the native-prefix
gain, qualifies dictionary composition and retains the complete native-array
gaps. Its samples are separate from the earlier preparation/prefix cohorts.

Ikea HELPER's subsequent actual-getter audit found useful native-byte run search
on unpredictable point traces with at least sixteen runs on Zen5 AVX2/AVX-512
and Neoverse V2. `unpredictable_point_reader` and
`unpredictable_compact_reader` select that maintained `run_end_source` in the
same reader templates; the ordinary aliases retain the raw comparison projection.
There is no runtime profile branch or extra prepared field. Tiny lists retain
Intervals' existing projection fallback. This is a curated private experiment,
not a new Frame default or public policy API. Predictable traces and non-RLE
code placement remain controls; no winner follows from the type names.
The [real-column integration](CURATED-RUNS.md) measures both programs on the same
bytes, preserving the native-prefix gains and the remaining native-array gap.
The newer opt-in arrival-code kind is not covered by these readers or this
frozen cohort; adopting the owner's borrowed operations is separate work.

Prediction with exceptions skips exception rows in the prediction chain. A point
at an exception reads its explicit-width exception plane; an ordinary point uses
the number of preceding ordinary steps, not its physical row number. Checkpoints
still sum stored residuals at physical rows. Shared point preparation uses that
explicit width and needs no sparse exception decoder/index table. This remains
a point-consumer experiment, not a replacement for the shared column's other
bindings or a selected checkpoint/codec policy.

`frame_checkpoint_point_check` compares every point directly with original input
values, independently of `decode_block`, across all four checkpoint modes.
Its authored image exercises all five default body kinds, signed
steps and high unsigned anchors. Coarse masks select original blocks without
renumbering rows. A protected descriptor proves that a none mask touches no
column material; deliberate selection in a child process faults as expected.
This proves architectural accesses, not speculative cache traffic.

`frame_delta_patched_check` adds 303,104 exact original-value comparisons covering
every residual width1..64, all four checkpoint choices, leading/trailing/scattered
and 255 exceptions, wrapped checkpoint sums, independent closed-form values,
coarse original coordinates and owner exact SUM/COUNT.

Dictionary access composes `dictionary_points` around an existing stored-point
reader, using each block's coded bit and its own part's dictionary edition.
Mixed raw/coded blocks stay in their original coordinates. The ordinary getter
gains no per-point dictionary branch; its constructor requires explicit
stored-code admission for dictionary images. Compact preparation also releases
redundant dictionary bindings. These are private consumer choices, not a Frame API.

On the isolated `f760ca39` material epoch with maintained Intervals, all59 retained
original columns pass 142,828,096 point comparisons for the default encoder,
142,846,528 with `--delta-exceptions`, and 142,864,960 each with that flag plus
`--dictionary=sorted` or `--dictionary=frequency`. All four checkpoint choices,
three readers, mixed part editions, coarse masks, all-width prefixes and
protected-none/fault checks remain. The count drops from the earlier double-run-
geometry check because the obsolete compact control is no longer constructed.
These are correctness checks, not timings of the later shared-catalogue migration.

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
build/clang/frame-checkpoints/workbench/spikes/frame-phase-1/point/frame_checkpoint_point_check \
  build/spikes/ikea-frame/jena-0/p__mbar.u64
```

Omit input arguments for the authored/protected-memory checks. Files must contain
nonempty complete blocks of little-endian u64 working-domain values; partial
source rows are not silently discarded. For ongoing native work use the owning
build directory, never another peer's active Ninja workspace.
