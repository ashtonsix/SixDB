# Toward a durable Orbital simulator

2026-09-27. Research context for Ashton's all-Orbital simulator. The
[reference simulator](../simulator/README.md) now owns the maintained native
library, executable models and experiment clients. The
[learning spike](../spikes/orbital-simulator/README.md) retains the experiments
that informed those boundaries and remains available for quick probes. This
note retains questions and prior-art lessons rather than operating instructions.

The ambition is a programmatic laboratory for repeated design experiments,
distress, disaster response and implementation checking over many years. Like
CAD before a wind tunnel, it should make expensive or elusive situations cheap
to investigate, with physical experiments improving its predictive value.
Durability comes from revisable models and useful counterexamples, not preserving
the behavior of the first simulator.

## Order of work

Ashton's intended sequence is:

1. Exercise the proposed architecture in the current prototype, using the demanding
   case and poorly understood areas. Record awkward construction, misleading
   shortcuts and missing protocol behavior as findings in their own right.
2. Build the lasting simulator directly under Workbench, carrying forward useful
   cases and evidence rather than inheriting all prototype interfaces.
3. Express the most correctness-critical Orbital guarantees and assumptions in
   TLA+ specifications. Counterexamples can still require a design change.
4. Sketch Orbital's internal divisions and relationships with other modules, as
   provisional guidance for their work.
5. Implement a small Orbital starter that unblocks real consumers, then develop
   the modules together. Durable objects are the leading candidate for that start.

The first boundary-validation exercise has led into the reference implementation.
Ashton explicitly allows these activities to overlap: an uncertainty encountered
in the maintained library can go back to a focused spike. Checked old-cut reads
and a separate checkpoint/reconstruction probe established useful caller
obligations; they did not settle every Orbital policy. The formal work will
concern authority, ordering, publication and lifetime rules; simulation and native
experiments continue to address resource and performance questions.

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

## Lessons from the learning spike

The [spike's findings](../spikes/orbital-simulator/FINDINGS.md) support keeping
actors, discrete events and shared physical resources. Actual acknowledgements
and retry timers exposed a replication feedback loop that inflated modeled MAN
scan traffic roughly eighteenfold. Faults between persistence and callbacks
exposed useful recovery boundaries. Neither would appear in a fixed replication
latency charge. These are model findings, not measured production improvements.

Sharing an event engine proved weaker than sharing executable components. Early
epoch experiments supplied positions; object experiments supplied read contexts.
The later contention composition reused one deterministic transaction fold behind
both standalone and actual prepared-quorum admission. That boundary paid off:
agreement, application transitions and physical scheduling could change separately.
The later checked-old-cut case joins extension verification to that fold. The
producer-payload/frontier path, physical object reconstruction and replacement
authority remain outside that composition. More isolated scenarios cannot
establish that their boundaries fit together.

Workload and evaluator reuse worked better than construction reuse. The same
campaign machinery handled flow, traffic and adversity, including refusals and
unfinished work by cohort. The original builders coupled role names, placement and topology:
in replicated contention, powering off a coordinator's machine also removed its
colocated leader. Explicit assembly seams were needed to separate those failures. Make
offered programs, role placement, network/failure domains, strategies and incident
recipes independently composable inputs. Reject unsupported combinations rather
than silently ignoring an option. Ordinary programmatic builders remain suitable;
this does not call for a universal scenario language.

The original storage boundary made recovery experimentation awkward: whole-prefix
reads required one large buffer, and the streaming alternative inferred the end
of a contiguous journal through missing records. The subsequent
[retirement probe](../spikes/orbital-simulator/RETIREMENT-PROBE.md) needed an adapter
to add bounded enumeration and durable deletion. Long-lived retention,
checkpointing and disaster restoration need bounded
enumeration, reads and reclamation with explicit durability and lifetime semantics.
Process membership also needs to be distinct from actors and machines if a process
crash is to kill several roles without cancelling the machine's submitted I/O.
These are environment capabilities; recovery policy still belongs to actors.

Full in-memory JSON traces, repeated state snapshots and ready-set hashing are
poor defaults for long load searches. Use compact online observations with bounded
or streamed diagnostics, retaining detailed replay evidence for failures and
selected comparisons. Simultaneous-event shuffling is useful but does not explore
all relevant delivery orders. Small correctness searches need deliberate schedule
variation and reduction of workloads and incident sequences; large load searches
need efficient queues and accounting. They can share components without recording
every experiment at the same detail.

Exact replay and semantic reproducibility serve different purposes. Preserve a
source-specific execution for diagnosis, but also retain the offered work,
required outcomes and fault intent so a revised implementation can face the same
question. A seed alone does not match histories across changed protocols. Launching
from immutable [source captures](../tools/README.md#captured-experiment-runs) would
avoid the mixed-source attempts detected by campaign hash guards; recording hashes
after a run is too late to isolate it. Keep source capture and artifact retention
in shared Workbench tooling rather than duplicating them in each campaign.

Independent observers deserve more investment. Replica agreement missed fabricated
client completions, and checking only completed transactions missed reads that
skipped still-pending predecessors. Check temporal obligations, durable evidence
and public outcomes separately from agreement, and preserve deliberately faulty
controls. More reuse of production code makes this independence more valuable.

## Connecting to implementation

Real plans, codecs and journals should eventually cross the simulator boundary in
both directions. A versioned adapter can decode implementation artifacts, preserve
identities, scope/visibility constraints and interpretation dependencies, and bind
them to simulated resources. Generated cases can in turn exercise native components.
An adapter must not silently flatten away an unsupported property. Engine or another
application owns plan meaning; Orbital and the physical environment consume opaque
requests and declared constraints. Prepared pointers and local bindings are rebuilt,
not treated as durable plan identity. Today's Python dictionaries need not become
a production format or universal intermediate representation.

Where practical, run the real deterministic interpreter, protocol transitions and
codec under controlled time, randomness and I/O. Compare their observable decisions
and results against independent semantics, not event-by-event timing equality.
Sharing these transitions prevents more model drift than format conversion alone.
Keep separate native checks for races and hardware behavior that atomic actor
handlers do not exercise.

Physical modeling must preserve when work becomes ready and how long it owns
resources, as well as bytes and service costs. The native handoff experiment's
low-load scalar fit failed badly with batching and sleeping consumers. Real codecs
can supply byte counts and measured work, but hiding serialization, batch assembly,
wakeup or retained buffers inside a free converter would still misrank strategies.
Use coarse and detailed resource models at the same component boundary, increasing
detail where measurements or sensitivity change the answer.

The old-cut transform and retained-view/checkpoint cases now provide related
semantic obligations for the native reference models. Reuse those obligations
across placement, admission and retention alternatives. Sustainable reclamation,
replacement authority and completion under exhausted budgets remain protocol
questions; the runtime should expose their failures without choosing a policy
on an actor's behalf.
