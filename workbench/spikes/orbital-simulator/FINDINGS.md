# Findings

## Moving the laboratory into its maintained home

The final boundary probe adds actual checkpoint bytes, durable root discovery,
paged enumeration and deletion, then delays an old version's first access until
after reclamation and restart. [The retained comparison](RETIREMENT-PROBE.md)
covers 30 histories and nine targeted tests. Its deliberate errors distinguish
premature checkpoint publication, losing either of two live roots, and freeing
a frame while one of two backend users still holds it. Whole-prefix scanning
stalls at a budget where bounded listing completes.

Together with the checked old-cut composition below, this is enough evidence to
begin the [maintained reference simulator](../../simulator/README.md). It supports
separating actor/process/host identity, logical retention roots, resident buffers,
storage operations, protocol policy and independent observations. It does not
settle distributed GC, authority replacement or completion guarantees under
exhausted resources. The spike remains available for focused experiments when
the reference implementation encounters an uncertainty; migration is not a
requirement to answer every Orbital design question here first.

## Exercising the proposed simulator boundaries

The [checked old-cut case](demanding_case.py) now joins actual transaction position
assignment, agreed read coverage, two recoverable checker invocations, private
queries and checked publication. The same program runs through standalone and
prepared-quorum admission. Checkers obtain source values themselves; the harness
neither supplies a cut nor transfers snapshot values between components.

[Retained evidence](evidence/checked-composition.json) covers **72 histories**:
66 combinations over seeds 1/7/19 and six longer continuing-write runs. All 1,080
foreground writes complete. Of 72 checked transactions, 54 complete and eighteen
remain unfinished: six with a missing required checker, six with disagreeing
reports, and six retaining too many input buffers at the tighter budget. No
evaluator errors, reported safety violations or missing incident milestones occur.
The captured source passes **190 tests**, including deliberately incorrect controls.

The longer runs continue writing during the delayed checker's reads. Its first
query follows nine or ten completed source writes, with another 22 or 23 completing
later. It still obtains the original cut. Checker process restart, source device
reset and coordinator failure after durable verification or outcome but before
its callback also complete. These are selected failure boundaries under static
authority, not arbitrary process recovery or authority replacement.

The exercise changed several simulator boundaries for concrete reasons:

| Difficulty | Change and remaining lesson |
| --- | --- |
| The existing read eagerly captured values and journaled the query. | Register complete coverage through the agreed fold, then use a shared, nonmutating observation function for private queries. Otherwise a supposed late first read tests prepared input, or unchecked query choices enter agreed history. |
| Builder placement coupled coordinator failure to leader failure. | Explicit role placement, physical hosts/links and factories before boot allow independent incidents and actual shared-resource comparisons. Defaults retain the old traces. |
| Faults and foreground arrivals relied on guessed absolute times. | Event-relative scenario steps start from actual context or durable-report milestones. Unfired milestones and untriggered input declarations remain visible. |
| Recovery could find matching reports alongside a different outcome. | Bind the durable decision to the exact checked position, observations and effects; revalidate that binding on recovery. |
| Correct final snapshots hid a skipped, still-undecided predecessor. | Check private reads against pending-output intervals even when the predecessor has no outcome by the observation deadline. A deliberately broken fold lets the checked transaction complete; the observer now rejects it. |

At an authored 8 KiB checker budget, retaining every input projection prevents
completion; consuming and releasing projections completes the same transform.
At 12 KiB both policies complete. Their peak modeled resident usage is about
4.4 KB versus 10.6 KB when allowed to finish. This separates a logical snapshot's
lifetime from its resident input buffers. It does not establish production sizing
or imply that every application can consume inputs in this order.

The awkward parts are also results. The checked coordinator still interposes on
its base class's phase names and recovery records. The scenario builder still
knows specific source and checker roles. Values come from logical retained history;
synthetic input projections do not exercise physical object reconstruction or COW.
There is no reclamation, replacement decision authority or agreed abort. A known
mismatch safely prevents publication but leaves an announced obligation unresolved,
so that case is incomplete failure handling. Checkers also retransmit completed
reports without a retirement acknowledgement; idle traffic depends on the horizon.
These histories therefore establish neither sustainable capacity nor idle wire
efficiency. [RETENTION.md](RETENTION.md) owns the remaining composition questions.

