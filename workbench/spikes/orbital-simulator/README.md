# Orbital simulator: learning the laboratory

Build a programmatic laboratory for Orbital as a whole: correctness under adverse
histories, placement and routing under shared load, and disaster response. This
spike learns its architecture through executable experiments. The maintained
[reference simulator](../../simulator/README.md) now starts afresh directly under
`workbench/`. Keep this spike for focused probes and historical evidence; neither
these Python interfaces nor earlier simulators are inherited contracts.

The organising idea is **local evidence under physical constraints**. A protocol
must obtain the evidence it acts on, and the machine must supply the resources
needed to act. An event becoming eligible grants neither authority nor free CPU.
The [earlier notebook](../../notebook/orbital-simulation.md) records the motivation
and prior-art lessons. The current [Orbital brief](../../../orbital/BRIEF.md) is a
design being tested, not a correctness oracle.

## Architecture

| Boundary | Responsibility |
| --- | --- |
| [Event kernel](kernel.py) | Integer virtual time, addressed event records, explicit simultaneous-event choices and causal history. No epochs, quorums, retries or scheduling policy for application work. |
| [Physical environment](sim.py) | Machines, directed links, shared service queues, finite resident/durable capacity, leased memory, storage completion and failure semantics. |
| Actors through [Context ports](API.md) | Local state, messages, local clocks, I/O and compute completions. Protocols implement evidence collection, retry, deduplication, recovery and adaptation here. |
| Application bindings | Interpret objects, scopes, programs and complete results. Orbital's substrate does not know cells, query meaning or reduction semantics. |
| Scenarios and observers | Construct topology/workload/incidents; independently check histories, account for unfinished work and compare policies. Global truth stays here. |

These are ordinary code boundaries. A placement algorithm can use agreed
configuration and reports delivered by actors; it cannot ask the environment
which remote copy survived. An omniscient comparator must be explicitly separate.
Python privacy is a convention, not a security sandbox; a test checks that hidden
remote failure does not change a local actor's actions before an observation.

Several actors share a host's worker/control CPU, transmit/receive bandwidth and
device. The first Loom policy is nonpreemptive FIFO service in separate worker and
control pools. This is replaceable physical policy, not event ordering. The device
has one byte-service resource and bounded concurrent completion slots, preventing
queue depth from multiplying bandwidth. Jobs retain memory through their final
completion handler. Dropped transmissions and unfinished active work still incur
cost. Local enqueue acceptance says nothing about remote receipt or persistence.

An actor restart constructs fresh volatile state and reads storage through ports.
It receives no saved Python object or original workload from the harness. Actor
failure, machine power loss and destruction of storage are different events.
Submitted device work can outlive an actor; a device reset cancels it and retires
its users. A packet that has left a machine can survive its destruction. Incarnation
checks prevent old completions from changing a replacement actor.

This structure leaves room for the whole system without putting all of it into
one state machine. Witness/authority protocols, transaction ordering, consumer
folds, object reconstruction, extension execution and distributed jobs are actor
or application compositions. Loom policies choose preparation and physical work.
Transport, storage, clocks and CPU models can gain fidelity independently.
Recovery must use the same modeled ports and surviving evidence as normal service.

## What is executable

The examples share the runtime, but have different independent observations:

| Composition | Experiments and deliberately exposed boundary |
| --- | --- |
| [Composed result path](composed.py) | Distributed work produces private materializations; an application joins required results and persists its outbox, then submits through the actual payload/witness/consumer path. Historical and current version reads go through storage. Missing result evidence delays the aggregate while a point write and foreground work proceed. Crashes between durable outbox/result writes and callbacks recover through the same ports. |
| [Admission through publication](protocol.py) | Two-domain payload persistence, static-leader 2-of-3 acceptance, follower propagation, payload fetch and actual integer programs. Direct/relay routes, loss, duplicates, consumer/producer/witness restart, late writes, domain destruction, finite storage and unrelated local work. Prepared leadership and authenticated evidence are supplied assumptions; election and authority transfer are absent. |
| [Epochs and pending transactions](epochs.py) | A broad snapshot read with narrow output while source writers proceed; dependent reads wait and unrelated results finish. Registered bounds, reversed installation, repeated reads and recovery from actual retained epochs. Replicas compare full logical state under different physical and enabled-read orders. Agreed epochs and positions are supplied; reservation acquisition and distributed commit are absent. |
| [Transaction traffic](traffic.py) | Offered clients reserve outputs, announce minima after every reservation is held, fix positions, release, read, compute and install through actor ports. Durable ordered inputs retain conflicting waiters without overtaking; partial reservations leave reads moving. Prepared single authorities and device-reset recovery remain the standalone boundary. |
| [Replicated contention](replicated_contention.py) | The same transaction fold consumes actual prepared-leader quorum input. Three consumers obtain individual witness receipts, persist contiguous epochs and derive equal logical state and protocol outputs. Lost callbacks, consumer reconstruction and unavailable follower paths are tested. Full request bodies are replicated here; the original producer-payload/frontier admission path, elections and decision-owner replacement are not composed into this binding. |
| [Checked old-cut composition](demanding_case.py) | A transaction registers its complete read coverage, then two recoverable checker actors privately query the actual retained cut while source and independent writers continue. Required matching reports gate its durable outcome. The same application runs through standalone and prepared-quorum admission; contexts, checker jobs and decisions survive selected restarts. Logical history and synthetic buffer leases stand in for physical object reconstruction and native execution. Reclamation and agreed abort remain absent. |
| [Views and extensions](objects.py) | Actual version reconstruction with full/window/demand preparation; late old readers, pressure and backend leases. Matching and mismatching complete extension transcripts, a missing checker, independent publication. Supplied positions/read contexts, modeled programs and direct publication continuations stand in for transaction integration, UFFD and sandboxes. |
| [Distributed application](dataflow_scenario.py) | Two sources and two receivers, complete partition coverage, durable retries/deduplication, filtering shared at relays, three placements and foreground competition. Restart between durable receipt and ACK; all source deliveries can finish while final materialization lacks capacity. These private artifacts become public only through an enclosing protocol such as the composed result path. |

Four retained bad variants expose early publication, ignoring pending effects,
substituting the latest object version and comparing only an extension's final
answer. Additional tests reject insufficient payload copies, corrupt enhancement
and treating local enqueue as remote completion. A safety check passing without
results is not success: the runner separately checks expected completion, refusal,
abort or unfinished status.

The bindings are intentionally small. The prepared quorum composition now produces
agreed transaction inputs through actual messages and persistence, including pending
continuations that cross epochs. It still uses one coordinator's durable position
and decision records, without replacement authority. The checked old-cut case now
composes fixture transcript verification with that transaction path; native
execution and physical object mapping remain outside it. Leader recovery, membership handoff,
SOS evacuation and restoration of a dependency-complete prefix remain open.

The [fidelity comparison](fidelity_campaign.py) retains both the historical traffic
model and its corrected reservation/queue lifecycle. Their timings are not
interchangeable. [Local release](local_release.py) separately tests removing the
explicit release round after a position is fixed locally, while preserving the
global gate before execution. The brief now adopts local release; the captured
experiment sources retain the strict baseline and candidate labels for comparison.
[Recovery footprint](recovery_footprint.py) compares whole-journal and
record-at-a-time replay against the same completed history and memory budget.
Neither recovery mode reclaims history; [retention](RETENTION.md) owns the next
bounded-state and completion-capacity question.

