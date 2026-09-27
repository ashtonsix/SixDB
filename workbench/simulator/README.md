# Reference simulator

A maintained, embeddable laboratory for Orbital and its consumers. The native
library runs actors against controlled time, messages, storage and finite
resources. Models and application bindings live above that boundary. A campaign
executable, native checks and Python experiment clients use the same library.
The [initial findings](FINDINGS.md) retain the validation results and construction
lessons, including the limits of the first captured campaign.
The [composed experiments](experiments/README.md) investigate regional reservation
convoys, the fairness tradeoff of granting eligible writers, and recovery and
retention under shared memory pressure.

Start with [the public runtime](include/sixdb/sim/runtime.hpp) and
[the Orbital composition](models/orbital.hpp). The Python learning runtime is
[retired](../notebook/retired-spikes.md); [its lessons and unported questions](../notebook/orbital-simulation.md#lessons-from-the-learning-spike)
remain research context. This implementation starts afresh from those boundaries;
its formats are laboratory formats, not production ABIs.
The [model guide](models/README.md) owns protocol assumptions and observation
limits. [The retained-view fixture](models/retained_view.hpp) separately tests
byte reconstruction, reader/replay roots, checkpoint publication and reclamation
through the same runtime. It is an experimental retention policy, not Orbital GC.

## Build and exercise

From the repository root, using the pinned Linux toolchain:

```sh
orb -m ubuntu cmake --preset dev
orb -m ubuntu cmake --build --preset dev --target simulator_validate simulator_run -j 4
orb -m ubuntu build/clang/dev/workbench/simulator/simulator_run --help
orb -m ubuntu build/clang/dev/workbench/simulator/simulator_run --seed 7 --incident consumer-reset
```

The default repository build does not build this laboratory. Shared code compiles
once into ordinary C++ libraries. `simulator_validate` builds its checks and runs
only the `simulator_` CTest group.

Capture a campaign while development continues:

```sh
orb -m ubuntu python3 workbench/simulator/campaign.py \
  --suite smoke --output build/experiments/simulator/smoke-1
```

This uses the existing Workbench capture/build receipt helper and a reusable
incremental build workspace. Choose a new output directory each time. `--suite
gauntlet` varies delay, offered load, failures, physical sharing and memory.
`--binary PATH` runs an already-built executable without a source-capture claim.
Python callers can import `evaluate`, `grid`, `ramp` and `pareto`; custom native
clients can assemble actors and advance the library directly.
`campaign.run_cases` also accepts an ordinary iterable of CLI argument mappings;
`investigate.py` captures the regional, ordering, hosted-recovery and retention
experiments through the same receipt machinery.
[The interactive example](examples/interactive.cpp) pauses at an actual private
read, restarts a checker and resumes the same experiment.
Each campaign trial retains its exact arguments, stdout/stderr and streamed choice
transcript. An append-only receipt preserves finished trials if the campaign is
interrupted; the final summary is replaced atomically. Detailed trace reruns are
diagnostics of the recorded case, not replacements for the original observation.

## Components and evidence

| Boundary | Responsibility |
| --- | --- |
| Event engine | Integer virtual time, event identity, seeded same-time ordering, streamed exact replay, bounded run/advance |
| Physical services | Shared host CPU, NIC and disk queues; directed links; finite resident and durable bytes; submitted-operation lifetimes |
| Actor ports | Local input, immutable message bytes, timers, explicitly charged computation, bounded storage operations and buffer ownership |
| Model components | Protocol transitions, authority assumptions, ordering, recovery and publication; application bindings interpret opaque values and scopes |
| Assemblies and clients | Roles, processes, placement, topologies, offered work and incidents; observations never become protocol evidence implicitly |
| Independent observers | Safety checks, all-offer accounting, causal explanations and selected telemetry |

An actor receives `Context`; it cannot inspect the environment or another actor.
A factory constructs a fresh actor on restart. Recovery must read retained records
or exchange messages. Actors, processes and hosts are separate identities: several
actors can die together while another process on their host remains alive.

`Simulation::run(deadline, event_budget)` supports repeated calls. Budget exhaustion
does not advance time past pending work. An observer may call `pause()` to stop
after the current atomic event, or schedule an incident with `at()`. Immediate
lifecycle mutation and recursive `run()` from an observer are rejected. Faults inside
an actor handler require that handler to expose another transition first. Observers
remain active when verbose trace output is disabled; trace saving never controls
fault injection, safety checks or offered-work accounting.

The runtime emits causal records without retaining a complete history. Each actor
delivery links to the prior local delivery; a storage read links to the write whose
bytes it returned. Asynchronous storage and buffer records retain the submitting
incarnation, even if a newer process exists when they finish. Large payload histories
are optional; small tests can retain records in memory.

## Physical semantics

The initial physical model is deliberately small and explicit:

- A host has one FIFO service queue each for CPU, transmit, receive and disk. A
  directed link adds shared serialization and propagation. Queue byte limits,
  rates and service delays are authored inputs. There is no implicit remote link.
  Same-host messages currently consume the host's transmit and receive queues.
- `compute(duration)` charges modeled CPU service. Actual native computation
  produces bytes and decisions, but its wall time never changes virtual time.
  Handler execution is atomic. The initial local clock is ideal monotonic time.
- Successful send completion means local departure, not remote delivery. Lost
  traffic retains work already incurred; receiving bytes consumes destination
  capacity. Protocols must acquire their own remote acknowledgements.
- A process crash discards actors, timers and queued callbacks, and stops that
  process's remaining CPU work so healthy colocated work can run. Submitted writes
  and already submitted sends can still complete. Power loss also cancels the
  host's incomplete disk, CPU and NIC work. Packets already in flight survive the
  sender's loss. Completed durable records survive reset; destruction removes them.
  Reset stops processes; the harness explicitly restarts each one.
  This is an instantaneous reset, not a sustained host-offline interval; model a
  longer outage with stopped processes and explicit link incidents.
- Storage is an actor-namespaced ordered map of atomic records on a host. Reads
  reserve their declared maximum destination bytes until callback retirement.
  Writes reserve the new value in addition to any prior value until replacement
  completes. Deletion frees capacity only when durable. Keys and directory
  bookkeeping are not included in the durable-byte counter.
- Listing has bounded key count and reply bytes, with a lexicographic cursor.
  Separate pages are not a snapshot. A protocol must discover and validate its
  durable root before interpreting children; the environment never supplies a
  hidden recovery inventory.
- Immutable `Buffer` capabilities carry backend borrows through send/write.
  Releasing ownership, cancelling a logical operation or losing a process does
  not retire a surviving backend borrow. Receive/read/timer payloads also consume
  capacity for their physical lifetime.

STL metadata and actor-held model state are not automatically priced by their
native allocator. Models must declare those allowances and any retained payload
ownership. Object pages, mappings/COW, kernel rings, worker pools, multi-device
layouts and protocol-specific completion reserves can be added as measured or
experimental components; the current host model does not claim to represent them.

## Reproduce and diagnose

```sh
orb -m ubuntu build/clang/dev/workbench/simulator/simulator_run \
  --seed 7 --trace build/orbital-trace.jsonl --choices build/orbital-choices.txt
orb -m ubuntu build/clang/dev/workbench/simulator/simulator_run \
  --seed 7 --replay build/orbital-choices.txt
orb -m ubuntu python3 workbench/simulator/trace.py build/orbital-trace.jsonl
```

Exact replay checks every scheduled event, cancellation and chosen dispatch, including payload
fingerprints. It rejects missing/unused choices and new events. Use the captured
source, case and cost model; this is not a cross-version replay format. The
64-bit fingerprints detect accidental divergence and are not cryptographic
verification. Semantic case identity and its obligations can outlive these bytes.
Cancelled events are removed from the queue, including their retained native
payloads; they do not accumulate as no-op dispatches until old deadlines.

`trace.py --slice RECORD_ID ...` follows incoming, local-state and durable-evidence
ancestry. Its diagnostic index is proportional to trace length, while the normal
runtime streams records. Trace checking and finite seeded runs are evidence about
observed histories, not liveness or refinement proofs. An unfinished offer, a
missing required incident and a violated safety property must remain different
results. Authored nanoseconds and byte allowances are not production performance
predictions.
