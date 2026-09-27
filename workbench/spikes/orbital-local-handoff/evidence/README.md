# Retained local handoff evidence

Keep both cohorts and all repeated cases, including refusals, poor policies and
reduced-timestamp controls. The summaries are exact per-run nearest-rank
statistics of completed samples; they are not pooled or fleet tail estimates.
[Findings](../FINDINGS.md) explain the comparisons; [method](../README.md) defines
the fixed schedule, admission policy and accounting.

| Cohort | Per-trial summary | Exact case/affinity order | Source/run receipt | Immutable worker archive |
| --- | --- | --- | --- | --- |
| Main: 47 cases × 3, 200 ms windows | [141 rows](20260927-main/summary.csv) | [Executions](20260927-main/executions.json) | [Provenance](20260927-main/provenance.json) | [Recovery reference](20260927-main/artifact.json) |
| Confirmation: 18 cases × 3, 100 ms windows | [54 rows](20260927-confirmation/summary.csv) | [Executions](20260927-confirmation/executions.json) | [Provenance](20260927-confirmation/provenance.json) | [Recovery reference](20260927-confirmation/artifact.json) |

Main worker/job: `20260927T004823Z-2768706b`, instance
`i-0aa1f86d94e2f3806`. Confirmation: `20260927T005646Z-82377596`, instance
`i-042ef04d2081d644c`. Both ran in `us-east-1b`, independently, and both are
[terminated](resources.json). This is **not** an inter-host or placement-group
comparison. Hardware and selected worker environment are retained beside each CSV.

Both native probe source hashes are:
`80b67e75908c386b023b9a159cef9ff64cd39ed91a0b78d464dacab408c5add7`.
Each provenance receipt records the full source digest, source archive hash,
compiler/flags, command arguments and selected output hashes. The original root
commit was `bd9c4b5a9a78c5bf7e9f8111caaa30b88d34d679` plus captured uncommitted
sources; that commit alone does not reproduce the experiment. No simulator code
was edited by this measurement task.

The original local collections are under:

```
build/workers/20260927T004823Z-2768706b/results/study/
build/workers/20260927T005646Z-82377596/results/study/
```

For each `name` in executions.json, the archive contains `name.csv.gz` with every
scheduled offer and its outcome/timestamps, plus `name.json` with native CPU,
queue, payload/order checks and accounting. `run.json` contains all source-file
hashes and output hashes; `source.tar.gz` holds the captured source tree. The
binary, compiler information and CMake flags are also archived. Main correctness
output is `correctness.stdout`; confirmation renamed it to `correctness.txt` for
Git retention. Both contain the same six native checks and twelve negative controls.

The 8M/15.625M controls are named `cliff-i125-*` / `cliff-i64-*` in confirmation.
Matched healthy/paused queue cases are `queue-{spin,wait}-q{16,256}-pause*`.
`large-burst-*` confirms the 16 KiB batching reversal. Reduced rows intentionally
contain no message latency quantiles. `steady_*` counts share the quantiles'
scheduled warmup cutoff; whole-run counts and CPU remain distinct.

Recover and recompute from Linux (prefix `orb -m ubuntu` in the macOS workspace):

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/orbital-local-handoff/evidence/20260927-main \
  build/recovered/local-handoff-main
python3 workbench/spikes/orbital-local-handoff/analyze.py \
  build/recovered/local-handoff-main
```

Use `20260927-confirmation` and a fresh destination for the second cohort. These
commands recover archived data and launch no workers. Worker collection and
retention verified the archived hashes; full raw reanalysis runs in each captured
worker study. Summary rows preserve all repeat ranges; no extra rerun or statistical
confidence claim is implied by successful archive verification.
