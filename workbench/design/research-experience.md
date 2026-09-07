# Notes from the first research loop

The aggregate-maintenance spike is [closed](../spikes/aggregate-maintenance/CONCLUSIONS.md).
These observations carry forward to the next question; they do not prescribe
the size or shape of every investigation.

2026-09-07, while exercising the [aggregate-delta question](../spikes/aggregate-maintenance/README.md).
Ashton emphasised that the experience of researching matters as much as this
particular answer. These are observations and working affordances, not an
adopted document template or promotion process.

The useful path now exists: an open question links to a small executable study;
one command configures, checks, measures, and collects evidence; tables can be
regenerated; [findings](../spikes/aggregate-maintenance/FINDINGS.md) distinguish
observations, interpretation, and unanswered questions. The code and timings
remain local to the study instead of establishing Engine interfaces.

## What helped

- **One entry point with a small default.** The study runner defaults to smoke
  and can select individual named cases for a longer follow-up. Full profiles
  remain available. The final 14-case screen took about 18 seconds, including
  a 2.8-second incremental build; the selected longer confirmation took about
  31.5 seconds with a no-op build. These are this machine's recorded runs.
- **Shared compiled implementation.** Changing either driver does not recompile
  the mechanisms. Changing the shared header correctly recompiles its users.
  Both correctness and timing exercise the same implementation, with accounting
  compiled out of the timed path.
- **A durable explanation of a local run.** Source snapshots include uncommitted
  files. Commands, compiler flags, affinity, raw repetitions, and hashes travel
  with results. Source changes during execution prevent a successful receipt.
  Generated tables remain separate from the human interpretation.
- **Failure is reviewable.** A deliberately nonexistent case request produced
  a failed receipt, preserved earlier logs, and reported available case names.
  No benchmark output is treated as a success merely because the process exits
  zero. Selected cases and repetition counts are checked before summarising.

## Friction that changed the scaffold

The editor could not initially find Google Benchmark's headers: clangd was
reading the empty default dev configuration while the prototype was configured
elsewhere. A temporary per-prototype personal override fixed that one file,
but would have made each new study a configuration chore.

The repository now routes clangd to a stable development configuration.
[dev.py](../tools/dev.py) adds/removes explicitly active spikes, preserving
other selections. The experiment runner refreshes it before configuring its
independent benchmark build. Target include paths and compiler definitions
come from CMake. Inactive or broken studies stay excluded. The language server
only reads the database; it never configures, builds, or fetches dependencies.
The personal prototype override was removed, and VS Code reported no problems
after restarting clangd on the benchmark file.

A disposable [integration check](../tools/check_dev.py) covers activation,
removal, dependency paths, definitions, C++23/tuning flags, exclusion of a broken
inactive study, and clearing a stale database when no source targets remain.

Two other corrections came from using the machinery: SixDB's retained release
assertions initially made Google Benchmark report a debug library, so the
vendor target now selects its own optimized runtime. Mutation and query RNGs
were separated so changing query load would not silently change the writes.
Mutation digests enforce that property across the relevant comparisons.

The first counters also combined leaf and interior summary adjustments.
Separating them made the recorded result answer the actual higher-stratum
question. The CPU numbers need similarly precise names: this cycle includes
queries and maintenance, but excludes primary-row updates and beforeimages.

## What should stay open

The current runner is a small local mechanism, not a fleet framework. Large
datasets should remain referenced in the existing S3 bucket; source snapshots
and artifact collection will need to scale beyond this small repository.
Remote execution, cancellation/deadlines, and distributed resource accounting
are still missing. Measurements used no cloud instances. At closeout, selected
run and validation bundles were retained under SixDB's prefix in the existing
S3 bucket; see the storage changes below.

Cases are currently defined in C++, which keeps one definition shared between
the oracle and benchmark but requires recompilation to change a scenario.
We should see whether that becomes friction before adding a configuration
language. Phase attribution, cache-residence control, and per-method memory
measurements need more than the current cycle timer and logical counters.
VM variability also prevents interpreting small timing differences confidently.

Most importantly, the first run changed the next question: a plausible reduction
in ancestor work exposed a substantial construction CPU cost. The workbench
should make following that evidence easy. The amount of narrative, retained
evidence, naming, and review process should remain steerable by Ashton.

## Keeping a question together and thoughts available

