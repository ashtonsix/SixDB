# Cached formats and byte-coordinate reads

This native screen combines a cached StreamPack link-format pointer with simpler
byte-coordinate bucket access, using the local-result caller throughout. It is
not an isolated comparison of either change. All three hosts passed complete
owner checks, guarded recipe checks, and checked/admitted link reservation tests,
then completed 45 cases × 10 arms × 9 phases × 9 repetitions. All 109,350 raw rows,
including losses and unchanged write/lifecycle controls, are retained. All three
instances terminated.

The source manifest is
`2013b46d6f86a483ebf11d70ab78e6dffcd5a5b2c702eb57ec3eba8c6756238e`;
[source-receipt.json](source-receipt.json) records the archive and selected source
hashes. Full source, compile commands, binary and assembly remain recoverable
through each platform's artifact receipt. Contexts and exact regeneration
commands: [Zen5 AVX2](../prepared-zen5/context.json),
[Granite AVX-512](../prepared-granite/context.json),
[V2 NEON](../prepared-neon/context.json).

The fixed arms are exact, wider fingerprint reads, wider suffix reads, and both;
each has composed and Direct bucket-field operations, followed by ordinary and
exact-key Abseil. All candidate arms have equal logical storage, geometry,
populations and operation counts in every repetition. Relative to the previous
paired-reduction capture's comparable arms, requested storage and populations
match, while metadata rises by 8 B per set/map owner and 16 B per multimap owner.
The captures differ in source and job/process placement, so they are not a
matched causal decomposition. No format or policy is selected here.

`comparison.csv` retains all 450 case/arm rows with phase medians and full ranges,
ratios and staged inventories; `ranges.csv` compares fixed arms, without taking
per-case minima. [report.py](report.py) is the frozen report;
[compare.py](compare.py) independently verifies full panel coverage, all candidate
memory/population controls and the precise randomized 450-process order.
`formats.csv` retains every repetition. Each process uses fresh-Pool repetitions
and complete matching read warmup; process placement and Abseil seeds remain.
The phase-summed lifecycle excludes reads, checks, inventories and Pool setup/drain.

The [shared source/DWARF audit](../reduction-native/audit-dwarf.py) resolves each
actual timing interval by its frozen-source callsite, including outlined timers,
then follows the complete timed CFG and direct whole-loop helper bodies.
[codegen-summary.py](../reduction-native/codegen-summary.py) checks all 1,800
hit/miss/serial/erase regions per host. The native binaries contain 166/147/166
outlined timing wrappers on Zen5/Granite/V2, making a fixed-clock-ordinal audit
inappropriate. No candidate or baseline read has per-key lookup/per-value calls.
The prepared candidate read loops also contain no static-format guard calls.
On x86, most loops inline into the timer; on V2, more whole-loop helpers remain.
This is call-boundary evidence, not a cycle explanation.

Only V2 has strictly identical complete read-helper controls in this capture:
60 comparisons, with timing ratios 0.968–1.062. Most x86 loops are timer-inlined,
so this helper-only test does not qualify their placement variation. The large
Zen5 K40/A10 anomalies are stable within repetitions and have no calls in their
actual timed regions. [Codegen notes](codegen-notes.md) preserve the loops and a
specific partial-register dependency hypothesis, with exact profiling addresses.
It is unconfirmed; no PMU samples or modified-code measurements belong to this
cohort.

This remains a single-writer finite screen at explicit table sizes, not proof of
cache residence, DRAM throughput, Loom prefetch benefit, ISA-only advantage,
concurrency, or production readiness. Initial logical memory wins ordinary
Abseil in 10/15 set cases and 0/15 map cases on every host. All map/multimap serial
and all update cells still lose to ordinary Abseil under every fixed candidate
policy. The broader throughput, latency and memory target remains unmet.
