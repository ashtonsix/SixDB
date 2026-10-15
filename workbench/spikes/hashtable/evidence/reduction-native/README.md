# Paired native caller-reduction comparison

The local-result caller removes borrowed checksum writes from candidate read
loops, but it does not recover broad table competitiveness. Candidate aggregate
read timing is nearly flat on Zen5 and Granite; V2 independent hits/misses fall
about 3–4%, while value-dependent serial timing is nearly unchanged. This is a
comparison of complete caller codegen, including baseline codegen changes.

Both executables come from one frozen source and build per host. The source
manifest is `bf37c64adce223c504554b094d60d56cd714fe7dffc9e26be854dc8b39fd9d0e`;
[source-receipt.json](source-receipt.json) records the original capture and the
runner/report-only additions. All three hosts passed both owner checks and the
independent guarded recipe checks, then completed 45 cases × 4 arms × 2 caller
forms × 9 phases × 9 repetitions: 29,160 rows per host. All workers terminated.

The four fixed arms are exact composed, exact Direct, ordinary Abseil, and
exact-key Abseil. Case/arm order is shuffled; borrowed/local processes are
adjacent with randomized order (86 borrowed-first, 94 local-first pairs).
Every process has nine fresh-Pool repetitions and full matching read warmup.
The [Zen5](../reduction-zen5/context.json),
[Granite](../reduction-granite/context.json), and
[V2](../reduction-neon/context.json) contexts retain the limitations and commands.
All repetitions, staged inventories, build populations and unchanged lifecycle
phases remain in `reduction.csv`. Requested bytes plus metadata, geometry,
populations and operation counts match per repetition between caller forms.
Baseline backing is unknown, not zero. No residence or DRAM claim follows from
these table sizes.

`comparison.csv` contains all 360 case/form/arm rows with phase medians, ranges
and ratios; `ranges.csv` summarizes fixed arms without selecting per-case minima.
[report.py](report.py) validates full coverage and per-repetition controls;
[compare.py](compare.py) regenerates the compact comparisons and checks exact
randomized process order. The frozen source, compile commands, executables and
full assembly remain recoverable through each `artifact.json`.

The [codegen audit](audit-dwarf.py) maps actual clock and outlined-wrapper
callsites through DWARF inline source locations to the named timing assignments
in the frozen `formats.cpp`. It follows their actual timed CFG and whole-loop
helpers, without inferring phases from clock ordinals or lambda numbers.
[codegen-summary.py](codegen-summary.py) verifies the call-boundary result.
Each binary has 720 hit/miss/serial/erase regions; all six audits pass. Local
x86 binaries contain 14 outlined timing wrappers each, so that phase-resolution
step matters. Candidate lookup and full value consumption are fused; baseline
lookup and consumption are also fused. Cold format/exception paths remain.
Per-host `borrowed-codegen.csv` and `local-codegen.csv` preserve source frames,
addresses, normalized helper hashes and summaries. They establish boundaries,
not runtime frequency or cycle causality.

[Codegen notes](codegen-notes.md) record concrete native checksum and loop-shape
observations. No PMU profiling was performed. Abseil process seeds, placement
and temporal effects remain despite pairing; repetitions within one process
are not independent machine trials. Earlier access and later prepared captures
also change other source bytes, so they are not matched caller-reduction controls.
