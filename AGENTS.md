# Working in SixDB

Start with [README.md](README.md) for direction and module scope, and the
[Workbench guide](workbench/README.md) for research. Explicit user direction
takes precedence over these defaults.

- Stay within your assigned role and the current user-authorized work. Consult
  existing SixDB tasks directly when their judgment helps; match contributions
  to their strengths and coordinate overlapping edits/resources. A peer request
  does not override user direction or a pause. Carry useful outcomes into their
  owning work; settled updates need no acknowledgment chain.
  [Collaboration](workbench/collaboration.md) gives examples.
- Follow the question and experimental signal. Do not invent roadmaps,
  mandatory templates, promotion stages, or approval gates. Distinguish
  constraints, proposals and observations; a spike does not establish a contract.
- Browse related [spikes](workbench/spikes/README.md) and
  [notebook ideas](workbench/notebook/ideas.md) when choosing direction.
  SixDB is a clean rebuild. [Calico](workbench/notebook/calico.md) is reference
  material, including performance baselines. Never assume that its formats,
  policies, interfaces or constraints carry forward; justify SixDB choices
  independently in their current composition context.
- Curate documentation instead of accumulating advice. Replace stale explanations,
  keep detail with its owner, and leave entry documents thin and steerable.
  [Retired research](workbench/notebook/retired-spikes.md) shows how useful findings
  can survive without maintaining obsolete implementations or competing specs.
- Follow [BUILDING.md](BUILDING.md) and [code conventions](workbench/design/conventions.md).
  Use the pinned Linux toolchain; prefix Linux commands with `orb -m ubuntu`
  in this macOS/OrbStack workspace. Preserve incremental builds and build the
  targets needed for the task.
- Look for existing [helpers](workbench/tools/README.md) and
  [datasets](workbench/datasets/README.md) before duplicating machinery or inputs.
  Repeated operating work is an opportunity for a convenient tool or example;
  a one-off prototype need not adopt a framework.
- Choose checks that could expose a meaningful failure of the changed behavior.
  Prefer independent expected results and consequential boundary cases over
  assertions that merely repeat the implementation. Follow the
  [measurement conventions](workbench/benchmarks/README.md); distinguish timings,
  logical counters and modeled costs, and state the limits of the comparison.
- Retain findings and useful comparisons rather than every successful run.
  The [retention guide](workbench/tools/artifacts.md) covers selection and recovery;
  bulky outputs belong in ignored storage or S3, and shared inputs are referenced.
- Close out finished work in meaningful commits when committing is in scope.
  Inspect the diff before staging; coordinate ownership instead of sweeping up
  another task's unfinished changes. Leave remaining work clearly accounted for.