All runs and the regression suite executed from the immutable capture
`build/captures/orbital-checked-composition`, verified as
`23a4933c6a3829be7aac21928208fe2809327405839ee57ce4e4470dcb438564`.
The evidence identifies every case, source hash, trace hash and full receipt.
Historical captures below remain separate comparisons.

## Corrected contention, replicated epochs and a simpler release rule

This iteration tests the brief's actual reservation lifecycle, composes it with
prepared quorum admission, and supports the local-release simplification now
adopted in [BRIEF](../../../orbital/BRIEF.md). The earlier
traffic results below belong to their captured historical model; they are not
performance evidence for the corrected protocol. All times and byte budgets in
these simulations remain authored costs, not measured SixDB performance.

The four final experiment sets retain **204 histories**: 72 fidelity comparisons,
54 quorum-composition cases, 60 release-policy comparisons and 18 recovery-footprint
cases. All evaluators finish without reported safety violations; twelve offered
obligations remain unfinished in the intentionally failing capacity/recovery and
unresolved-predecessor cases. That capture's **163 tests passed**, including deliberately incorrect
variants and exact replay. Superseded runs and diagnostic traces remain in ignored
experiment storage; they are not added to those final-set counts.

The code capture `build/captures/orbital-contention-composition` verifies as
`f9c6685e10f97fd48b5f764a157cdf2eac3112abef7c68498eb3d5a6a6f5ad1e`.
Every final experiment's recorded implementation hashes match the retained code.

### Fixing fidelity changes the cost, while preserving locality

The historical traffic binding blocked reads behind partial reservations,
calculated minima during acquisition, let some conflicting waiters overtake, and
released each shard's reservation before all position acknowledgements returned.
These differences pulled in opposite directions; the model was not uniformly
conservative. The corrected binding journals waiting requests, prevents conflicting
overtaking, leaves reads moving during partial reservation, and announces minima
only after all reservations are held. Reads register bounds before waiting. The
strict baseline explicitly releases after all position acknowledgements.

[The matched comparison](evidence/fidelity-campaign.json) contains 72 histories,
36 old/current pairs over seeds 1/7/19. All 7,200 offers complete with no reported
violations. The table shows the range of each cohort's maximum across those seeds.

| Workload / cohort | Historical binding | Corrected strict binding |
| --- | ---: | ---: |
| WAN transaction, independent local points | 98.7–99.1 µs | 132.7–133.1 µs |
| WAN transaction, dependent local points | 322.8–324.0 ms | 484.9–486.3 ms |
| Broad WAN source read, narrow output: local points | 85.6–85.8 µs | 107.8–108.0 µs |
| Hot steady workload | 0.946–0.958 ms | 1.328–1.364 ms |
| Overlapping bridge: point work | 4.307–4.345 ms | 4.211–4.212 ms |

The corrected protocol preserves independent service, but its extra durable
transitions and round trips matter. The bridge comparison also shows why this is
not a scalar adjustment to old results. The 500-offer continuing stream completes
in both versions; retained modeled memory grows from roughly 377 KB to 556 KB.
Neither has reclamation, so this does not establish sustainable capacity.

Port-level [fidelity tests](test_contention_fidelity.py) check old/new-cut reads
during partial reservation, updated minima, overlapping queue chains with disjoint
bypass, interval narrowing and registered bounds across power loss. They also
delay actual acquisition/fix acknowledgements and cut power between journal
persistence and callbacks at each lifecycle stage.

### Agreed input now drives the same transaction fold

[The prepared quorum adapter](replicated_contention.py) obtains immutable,
hash-chained input batches from actual leader/follower PLP writes. Consumers need
matching individual receipts from the leader and a follower before persisting and
folding an epoch. Three consumers run at different physical speeds and compare
full logical state, including blocked requests/read continuations, and protocol
outputs. A waiting read does not prevent later independent epochs from finishing.
Destroyed consumers rebuild through actual witness reads and messages.

This binds admission, ordering, execution and installation together more closely
than the earlier separate probes. It uses full input bodies at witnesses, not the
producer-copy/frontier admission path. Positions and outcomes remain durable at
one coordinator; replacement decision authority, extension checks, object mapping,
elections and handoff are not established. These are finite correctness/progress
experiments, not a representative production throughput path.

