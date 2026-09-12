# Working in SixDB

Start with [README.md](README.md) for direction and module scope. For research,
use the [Workbench guide](workbench/README.md) and follow the links relevant to
the question. Explicit user direction takes precedence over these defaults.

## Research and design

- Follow the question and experimental signal. Do not invent a fixed roadmap,
  mandatory templates, promotion stages, or approval gates.
- Distinguish established constraints, proposals, and experimental observations.
  A spike result does not by itself establish a production interface or design.
- Browse relevant existing spikes and [notebook entries](workbench/notebook/ideas.md)
  when choosing or changing direction; link findings to ideas they change or revive.
- Keep entry documents thin and steerable: replace stale advice, remove repetition,
  and link to the owning guide or spike instead of accumulating detail here.
- Draw on [Calico](workbench/notebook/calico.md) as prior work. Recalibrate its
  architectural bets and implement SixDB afresh rather than lifting implementation.

## Building and iteration

- Follow [BUILDING.md](BUILDING.md) and the [code conventions](workbench/design/conventions.md),
  which distinguish implemented settings from proposals still open for discussion.
- Use the pinned Linux toolchain; from this macOS/OrbStack workspace, prefix Linux
  commands with `orb -m ubuntu`. Keep TUs independently compilable and preserve
  incremental builds; build the targets needed for the task.
- Look for existing [run helpers](workbench/tools/README.md) and
  [shared datasets](workbench/datasets/README.md) before duplicating machinery or
  inputs. Use them where helpful; a one-off prototype need not adopt a framework.

## Evidence and validation

- Choose checks and comparisons that address the change or scientific uncertainty.
  Run relevant checks; broaden testing when failures or unresolved concerns justify it.
- Follow the [measurement conventions](workbench/benchmarks/README.md), including
  CPU pinning and sequential cases/repetitions. State what was measured and its
  limits; distinguish timings, logical counters, and modeled costs.
- Retain evidence for findings and reusable comparisons, rather than every successful
  run. Compact formatting alone does not make a sweep worth keeping in Git.
  The [retention guide](workbench/tools/artifacts.md) covers selection and recovery;
  broad output belongs in ignored storage or S3, and shared inputs stay referenced.
