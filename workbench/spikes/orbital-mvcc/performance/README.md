# Historical versioned-consumer performance

This completed study compared actual Object/codec compositions, LMDB and
rejected metadata alternatives. Its timing executables, candidate matrices and
campaign launchers are retired. The [retirement guide](../../../benchmarks/retired-drivers.md)
records source recovery, preserved current correctness checks and offline reports.

[Iteration feedback](FEEDBACK.md) retains paired read/publication results,
larger-body surveys, interleaving, append/conversion and ready-Message service
observations, including negative results. The earlier
[thin-cursor findings](FINDINGS.md) remain a historical baseline comparison.
Each observation belongs to its own source/binary/host capture. Original
artifact references recover those measured bytes.

## Find evidence by operation

Start with [outcomes](../OUTCOMES.md) for the interpretation and outstanding
composition gaps. The feedback log is chronological; use these direct entries
for its consequential comparisons:

| Question | Evidence, including the limiting comparison |
| --- | --- |
| Repeated selection/interpretation on short reads | [Coherent read fusion](FEEDBACK.md#coherent-read-fusion), [canonical selected reads](FEEDBACK.md#canonical-selected-read-specialization), with mapped LMDB lifecycle controls |
| Publication, old readers and bounded reclamation | [Publication controls](FEEDBACK.md#publication-and-retained-reader-controls), [full versus bounded maintenance](FEEDBACK.md#full-and-bounded-maintenance), [fresh-reader regression](FEEDBACK.md#masked-maintenance-retained-reader-gain-fresh-reader-regression) |
| Wider bodies and longer retained histories | [Broader-body survey](FEEDBACK.md#broader-bodies-and-retained-readers), [wider operating survey](FEEDBACK.md#first-wider-operating-survey) and its [logical exposure](controls/survey-exposure/README.md) |
| Many interleaved requests rather than one dependent chain | [Wider request interleaving](FEEDBACK.md#wider-request-interleaving-and-append-growth): actual request/cohort geometry, Ring underfill losses and synthetic workload limits |
| Append copy grain and stable suffix construction | [Append growth](FEEDBACK.md#wider-request-interleaving-and-append-growth), [shared-prefix alternative](FEEDBACK.md#bounded-shared-prefix-append-alternative) and [private-prefix control](controls/prefix-growth/README.md) |
| Conversion's full physical capacity | [Conversion identity census](controls/conversion-capacity/README.md), including source/target overlap, reusable slack and settlement |
| Service useful completions during a long native read | [Ready-Message comparison](FEEDBACK.md#ready-message-service-during-large-dependent-reads), including throughput cost, empty polls and the finite-arrival limit |
| Smaller state that made the operation worse | [Schema reconstruction](FEEDBACK.md#transient-schema-construction-phase), [diagnostics removal](FEEDBACK.md#removing-diagnostics-rejected-complete-operation-tradeoff) and the retained-reader/fresh-reader maintenance tradeoff above |

Each row refers to its own captured implementation. These do not form a combined
final-build speedup. Current [directory consumers](../../directory/PROGRESS.md#selected-evidence)
measure navigation, packed data and maintenance that the fixed-record study
did not supply. [Execution](../../../notebook/execution-and-codegen.md) and
[memory/lifetimes](../../../notebook/memory-and-lifetimes.md) connect related work
without copying its result tables.

## Offline reports and reuse

`run.py` now provides only `--summarize`, `--abi`, `--counters` and `--report`.
`broader_run.py` and `cooperative_run.py` keep `--report-only INPUT --output OUTPUT`.
Their original report calculations and validation remain available without
rerunning an experiment. Full reports need the recorded input files and binaries.

Current Object/codec/Loom consequence tests use their own private fixture,
independent of this old LMDB adapter and rejected implementation. Future
comparisons can link [actual LMDB](../../../peers/README.md) directly; scenario
setup, encoding, snapshots and timing remain the owning caller's responsibility.
