# Comparative benchmarking

Make useful comparisons easy to run, inspect and repeat. Share input preparation,
execution and recovery; keep the actual request, peer implementation and timing
boundary visible. A receipt proves particular bytes were used. An answer oracle
proves particular outcomes. Neither establishes that the competitor was used
competently or charged equivalent work.

## Failures worth learning from

The October 20, 2026 review checked Calico history and SixDB sources and asked
Engine, Orbital, Loom, Ikea and Ikea HELPER about concrete failures and friction.
The recurring errors were in the measured work, not the number of declarations
surrounding it.

| Case | What exposed or would expose the mistake |
| --- | --- |
| Calico's ClickBench peer added a global `row_number() OVER (ORDER BY ...)` lineage operation, peer-only tie work and string decode expressions that obstructed normal scan pushdown. Exact answers still passed. | Inspect the generated SQL and plan against ordinary peer usage. `ff171267` removes the lineage sort and restores string reading at the Parquet boundary. |
| A later DuckDB adapter widened every unsigned source integer to `HUGEINT`. Correcting widths changed q29 from 3.427 s to 16.4 ms; the complete prepared 3M walk changed from 16.216 s to 7.434 s. | Check loaded physical types as well as logical values; inspect a surprising query against a normal native invocation. Calico correction `d2cecb4b` records both the invalid and replacement cells. The query improvement is not the whole-suite improvement. |
| Calico's old YCSB F was all RMW, databases carried state between workload cells, and peers did not replay one common event stream. | Check trace composition and starting state independently. A final-state oracle for each different trace cannot prove common work. The historical audit is `1825cfdc:workbench/benchmarks/COMPARAND_TRUST.md`. |
| SixDB's breadth run passed 304 oracle trials, but its intended fusion patch had silently failed to land. | The actual captured source and emitted scalar symbol corrected the treatment label. See [the retained account](../spikes/orbital-mvcc/performance/FEEDBACK.md#broader-bodies-and-retained-readers). |
| Hashtable benchmark consolidation introduced an outlined payload mixer inside timed RMW loops without changing the table. | Unexpected results prompted complete-caller disassembly; logical checks and the result-grid auditor did not detect the added calls. See [the correction](hashtable/ACCUMULATION.md#harness-correction-and-validation-scope). |
| A later directory report could accept equal checksums despite contradictory backing, cohort, pruning or shared-prefix controls, or inconsistent packet/command timing denominators. | The study's [offline reporter](../notebook/retired-spikes.md#index-directory-spikes--october-2026) now checks emitted effective controls against its existing executable plan and reconciles packet and command totals, allowing for printed rounding. These checks run during ordinary reporting. |
| A generic L1D-miss event opened successfully but returned zero across 360 real-memory control intervals. | An independent sensitivity experiment exposed the unusable measurement. See [the memory controls](orbital/evidence/prefetch-control-20261007/README.md). |

Calico also compared asymmetric warmups, storage lifecycles, memory limits and
setup-inclusive versus query-only memory peaks. Its [replacement baseline](../../../calico/workbench/benchmarks/clickbench/baseline/README.md)
records the repairs and its differences from upstream ClickBench. Commit
`5e59be7a` removed 4,369 lines while adding 647 to simplify the workflow, including
removal of comparison-contract and run-declaration machinery. The later width
error above still occurred. More repetitions, hashes, policy files or publication
stamps cannot repair a systematically distorted adapter. Keep targeted tests
which exercise the actual distinction: operation intent, loaded types, returned
bytes, starting state, source postimage or observed call path.

Use `git -C ../calico show COMMIT` from the SixDB root to inspect those historical
changes. Their numbers belong to those cuts, not the current products.

## Choose the comparison being made

A product comparison runs the same logical requests through each product's
ordinary competent interface, consuming all required output. Engines can use
different layouts, algorithms and automatic optimizations. Describe meaningful
differences in transaction, durability, indexing, preparation and concurrency
policy. Equal index counts or identical adapter code do not establish fairness.

A controlled mechanism comparison can instead hold encoded bytes, codec body,
prefetch coverage or publication grain fixed. Name that narrower question. For
example, mapped LMDB decoding the same legal Tuple bytes isolates different work
from LMDB navigating ordinary row keys. Keep both where useful rather than
presenting one as the other. Inspect the actual peer algorithm too: the maintained
hashtable growth control measures Abseil copy/reserve, not every available Abseil
growth strategy.

For an unfamiliar competitor, start with its maintained example or native
benchmark and compare one small case through the adapter. Check a surprising
result against that direct path before expanding the matrix. Plans, loaded types,
linked disassembly or operation counters answer specific doubts; enable the ones
that could reveal the suspected mistake. A clean wrapper or an independent answer
oracle cannot inspect the work it does not observe.

## Reuse machinery and keep the caller explicit

| Shared capability | Existing entry point | What stays with the comparison |
| --- | --- | --- |
| Stable source, incremental builds and worker execution | [Capture and experiment helpers](../tools/README.md#captured-experiment-runs), [workers](../tools/workers.md) | Actual measured backend, build/runtime configuration and useful variants |
| Pinned source data, parameterized transforms and shared checked caches | [Datasets](../datasets/README.md), `datasets.get/cached`, `Run.input` | Query generation, sampling, weighting, access order and interpretation |
| Archived binary replay and selected summaries | [Replay and summarize](README.md#replay-and-summarize) | Semantic equivalence, result comparison, cost boundaries and meaningful units |
| Reproduction and compact evidence | [Retention](../tools/artifacts.md), [results explorer](../results/README.md) | A useful result entry linking the table to its source, configuration and recovery |

The [pinned Abseil provider](hashtable/abseil.cmake) is already reusable. Keep
dependency acquisition/versioning separate from workload adapters. A reusable
peer adapter should expose the real product's setup, request submission, complete
result consumption and teardown; transaction lifetime and physical design remain
explicit choices. Share that code when a second caller needs it. Engine owns its
real caller and logical contract; Workbench should not impose a universal Store
interface, fixed record schema or either old driver's invented database layout.

All five interviews identified duplicate build work or recovering the meaning of
old results as friction. Reuse stable compatible incremental workspaces; share
only matching compiler, ISA, optimization and instrumentation configurations.
Coordinate heavy builds across owners and honor the selected job budget. An
observer timeout is not evidence that a compiler or remote job stopped: inspect
the actual process/job status. Analysis-only or documentation changes need their
own relevant checks, not a rebuilt timing campaign.

Captures now display actual source additions, modifications and removals. A
no-op replacement is visible as zero changes. Existing `capture.py create
--expect PATH=SHA256` can enforce a reviewed postimage before capture creation.
Check the compiled caller/backend as well: source presence does not prove a
function was selected or inlined.

## Inputs and measured work

Preserve original bytes, types, ordering, duplicates and source lineage when
preparing real data. Record transforms and sample selection so another peer can
consume the same logical input. Keep preparation outside service timing unless
preparation is the operation being compared; still report its cost when relevant.
An identity hash proves which sample was used, not that it represents a population.

The existing [keyset inputs](../datasets/keyset-windows.md) illustrate important
boundaries: related projections share lineage; minimum-cardinality filtering
changes coverage; missing empty windows need a defined universe. The MS MARCO
keyset sample is Boolean membership, not a full text-search corpus with term
frequencies and document lengths. Keep sampling/access policy with the workload
until there is an actual second user for that policy.

Time the useful consumer, including required lookup, decoding, output and
maintenance work. Distinguish setup, request service and settlement rather than
hiding expensive work in a differently named helper. Check complete outcomes and
retained state independently outside the interval; checksum consumption alone
only prevents dead-code elimination. Operation-intent and spatial-coverage tests
catch different bugs from final-state checks, as the [older LMDB/fragmentation
corrections](orbital/measurement.md#bounded-staged-reads) show.

Where arm options change the measured work, keep invocation labels and expected
controls with the compact plan that runs them. Let the report check the program's
emitted effective values against that plan. Derive timing units from their actual
denominators: `ns/packet × packets` should agree with `ns/command × commands`
over the same interval. Keep these checks in
ordinary execution/reporting, using the values the program already emits. This
removes a manual consistency check while leaving semantic fairness to the caller.

Run measured arms exclusively and alternate or counterbalance their order across
independent trials. Preserve declared per-case setup and conditioning. Retain raw
repetitions, sample counts, all selected cells and failures; add repetitions when
variation obscures the decision rather than choosing a universal minimum. A large
methodology-sensitive change calls for explaining the path before another long run.

Keep dependent latency, reciprocal throughput, whole-batch makespan and individual
request latency distinct in summaries and plots. Offered concurrency, actual
operands visited, helper CPU, copy work, retained capacity and RSS answer different
questions. Use counters only after a sensitivity control demonstrates that the
event measures something on that host. Mark unavailable values as unavailable.

## External benchmark submissions

For an external result, implement the benchmark project's protocol directly.
Keep research subsets and adapted workloads named separately. No current SixDB
driver establishes submission readiness or acceptance.

The [ClickBench rules](https://github.com/ClickHouse/ClickBench#rules-and-contribution),
checked October 20, 2026, call for the published 99,997,497-row input, 43 query
slots and three consecutive runs per query. Unsupported or failed timings use
`null`. True cold runs restart the database and clear page cache before each
query's first execution; page-cache-only submissions use the `no-cold` tag.
Disable result caching and include result delivery in client-to-client elapsed
time. Report load time and stored bytes, including applicable indexes/logs.
Keep vanilla and tuned configurations separate and follow the stated index and
preaggregation rules. Supply reproducible fresh-VM scripts and dated result JSON.
The supplementary concurrent test should be skipped for engines that start a
fresh full-machine process per request.

Reuse upstream's adapter structure and compare a normal peer invocation on the
same setup. The old complete-walk, reduced-row or codec-only results are useful
research comparisons, not replacements for this protocol. Recheck upstream at
submission time; passing our own runner cannot promise leaderboard acceptance.
