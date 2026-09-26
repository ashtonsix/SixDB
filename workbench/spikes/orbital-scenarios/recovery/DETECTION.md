# Detect early, restore coverage, then change authority

2026-09-26. Proposed failure response for [brief](../../../../orbital/BRIEF.md).
The priority is preserving data, then maintaining writes, then
avoiding wasted recovery. Thresholds and failure forecasts remain experimental;
this note specifies no production timeout or implemented detector.

Ordinary operation remains 2-of-3 with the first durable follower's early
propagation path. Background nonvoting copies need not delay that path. Any
stronger consensus/result predicate below is an explicit failure-response mode,
with safe activation and eventual return to ordinary 2-of-3.

Preservation covers durable acceptance obligations and every committed or
possibly chosen record, even when its acknowledgement never reached a caller.
References to acknowledged coverage do not permit discarding uncertain replies.

## What the detector observes

An unusually slow outstanding operation starts health investigation before it
finishes. Ashton's illustrative 100 ms across same-region AZs is one possible
trigger, not a deadline after which a VM is known dead. Use local monotonic time
from a defined boundary: queued, sent, response received, or persistence
completed. Do not mix network RTT, PLP completion and committed-response latency.

Maintain separate histories for peer, direction, route, operation and load class.
Compare recent behavior both with a stable healthy reference and a recent
window: a detector that learns every incident into its baseline can normalize
failure. Route changes and warmup need explicit treatment. A latency sample from
a fast ping cannot replace evidence of progress on the persistence path.
Timeouts are censored observations, not successful samples at the timeout value.
Sparse histories require conservative fallback and explicit uncertainty.

