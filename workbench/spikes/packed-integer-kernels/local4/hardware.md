# Local4 projected transpose on Zen 5 and Granite Rapids

The projected32 candidate improves all twelve carrier/count cases on each
machine in the shared-buffer AVX2-only experiment. Each worker passed the
19,971 independent checks and 51 benchmark fixtures before the timed run.
[All 102 case records](hardware.csv) retain the 20 balanced repetitions and
buffer/function offsets. [Capture receipts](hardware.json) identify exact
sources, binaries and recoverable worker artifacts.

At 8,192 values, projected32/current32 ratios for u8/u16/u32/u64 are
0.890/0.873/0.917/0.948 on Zen and 0.877/0.895/0.925/0.935 on GNR.
Across all three extents, the improvement ranges from 4.9% to 12.7% on Zen
and 6.5% to 15.2% on GNR. This supports testing the projected writer in the
actual public operation. It does not yet select a production patch.

The separate 64-value grain control is informative but not part of that patch.
For example, Zen u8 at 8,192 values is 0.969× for current64 and 0.845× for
projected64, relative to current32. Its direct predecessor is 0.836×. On GNR,
projected32 and the direct predecessor are close: 0.877× and 0.858×. A residual
whole-operation gap cannot be inferred from these raw-array ratios. Some
larger GNR carrier cases also benefit from a different grain; this is not
evidence for one universal unroll choice.

The paired public experiment keeps the 32-value grain, relinks the same
coherent benchmark objects, and replaces only the native object compiled with
the projected-writer header overlay. It includes Local K4/H0, K12/H8 and
K20/H16, unchanged Local3/5 controls, and all corresponding decode controls
at 256/8,192/65,536 values in both process orders. Existing independent public
operations and exact range guards passed before timing on both machines.

## Public result and selection

The public result supports selecting the projected32 writer for integrated
validation. [All 480 process/case records](public-evidence/cases.csv) retain five
repetitions each; [capture receipts](public-evidence/capture.json) record the
preserved coherent libraries, new native objects, public checks and exact
source/binary recovery. No input/header changes were applied to production
during this comparison.

At 8,192 values, Local4/u8 encode improves 12.7% on Zen and 12.2% on GNR;
Local4/u64 improves 8.5% and 6.5%. Every affected measured encode median
improves at all three extents, including headed K12/H8 and K20/H16. Some
headed u64 gains are only 1–4%, consistent with their larger unchanged work;
the raw-kernel savings should not be claimed for the whole operation.

GNR's unchanged controls at 8,192/65,536 values move less than 0.5%. Zen has
larger control shifts, approximately −4% to +4.5% at 8,192, and therefore does
not support attributing every small headed change to the projection. The
consistent primary Local4 improvement on both hosts, the shared-buffer result,
the simpler discarded-plane computation and the independent guards justify
the narrow selection. The separate 64-value grain control is not selected.
