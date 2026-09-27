# Adversarial gauntlet

Challenge the simulator's strategies and its ability to represent the challenge.
Orbital LEAD owns the complete architecture. This map follows the [existing
boundaries](README.md#architecture): actors act on received evidence, physical
resources constrain action, and independent observers know the authored workload
and incident history. A missing mechanism is a model gap; a harness must not
quietly manufacture its successful outcome.

The connected reference path is offered input → private computation/materialization
→ durable outbox → payload persistence → chosen input → public state → versioned
read. Delays, recovery and retained obligations can cross every boundary. The
transaction traffic binding investigates reservation, position, read dependencies
and installation. Its prepared quorum adapter now obtains ordered inputs through
actual witness writes and compares replicated folds. Full-body replication and
coordinator-local decisions remain assumptions of that adapter; it does not replace
the original producer-frontier path or establish election/disaster tolerance.

## What every comparison preserves

- Count harness-authored obligations, including arrivals the actor never handles.
  For each service class, offered = completed + refused/aborted + unfinished.
  Distinguish acceptance, chosen input, publication, client knowledge and repair.
  A safe empty result stream is not successful service.
- Start latency at the scheduled offer or declared job obligation, including
  queueing. Retain unfinished ages and the offering/draining windows. Report
  cohorts and sample counts; twenty operations do not estimate p99.9.
- Separate past lawful durability from current survival, current reachability and
  usable authority. A prior receipt remains a historical fact after storage is
  destroyed; it cannot establish a surviving replay source.
- Retain bytes and work until their actual last user finishes. Include control,
  probes, duplicated transmissions, cold starts, catch-up and finalization.
  Lower costs at lower delivered service are not equivalent savings.
- Hold input meaning, required checking, resource budgets and exogenous incidents
  fixed across strategies. A causal-boundary fault answers a different question
  from the same wall-time incident applied to two differently paced strategies.
- Keep an independent semantic oracle and a negative control that it actually
  rejects. Timed-out work is unfinished at that horizon, not a deadlock proof.
  Exceptions/event-budget exhaustion are simulator errors, not strategy stalls.

## First executable families

[adversity.py](adversity.py) wraps the existing builders and fault API. It adds no
protocol actors. Its `evaluate(case, seed, output_path)` returns the shared
[campaign Observation](campaigns.py); `build_case` and `observe` are available
for programmatic inspection. [test_adversity.py](test_adversity.py) checks the
observations and adversarial histories. These are bounded probes; the protocol
still permits at most eight entries and this adapter offers four (two submitted results in the composed path). They cannot
measure sustainable production throughput.

| Family | Escalation and discrimination | Required observations |
| --- | --- | --- |
| `payload` | Healthy; one holder's return path blocked; every return path temporarily blocked; permanent path isolation; destroy all payload holders after a prefix is chosen while witnesses survive; destroy them after publication, with and without the consumer. | Chosen/public prefixes, historical payload copies, current durable replay bodies, current public state, missing-body waits. Temporary inaccessibility differs from body destruction. No fabricated result fills a hole. |
| `response` | Reverse response loss; add a consumer power cycle; distribute route v2 unevenly then deliver v1; extend loss beyond the observation. | Exact public state and stable operation identities, producer knowledge, actual storage recovery, route generations, duplicate traffic, unfinished obligations. A reply lost after persistence does not undo the result. |
| `finalization` | Constrain consumer durable space, or pause worker/control/device service during arrivals. | Chosen input can outpace application persistence; scarce finalization resources must not become false success. Measure the ordinary colocated operation separately and drain after a temporary pause. |
| `composed` | Hold one required artifact; slow the shared consumer workers; power-cycle the consumer and crash/restart the application on an explicit incident timeline. | Private materialization remains separate from proposal/publication/version reads. All initial job and foreground obligations remain counted through faults. Measure collateral work and whether recovery actually finishes. |

Timed cases name their start/heal times; semantic destruction occurs only after
actual chosen evidence. A run records whether its intended incident happened.
The post-publication variants preserve the historical safety verdict while showing
canonical replay-body coverage fall to zero. A surviving consumer may still hold
materialized versions; destroying it removes that separate basis too. Neither
its snapshot nor the witnesses alone supply missing original payload/code for
general replay. No full restoration authority or dependency closure is inferred.

Present replay-body coverage here concerns the small protocol's canonical bodies;
it does not certify general code/dictionary/key/decision closure. The observer
can inspect surviving storage; it never gives that inventory to an actor. An
online holder is not automatically reachable through the installed route.

## Broad scenario map

The following are challenges, not a commitment to implement a universal runtime.
“Existing path” means an adapter can exercise the current composition; it does
not mean every variation is implemented. “Binding gap” means LEAD must connect or
extend the relevant behavior before drawing the stated conclusion.

| Priority / challenge | Adversarial history and increasing severity | Oracle, negative control and strategy contrasts | Present boundary |
| --- | --- | --- | --- |
| A: healthy metadata, unusable data | Keep the witness majority connected while payload GETs stall; then remove holders, interpreter or dictionary independently. | Admit/publish/recover are distinct. Count the exact recoverable closure, never surviving witness count alone. Negative: accept latest-version bytes or use a historical copy count as current survival. | Payload-only existing path; general interpretation/access closure is a binding gap. |
| A: retry and repair avalanche | Align small-message fan-in, ACK loss and many recoveries on one host; heal briefly, then fail again before backlog drains. | Useful service and exposure must improve, with all repair and refusal costs included. Compare fixed retry, jitter/pacing, deduplication and bounded recovery credits only when implemented as actor policies. | Existing retries/restarts/shared resources; many-shard repair controller is a gap. |
| A: finite finalization | Fill intermediates, retained source versions or pending output state; separately exhaust memory, disk bytes, queue entries and control service. | Acyclic work can still have a resource wait cycle. Spare workers alone do not establish a usable decision owner. Negative: reclaim buffers on cancellation or treat receipt as publication. | One finite transaction now reproduces consumer capacity exhaustion after announcement. General resolution authority and retention composition remain gaps. |
| A: stale route meets recovery | Lose completion, restart sender/receiver, overlap old/new routes and late backend completion; replay an older route after newer configuration persists. | One stable logical result, no silent release of outstanding deliveries, no old authority acquiring new rights. Compare slow deliberate rollout with aggressive movement including overlap costs. | Versioned relay route and restart are executable; membership/authority handoff is a gap. |
| A: apparent latency win from missing work | Increase scheduled offers while boot, handler or device queues stall; stop offers and allow a measured drain. Skew toward a tiny slow class. | Scheduled denominator, per-class outcomes/ages, source-to-result latency and residual debt. Negative: completed-only p99, actor-arrival denominator, silent clipping to the bounded stream. | Adapter accounting; sustained traffic belongs to LEAD's traffic binding. |
| A: cost-optimal tree becomes a bottleneck | Edge-origin versus cloud-origin requests; local/MAN/WAN crossings; tiny requests and large direct returns; deepen trees, align bursts, share a gateway. | Equal information, required recipients and actual reply contract. Include packet CPU, both byte directions, route setup and fixed capacity where applicable. Compare direct, shallow/regional and constrained tree families. | Static relay comparison exists; general tree construction, shared corridors and full cost model are gaps. |
| B: two “replicas” do the same read | Partition a scan across shard replicas, hedge slow chunks, change readiness and reclaim an old cut while results return. | Complete nonduplicated logical coverage at one usable cut. Two copies of the same range are not two ranges. Negative: count responses or largest sequence. Compare whole-query distribution, chunks and work stealing with actual setup/cancel cost. | Source survey and isolated probes; general replica-read composition is a gap. |
| B: broad input versus broad possible effects | Hold actual changes fixed while widening source coverage or possible output scopes; add max-selected-row, conditional no-op, bridge x→(x,y)→y and WAN predecessors. | Preserve the application's result/atomicity; identify logical dependencies separately from physical pressure or incidental shard barriers. Negative: skip pending effects or widen the transaction semantics to help the scheduler. | LEAD's transaction traffic work; fixed agreed-epoch checks alone do not establish reservation behavior. |
| B: correlated false alarms | Give observers indistinguishable missing replies from local pause, directed path failure and PLP delay. Correlate alarms across shared witnesses; flap around thresholds. | No authority change from suspicion alone. Count needless migration, repeated warmup, useful copied bytes and unaffected service. Compare hysteresis and exposure-aware repair; new evidence may justify escalation before cooldown expires. | Fault delivery exists; health detector and response controller are gaps. |
| B: enhancement needs missing context | Deliver payload and admission separately; change filter selectivity, row width, receiver residency, layout/encoding and expensive edge direction. Drop or delay the sidecar. | Enhancement cannot change the agreed result or substitute for required independent execution. Negative: apply a bitmap to another layout/version, or wait forever for optional enhancement. Compare local computation, shared masks, exact reductions, compressed sidecars and bypass. | Fixed filter mask exists; general bindings/representations and independent checking are gaps. |
| B: verifier straggler consumes the database | Delay one required checker, grow source history/output, continue independent writers, then cancel caller or restart worker. | No publication before all required complete interactions match; independent work remains eligible. Source/code and pending effects survive physical cancellation. Negative: compare final return only or reuse one cached result as several independent executions. | Isolated checker/object probes; end-to-end transaction integration is a gap. |
| B: membership grows during dissemination | Join a VM between snapshot and tail; make a high sequence arrive first; replace a relay holding the only undelivered material. | Subscription/serving cut and retained-tail coverage, not current tree leaves, define who is owed data. A new physical replica adds no duplicate logical contribution. Negative: declare current from highest received sequence. | Prior bounded membership study; live membership in this simulator is a gap. |
| B: streaming, skew and dynamic work | A hot join key expands output; a traversal revisits A→B→A; recursion briefly empties queues before new work appears. Lose an offset/output checkpoint boundary. | Application-specific coverage/fixpoint, exact pair ownership and coherent state/input/output recovery. Negative: independently salt both sides, equate quiet queues with completion, or checkpoint offsets without effects. Compare recomputation, selected materialization and legal partial aggregation. | Fixed reduction existing; general streaming/recursive binding is a gap. |
| B: shared computation loses its last safe owner | Several jobs reuse a result; one cancels, one needs an old source cut, and a cache provider disappears mid-transfer. | Consumer-specific retention commitments, stable content/version identity, actual backend retirement. Negative: one subscriber cancels everybody or an inventory advertisement releases the last source. | Lease primitive/isolated lineage probes; shared-job lifecycle is a gap. |
| C: disaster response cannot obtain capacity | Many shards share an AZ/provider/admin dependency; spare VMs cannot boot or authenticate; archive GET and PUT fail independently. | Separate running usable capacity from nominal reservation, readable prefix from resumed writes and restored redundancy. Negative: add replicas in the same failed dependency set or assume a returned node is current. | Prepared static authorities; failover/repair/authority protocols absent. |
| C: fragments survive without a complete cut | Only small SOS records escape; exporters die mid-object/inventory; old hidden quorum continues admitting. Later combine independently retained fragments. | Preserve useful evidence without claiming complete recovery, finality or new write authority. Negative: require a complete manifest before salvage, or interpret one local pause as a global stop. | Source histories only; no SOS/PITR implementation. |
| C: restore the bytes but lose ordering | Recover application pages without a completed reader's bound, an undecided output, or a cross-shard decision. Reconnect old lineage and delayed effects. | Application visibility, ordering constraints, source lineage and effect dedup horizon must all survive. Negative: timeout aborts possibly committed work, or fresh dedup state repeats old effects. | Isolated histories; distributed restoration is a gap. |

## Oracle traps found during this pass

The later replicated composition exposed two more traps. Agreement among consumers
does not establish that their shared answer is correct, or that a scored client
response follows a durable decision and installation. Those checks now accompany
quorum and full-state comparisons. A finite history can also end before an earlier
transaction decides: reconstructing snapshots only from completed outcomes then
misses a read that skipped its still-pending effect. The observer checks the read's
announced/fixed blockers at the time it answers, without waiting for later outcomes.

These are reproduced review findings handed to LEAD for changes in the owning
traffic binding. They are not additional simulator implementations here.

- **Scored completion without a decision.** With one point request offered at
  100,000 ns, inject `done(tx=1)` at the client at 102,000 ns and inspect at
  110,000 ns. The initial observer accepted a `traffic_response` without any
  `traffic_complete` or durable outcome. Every scored response needs preceding
  durable decision and installed effect coverage; checking only another event
  type leaves the performance denominator open to false success.
- **Durable journal before lost callback.** With one point request, power off
  `h0` when `shard0` durably writes a journal batch containing `fix` or `resolve`;
  power on at 1,000,000 ns and inspect at 4,000,000 ns. Both histories recovered
  and completed, but the initial observer reported missing installation or a
  wrong position/history because `traffic_transition` had not been emitted
  before the crash. Reconstruct durable facts and actual replay/application;
  a volatile callback trace is not the only evidence that state was recovered.
- **Uncharged initial dataset.** Before offers, widths 2 and 2,000 produced 3
  and 2,001 versioned keys on shard0 with the same 1,026 charged host bytes.
  Charge a declared representation or exclude that source residency explicitly
  from capacity claims. Likewise, varying a metadata coefficient only at batch
  flush cannot establish the cost of uncharged floor/read/recovery scans.

The first two are negative and positive controls for the observer itself. Exact
replay of an incorrect observer does not repair either. Historical examples here
identify the failing version of the boundary; LEAD owns the corrected checks and
final campaign source receipt.

## Generate combinations that can teach us something

Vary dimensions independently before choosing interactions:

- **Topology:** same host/cross core, distinct hosts in one AZ, MAN, asymmetric
  local+WAN, multiple regions/providers, edge origin, shared corridor, several
  shard roles on a VM. The local NIC abstraction cannot establish a 100 ns ring
  handoff or the 170/250 µs target; those need their own completion definition and
  calibrated local/remote service models.
- **Meaning/cardinality:** one/few/many origins × one/few/many destinations, with
  interchangeable copies versus distinct contributions and mandatory recipients
  versus alternative workers stated separately. A virtual source is not free
  creation of extra copies; a family of trees is not a complete delivery ledger.
- **Work shape:** payload/result ratio, selection density, hot-key skew, empty
  outputs, broad reads, broad possible effects, code/model warmth, packet/chunk/
  task grains, bursts and open-loop rates. A padded message does not simulate
  constructing, retaining and reading a large result.
- **Interference:** foreground local work, other producers/shards, scans,
  verification, repair, archive debt, source retention and completed-but-undrained
  outputs. Preserve per-class outcomes and response routes.
- **Incidents:** duration, affected fraction, warning time, directional/correlated
  loss, onset relative to causal boundaries, repeated flap, destruction versus
  inaccessibility, stale control data, and observer health.

Use the shared campaign combinator for achievable pairs, then explicitly retain
consequential triples: ACK loss × small packets × control pressure; late verifier
× large output × old-version retention; route rollover × receiver restart × lost
receipt; repair storm × archive outage × shared spare pool. Pairwise coverage does
not establish these interactions. Avoid blindly multiplying impossible cases.

Ramp offered rate, burst size and incident severity independently. Preserve all
observed levels: batching, retries and alignment can make response nonmonotonic,
so a binary-search threshold is not automatically meaningful. Probe around the
first observed failure, vary the drain horizon, and distinguish still-improving
progress from a stable repeated wait. A true cycle claim needs a witnessed wait
cycle or stronger proof, not merely another timeout.

A strategy search must first satisfy correctness and service obligations under
an explicit failure budget. Then compare useful throughput, tail/age measures,
resource work, network cost, collateral harm, recovery time and retained exposure.
Report the best-found Pareto alternatives, not a universal winner. Use independent
starting policies, held-out incident timelines/seeds and uncertain-cost ranges;
do not feed validation outcomes back into selection. A calibrated effect target
cannot be established by summing synthetic stage percentiles.

## Mining provenance

This map generalizes suggestions rather than selecting them as architecture:

- [Current remit U01–U26](../orbital-dissemination/SOURCE-SURVEY-REMIT.md): all
  cardinalities, read partitioning, fan-in batching, enhancement, heterogeneous
  edges, multiple origins, fast producer follower, shared roles, rings, route
  families, adaptation, sender selection, failure and late membership.
- [Network catalog N01–N80](../orbital-dissemination/SOURCE-SURVEY-NETWORK.md),
  [delivery D01–D40](../orbital-dissemination/SOURCE-SURVEY-DELIVERY.md) and
  [read/execution W01–W51](../orbital-dissemination/SOURCE-SURVEY-WORKLOADS.md):
  general source inventory, distinct mechanisms, evidence and gaps. The
  [catalog](../orbital-dissemination/CATALOG.md) owns the full 223-entry survey.
- [Edge economics](../orbital-dissemination/EDGE.md), [route/sender
  comparisons](../orbital-dissemination/ADAPTATION.md), [delivery
  obligations](../orbital-dissemination/DELIVERY.md) and [read
  scale-out](../orbital-dissemination/READS-AND-EXTENSIONS.md): prior counterexamples,
  not inherited models or current provider prices.
- [Dataflow's 19 families](../orbital-dataflow/WORKLOADS.md),
  [resource cycles](../orbital-dataflow/RESOURCES.md),
  [progress/recovery](../orbital-dataflow/PROGRESS.md) and
  [Spark-like jobs](../orbital-dataflow/SPARK-STYLE.md): job, transaction, result,
  replay and resident-resource lifetimes remain distinct.
- [Recovery scenarios S01–S26](../orbital-scenarios/recovery/SCENARIOS.md),
  [detection](../orbital-scenarios/recovery/DETECTION.md),
  [locality](../orbital-scenarios/reconsideration/LOCALITY.md), current
  [BRIEF](../../../orbital/BRIEF.md), [PHYSICAL](../../../orbital/PHYSICAL.md) and
  [MINING](../../../orbital/MINING.md): current obligations, older traps and
  physical limits. Prior spikes supply hypotheses; their passed checks do not
  establish the new simulator's integrated behavior.
