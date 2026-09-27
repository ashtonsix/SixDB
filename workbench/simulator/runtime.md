# Simulator runtime and diagnostics

The [simulator guide](README.md) starts with an executable experiment.
This reference describes the shared [C++ runtime](include/sixdb/sim/runtime.hpp)
and its fault, resource and replay semantics. Protocol assumptions belong to
the [model guide](models/README.md); protocol state does not live in this engine.

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
