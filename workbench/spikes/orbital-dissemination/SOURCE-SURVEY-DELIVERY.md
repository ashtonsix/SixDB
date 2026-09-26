# Recorded delivery and recovery ideas

2026-09-26. This is the delivery/recovery part of the dissemination source
survey, with stable `D01`–`D40` identifiers for the owning catalog. Each entry
explores a recorded idea, the condition that makes it useful, and a failure or
cost that limits it. **Analytical exploration is not a measurement.** Neither
the historical designs nor this spike silently establish SixDB guarantees.
Calico and Consurgent are idea and evidence sources, not inherited contracts.

## Sources inspected

| Source | Inspected scope and interpretation |
| --- | --- |
| [Orbital MINING](../../../orbital/MINING.md#failure-recovery-and-retained-evidence) | Failure/recovery pointers and their stated evidence limits. Older Orbital consensus drafts are inventoried by the networking survey. |
| Recovery [README](../orbital-scenarios/recovery/README.md), [DETECTION](../orbital-scenarios/recovery/DETECTION.md) | Recovery scope; detection, evidence, repair recipes, correlated failures and resource reserves. |
| Recovery [QUORUM](../orbital-scenarios/recovery/QUORUM.md) | Vulnerability after loss, counterexamples, protection predicates, relocation and recovery-state requirements. |
| Recovery [HANDOFF](../orbital-scenarios/recovery/HANDOFF.md) | Any-witness ingress, protocol/transport separation, terminal transfer, interruption histories, constraint frontiers and limits. |
| Recovery [SOS](../orbital-scenarios/recovery/SOS.md) | Local pause, bounded raw export, partial inventories, provenance and recovery limits. |
| Recovery [PITR](../orbital-scenarios/recovery/PITR.md) | Recovery cuts and closure, reopening, dependency retention, lineage/effects, peer recovery and RPO/RTO. |
| Recovery [SCENARIOS](../orbital-scenarios/recovery/SCENARIOS.md) | All authored scenarios S1–S26; mapping below. Historical incident links were not independently re-researched for this catalog. |
| Calico [omachine CONTRACT](../../../../calico/omachine/CONTRACT.md) | Actor/instance identity, locator and trust interpretation; directed connections, epochs, half-close and physical trunks. Its exact identifier layout is not proposed here. |
| Calico [xmem DESIGN](../../../../calico/xmem/DESIGN.md) | Message references/claims, durability/visibility, fencing, egress, certificate dissemination, enrollment cuts, retirement pins and late refill identity. Network transport details also belong to the networking survey. |
| Calico [fold.h](../../../../calico/xmem/include/xmem/fold.h), [test_fold.cpp](../../../../calico/xmem/test/test_fold.cpp) | Egress accept rule, canonical numbering, boundary counter restoration, replay-edit delivery limit; total-resend and lane-isolation checks. This is source inspection, not a fresh Calico build. |
| Calico [wire.h](../../../../calico/xmem/include/xmem/wire.h), [wireauth.h](../../../../calico/xmem/include/xmem/wireauth.h) | ACK identity/status/config binding, custody-range checks and retained verification receipts; not an audit of the complete transport/security implementation. |
| Calico [physical reopen ledger](../../../../calico/workbench/science/systems/recovery/XMEM_REGION_OPEN.md), [test_persistent_region.cpp](../../../../calico/xmem/test/test_persistent_region.cpp) | Explicit bootstrap/tail-discovery gaps versus existing local manifest/fencing primitive and its authored tests. |
| Consurgent [design2 delivery topology](../../../../consurgent/archive/notes-v0/design2.md#delivery-topology), [design delivery topology](../../../../consurgent/archive/notes-v0/design.md#delivery-topology) | The historical M-of-N/anycast rejection and deterministic-fallback suggestion. The claimed sufficient condition needs qualification in D05. |

Evidence shorthand below refers to owning material, not assurance levels:

- **Ledger**: [delivery.py](delivery.py) and [26 authored checks](check_delivery.py),
  using trusted admission, durability, snapshot and ownership facts.
- **Join network**: [membership_scenario.py](membership_scenario.py) and
  [five checks](check_membership_scenario.py), with finite modeled queues,
  persistence, timers, in-flight joins and reply-path loss.
- **Write network**: [experiments.py](experiments.py), a resource/latency model
  with explicit payload/journal/effect boundaries, not a consensus implementation.
- **Proposal timing**: [PROPOSAL-TIMING.md](PROPOSAL-TIMING.md), bounded local
  receipt observations and received warnings selecting future proposal timing;
  it does not diagnose failure causes or change old journal positions.
- **Enhancement**: [ENHANCEMENT.md](ENHANCEMENT.md), exact selection validity and
  four network-backed policies over ten bounded workloads.
- **Source probe**: a bounded probe described by the recovery owning document;
  its trusted facts and limits remain those of that source. This survey did not
  rerun or promote it into transport evidence.

## Identity, obligations and fan-in/out

### D01 — Stable logical identity, mutable location, distinct incarnation

[omachine](../../../../calico/omachine/CONTRACT.md) separates an actor from its
materialized instances and locators; the [reopen ledger](../../../../calico/workbench/science/systems/recovery/XMEM_REGION_OPEN.md)
adds durable incarnation fencing. This permits movement and multihoming without
reissuing logical work. A reused VM name, IP, socket or blank store cannot restore
dedup/authority; trust-domain policy also cannot be inferred solely from location.
**Evidence:** Ledger checks stale-incarnation rejection. Locator migration,
authorization changes and a real persistent identity allocator remain unmodeled.

### D02 — Connection lifetime need not equal socket lifetime

[omachine connection epochs](../../../../calico/omachine/CONTRACT.md) distinguish
physical reconnection from deliberately abandoning a backlog; either side can
close while an open receiver drains in-flight events. This avoids replaying or
discarding work on every path failure. Conversely, a new epoch is a semantic
decision: silently bumping it on reconnect loses obligations; half-close needs a
defined last accepted range and retirement condition. **Evidence:** Ledger keeps
route and obligation identity separate; explicit half-close/abandonment is a gap.

### D03 — Duplicate identity must bind the same content and scope

The [reopen ledger](../../../../calico/workbench/science/systems/recovery/XMEM_REGION_OPEN.md)
records exact-retry idempotence versus conflicting duplicate bodies. Scoped
keys allow retries and multipath races without duplicate effects. A producer-only
key can conflate recipients, and a restarted counter can collide with old work;
accepting a different payload under an existing key hides corruption.
**Evidence:** Ledger checks payload, lineage and recipient isolation. Wire-level
authenticated conflict handling and durable metadata reconstruction are gaps.

### D04 — Dense per-recipient sequences bound dedup state but create gaps

[fold.h](../../../../calico/xmem/include/xmem/fold.h) accepts exactly `wm+1`, assigns
egress numbers in canonical rank order and restores counters at a certified
boundary. This supports compact dedup and independent lanes. Accept-if-greater
loses predecessors; refusing reordered successors needs buffering or eventual
re-offer and can amplify work quadratically in a reverse-order full resend.
**Evidence:** Calico's [tests](../../../../calico/xmem/test/test_fold.cpp) explicitly
use in-process calls, not blocking network channels. Ledger tests reordering;
bounded reorder-buffer versus selective repair performance remains open.

### D05 — Sender preference and recipient choice are different problems

[xmem egress](../../../../calico/xmem/DESIGN.md) suppresses duplicate senders;
[Consurgent](../../../../consurgent/archive/notes-v0/design2.md#delivery-topology)
considers fixed recipients, shared-state replicas and deterministic fallback.
Deterministic/rendezvous sender preference can reduce traffic without granting
exclusive effect authority. **Counterexample:** A executes, its reply is lost,
then deterministic fallback B executes using independent dedup state. Determinism
alone does not bound delivery to M distinct workers. Shared effect state, valid
fencing/transfer, or proof A cannot execute must resolve that ambiguity.
**Evidence:** Ledger covers sender disagreement and sink ambiguity; no distributed
sender election or arbitrary M-of-N effect protocol is implemented.

### D06 — Share a physical trunk while preserving logical recipient duties

[omachine](../../../../calico/omachine/CONTRACT.md) lets several logical connections
share one transmission until paths branch. This saves network/packet work when
payloads and validity are common. A trunk ACK cannot imply all leaves accepted;
a failed branch still owes its own recipients. Different cuts, encodings or
access rights can prevent sharing. **Evidence:** Write routing compares shared
dissemination; Ledger distinguishes required recipients. Per-subtree durable
repair responsibility and authorization-aware trunk sharing remain open.

### D07 — References and retained claims can replace repeated payload copies

[xmem message references](../../../../calico/xmem/DESIGN.md) and
[PITR retained closure](../orbital-scenarios/recovery/PITR.md) suggest transmitting
immutable identity plus permission/lifetime information. This helps resident
consumers and repeated shared inputs. A reference to evicted bytes creates a
fetch dependency; a digest supplies neither authorization nor retention, and a
physical address can name new content after reuse. **Evidence:** Enhancement
checks version/domain/mapping bindings and missing references, charging absent
row data. Distributed claims, GC handoff and zero-copy pin cost are gaps.

### D08 — Enrollment/subscription cuts close joins missed by old routes

[xmem enrollment cuts](../../../../calico/xmem/DESIGN.md) separate durable input
from selected membership; [HANDOFF](../orbital-scenarios/recovery/HANDOFF.md)
requires registration closure. A snapshot at a bound cut plus retained tail
makes a new subscriber independent of old-tree coverage. Snapshot races, lost
install ACKs and exhausted tail retention must not become silent gaps. An atomic
snapshot capture is one model, not the only mechanism. **Evidence:** Ledger and
Join network cover omitted joiners, partition/crash and finite retention;
multi-stream registration/authority cuts remain open.

### D09 — Early bytes and computation can overlap without early authority

[QUORUM](../orbital-scenarios/recovery/QUORUM.md) distinguishes persistence from
admission and allows early dissemination. This can overlap payload transfer,
journal work and useful relay computation. Every choosing quorum still needs the
required independent payload/history evidence; a bare tentative journal cannot
be relabeled eligible after recovery. Different role placements and integrated
payload journals may realize different overlap. **Evidence:** Ledger checks the
conditional candidate and interrupted writes; Write network compares timing.
Proposal timing also compares fixed and locally observed future scheduling.
None establishes composed consensus safety or a universal placement.

### D10 — Active transmission windows and historical retention have different gates

[HANDOFF](../orbital-scenarios/recovery/HANDOFF.md) separates bounded active slots
from lagging replica catchup. Releasing active capacity on sufficient evidence
can preserve ordinary quorum progress while retaining repair bytes elsewhere.
Holding every slot for every follower stalls a healthy quorum; releasing all
history at active-slot completion prevents recovery. A tentative missing-payload
slot also blocks a strict admission prefix, including ready streams behind it.
**Evidence:** Write network exposes that prefix stall; Ledger preserves gaps.
Proposal timing preserves the hole after a switch to strict scheduling and tests
small unanswered-receipt windows. Such a window does not bound stall duration;
other postponed-allocation/retention policies need their own protocol and study.

### D11 — Frontier aggregation must preserve what each number covers

[PITR reopening](../orbital-scenarios/recovery/PITR.md) and
[xmem retirement](../../../../calico/xmem/DESIGN.md) motivate compact monotone
progress reports. Combine duplicate reports for the same stream with `max`;
release all-required retention at their `min`. Different streams, cuts, domains
and membership cannot share an unqualified maximum, and a missing fragment is
not an empty result. **Evidence:** Ledger tests frontier permutations and slowest
recipient retention; Enhancement checks exact empty coverage. Network-backed
hierarchical frontier batching and membership changes remain open.

### D12 — Old evidence can remain valid after old authority expires

[HANDOFF](../orbital-scenarios/recovery/HANDOFF.md) preserves exact previously
chosen outcomes under their original configuration. This lets delayed replies
complete work after leader movement without an extra new decision. Rejecting all
old terms loses valid history; accepting an old term for new work violates the
boundary. Partial certificates cannot mix ballots or prefixes. **Evidence:**
Ledger separates logical lineage/route/incarnation; recovery source probes cover
historical authority examples. Full authenticated protocol delivery is a gap.

## Detection, protection and repair capacity

### D13 — Diagnose slow operations at their actual completion boundary

[DETECTION](../orbital-scenarios/recovery/DETECTION.md) records queue/send/PLP
progress, direction, load and observer age. This can distinguish a paused
observer, asymmetric path, responsive admin process and wedged durable storage.
Small probes can succeed while data fails; timeout samples are censored, and a
delay-rarity score is not a probability of death. **Evidence:** Network faults
exercise CPU/link/device paths; Proposal timing models a bounded payload-receipt
watchdog, actual replies, false timeouts and received warnings. It does not
diagnose the cause or predict faults. Operation-specific diagnosis, probe size
and calibrated sparse-history detection remain analytical.

### D14 — Coalesce incident probes across shards, preserving authority boundaries

[DETECTION](../orbital-scenarios/recovery/DETECTION.md) proposes piggybacked
progress plus bounded quiet-shard probes and incident-level coalescing. This
reduces a many-shard monitoring storm and lets evidence trigger cheap protective
actions. Shared observations can be stale or describe a different route/operation;
coalescing cannot merge consensus authority. **Evidence:** Shared-resource write
fixtures model interference, not this control loop. Probe fan-in, freshness and
per-incident repair suppression need a dedicated comparison.

### D15 — Count reachable votes, recoverable bytes and restart authority separately

[QUORUM](../orbital-scenarios/recovery/QUORUM.md) distinguishes availability now,
history surviving a named fault and authority to resume. This prevents healthy
consumers or payload copies from being mistaken for admission recovery. A fifth
regional witness may leave every deciding quorum regional; a copied checkpoint
can coexist with an unknown chosen suffix. **Evidence:** Conditional Ledger
checks reject bare-journal quorums. General deciding-set/placement enumeration
and correlated-loss recovery are not established by the latency simulator.

### D16 — Independence belongs to actual evidence and failure domains

[DETECTION](../orbital-scenarios/recovery/DETECTION.md) counts complete recovery
recipes, not VM labels: host, disk, AZ, region, provider, credentials, rollout and
shared dependencies may correlate. Diverse copies/backups can reduce exposure
when the declared domains are useful. A forwarded receipt is still one copy;
separate AZ names do not remove shared KMS/control failure. **Evidence:** Ledger
checks sender backup diversity only. Domain-level storage loss, correlated
control failure and recipe eligibility remain gaps, not inferred protection.

### D17 — New-work protection does not repair historical holes

[QUORUM](../orbital-scenarios/recovery/QUORUM.md) and
[DETECTION](../orbital-scenarios/recovery/DETECTION.md) distinguish stronger future
receipts from repairing older history and its dictionaries/payloads. A policy
change protects new traffic sooner while background work repairs the past.
Claiming complete coverage at activation overlooks old gaps; copying only popular
cache contents can miss unique replay inputs. **Evidence:** Join network retains
an exact single-stream tail. Historical coverage maps, policy boundaries and
dependency-aware evacuation remain analytical.

### D18 — Prioritize endangered obligations and measure net catchup capacity

[DETECTION](../orbital-scenarios/recovery/DETECTION.md) considers weakest coverage
first and actual catchup rates. This spends finite bandwidth where it can remove
the most immediate loss exposure. If arrival rate is at least repair service,
the tail cannot drain; improving bytes/second alone may delay the tiny authority
record needed to make copied data usable. **Evidence:** Join network measures
retention/backlog/refusal under shared service. Priority classes, multi-source
repair allocation and protection-time objectives are not simulated.

### D19 — A warm replica is useful only if its dependencies also work

[DETECTION](../orbital-scenarios/recovery/DETECTION.md) and
[SCENARIOS S9](../orbital-scenarios/recovery/SCENARIOS.md) distinguish an already
running diversified spare from an instance requiring the failed image/KMS/IAM or
control service. Prepared capacity can shorten repair initiation and reduce
bootstrap risk, at recurring cost. A nominal spare can still lack persistence,
keys, executable code or history. **Evidence:** Current topology nodes start
available; dynamic provisioning/dependency readiness and pool economics are gaps.

### D20 — Reserve an executable decision path, not merely spare buffer bytes

[PITR reopening](../orbital-scenarios/recovery/PITR.md) and
[DETECTION](../orbital-scenarios/recovery/DETECTION.md) require control metadata,
inputs, keys, CPU, persistence and message service that blocked application work
cannot consume entirely. This can break resource dependency cycles and allow
reclamation. Reservations alone do not resolve an unknown authority decision;
over-reservation also reduces foreground throughput. **Evidence:** The shared
network models finite resource queues, but not a cyclic application/decision
dependency graph or reserved-class scheduler.

### D21 — Protection and return-to-normal can use asymmetric hysteresis

[DETECTION](../orbital-scenarios/recovery/DETECTION.md) proposes quick protective
copies, slower contraction, jitter and exposure/outage metrics. This avoids
repeated evacuation on noisy paths and preserves useful speculative copies.
Recovered latency is not restored durable coverage; never shrinking wastes
money and storage. **Evidence:** Proposal timing measures the latency/work cost
of fixed cooldowns, useful/expired/false warnings and missing replies when
selecting future proposals. It does not promote/shrink copies or estimate a
forecast's accuracy. A placement/protection controller would additionally need
coverage, copy cost and restored-redundancy evidence.

### D22 — Accept stable submissions at any witness without making it a leader

[HANDOFF ingress](../orbital-scenarios/recovery/HANDOFF.md) permits bounded
retention/forwarding of producer frontiers and ballot/config-scoped leader hints.
It can remove leader-discovery round trips and enable ring-style return paths.
An inbox receipt is not admission; stale hints need loop/attempt bounds and
retention credits. **Evidence:** Ledger stable identities and ring/read analyses
cover the semantic separation. Network-backed stale-hint forwarding, deduplicated
ingress and overload across shared witness inboxes are gaps.

### D23 — Pre-copy can shorten handoff, but the named authority boundary still matters

[HANDOFF](../orbital-scenarios/recovery/HANDOFF.md) separates background copying,
terminal choice, target initialization and restored redundancy. This overlaps
bulk work with old service and can avoid another old-region trip after evidence
escapes. A copied prefix without terminal evidence cannot activate a successor;
after a terminal names a failed target, the old authority cannot freely retarget.
**Evidence:** Source probe explores interruptions. There is no timed
reconfiguration/certificate-escape simulation in this spike.

### D24 — Retire sources only against complete, scoped replacement evidence

[HANDOFF retirement](../orbital-scenarios/recovery/HANDOFF.md),
[SOS](../orbital-scenarios/recovery/SOS.md) and [xmem pins](../../../../calico/xmem/DESIGN.md)
separate copied fragments from discharged obligations. This permits incremental
transfer without prematurely destroying recovery state. Partial ACKs, half
initialized successors or a disconnected pin-holder can block reclamation;
partition-safe retention can therefore exhaust space. **Evidence:** Ledger stale
retry floors and Join network credits cover a small case. Distributed GC,
long-lived pins, ejection authority and interrupted source deletion remain open.

## Recovery, rescue and external effects

### D25 — Discover unknown durable tails; known-ID lookup is insufficient

[MINING](../../../orbital/MINING.md#failure-recovery-and-retained-evidence) and
the [reopen ledger](../../../../calico/workbench/science/systems/recovery/XMEM_REGION_OPEN.md)
require bounded enumeration beyond a certified base, including accepted but
unlearned/unadmitted work. This prevents recovery from erasing outcomes whose
replies were lost. A known transaction lookup cannot discover an unknown suffix;
arbitrary older-media corruption is not necessarily an unacknowledged torn append.
**Evidence:** Ledger interrupted-write facts are assumed. Real log discovery,
media corruption and completeness proofs remain gaps.

### D26 — Recovery must retain completed readers' constraints as well as pending work

[PITR's reopening frontier](../orbital-scenarios/recovery/PITR.md) gives a concrete
cycle when a completed observation bound is dropped. Retaining exact bounds or
sufficient monotone floors lets independent scopes reopen without finishing every
program. Pending-only inventories, remapping without registration closure and
checkpointing only visible rows can lose these constraints. **Evidence:** Source
probes and the owning read analyses cover the reasoning. Ledger/frontier counters
are not a transaction-constraint recovery implementation.

### D27 — A restart cut and a completed historical view carry different obligations

[PITR](../orbital-scenarios/recovery/PITR.md) distinguishes a cut retaining channel
state, pending decisions and continuations from a fully materialized historical
answer. This may reopen service earlier or avoid globally pausing shards. Taking
the maximum/minimum shard frontier can omit a cross-shard decision, and later
protocol records may be needed to resolve earlier work. **Evidence:** Join network
models one snapshot/tail stream only. Distributed channel snapshots and coherent
multi-shard PITR cuts remain unmodeled.

### D28 — Recovery closure includes representation and executable dependencies

[PITR](../orbital-scenarios/recovery/PITR.md) enumerates code, ABI/runtime,
dictionaries, bases/deltas, captured external facts, approved results,
continuations and authorized keys. Preserving this closure makes compact or
derived network representations reconstructible after their original producer
dies. Hash-valid payload bytes may be useless without meaning/decryption; old
code cannot be collected merely because a deployment changed. **Evidence:**
Enhancement checks exact selection state/mapping; full executable/codec/key
closure and its transfer/retention cost remain gaps.

### D29 — PITR lineage fencing and external effect identity need different treatment

[PITR](../orbital-scenarios/recovery/PITR.md) gives a new branch explicit ancestry
and rejects unsolicited old-lineage mutations. This separates recovered history
from new work. A new lineage ID alone cannot fence an external sink; renaming all
inherited effect IDs can reissue old payments/messages. Preserve inherited IDs
for reconciliation/dedup, give genuinely new work new IDs, and explicitly
resume/rewind/reconcile connectors. **Evidence:** Ledger rejects stale lineage
and models sink ambiguity; branch/connector recovery is not implemented.

### D30 — Safe retry depends on the sink's atomic boundary

[PITR](../orbital-scenarios/recovery/PITR.md) preserves connector/outbox and effect
history; [DELIVERY](DELIVERY.md#retry-cannot-complete-every-external-action-safely)
explores the gap. Recording an effect marker before a nontransactional action
can omit the action after a crash; recording it after can duplicate it. Atomic
sink idempotency, a transactional effect ledger or outcome reconciliation can
resolve particular cases at added latency/state cost. **Evidence:** Ledger
executes both counterexamples and the idempotent-sink case, not a general
exactly-once network guarantee.

### D31 — SOS can preserve evidence before a complete recovery plan exists

[SOS](../orbital-scenarios/recovery/SOS.md) locally pauses ordinary admission and
exports raw fragments without waiting for a quorum, complete snapshot, manifest,
interpretation or application drain. This helps when only a brief escape window
remains. Local cessation cannot prove a hidden quorum stopped; incomplete scans
do not prove absent records, and pending outcomes cannot become forced aborts.
**Evidence:** Source probe includes partial rescue histories. The network model
does not simulate rescue scheduling, interrupted inventory or hidden authorities.

### D32 — Incremental rescue can combine complementary destinations and provenance

[SOS](../orbital-scenarios/recovery/SOS.md) prioritizes endangered unique material,
small authority facts and useful bulk concurrently over any preauthorized surviving
path. Different destinations can collectively retain closure. Requiring the
ideal destination first may lose everything; choosing the newest timestamp or
largest frontier discards conflicting evidence. No fragment receipt releases the
whole source. **Evidence:** Dissemination compares paths, but not rescue objective
functions, provenance inventories, complementary fragments or dependency closure.

### D33 — Immutable peer recovery can bypass an impaired origin service

[PITR peer recovery](../orbital-scenarios/recovery/PITR.md) allows exact authenticated
chunks from consumers, producers, logs or archives. This can improve RTO and use
cheap/local links. Peer “latest” is not a trusted recovery cut; exact version,
length, digest and codec dependencies matter, as do source access rights.
**Evidence:** Enhancement charges a cold relay/consumer and rejects stale input;
general multi-source chunk fetch, corruption repair and independent recovery
source selection remain open.

### D34 — Discovery and authorization need usable bootstrap paths too

[PITR](../orbital-scenarios/recovery/PITR.md) and [SCENARIOS S9/S17](../orbital-scenarios/recovery/SCENARIOS.md)
consider bounded inventory/directories and credentials outside the failed
DNS/blob/KMS/IAM path. Prepared discovery can make intact remote copies usable.
Knowing a content hash supplies neither location nor authorization; copying
ciphertext alone may not reduce outage. **Evidence:** Simulator links have no
authentication/bootstrap dependency graph. This is analytically explored,
without claiming that replica placement alone solves it.

### D35 — Counted recovery copies need retention; opportunistic caches do not

[PITR](../orbital-scenarios/recovery/PITR.md) requires actual persistent coverage
receipts, retention horizons and replacement before releasing counted copies.
This lets peer storage contribute to protection when it accepts the obligation.
Stale advertisements and cache eviction can invalidate a presumed recipe;
requiring all caches to pin everything destroys the cache economics.
**Evidence:** Join network uses explicit retention/ACK credits. Variable cache
residency, durable pin receipts and distributed eviction decisions remain gaps.

### D36 — Recovery traffic and archive debt need bounded, separate control

[PITR](../orbital-scenarios/recovery/PITR.md) proposes request coalescing, resumable
verified chunks, independent sources and control reserves. These reduce repair
amplification and protect foreground progress. GET and PUT can fail separately;
continued writes while archive PUT is unavailable accumulate protection debt,
and evacuating every cache at once can worsen overload. **Evidence:** Join network
has bounded snapshot/repair attempts, competing traffic and refusal. Archive
debt, shared-request coalescing and multi-tenant recovery scheduling are gaps.

### D37 — Report recovery milestones and critical paths, not a single “recovered” bit

[PITR RPO/RTO](../orbital-scenarios/recovery/PITR.md) distinguishes complete
recoverable cuts, read-only readiness, writable authority and restored redundancy.
This makes partial success useful and reveals discovery/key/decision bottlenecks.
The newest object is not necessarily the RPO; summed stage percentiles do not
give end-to-end tail latency, and catchup cannot finish if service fails to exceed
ongoing arrivals. **Evidence:** Join network reports cutoff/drained readiness;
Write network reports distinct boundaries. Full disaster RPO/RTO remains absent.

### D38 — Byte integrity does not prove application correctness

[SCENARIOS S21](../orbital-scenarios/recovery/SCENARIOS.md) considers logically bad
code replicated with matching hashes; [PITR](../orbital-scenarios/recovery/PITR.md)
retains checked inputs/results. Exact identities help verify which computation
ran and support replay/reconciliation. They do not show that its predicate,
program or user intent was correct. **Evidence:** Enhancement uses an authored
truth predicate and binding checks; arbitrary relay execution still needs its
own sandbox/determinism/verification story and publication gate.

### D39 — Bootstrap publication must follow bytes and preserve fencing

The [reopen ledger](../../../../calico/workbench/science/systems/recovery/XMEM_REGION_OPEN.md)
separates an existing local manifest from missing composed region recovery.
Ordering referenced bytes before an atomic root publication permits bounded
restart discovery. Falling back to an older valid incarnation slot can reuse a
fence; treating corruption as an empty region can overwrite history.
**Evidence:** The inspected [Calico test](../../../../calico/xmem/test/test_persistent_region.cpp)
checks local incarnation 1→2→3 and corruption/identity refusals. It does not
establish SixDB multi-node reopen or self-discovering witness tails.

### D40 — Disseminate reusable evidence without widening what it proves

[wireauth](../../../../calico/xmem/include/xmem/wireauth.h) binds ACK identity,
content, status and configuration, checks custody ranges/generations, and caches
verification receipts after successful verification. [xmem DESIGN](../../../../calico/xmem/DESIGN.md)
also explores certificate dissemination. Reusing exact verified evidence can
reduce repeated public-key work and coordinator dependence. A hash or trusted
verification receipt is not a new independent witness; cached validation must
not survive changed bytes/configuration. **Evidence:** Code inspected only;
crypto/receipt cache cost, certificate batching and verification-aware routing
are not measured by this spike.

## Coverage of the recorded recovery scenario family

This mapping accounts for each authored scenario rather than treating the source
list as exploration. It identifies the analytical entries above; it does not
claim the present simulator ran all scenarios.

| Recorded scenario | Exploration |
| --- | --- |
| S1 observer pause; S2 asymmetric/data-size path; S3 admin healthy/storage wedged | D13–D14; bounded proposal watchdog exists, cause diagnosis and calibrated detection remain gaps. |
| S4 old leader returns | D01, D12, D23; ledger/source-probe boundaries, no full protocol. |
| S5 one witness lost; S6 lone checkpoint/payload | D15–D18, D25; available votes differ from recovery closure. |
| S7 interrupted evacuation; S8 witnesses escape but payload/dictionary trapped | D17–D18, D23–D24, D28; transfer milestones and dependency holes. |
| S9 cold-spare bootstrap failure | D19, D34; nominal capacity versus usable dependencies. |
| S10 GET/PUT/regional-path failures | D33–D36; peer fetch and archive debt are separate. |
| S11 shared-shard repair storm; S12 decision/resource cycle | D14, D18, D20, D36; shared queues exist, control loop/cycle does not. |
| S13 nongeographic common failure; S14 unsafe shrink after recovery | D16, D21; domain model and coverage-based hysteresis. |
| S15 fragmented mixed-version cache; S16 last cache evicts | D07, D28, D33, D35; exact closure and real retention. |
| S17 ciphertext/keys; S18 old archive/hidden tail | D25, D28, D34, D37; intact bytes versus usable complete recovery. |
| S19 participant before decision | D26–D27; restart versus completed-view cuts. |
| S20 old/new lineage and external retry | D29–D30; stable inherited effect IDs and sink ambiguity. |
| S21 logically wrong program; S22 completed-observer bound lost | D26, D38; neither matching hashes nor pending-only inventories suffice. |
| S23 tiny SOS escape; S24 complementary exports; S25 interrupted inventory; S26 local pause/hidden quorum | D31–D32; preserve partial evidence without manufacturing a final frontier or authority. |

The measured delivery contribution is deliberately smaller than this catalog:
trusted-fact ledger histories, a network-backed single-stream join/retention
scenario, resource-model writes and observed proposal timing, and exact-selection
enhancement.
The remaining entries have explicit conditions and counterexamples above.
They identify useful further experiments and composition questions for Orbital
LEAD; they do not prescribe an implementation sequence or selected architecture.