The [54-history composition campaign](evidence/replicated-contention.json) accounts
for 270 offers: 261 complete and nine remain unfinished across six histories.
Three are the single-transaction capacity failure below; the other three retain a
long-running predecessor and its dependent transaction beyond observation, while
the independent shard completes. Every disruption/recovery case completes, and
unaffected-shard work finishes before restoration. Replica state and output hashes
agree at every compared prefix, with no lag remaining at the final observation.

The campaign also exposed a replication feedback loop. Stale acknowledgements
and ordinary progress repeatedly sent an already outstanding batch. Each follower
now has one outstanding epoch sent on ordinary progress; explicit timers handle
retries. The retained matched before/after receipts and core diff show MAN scan
traffic falling from 6.76–7.07 MB to 376–377 KB. All offers still complete. Latency
does not improve in every case, so the supported result is less redundant traffic,
not a universal speedup. This informs delivery implementation, not a new authority
or dissemination mechanism.

Independent review caught two observer holes. Replica agreement could hide a
fabricated client completion or an outcome inconsistent with installed effects.
Also, reconstructing snapshots only from completed outcomes missed a read that
skipped a predecessor which had not decided before observation ended. The observer
now checks durable decisions, actual installs and temporal pending-output gates,
with deliberately faulty controls for each. Equal replicas are not sufficient.

### Local release removes a round trip

[Local release](local_release.py) changes one step: applying `fix(c)` durably at
a shard also releases its local reservation. The coordinator still obtains every
reservation and every minimum before recording one immutable `c`, and still waits
for every fix acknowledgement before computing. The separate release request and
acknowledgement round disappears. BRIEF now adopts this rule; the experiment
sources retain the strict baseline and historical candidate labels for comparison.

The ordering argument is local. Once `c` is fixed, later conflicting reservations
must obtain positions above it. Other participants already hold announced intervals
covering `c`, so their relevant readers cannot bypass it. Releasing the reservation
does not resolve its pending effects or publish its outcome. Recovery must preserve
the complete envelope and the same `c`; it cannot choose a replacement position.
Global completion of minimum announcement remains essential: fixing early on one
shard while another has only a partial reservation would let a later reader raise
that other's minimum above the already chosen position.

The [adversarial local-release test](test_local_release.py) blocks the remote fix
after its minimum announcement. A blind local successor completes while the
predecessor has neither a remote fixed position nor an outcome. A cross-shard
snapshot remains pending remotely. After the remote device resumes, the snapshot
completes; both the later replacement and the predecessor's old logical view are
correct. This is exercised through both admission bindings. Lost replies and
fix-before-callback power cuts also retain the same position.

The [matched release comparison](evidence/local-release.json) preserves all
offers and compares the strict and candidate policies using the same corrected
queue/read rules. On the authored 40 ms one-way WAN, the candidate removes roughly
80 ms from the dependent point cohort's maximum, from about 485–486 ms to 404–405 ms.
Independent local points improve from about 133 µs to 121–122 µs. The long bridge
dependency chain changes little. The simplification has a concrete ordering
argument and bounded evidence; it does not remove real data
dependencies or establish correctness under replacement authority.

All 2,784 offers complete across the 60 final release-policy histories, with no
reported violations; all twelve requested power cuts fire. Replicated LAN transfer
maxima fall from roughly 991 µs to 785 µs, and point work alongside replicated scans
from 198–210 µs to 164 µs. These are complete finite cohorts under authored costs,
not estimates of production p99.9. Removing the release round is the architectural
simplification supported by this pass; removing global announcement completion or
the all-fix-acknowledgement execution gate is not supported.

### Recovery footprint and completion capacity are different failures

One accepted transaction can exhaust the replicated consumers' finite memory after
announcing an output and before finishing its position protocol. This occurs even
with no long history: an 8 KiB fixture leaves it unfinished, while 12 KiB completes.
The [composition campaign](evidence/replicated-contention.json) retains that failed
progress case alongside successful service and recovery. Raising defaults would
hide the missing completion-capacity policy. BRIEF already requires a path to
completion or agreed failure; this binding does not yet implement that obligation.