An accrual score is a candidate: for a fitted healthy delay distribution `F`,
`phi(t) = -log10(1 - F(t))` expresses how unusual the elapsed delay is under that
model. It is **not** the probability of machine death or a regional disaster.
Different action costs can use different suspicion levels. This separation of
monitoring from action is the useful prior art in
[Hayashibara et al.](https://dspace.jaist.ac.jp/dspace/bitstream/10119/4784/1/IS-RR-2004-010.pdf).
The distribution, windows and thresholds need production-like failure traces;
assuming independent Gaussian tails would not establish them.

The existing [network measurements](../../cft-commit-latency/network/README.md)
show host/flow-dependent behavior over short captures. They do not measure
detector false positives, outage prediction or durable acknowledgement tails.

Normal-path optimization uses the same evidence. The
[host/port study](../../cft-commit-latency/network/selection.md) found fixed-host
RTT classes spanning 266.2 microseconds after changing one UDP endpoint port;
the later [port study](../../cft-commit-latency/network/studies/port-sampling.md)
also observed some fixed-tuple changes up to 103 microseconds in 18 minutes.
Screen actual hosts/tuples, validate separately, retain useful flows and recheck;
neither AZ labels nor a new connection on the same tuple selects a proven faster
path. Clock uncertainty limits directional rankings, and the joint durable
two-follower race and fallback still need validation. Evaluate outgoing network
and persistence costs together rather than treating half an RTT as measured.

Changing an authenticated flow between existing witnesses need not change their
ballot. Validate a leadership or witness change against its gain, transition cost
and fallback, with evidence against churn; do not rotate on an arbitrary schedule.
There is no calibrated selection controller here. The
[handoff note](HANDOFF.md#keep-submissions-moving-across-route-and-leader-changes)
separates routing, same-configuration leadership transfer and membership change.
Keep ordinary traffic flowing under its existing authority while probing; an
anomaly is not itself a requirement to pause and confirm the leader.

## Probe the failed obligation

Use a bounded, coalesced probe round per incident, shared across shards on the
same VM where appropriate. Responses bind peer incarnation, request nonce,
configuration and a recent progress frontier. Collect observations, including
their observer and age, rather than publishing a single unqualified healthy bit.

| Question | Probe or evidence | What it cannot establish |
| --- | --- | --- |
| Did this observer stall? | Local scheduler lag, outbound queue age, CPU and clock discontinuity; independent observers' recent traffic | Healthy local telemetry cannot prove the remote is dead. |
| Is the route failing? | Direct bidirectional RPC plus a small number of probes through genuinely different paths/observers | Alternate ports need not be independent physical paths. |
| Is the process serving? | Authenticated application response and monotonically advancing progress | A responsive admin thread can coexist with a wedged log. |
| Can it retain new obligations? | Relevant WAL/metadata persistence progress; if necessary a bounded durable challenge through the actual write path | One successful write does not revalidate previously lost or corrupt history. |
| Is the problem shared? | Peer failures across hosts/AZs, network/storage errors, provider events and independent regional observations | Correlated observers may share the same broken dependency. A status page may also fail. |
| Can recovery actually start? | Test access to warm candidates, retained history, payloads, keys, credentials and transport | An empty VM or capacity reservation is not a ready replacement. |

Data-plane progress can supply probes during ordinary traffic. Quiet shards
still need a low-rate background check; they must not discover weeks of silent
degradation only when the next write arrives. Persistent checks require a budget
so monitoring does not become the storage overload it reports.

## Responses need different evidence

These are action conditions, not a compulsory sequence of fixed delays. A clear
correlated signal can skip waiting; a blip can end without any membership change.

| Evidence and exposure | Proposed action | Authority boundary |
| --- | --- | --- |
| One anomalous response, no corroboration | Probe, preserve telemetry, inspect available coverage, prepare an eligible destination | No changes to votes, agreed outcomes or retention authority. |
| Repeated lack of relevant progress, or independent corroboration | Start bounded repair to an eligible nonvoting candidate; prefer a destination outside plausible shared causes | Copying may be speculative. A copy counts only after verification and its required persistence/retention acknowledgement. |
| One witness unreachable but two retain valid authority | Continue the existing consensus protocol where its durability policy is met; repair the exposed suffix and prepare replacement | A local timeout never lowers the quorum or recreates lost votes. |
| Evidence threatens the remaining region/domain | Prioritize export of history, decisions and replay closure; require the stronger protection for new work at a defined protocol boundary; relocate under a safe handoff | New guarantees have a recorded effective frontier. Existing work stays exposed until separately repaired. |
| Health is very bad and useful evidence may soon become inaccessible | Start [SOS broadcast](SOS.md): pause all ordinary admission paths locally and disseminate complete state or useful fragments through surviving flows | Export needs no quorum, complete manifest, new epoch or successor grant. A local stop does not prove hidden authorities stopped. |
| Required history, quorum, fence or durable capacity unavailable | Stop the affected new acknowledgements/admissions; retain obligations, continue bounded investigation and unaffected work | A recovery timeout is not permission to invent a decision or accept acknowledged loss. |
| Progress returns and coverage is verified | Drain excess speculative work and return to the chosen placement through ordinary authority rules | Returning stale disks/messages cannot revive an old configuration; do not immediately migrate back. |

Ordinary leader election can run concurrently with this investigation under the
chosen consensus protocol. Probe corroboration must not become an extra voting
threshold. Likewise a hardware loss report can justify immediate copying; it
does not supply missing consensus history.

Local observations can trigger probes, routing attempts, safe extra copies or
refusal of new work. They cannot independently change a transaction outcome,
membership rule or guarantee on which dependent consumers rely. Those changes
need configuration/decision records under the existing authority. An isolated
minority may provision and copy; it cannot activate itself because it has a
more alarming forecast. Mechanically stopping a suspect process is also not
proof that another old process cannot continue admitting.

## Count survival of obligations, not machines

For each durable obligation, retain at least one complete recovery recipe:
an appropriate checkpoint or ordered replay inputs, plus every dependency needed
to interpret it and the authoritative metadata. Several recipes can be
alternatives. For each anticipated failure set, at least one entire recipe must
survive. A receipt names bytes, holder incarnation, storage semantics, retention
obligation and the domain information used by the policy. Receipt forwarding or
two processes on one failing disk creates no additional copy.

Track separately:

- evidence of acknowledgement and accepted-but-not-yet-admitted producer work;
- witness history, including proposals that could already have been chosen;
- payload and replay-artifact coverage, including staged outputs and decisions;
- transaction/observation state needed to preserve outstanding dependencies,
  including completed-reader bounds or sufficient monotone position floors;
- ability to form a legal recovery quorum and to access the surviving bytes.

The first four concern recovering the intended history; the last can fail
without data destruction. Conversely a healthy quorum can coexist with missing
payloads. Losslessness is relative to a stated set of failures; there is no
finite placement that survives destruction of every copy.

With A/B/C in three AZs of one region, C's absence leaves A/B able to form the
original quorum. A decision subsequently retained only on A/B is vulnerable to
loss of both. C might hold older history, but contributes nothing to that new
suffix until caught up. Stronger placement must cover the metadata and the
payload/replay closure. A remote fourth witness cannot protect payloads that
remain only in the threatened region; nor is an uncounted asynchronous witness
a guaranteed holder of the latest decision.

Place against several plausible correlated causes, not only the single most
likely one: host, disk, AZ, region, provider, software rollout, credentials and
shared storage can overlap. Favor candidates outside the union of supported
threats when feasible. Otherwise expose the uncovered failure cases. Provider
and AZ labels alone are an incomplete independence argument.

## Two repair frontiers

Distinguish **new-work protection** from **historical coverage**. Strengthening
new work can take effect while the old suffix is still being copied. A useful
reported historical frontier says which durable history and dependencies
now satisfy the stronger policy. Sparse repairs also need explicit holes;
independent stream LSNs and cross-shard dependencies cannot be collapsed into an
arbitrary maximum timestamp. A serial position is not a complete recovery cut.

Record the transition's authority and boundary and the candidate's retained
history frontier. Catch up a snapshot plus tail while current work continues;
activation must cover the required handoff boundary, including any concurrent
suffix. Re-check durable coverage after promotion before retiring the former
holder. The [quorum study](QUORUM.md) owns the protocol-specific obligations.

Prioritize bytes with the least surviving independent coverage and the records
needed to unlock their recovery; copying an entire popular cache first can leave
the only accepted WAL tail behind. Do not destroy or reclaim threatened copies
just because a migration started. Report the remaining exposure while repairing.

An approximate exposure interval is
`detection + destination readiness + required copy/catchup + safe handoff`.
These stages may overlap; this is a dependency explanation, not an additive
latency prediction. For backlog `B`, copy service `r`, and continuing relevant
growth `w`, `B/(r-w)` is a simple catchup model only if `r > w` and those rates
hold. Otherwise throttle new intake, add capacity or change the repair plan;
waiting longer cannot drain that model. Real measurements must include tail
growth, source load, sharing and verification.

## Bound reaction cost without postponing essential repair

Start with a small warm pool diversified across plausible failure domains.
Warmness includes reachable processes, persistence capacity, working
credentials/keys, protocol code and a bootstrap path independent of the
threatened blob/control service. How much state to replicate continuously is an
explicit normal-operation bandwidth/storage cost against exposure time. Merely
launching a VM after the alarm relies on the impaired infrastructure.
[AWS's static-stability guidance](https://aws.amazon.com/builders-library/static-stability-using-availability-zones/)
supports separating recovery from provisioning dependencies; it does not prove
that any proposed pool size covers SixDB's incident load.

Coalesce probes and copying across tenants/shards without merging their consensus
or publication boundaries. Bound per-source and per-destination repair traffic,
use idempotent requests, jitter retries and reserve control/log/decision service
ahead of bulk copying. Retry amplification under overload is a known hazard
([AWS Builders' Library](https://aws.amazon.com/builders-library/timeouts-retries-and-backoff-with-jitter/)).
Rate limits and cooldowns must allow an urgent escape from a newly threatened
domain; they are not a rule to wait for a second destructive failure.

Resource reservation must include the services that reclaim resources. A repair
that fills the disk or NIC needed to finalize existing decisions can make its
own retained-state backlog permanent. Preserve pre-announcement capacity
reservation and independently serviceable metadata from the
[existing capacity-cycle analysis](../RELEASE-AFTER-POSITION.md#capacity-must-not-introduce-a-metadatadata-cycle).
Headroom must include an independently authorized and executable resolution
path with its inputs, keys and code; spare bytes or threads do not resolve a
decision. Stop new intake before emergency headroom is consumed. A blob outage must not
silently turn every pending transaction into a dependency of shard-wide repair.

Use faster entry into protective copying than return to preferred placement.
Require sustained relevant progress, validated state and completed ordinary
membership rules before restoring a returning node. Keep speculative verified
copies when useful rather than deleting and recreating them on every flap.
The stabilization period and budgets need measurement, not universal constants.

## Evidence that would choose the policy

Replay observed latency/progress traces with authored partitions, durable-write
stalls, observer pauses and correlated dependency failures. Compare always-wait,
replace-on-first-timeout and staged probe/copy/handoff on the same histories.
Measure time with uncovered durable obligations or possibly chosen history first,
legal write outage second, then false promotions, copied bytes and wasted pool
occupancy. Also
report probe load, repair-induced latency and unfinished obligations. Do not
infer failure probabilities from a scenario's presence in the test set.

The adjacent [small probe](probe.py) checks placement and replay-closure
counterexamples only. It neither calibrates this detector nor implements its
control loop. The next decision needs real tail/progress traces and a chosen
complete consensus/reconfiguration construction. More arbitrary simulated
timeouts would not settle either question. [The handoff construction](HANDOFF.md)
now selects prefix consensus, one terminal handoff entry and durable successor
initialization; that proposal still needs implementation and validation.
Losing the usually faster follower can worsen the remaining quorum's tails even
with unchanged 2-of-3. An emergency remote result gate affects every transaction
in its policy scope, including disjoint envelopes. These costs differ from
transaction contention and from the explicit handoff pause.
