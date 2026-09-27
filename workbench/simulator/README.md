# Reference simulator

A programmatic laboratory for Orbital and its consumers. Actors run against
controlled time, messages, storage and finite resources. Experiments compose
models, placement, workload and incidents, with independent observers checking
what actually happened.

The [Orbital model guide](models/README.md) states what the native models execute.
Their prepared journal path and local coordinator records are narrower than the
[formal models](../../orbital/spec/README.md). Retention fixtures are separate
experiments. The [Orbital walkthrough](../../orbital/ARCHITECTURE.md) explains the
intended system; this laboratory is not its production implementation. Authored
service costs do not predict production latency or throughput.

## Start with one experiment

From the repository root, using the [pinned Linux toolchain](../../BUILDING.md):

```sh
orb -m ubuntu cmake --preset dev
orb -m ubuntu cmake --build --preset dev --target simulator_interactive -j 2
orb -m ubuntu build/clang/dev/workbench/simulator/simulator_interactive
```

[interactive.cpp](examples/interactive.cpp) assembles an Orbital case in a
caller-owned simulation, pauses after a private read, restarts the checker,
and resumes. Its observer is outside the protocol: actors do not receive the
fault schedule or another actor's state. The result separates failed and
unfinished work from safety violations and event-budget exhaustion.

To change the experiment, start with [Case and assemble](models/orbital.hpp)
and the [model assumptions](models/README.md). To change a service or add a model,
use the [runtime reference](runtime.md) and its [actor ports](include/sixdb/sim/runtime.hpp).
Production logic can be hosted through controlled services as it develops;
the current integer fixture and diagnostic formats are laboratory choices.

## Run cases and retain comparisons

```sh
orb -m ubuntu cmake --build --preset dev --target simulator_validate simulator_run -j 2
orb -m ubuntu build/clang/dev/workbench/simulator/simulator_run --seed 7 --incident consumer-reset
orb -m ubuntu python3 workbench/simulator/campaign.py \
  --suite smoke --output build/experiments/simulator/smoke-1
```

The default repository build excludes this laboratory. Shared code compiles
once into ordinary libraries. `simulator_validate` runs only its native/client
checks. Choose a fresh campaign output directory; capture and build use the
existing Workbench helpers and an incremental build workspace.

`--suite gauntlet` varies delays, offered load, failures, sharing and memory.
`--binary PATH` runs an existing executable without claiming source capture.
Python clients can import `evaluate`, `grid`, `ramp` and `pareto`, or pass an
iterable of CLI argument mappings to `campaign.run_cases`. Each trial retains
arguments, stdout/stderr and choices as it runs; an append-only receipt preserves
completed trials if collection is interrupted.

The [experiment guide](experiments/README.md) owns regional reservation,
hosted recovery and retention-pressure comparisons and their `investigate.py`
commands. [Replay and trace slicing](runtime.md#reproduce-and-diagnose) diagnose
captured cases; a diagnostic rerun does not replace the original observation.

## Earlier findings

The [initial campaign](FINDINGS.md) retains its dated results and construction
lessons. The [simulator notebook](../notebook/orbital-simulation.md) keeps prior-art
lessons and unported questions; the learning runtime is [retired](../notebook/retired-spikes.md).