Separately, [record-at-a-time recovery](evidence/recovery-footprint.json) fixes a
specific temporary-footprint problem. Twelve writes complete under a 12 KB host
budget, but whole-journal reopening cannot rebuild them and a new write remains
unfinished. Streaming the same journal restores the complete ordering-state hash,
preserves versions and serves the new write. Across eighteen histories, the three
whole-read/12 KB cases fail recovery; all other cases complete all thirteen offers.
At 16/20 KB both work, and the whole read reopens in about 13 µs versus 263 µs for
record-at-a-time replay. These authored timings expose a memory/I/O tradeoff, not
an argument to use single-record reads universally.

The alternative bounds one temporary journal record, not the accumulated retained
state. It assumes a contiguous single-writer journal and device-reset recovery.
[RETENTION.md](RETENTION.md) defines the next experiment: a fixed old snapshot,
continuing overwrites, a delayed required checker, actual reclamation and a
recoverable path to finish admitted work. That separates unnecessary history
growth from completion capacity and unavailable decision/checking authority.

## Earlier stress campaigns: useful improvements, with limits

The three main campaigns contain **697 matched case/seed records covering 825
simulated histories**: [298 dataflow cases](evidence/flow-campaign.json),
[342 transaction cases covering 470 histories](evidence/traffic-campaign.json),
and [57 adversarial cases](evidence/adversity.json). Transaction search cases
contain three distinct training workloads. Every evaluation completed and the
correctness observers reported no violations; some policies deliberately failed
service limits, refused work or left work unfinished. Negative controls in the
tests separately demonstrate that observers reject incorrect results.

[Eight focused follow-ups](evidence/pressure-followups.json) extend the recovery
deadline and offering horizon, and isolate competition for a single worker. That
brings the retained campaign evidence to **833 histories**, alongside **132 tests**.

The [campaign API](CAMPAIGNS.md) supplies constrained combinations, matched seeds,
load/severity ramps, Pareto neighborhood search, held-out validation and per-cohort
accounting. [The gauntlet](GAUNTLET.md) maps the wider mined scenarios to executable
cases and explicit gaps. All numerical times below are authored model costs,
**not measured SixDB performance**. Maxima describe finite samples, not p99.9.

### Locality survives WAN delay only where the dependency is local

One remote transaction overlaps 99 continuing point transactions, offered every
2 ms. The remote link has 40 ms one-way latency; local links have 12 µs. The
table gives seed-1 point-response maxima. All requests eventually complete.

| Workload | Release allocation tickets before execution | Hold declared scopes through execution | Hold the participant shard through execution |
| --- | ---: | ---: | ---: |
| Remote transaction; independent local keys | 0.099 ms | 0.099 ms | 324.016 ms |
| Remote transaction; local writers depend on its output | 323.988 ms | 324.015 ms | 324.016 ms |
| Broad cross-region read; narrow local output | 0.086 ms | 325.378 ms | 325.352 ms |

This supports scope-local waiting and releasing allocation access before remote
execution. It does not remove real dependencies. In the local maximum-selected
update, widening possible output coverage still delays later read-dependent point
writes: 0.829 ms maximum versus 0.100 ms for a scan with narrow output. Blind
overwrites, RMW, conditional no-ops, bridges and transfers have different waiting
behavior; “large transaction” is not one useful class. The protection comparators
are explicit conservative policies, not a reconstruction of the old arbitration
component algorithm.

### Batching and wakeup help; a finite drain can conceal overload

At 400 point requests offered over 1 ms, one-record journal writes leave 334
requests outstanding at the end of offering; batches of up to 16 leave 186.
Both eventually complete, but their seed-1 maximum latencies are 4.421 ms and
0.889 ms, with 4.392 ms versus 0.873 ms of further drain. At 200 offers/ms,
batching keeps the maximum below the experiment's 0.5 ms limit; at 400 it does not.
The limit is an authored comparison criterion, not a production capacity claim.

A tenfold longer low-rate point run completes all 500 requests with a late-window
rate matching the 50,000/s offered rate and about 0.096 ms maximum latency. That
supports the distinction between service saturation and short-burst absorption
for this finite horizon; retained history still grows. With only one worker,
breaking the broad scan into 20 µs compute quanta reduces the two-seed point maxima
from 0.60–0.67 ms to about 0.134 ms while all work completes. Logical independence
needs physical scheduling capacity to be useful.

