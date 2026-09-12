# Working in SixDB

Start with [README.md](README.md) for direction and module scope, and the
[Workbench guide](workbench/README.md) for research. Explicit user direction
takes precedence over these defaults.

- Follow the question and experimental signal. Do not invent roadmaps,
  mandatory templates, promotion stages, or approval gates. Distinguish
  constraints, proposals and observations; a spike does not establish a contract.
- Browse related [spikes](workbench/spikes/README.md) and
  [notebook ideas](workbench/notebook/ideas.md) when choosing direction.
  Draw on [Calico](workbench/notebook/calico.md), recalibrating its bets and
  implementing SixDB afresh.
- Curate documentation: replace stale explanations, remove repetition, and put
  detail in its owning guide. Entry documents should stay thin and steerable.
- Follow [BUILDING.md](BUILDING.md) and [code conventions](workbench/design/conventions.md).
  Use the pinned Linux toolchain; prefix Linux commands with `orb -m ubuntu`
  in this macOS/OrbStack workspace. Preserve incremental builds and build the
  targets needed for the task.
- Look for existing [helpers](workbench/tools/README.md) and
  [datasets](workbench/datasets/README.md) before duplicating machinery or inputs.
  A one-off prototype need not adopt a framework.
- Choose checks that address the change or uncertainty. Follow the
  [measurement conventions](workbench/benchmarks/README.md); distinguish timings,
  logical counters and modeled costs, and state the limits of the comparison.
- Retain findings and useful comparisons rather than every successful run.
  The [retention guide](workbench/tools/artifacts.md) covers selection and recovery;
  bulky outputs belong in ignored storage or S3, and shared inputs are referenced.
