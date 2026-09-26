# Toward a durable Orbital simulator

2026-09-26. Preparation for Ashton's proposed all-Orbital simulator. Start the
learning spike **after current networking and other Orbital design work settles**;
then use that experience to build a lasting home directly under `workbench/`.
This note seeds questions and examples, not an architecture, API or tool choice.

The ambition is a programmatic laboratory for repeated design experiments,
distress, disaster response and implementation checking over many years. Like
CAD before a wind tunnel, it should make expensive or elusive situations cheap
to investigate, with physical experiments improving its predictive value.
Durability comes from revisable models and useful counterexamples, not preserving
the behavior of the first simulator.

## What should it help us decide?

Three uses need related but different evidence:

| Question | Useful experiment |
| --- | --- |
| Can this protocol produce a forbidden result? | Small histories, adverse message schedules, crashes and recovery; independent checks of promised behavior. |
| Which placement, routing or scheduling policy works better? | Matched workloads and completion obligations, shared resource limits, sensitivity to uncertain costs and correlated incidents. |
| Does an implementation still realize the intended protocol? | Production logic under controlled I/O where practical, semantic trace checks, and replay of minimized failures. |

One event engine may support several uses; a detailed timing model need not also
be the formal model. Cover write and read paths, extensions, changing membership,
retention and recovery. Keep Engine's object meaning and visibility rules in a
replaceable application binding. A small application that actually reads and
changes state can expose errors that a scheduler of predeclared key sets cannot.

## Give actors only what their machines could know

Separate protocol actors, the simulated environment and observers. The environment
can know the fault schedule and every machine's state; an actor acts on its own
volatile/durable state, received messages, local clocks and completed I/O.
An observer can check global truth without making it available to the algorithm.
An idealized placement oracle is a useful comparator when named as such. Calico's
[M1 model](../../../calico/xmem/spec/M1.tla) guards `LandRegion` with global durable
evidence: useful for checking evidence compatibility, but its actor-level
realization still needs to acquire that evidence through messages.

Explore actor boundaries without equating actor, role, process, host and shard.
Several roles may share a machine's CPU, memory, NIC and device. Discovery may
revisit a shard or introduce participants not known in advance. A supplied
certificate, coherent snapshot or atomic cut is an assumption until its creation,
distribution, persistence and recovery are actually modeled.

Calico's [witness daemon](../../../calico/xmem/src/witnessd.cpp) injects clean,
partial and complete unacknowledged tails before killing a real process. Keep
that contact with implementation boundaries alongside simulation. Process SIGKILL
leaves the OS page cache alive; it is not a machine power-loss test. Partition,
restart and destruction need distinct semantics. A departed packet can outlive
its sender; an old callback cannot resurrect volatile buffers. Let scenarios
place faults at causal boundaries as well as times.

## Make results explainable and challenge the model

The [networking model](../spikes/orbital-dissemination/MODEL.md) records concrete
lessons: omitted receive capacity, completion slots multiplying device bandwidth,
and dropped traffic losing its already-incurred cost all made early results too
optimistic. Small conservation and causal checks are valuable simulator tests.
They complement checks of the protocol being simulated.

Keep payload durability, admission, transaction position, readiness, publication,
external effect and response distinct. Instrument why work waits: missing
evidence, ordering dependencies, reservations or physical pressure. Follow all
offered work through refusal, cancellation, completion or unfinished status,
including repair and catch-up. Preserve per-producer/class views: a better
aggregate can conceal an independent producer made much worse.

Choose fidelity for the decision. Charge packet/byte work, shared capacity and
finite storage where they can change the conclusion; add detail when sensitivity
or measurements justify it. Calibration should compare matched causal traces
and resource counters under held-out loads/faults, preserving clock uncertainty
and correlated time windows. Marginal stage percentiles do not compose into an
end-to-end tail. The current networking model owns the concrete calibration gaps.

Useful telemetry would let a researcher ask “why did this obligation not finish?”
and slice backward through its evidence and resource dependencies. Prefer such
queries and regenerated views to more manually maintained reports. Large traces
can be selective or rerun for diagnosis; telemetry itself has storage/runtime cost,
and any modeled production telemetry should consume modeled capacity.

## Repeatability, verification and implementation