Waking contenders when a ticket is released avoids idle retry gaps. At 200 hot
updates/ms, the wakeup policy drains both tested seeds while timer-only retries
leave some work unfinished in one seed. At 400, even wakeup exceeds the observation
budget. These actors execute individual read-dependent updates; they do not yet
model native compatible folding, so this is not Orbital's intrinsic hot-key limit.

The flow campaign exposes the same trap more directly. Shared filtering can let
a 250-request foreground burst complete where recomputation refuses requests.
Extending that offering window tenfold produces 711 refusals out of 2,500 even
with the shared filter. Late-window service approaches the authored one-worker,
600 ns/job ceiling. Enhancement removes competing work; it does not create CPU.

### Application knowledge can remove a wait, within precise bounds

The conservative read rule waits for every earlier overlapping pending output.
If the application proves that a later installed value completely replaces a
cell, the old pending effect cannot change that cell's newer value. An optional
supersession policy exploits this without discarding the old transaction or its
other outputs. With one slow wide update and 47 point operations, the seed-1 point
maximum falls from roughly 1.96–2.05 ms to 0.096–0.104 ms across compute-quantum
and metadata-cost variations. Every outcome is checked against its logical view.

Targeted tests retain an older read cut and a second cell that has not been
replaced: both still wait and obtain the correct old value. This is an
application-supplied complete-replacement law, not permission to infer independence
from compatible final deltas. It is a promising experiment to carry into the
composed protocol, not an automatic revision of the brief.

### Search finds tradeoffs and exposes weak conclusions

The final transaction search evaluates 32 distinct policies on three training
workloads and two seeds. Six policies remain nondominated across point/hot maxima,
wire bytes and drain time. They all use scope-local pending versions and waiter
wakeup. Four-credit windows reduce traffic but increase queueing; sixteen-credit
windows serve the selected foreground more quickly. Some choices tie on training
but differ on held-out cases. No global optimum is claimed.

The six survivors and three starting policies all complete their obligations on
held-out seeds 19/41 with changed output coverage, bursts, metadata/handler costs,
pause timing and asymmetric remote work. Passing completion is weaker than meeting
a latency target: broad-output held-out cases can still take milliseconds. The
flow search retains the distinct latency/WAN-byte/finalization tradeoffs of source
versus destination enhancement rather than naming one universal placement.

### Distress has distinct failure boundaries

Exponential retry backoff reduces completed wire traffic during the longest
modeled device pause from about 190 KB to 37 KB while both policies drain. After a
long power outage it spends fewer bytes but can leave a wide transaction unfinished
at the original deadline. Backoff therefore needs a recovery/progress policy; a
traffic saving alone is not a resilience win. Memory ramps eventually produce
both pre-acceptance refusals and retained unfinished obligations, all still counted.
Extending that power-outage observation completes the remaining work at about
14.902 ms: the original symptom was delayed recovery, not permanent deadlock.

The bounded witness probes separate historical safety, current data survival and
client knowledge. All three witnesses can remain live while the canonical replay
body prefix is zero. After a lawful publication, a surviving consumer can still
hold materialized state at LSN 4; destroying it removes that separate basis too.
Witness survival does not certify payload/code/dependency closure or restoration
authority. Temporary inaccessibility, destruction, lost responses and exhausted
finalization space produce different observations.

### The gauntlet improved the experiment, not only its policies

Adversarial review caught observers that accepted a fabricated read/result pair,
an install differing from its decision, and a client completion without an installed
outcome. It also caught the opposite error: rejecting valid recovery when a journal
write survived but its callback did not. Checks now use independent snapshot/effect
reconstruction, durable journal evidence, recovery application and scored-response
obligations. Initial source residency and additional metadata traversal work are
charged; cost sensitivity includes a zero-cost bound and a larger coefficient.

Quiet cohorts and old individual obligations are reported independently, so a fast
class cannot conceal a stuck one. A quiet interval or finite timeout is a symptom,
not proof of deadlock; event-budget exhaustion is classified as a simulator error.

These historical execution captures assume prepared single authorities and
device-reset recovery. They do not compose replicated admission/epochs, extension
checking or object mapping. Retained histories are not reclaimed, and costs remain
uncalibrated. Elections, handoff, health-response controllers, general routing-tree
adaptation, SOS/PITR and full dependency restoration remain explicit gaps. These
limits matter when deciding which conclusions can inform Orbital; passing this
campaign does not establish the whole design.

