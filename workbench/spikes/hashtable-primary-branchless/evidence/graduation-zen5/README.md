# Maintained lookup promotion

[Decision and limits](../../GRADUATION.md), [full scorecard](tables.md),
[audit](audit.json). Native Zen 5 AVX-512, whole-table independent reads with
actual Boost 1.92 and pinned Abseil. Lower ns/lookup is better. The original
Ikea callback loop and vendor loops are controls; default predictable,
static unpredictable and automatic use the corresponding complete consumer.
Input order, full useful answers and Ikea-owned bytes agree across arms.
The original fixture geometry, reserve arguments and vendor hashers are fixed;
this is not a new load/geometry search or a serial-latency/MVCC write comparison.

Full cohort: 1,440 fresh processes / 6,264 complete PMU intervals, three seeds,
two fresh trials/arm/case, three repetitions/process. Only calling-thread
userspace loop work is counted; conditioning, setup and query creation are
outside. Coverage 1, unscaled, no multiplexing. Cold population 14,260,633,
conditioning 256 MiB. PF32 remains continuous across selection boundaries.
Boost exposes its native candidate prefetch but no public home hook.

The separate persistent-stream seam check has six fresh processes / 5,040
intervals. Trials/repetitions inside a process are not additional independent
processes. Input batches are immutable and preallocated, all useful results
match, and actual call/controller overhead is timed. It has no PMU.

Qualification retains all 29 maintained native tests, three sanitizer checks,
462 sanitizer tail processes, expanded live-primary zero/max-quotient and
zero-valued-hit checks before overflow, plus a retained native capture through
rebuild/erase. Local portable checks cover correct fallbacks; no new native
AVX2/NEON performance claim is made. The one excluded source-capture failure
occurred before compilation or timing; its retry uses the passing full source
snapshot plus owned changes, independently recorded in stream-capture-basis.json.

The [recovery bundle](artifact.json) keeps raw CSV/PMU, all 38 audited native
ELFs, complete linked read loops, compiler commands, host/source manifests,
qualification and frozen report code. The two worker references recover exact
full source archives, omitted diagnostics and original emitted assembly.
Thirteen locally omitted assembly files were disassembled from their audited
ELFs with pinned LLVM 21; that record is in the bundle. New codegen names parse
prefetch/full/method template arguments exactly, avoiding historical ambiguous
export names; old evidence exports are unchanged.

Promoted runtime headers match the passing native source byte-for-byte.
Reports recover from the bundle and regenerate without new timing:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-primary-branchless/evidence/graduation-zen5 \
  build/reports/hashtable-primary-graduation
```
