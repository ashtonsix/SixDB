# Orbital formal models

These TLA+ models check how Orbital's contracts compose under concurrency,
failure and resource pressure. For the system itself, start with the
[architecture walkthrough](../ARCHITECTURE.md). Model conveniences and candidate
mechanisms do not amend [BRIEF](../BRIEF.md) or [PHYSICAL](../PHYSICAL.md) implicitly.

- [Results](RESULTS.md): current coverage, limitations and unfinished work.
- [Verification obligations](PLAN.md): guarantees, assumptions and required compositions.

Each selection linked from Results identifies exact inputs, outcomes and recovery
of its full evidence. Earlier selections describe their own snapshots.

For a particular mechanism, use the family reports:
[journal authority](JOURNAL.md), [material and recovery](MATERIAL.md),
[execution and restoration](EXECUTION.md), [delivery](DELIVERY.md), and
[physical views and capacity](RUNTIME.md). These are views of a composed system,
not independent subsystem specifications or C++ module definitions. They distinguish
unrestricted finite families from joined cases with authored service schedules.

## Find and run a check

From the repository root on Linux; prefix commands with `orb -m ubuntu` on the Mac:

```sh
python3 orbital/spec/suite.py --list
python3 orbital/spec/suite.py --case Transaction-small --output build/orbital-spec/transaction
```

Repeat `--case` to select several. `--manifest orbital/spec/tx-cases.json` selects
one family; the default [catalog](cases.json) includes those family catalogs
without copying their cases. `--tier` selects the catalog's workload grouping.
A case can set `workers` and `heap` (defaults: one worker and `512m`); `--list`
shows them, and receipts record the actual resources. Heap is a run requirement,
not part of the model semantics: a completed check at another heap remains useful.
Runs freeze model/configuration and catalog inputs. A timeout is incomplete;
`--timeout` sets the per-case budget. Use a fresh output directory.

To move selected cases to a worker using the same runner and captured sources:

```sh
python3 workbench/tools/worker.py run orbital/spec/worker.sh \
  --machine zen5 --instance-type c8a.2xlarge --setup minimal \
  --capacity spot --sync-seconds 30 --deadline 1200 -- \
  --case Transaction-small --timeout 900
```

The adapter installs or reuses Java and writes to the worker's results directory.
Put `check` first after `--` to pass single-case options to `check.py` instead.
Live output sync preserves uploaded completed-case evidence; it does not recover
an interrupted case's TLC state. For long individual cases, use the separate
[checkpoint adapter](../../workbench/tools/tlc/README.md).

`recover.py` continues a verified rescue from this checker, preserving its actual
seed, source closure and JVM settings. The original receipt stays unchanged;
each continuation produces `resume-result.json` with separate attempt timing.
For an existing rescue reference captured in the worker source:

```sh
python3 workbench/tools/worker.py run orbital/spec/worker.sh \
  --machine zen5 --instance-type c8a.2xlarge --setup minimal --capacity spot \
  --checkpoint-script workbench/tools/tlc/checkpoint.sh --checkpoint-seconds 300 \
  --deadline 4200 -- recover --reference path/to/rescue-reference.json --timeout 3600
```

Later `worker.py resume JOB` uses the committed checkpoint, including its original
lineage, without fetching the initial rescue again. Local runs may pass `--archive`
for an already downloaded archive; its reference hash and members are still checked.
The shared checkpoint adapter's limits apply: a request must reach a TLC checkpoint
opportunity, and an interrupted final liveness search may need to repeat that phase.

## Collect evidence for the current inputs

```sh
python3 orbital/spec/evidence.py --output build/orbital-evidence/current
```

The collector searches the usual Orbital run roots under `build`, plus retained
bundles in `build/orbital-evidence`, printing the searched paths. Only cases selected by the
catalog are eligible; `--search PATH` overrides the roots and can be repeated.
`--manifest` narrows the selection; a collected worker's `results` directory can
be a search root. An archive reference alone supplies no receipts: use
[artifact recovery](../../workbench/tools/artifacts.md) to fetch absent evidence
before selecting it.
It matches actual parsed local dependencies, configuration, runner,
tool pin and expected property/witness, verifies the captured bytes, and rechecks
the raw outcome. An unrelated model edit does not invalidate a receipt. Legacy
receipts did not hash logs at production; the collector cross-checks parsing and
semantic module lists and hashes the selected logs, but cannot authenticate their
earlier edit history.

`selection.json` records the selected receipts and rejected/stale input reasons;
`SUMMARY.md` presents their dispositions. `checks.csv` and `inputs.json` provide
a compact outcome index and exact input hashes for retention in Git. Reconciled
diagnostic failures remain linked to the later complete receipt; a tool failure
alone never supplies completed evidence. `missing.json` is a runnable catalog
of cases lacking usable current receipts in the searched paths. Check retained
archives and active suites before dispatching it.
Completed checks, detected deliberate defects, witnessed paths and incomplete
runs have different meanings; their counts do not establish architectural coverage.

When retaining a selection, `--bundle` copies its source dependencies, raw logs/counterexamples,
progress and receipts, plus the explanatory reports. It omits solver scratch and
unparsed sibling sources. Keep the bundle in ignored storage and use the existing
[artifact commands](../../workbench/tools/artifacts.md) to retain useful findings
and a verified S3 recovery reference. Full logs can be large; ordinary collection
references existing runs without copying them. Collection itself uploads and deletes nothing.

The earlier [formal spike](../../workbench/spikes/orbital-formal-first-pass/README.md)
retains its own narrower questions and historical evidence.