Run recipes are in [CAMPAIGNS.md](CAMPAIGNS.md). Full receipts and selected failed/
unfinished traces remain under ignored `build/experiments/orbital-simulator/`;
the selected JSON names sources and all competitors needed for these comparisons.
The code snapshot `build/captures/orbital-gauntlet` is verified with source hash
`07abc7d69772239cbefb966ccb928293009800ab12bceaa95c5932caa0be702a`.

## Native calibration: reject the scalar shortcut

The [local handoff study](../orbital-local-handoff/FINDINGS.md) supplies 195 native
trials on two fresh Zen 5 hosts, with 63,873,600 offers. Independent aggregation of
its retained summaries confirms 61,780,606 completions, 2,092,994 refusals, zero
unfinished offers and zero reported payload/order violations. The measurement's
source and retained artifact hashes were verified; these are separate hardware
observations, not additional simulator histories.

[local_calibration.py](local_calibration.py) deliberately tries a restricted model:
preparation-to-consumption median depends only on payload size. Fitting three
low-load spinning cases gives `171 ns + 0.0531 ns/byte`, within 2.6% of those
training medians. The [held-out comparison](evidence/local-calibration.json) rejects
it as a general same-host model: measured batched medians are roughly 7–15 times
the prediction, and sparse atomic-wait latency is about 33 times it. Repeat ranges
at the same payload size do not overlap, so rejection does not depend on an
arbitrary fit-error threshold. The coefficients are diagnostic, not simulator
constants or bare transport costs.

The physical model needs separate causes: active preparation/consumption,
publication readiness, finite queue admission, polling occupancy and wakeup.
Spinning occupies nearly a full receiver core even at 1,000 messages/s; that CPU
cannot be charged only when a message completes. For 16 KiB messages, preparing
32 before publishing the batch delays availability of a 512 KiB group. The
measured batching reversal repeats on the second host. The current same-host
NIC path does not express these mechanisms, and a good fit to one completed
latency would not validate them.

Instrumentation belongs in the comparison too. At 8M local offers/s, the fully
timestamped batch-1 fixture needs roughly 130–133 ms to finish a 100 ms offering
window; reduced timestamp controls finish near 100 ms. Other high-rate controls
change refusal rates as endpoint balance changes. Subtracting a clock-call
constant cannot repair those histories. The native study retains both versions,
their offered cohorts and bounded drain. These constraints should accompany an
explicit same-host path; they do not justify changing the current campaign's
synthetic constants or claiming production throughput.

Reproduce this calibration challenge without launching workers:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-simulator/local_calibration.py \
  --output build/experiments/orbital-simulator/local-calibration.json
