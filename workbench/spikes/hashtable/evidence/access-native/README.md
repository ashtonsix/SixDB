# Native owner-wide read permission comparison

This cohort holds physical recipes and populations fixed and changes the bytes
that a reader is allowed to load from an initialized, exclusively owned bucket.
The four policies are exact fields, wider fingerprint windows, wider suffix
loads, and both. Each has composed and independent raw bucket-field code; the
last two arms are ordinary and exact-key Abseil. No policy is selected here.

All three native runs passed the full-owner and guarded independent byte oracles,
then completed 45 cases × 10 arms × 9 phases × 9 repetitions. The source manifest
is `0e9190e5694d3781524512389cdfe47fa11becbf9883b7a1669fe7a19283913b`;
[source-receipt.json](source-receipt.json) distinguishes it from the archive SHA.
Each platform context records its job, checks, termination and recovery:
[Zen5 AVX2](../access-zen5/context.json),
[Granite AVX-512](../access-granite/context.json),
[V2 NEON](../access-neon/context.json).

The raw `formats.csv` retains all repetitions; `comparison.csv` has 450 case-arm
rows with phase medians/ranges, ratios and staged inventories. `ranges.csv`
compares each fixed policy across the complete set of cases. Ordinary/exact-key
Abseil are both retained, and no table-size label establishes cache residency.
Read controls do not change requested bytes, metadata or build populations.
Full source, compile commands, executable and assembly are recoverable through
each `artifact.json`; selected compile commands remain beside the context.

The [audit](codegen-audit.py) follows complete timed hit/miss/serial/erase CFGs,
then the actual whole-read-loop helpers. It found no per-key lookup or per-value
callback boundary in warmed candidate reads, and no calls in any baseline read
interval. Cold format/exception branches remain. `codegen.csv` retains all 1,800
intervals per host with helper addresses and normalized hashes.
`identical-code-controls.csv` records comparisons whose complete helper bodies
match after strict address normalization. Their spread is evidence of remaining
process/placement variation, not an instruction-level mechanism.

The [geometry model](geometry.py) reconstructs the frozen byte recipes and
[admitted reads](admitted-reads.csv). Capacities are checked against native
measurements, but this model is descriptive, not the independent correctness
oracle. Wider fingerprints change loaded bytes in 40/45 cases, suffixes in 27/45;
several labels therefore describe identical operations. All widened fingerprint
loads remain in the first cache line. Four modeled tier/case combinations add a
suffix load crossing: K48 set secondary A6/A10, K56 set primary A6, and K64 set
primary A10. This is geometry, not observed cache traffic or spatial prefetch.

[report.py](report.py) is the frozen native summary program.
[compare.py](compare.py) regenerates the compact comparisons, equality checks,
call-boundary checks and identical-code controls from raw measurements and audit
JSON. Exact commands and limitations are in each context. Timings use
`std::chrono::steady_clock`, complete read warmup, fresh processes per case/arm
and fresh Pools per repetition; Abseil seeds and process placement remain
uncontrolled. No hardware profiling or causal cycle attribution is claimed.