The construction experiment adds explicit role placement and actor factories to
the prepared-quorum builder. A small [scenario helper](scenario.py) schedules
incidents and external inputs relative to semantic events, recording unexercised
milestones. [The notebook](../../notebook/orbital-simulation.md#order-of-work)
retains the intended sequence from this prototype exercise to a lasting simulator,
formal specifications, an Orbital architecture sketch and a small module starter.

The composed case is a check on these boundaries, not a bridge operated by the
test harness: actors send real messages, retain stable result identities, retry
lost responses and reread their own durable records after restart. Observers never
transfer a result from one subsystem into another. A private artifact, a durable
outbox entry, a chosen journal entry and public state remain distinct throughout.

## Use it from a program

For combinations, overload/severity ramps and strategy search, start with the
[campaign guide](CAMPAIGNS.md). The [adversarial gauntlet](GAUNTLET.md) mines earlier
requests and research into challenging histories, with executable cases and model
gaps distinguished. [Findings](FINDINGS.md) retain useful comparisons and reversals.

Run from this directory, or add it to Python's import path:

```python
from protocol import build_scenario, audit

w = build_scenario(mode="relay", seed=7, ordering="shuffle")
w.when("durable_write",
       lambda event: event["actor"] == "consumer" and event["key"] == "applied/1",
       "crash", actor="consumer")
w.fault(800_000, "restart", actor="consumer")
w.run(until=1_800_000)
assert audit(w, raise_on_error=True)["applications"] == 4
history = w.explain("ordinary")
```

Construct any directed topology with `add_host`/`add_link`, put several actors on
one host, or replace actor factories. `fault` schedules incidents; `when` triggers
one at a semantic boundary. Partition, pause of new resource service and slowdown
are distinct from death. Triggers cannot interrupt the middle of an atomic actor
handler; expose another transition when that boundary matters.

From the repository root on the Mac:

```sh
orb -m ubuntu python3 -m unittest discover -s workbench/spikes/orbital-simulator
orb -m ubuntu python3 workbench/spikes/orbital-simulator/run.py \
  --output build/experiments/orbital-simulator/example
orb -m ubuntu python3 workbench/spikes/orbital-simulator/run.py \
  --replay build/experiments/orbital-simulator/example/admission-loss \
  --output build/experiments/orbital-simulator/replayed
```

Output directories must be new. Each case preserves its parameters, source hashes,
Python version, exact choices, full causal trace and observation, including on a
failed check. Replay rejects changed sources or enabled events. Preserve source
bytes with the existing [capture helper](../../tools/README.md#captured-experiment-runs)
or Git; a hash alone is not a source archive. The [selected findings](FINDINGS.md)
retain compact comparisons; full run output stays under ignored `build/`.

## Explain, challenge, calibrate

`explain(op)` follows causal parents across queued messages, resource service,
actor state and durable reads. It includes reported blockers, not a proved minimal
dependency graph. A crashed actor's wait reports move to `retired_waits`; that is
loss of the reporter, not discharge of the logical obligation. Application
observers check completion independently. [Incident reduction](reduce.py) deletes
authored faults while preserving a caller's particular failure predicate.

Exact replay, alternative schedules and policy comparisons answer different
questions. Replay reproduces one identified execution. Shuffle varies simultaneous
eligible events and ready-read order; it is bounded exploration, not exhaustive
model checking. Fold comparisons hold agreed history fixed. Policy comparisons
hold offered workload and exogenous incidents fixed while allowing completion
times to change. Packet randomness uses flow identities, so a retry does not shift
every other flow's random draws.

Physical fidelity is currently coarse and **all numerical costs are synthetic**:
whole-message store-and-forward TX/link/RX, atomic durable records, FIFO service,
constant handler cost plus explicitly charged computation. Local messaging also
uses the modeled NIC. Link queues are bounded by message count; arbitrary actor
metadata, timers and simulator trace storage are not automatically byte-accounted.
Bindings reserve allowances or payload leases for the state they model. There is
no instruction/cache/NUMA model, OS process/thread lifecycle, packet congestion
control, torn device record or implementation calibration. A timeout establishes
unfinished work under that run, not deadlock or general loss of liveness.

The lasting simulator should retain the useful histories and independent checks,
then earn more fidelity where changing a model can change a design decision.
The first [native calibration challenge](FINDINGS.md#native-calibration-reject-the-scalar-shortcut)
rejects a single size-based same-host latency fit across batching and wait policies;
it constrains a future explicit IPC path without retuning these synthetic results.
Measured causal traces and held-out mixed loads should calibrate resource models;
sensitivity experiments should expose uncertain rankings. Implementation reuse
belongs behind controlled I/O ports where practical. Formal models can abstract
named transitions and evidence from small counterexamples; matching traces is not
a refinement proof. No formal stack or production-language binding is selected.