A seed repeats one execution under particular sources, runtime and scheduling
rules. It does not establish protocol determinism. Explore alternative permitted
orders; when checking fold equivalence, hold the **agreed input history** fixed
and compare logical state, metadata and outputs. Network changes can legitimately
change what gets admitted. Equal final payloads can hide different intermediate
requests or externally visible results.

Calico's [crash driver](../../../calico/xmem/test/crash_driver.cpp) separates
workload randomness from crash scheduling and reconstructs final bytes from
witnesses. Keep those ideas, but recognize its recovery-from-seed assumption:
the driver already knows the operations and initial geometry. The
[reopen ledger](../../../calico/workbench/science/systems/recovery/XMEM_REGION_OPEN.md)
identifies what a fresh process would instead need to discover. Recovery tests
should withhold information the restarted program has not durably retained or
received. Matching final hashes alone also misses transient forbidden results.

The [crash harness](../../../calico/xmem/test/test_crash_recovery.cpp) removes its
temporary tree even on failure. Preserve failed state and causal evidence before
cleanup; a workload seed does not reproduce OS scheduling. Keep runtime/source
identity and nondeterministic choices, then minimize the failure. Historical
replay belongs to its source version; current checks need not preserve old digests.
Use the existing artifact tools rather than retaining every generated state in Git.

Start formal work with a real question, as xmem's
[M1RES](../../../calico/xmem/spec/M1RES.tla) does: a reader pins locally, registration
is delayed, and the custodian retires the still-needed version. It retains both
async and sync configurations; [M1RESL](../../../calico/xmem/spec/M1RESL.tla) explores
local registration with delayed frontier views and retirement attestations.
Keep such rejected alternatives and negative controls. The
[BFT crash falsifier](../../../calico/xmem/test/test_bft_crash_recovery.cpp)
deliberately corrupts recovered signed evidence to exercise rejection.

Define properties independently, and name how implementation observations map
to abstract actions/state. Trace checking tests that mapping on observed runs;
it is not a refinement proof. xmem's
[liveness configuration](../../../calico/xmem/spec/MC_M1_CFT_LIVE.tla) checks one
transaction and one pull generation under explicit driver fairness. Preserve
those bounds and assumptions with results; neither a finite check nor a seeded
battery is a general proof. A timeout alone does not establish a liveness failure.

Good starting references, without selecting a stack:

- [FoundationDB simulation](https://apple.github.io/foundationdb/testing.html)
  combines deterministic actor execution with separate live performance and
  hardware-failure testing. Explore reusing protocol logic and workloads through
  controlled time, randomness, network and persistence boundaries.
- [AWS's TLA+ experience](https://lamport.azurewebsites.net/tla/formal-methods-amazon.pdf)
  shows the value of compact design models at different abstraction levels.
  [P](https://p-org.github.io/P/) offers communicating state machines and systematic
  schedule exploration. Try a small consequential protocol question before
  choosing either; neither needs a packet-level rendering of all Orbital.
- [IronFleet](https://www.microsoft.com/en-us/research/publication/ironfleet-proving-practical-distributed-systems-correct/)
  connects state-machine refinement to implementation verification. It is a
  reference for what a proved connection entails, beyond two models agreeing.

Independent history/property oracles remain useful when simulator and
implementation share code. The xmem sources above were inspected, not rerun,
for this note. Their techniques and counterexamples are prior art; their custody,
page, quorum and retention policies do not become SixDB requirements.

## What to learn in the first spike, later

Choose a narrow end-to-end obligation from the settled design, with one adverse
case and one alternative policy. Programmatically build its topology, submit
work, inject a failure, inspect its causal history and recover a small replay.
Then try a materially different topology or obligation: does reuse save work,
or does the first fixture's protocol leak into the simulator's core?

The present [dissemination study](../spikes/orbital-dissemination/README.md) will
supply useful cases and lessons, without becoming the durable implementation by
default. Counterexample slicing, actor-local feedback and explicit ownership of
shared resources emerged as useful opportunities in both task consultations.
Existing [capture and run helpers](../tools/README.md) and
[artifact retention](../tools/artifacts.md) suffice meanwhile. Learn which seams
deserve to endure before choosing a runtime, universal trace format, UI or formal
tool integration. No new framework or simulator implementation starts here.
