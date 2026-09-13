# Focused module validation

The retained AVX2 worker result exercises the **ordinary scalar fallback**:
68,104 independent wire/replacement cases, 2,048 ordinary pair cases, examples
and five independently compiled supported headers. It does not establish a
native AVX2 Bec256 profile. The captured pair check used an unconditional final
message mentioning native/CPS checks; those sections are compiled out in this
profile. The current check prints that distinction explicitly. The raw log is
retained unchanged.

On AArch64, the native release and ASan/UBSan builds also passed the 2,048 pair
checks, 16,448 casing checks and ordinary/native/integration examples. The renewed
metadata study adds 174,960 range, prefilter, logical-tail, metadata and execution
comparisons, including the fused continuation stage. These also passed ASan/UBSan;
its hardware artifact references retain release check logs.

The AVX2 focused target build took 12.32 seconds wall time with one build job and
393 MiB peak compiler RSS on the selected Zen worker. This is the library plus
focused checks/examples in a newly configured profile, with toolchain and
workspace dependencies already present; it is not a clean SixDB build comparison.

The [prediction fallback repeat](prediction-avx2.txt), recoverable through its
[artifact reference](prediction-avx2-artifact.json), also passes 8,192 statistical
predictor cases, 57,344 threshold cases and seven supported header checks on
AVX2-only x86. Its threshold log uses a generic “checked/native” label, but the
native portions are compiled out in this profile; the current log is neutral.
The same cases pass on AVX-512 and NEON, including their native block/pair paths,
and under ASan/UBSan on AArch64. They cover exact input page bounds, threshold
equality, full existing journals, invalid population, admission failures and
unchanged output/effects on heuristic decline. Prediction conformance is distinct
from statistical accuracy, whose evidence remains in the predictor study.
