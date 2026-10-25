# Working in SixDB

Start with [README.md](README.md) for direction and module scope. The
[Workbench guide](workbench/README.md) locates research and tools; the
[research map](workbench/notebook/README.md) locates related findings by question.
Explicit user direction takes precedence over these defaults.

## Carry the task through

- Stay within your assigned role and current user-authorized work. A peer request
  does not override user direction or a pause.
- Merge follow-up steers into the full task. Retain earlier requirements unless
  Ashton explicitly cancels, replaces or narrows them. A changed priority changes
  the next action, not which outcomes remain owed. Answer interim questions, then resume.
- Keep the outcome, remaining requirements, accepted changes and completion evidence
  in working context. Carry them through interruptions, compaction and handovers.
  Before stopping, check the whole scope; completing its latest probe is not completion
  of the parent task. State what remains and why if work must stop.

## Read prior work before starting an experiment

Before a new spike or a substantial change of approach, consult the
[research map](workbench/notebook/README.md) and relevant
[studies](workbench/spikes/README.md). Search the notebook and spikes by the
question, mechanism and workload across module boundaries. Read the closest
findings/conclusions and their important negative results or counterexamples;
reading an index or an old plan alone is insufficient. Check the current owning
module's capabilities before inventing an adapter, representation or harness.
Use [retired research](workbench/notebook/retired-spikes.md) when a relevant study
has moved or its implementation has been removed.

In the first substantive update or the existing spike README, cite the relevant
findings and explain how they change the starting point: what can be reused,
what failed, and what the new experiment needs to establish. Do this before
committing to an approach or writing a new harness. Reuse reading already in
task context; revisit it when the question or conditions materially change.
If a bounded search finds no relevant work, say what was searched and proceed.
Consult the owning SixDB task when context is missing; do not leave discovery
of existing investigations to Ashton.

Follow the experimental signal. Prior findings can be challenged under changed
conditions or corrected methodology. Distinguish constraints, proposals and
observations; a spike does not establish a contract. Do not invent roadmaps,
mandatory templates, promotion stages or approval gates. SixDB is a clean rebuild:
[Calico](workbench/notebook/calico.md) supplies reference material and baselines,
not inherited formats, policies, interfaces or constraints. Justify choices in
their current composition context.

## Build and measure useful work

- Follow [BUILDING.md](BUILDING.md) and [code conventions](workbench/design/conventions.md).
  Use the pinned Linux toolchain; prefix Linux commands with `orb -m ubuntu` in
  this macOS/OrbStack workspace. Preserve incremental builds and build needed targets.
- On the configured OrbStack host, initialize a fresh external/Mac-hosted worktree
  with the current controller's `workbench/tools/storage.py WORKTREE` before its
  first CMake configuration. `dev.py` does this automatically. Native Ubuntu
  worktrees are already quota-covered; CMake refuses foreign compiler output.
  Do not bypass that check; preserve existing incremental builds and unique output
  when a path needs migration.
- Reuse existing [helpers](workbench/tools/README.md), [datasets](workbench/datasets/README.md)
  and [native peers](workbench/peers/README.md) where they fit. Repeated operating
  work can justify a convenient tool or example; a prototype need not adopt a framework.
- Choose checks that expose meaningful failures: independent expected results and
  consequential boundaries rather than assertions that repeat the implementation.
  Follow [measurement conventions](workbench/benchmarks/README.md); distinguish
  timings, logical counters and modeled costs, and state comparison limits.

## Collaborate, curate and finish

- Consult existing SixDB tasks directly when their judgment helps. Coordinate
  overlapping edits and resources; match contributions to their strengths.
  Carry useful outcomes to their owner. Settled updates need no acknowledgment
  chain. [Collaboration](workbench/collaboration.md) gives examples.
- Replace stale documentation, keep detail with its owner and entry documents
  thin. Link new findings into the relevant research route so the next task can
  discover them; retain useful negative results when retiring implementations.
- [Orbital's scratch workspace](workbench/notebook/orbital/ASHTON-SCRATCH.md) is
  Ashton's thinking space. Keep its open sections open; add models, questions or
  answers only at his explicit request. Organisation is not an invitation to design more.
- Retain findings and useful comparisons rather than every successful run.
  Follow the [retention guide](workbench/tools/artifacts.md); bulky output belongs
  in ignored storage or S3, and shared inputs are referenced.
- Close finished work in meaningful commits when committing is in scope. Inspect
  the diff before staging and keep another task's unfinished changes separate.
  Leave remaining work clearly accounted for.