```

## Earlier architectural probes

2026-09-27. The [retained comparison](evidence/initial.json) contains the 31 authored
cases at seed 1, source hashes and receipts for seeds 0, 1, 7 and 19: 124 runs.
All met their stated expectations, including refused/unfinished cases and four
deliberately unsafe variants rejected by observers. The 89 focused tests include
additional schedules, negative controls, incident reduction and resource checks.
This is bounded evidence about the implemented models, not an Orbital proof.

## Coherence needs an executable path

Sharing the runtime initially left an ambiguity: the dataflow example called a
locally retained application result “published.” It now calls that **private
materialization**. The composed case sends actual artifacts to an application,
retains a stable outbox entry, obtains ordinary payload and witness evidence,
publishes through the consumer and serves historical/current versions from durable
records. The observer does not connect the pieces or tell actors which facts exist.

With one required artifact held back until 800,000 modeled ns, both workers had
materialized their private results before the cutoff. The point write published
at 269,915 ns and all 20 foreground operations completed; the aggregate published
at 1,065,809 ns, after the missing artifact arrived. A separate history crashes the
application after its outbox write and the consumer after its result write, before
their callbacks. Both recover through storage/messages; exact replay reproduced
the complete causal trace and observations. These numbers describe authored costs,
not a production latency forecast.

This supports the basic actor/environment boundary. It does not yet connect the
full transaction reservation protocol, extension verification and object mapping
into that path. Completing those connections must preserve the same meanings of
private work, agreed input, public state and retained recovery obligations.

## The model itself produced useful counterexamples

- **An empty recovery scan need not be final.** A submitted write can finish after
  its actor restarts and scans storage. The producer now pays for reconciliation;
  neither immutable keys nor a fresh scan alone discover every late completion.
- **Power loss must retire device work.** Merely fencing an old completion left
  cancelled work occupying the byte queue and overstated recovery delay. Device
  reset now cancels queue/active users once, while retaining already-incurred cost.
  Actor death still permits submitted backend work to finish.
- **A logical fixpoint includes its continuations.** Omitting pending read inputs
  let different future behaviors have the same state hash. Hashes now include
  those inputs; separate reads under one transaction have separate identities.
- **Causality must survive queues.** A message that arrived while its receiver
  was busy lost its sender's causal chain when later dispatched. Mailbox and
  resource records now retain that parent, including durable reads across restart.
- **Cancellation, acceptance and publication are different milestones.** A
  cancelled owner's buffer remains charged through its backend completion.
  Delivered application chunks can all be acknowledged while capacity for their
  final materialization is still missing. Neither case may be reported as success.

These corrections belong to simulator mechanics or exercised recovery behavior.
They do not justify changing Orbital's brief yet. In particular, the replica and
epoch examples use prepared leadership and assigned positions; passing them
cannot establish the missing election or contention protocols.

## Placement has several costs, with no universal winner

The same two-source/two-receiver workload changes relay location and whether
receivers reuse a computed filter mask. The table uses seed 1. Every row delivers
the exact same two results and all foreground work, without refusals.

| Placement / filter | Worker ns | WAN bytes | Total wire bytes | Foreground maximum ns |
| --- | ---: | ---: | ---: | ---: |
| Origin / recompute | 78,880 | 7,168 | 7,168 | 12,584 |
| Origin / shared mask | 48,160 | 7,232 | 7,232 | 620 |
| Destination relay / recompute | 78,880 | 5,120 | 10,752 | 13,860 |
| Destination relay / shared mask | 48,160 | 5,120 | 10,816 | 620 |
| Consumer host / recompute | 78,880 | 5,120 | 7,936 | 10,540 |
| Consumer host / shared mask | 48,160 | 5,120 | 7,968 | 4,980 |

Receiver-side fanout saves expensive crossings but adds local traffic. Shared
computation saves worker service but spends bytes, and putting it on the consumer
still delays foreground work. When filter/consume cost is changed to zero and WAN
bandwidth reduced, carrying the mask becomes worse: 7,232 versus 7,168 WAN bytes,
and the last materialization moves from 78,848 to 79,652 modeled ns. Retaining that
reversal matters more than naming one placement as best.

All values above are synthetic resource accounting. Foreground maxima come from
20 offered operations, not tail-latency estimates. Wire bytes count completed link
service including subsequent loss; unfinished partial service remains in resource
busy time. The models omit packet pipelining and real local IPC costs. They test
whether the laboratory can expose competing objectives, not which deployment to
buy or how SixDB will perform.

The object comparison similarly returns identical old-version values using four
demand batches, two windows or one full preparation. Full preparation needs the
larger resident budget; under the smaller budget it is explicitly refused. Missing
extension evidence leaves its transaction unfinished, mismatching interactions
abort, and independent work completes in both cases.

## Reproduce and retain

The three historical source captures are archived with verified recovery references:
[gauntlet](evidence/sources/gauntlet.json),
[contention composition](evidence/sources/contention-composition.json), and
[checked composition](evidence/sources/checked-composition.json). Recover one with
`workbench/tools/artifacts.py fetch REFERENCE build/recovered/NAME`.
These archives preserve the source versions behind the earlier comparisons;
the selected result summaries remain beside this guide. They are source captures,
not archives of every original campaign trace. The final working prototype passes
199 tests, including the retirement probes; historical test counts above describe
their own captured revisions.

Run [run.py](run.py) with `--seed 1` for the baseline and seeds `0`, `7`, `19` for
the other schedules; the README gives output/replay commands. Full traces and
choices live in ignored `build/experiments/orbital-simulator/integrated-v2/` in this
workspace. The selected JSON retains every baseline competitor and negative
control plus per-seed result hashes, without committing routine full logs. Source
hashes refer to every adjacent Python file; use the matching sources and recorded
Python version for exact replay. Small history reduction is tested separately;
it guarantees no single remaining incident can be deleted while retaining that
predicate, not a globally minimal failure or exhaustive exploration.