Ashton subsequently chose one stable home per investigation. Aggregate
maintenance now keeps its question, design, literature, code, runners, and
evidence under [spikes/aggregate-maintenance](../spikes/aggregate-maintenance/README.md).
The former prototypes category is replaced by spikes. Reading stays with the
question it informs; the former prior-art category is removed. Cross-cutting
design and shared tools, workloads, and datasets keep their own homes.

The [notebook](../notebook/ideas.md) gives incomplete thoughts somewhere to
live without requiring an experiment. The habit is to capture an intuition,
develop an investigation when useful, and reconnect earlier thoughts with
new evidence. The first aggregate result now links back to the broader
construction-cost question. No statuses or promotion machinery are needed.

Calico's [bytepack](../../../calico/workbench/prototypes/bytepack/README.md)
and [three-array](../../../calico/workbench/prototypes/three-array/README.md)
investigations already keep hypotheses, implementations, measurements, and
interpretation together. That is useful inspiration. SixDB's spike can still
begin as a single README; those existing collections are not a template.

Several first-person research accounts reinforce parts of this choice:

- [Oudeyer on Flowers lab notebooks](https://pyoudeyer.com/openLabNotebooks22.pdf):
  tentative ideas, failures, and discussion belong in the notebook, and a
  notebook need not correspond to a project or function as proof of work.
- [Hamming, *You and Your Research*](https://www.cs.jhu.edu/~kevinduh/projects/researchtips/hamming.pdf):
  keep unresolved problems in mind and revisit broader direction, so a new
  idea can suggest an approach to an older question.
- [Boettiger's lab notebook account](https://www.carlboettiger.info/2012/09/28/Welcome-to-my-lab-notebook.html):
  loose notes and coherent project code/analysis complement each other;
  chronology alone makes an investigation's overall shape hard to recover.
- [Matuschak on incomplete notes](https://notes.andymatuschak.org/A_writing_inbox_for_transient_and_incomplete_notes):
  easy capture needs revisiting to remain useful. We borrow that principle
  without adopting his whole note system or requiring every note to mature.

These are examples of research practice, not evidence that a filing scheme
causes success. They suggest keeping capture easy, browsing relevant thoughts
when choosing or changing direction, and linking surprises to earlier ideas.

## The next candidate: cheap dirty marking

The [dirty-buffer probe](../spikes/aggregate-maintenance/dirty-buffer/README.md)
reused source capture, receipts, editor configuration, and Google Benchmark
while keeping its mechanisms, scenarios, and analysis in the same spike.
The first screen exposed a hot-location win; separating reset from replay
explained where a smaller filter could help. Adding a no-filter control then
changed the question again: which clean reads actually repay the metadata?

Two pieces of friction were concrete. A selection regex exposed the analyzer's
omission of Google Benchmark's name suffix; the failed receipt remained visible
and the affected run was repeated after fixing it. Inspecting object layout
also caught the append counter sharing a line with benchmark-control metadata.
Retained measurements use isolated allocation metadata.

The broad 85-comparison confirmation spent about 140 seconds benchmarking,
partly because untimed initialization repeatedly cleared a large allocation
even for hot-location cases. The 30-comparison follow-up took about 30 seconds.
The runner now defaults to a small four-writer selection; broad one-writer
sweeps are explicit. Keeping work outside the timer does not make it free to
the researcher, and future probes should budget their setup loop too.

## Keeping evidence without accumulating full runs in Git

The first closeout staged about 42,000 added lines, including roughly 31,000
lines of Google Benchmark JSON and 3,000 lines of build caches/databases.
That was review noise and unnecessary repetition for a recurring workflow.
Source archives also recaptured older retained evidence and source archives.

The revised [retention command](../tools/artifacts.md) keeps every individual
repetition in compact CSV, accounting, readable tables, and provenance in Git.
Full bundles go to content-addressed S3 keys. Conditional upload and a verified
download precede writing the Git reference; fetch verifies and restores the
bundle. Failed uploads preserve local results and can be retried independently
of the experiment. Local runs print the command to retain them.

Both analyzers regenerate the same numerical tables from the compact evidence.
The four measured runs and two validation bundles were uploaded, downloaded,
hash-checked, and restored before removing full bundles from the Git selection.
Original source archives remain byte-for-byte intact in S3. Future snapshots
exclude all spike evidence and build output, even if tracked. This is a
convenience for chosen results, not an obligation to retain every run.
Both local experiment runners subsequently passed smoke runs with the revised
source capture and analyzers. Offline artifact checks exercise upload failure,
retry, corrupt compact inputs/downloads, unsafe archives, and source exclusions.
