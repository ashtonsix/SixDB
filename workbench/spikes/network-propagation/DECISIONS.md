# Choosing and changing delivery routes

A delivery controller chooses where copies travel, when to send them, and how
to respond to unexpected progress. Changing its graph does not remove work
already queued or in flight.

This decision framework does not claim that every capability below is implemented.
The [model and assumptions](MODEL.md) own implementation scope; the
[spike entry point](README.md) leads to results.

## State the obligation and objective

For propagation, completion means the full payload is available at every
required recipient. For fetch, completion includes the request, modeled service
work and response from an eligible holder. Confirmations are a separate milestone.
Compare policies with the same recipients, content, eligible holders and legal
relay locations.

A useful objective is **minimise total cost while meeting a stated completion
target within resource limits**. The target names its deadline, required success
fraction and tolerated incompletion under declared workload and failure
assumptions. Cost includes transfers, request charges, retries, duplicates and
route changes. Show fixed costs separately, with a traffic volume if amortising them.

A relative latency allowance expresses a different preference: pay the least
among candidates close enough to the fastest candidate found. An absolute
deadline can admit a much cheaper route that this relative preference excludes.
Expose that choice and both outcomes. Say which candidates were evaluated;
bounded search establishes a best found choice, without proving a global optimum.

If no evaluated policy meets the target, retain that result. Waiting, refusing
new work, provisioning capacity or relaxing the target have different costs.
Keep waiting before acceptance, rejections and unfinished delivery visible against
the same offered workload. Successful-only latency omits those outcomes.

## Separate observations from assumptions

The simulator may know the true capacity or scheduled failure. The controller
needs an explicit account of what it can learn, when, and at what cost.

| Decision input | What needs to be visible |
| --- | --- |
| Delivery progress | Acknowledged chunks/objects at each holder, receipt age, outstanding sends and uncertain outcomes. |
| Resource pressure | Queued/retained bytes, oldest waiting work, buffer and connection capacity, CPU demand and shared uplink service. |
| Path behaviour | Delay, loss and progress observations, their age and variation, and concurrent load. |
| Failure exposure | Shared hosts, zones, uplinks and other failure domains; evidence for suspected failures. |
| Legal choices and prices | Eligible holders/relays, placement restrictions, directional byte/request charges and fixed commitments. |

A delayed receipt leaves uncertainty about delivery. An observed slowdown may
reflect a congested shared resource; moving to another logical edge using that
resource can increase the backlog. State observation delay, probe traffic and
how estimates change. Unknown capacity and failure dependence remain uncertainties.

## Distinguish planning from online recourse

Static planning selects a route from assumed conditions before delivery starts.
Held-out evaluation tests different arrivals or disturbances. Injecting failures
into replay tests the fixed route's exposure; it does not demonstrate adaptation.

Online recourse changes future actions using observations available at that
moment. Candidate actions include slowing new intake, scheduling different
chunks, changing a parent or holder, repairing missing data, and sending a
speculative copy. Specify the trigger, observation delay, permitted action and
resource budget. Include planning and coordination work that delays action or
competes for capacity.

State whether a rewrite affects only new objects or unfinished objects too.
For unfinished work, preserve delivered chunks, outstanding sends and retained
copies. A replacement relay must possess the content it is asked to forward.
Overlapping old and new instructions need loop prevention and duplicate
suppression even if each tree is acyclic. Delayed receipts still refer to the
same content and original work.

Cancellation may stop unsent bytes; it cannot reclaim bytes already transmitted.
Charge overlapping sends, new requests, setup and duplicates to the transition.
Keep necessary copies until responsibility for remaining delivery is established.
Compare the benefit with switching costs; state how the policy avoids oscillating
between routes on noisy observations.

## Bound resources and describe failure protection

Bandwidth sharing alone does not bound queued work. Account for source and relay
buffers, retained payloads, outstanding requests and CPU queues. Old and new
routes share those ceilings during a transition. Protect capacity for progress
messages and repair so payload saturation cannot prevent its own relief.

At a limit, specify backpressure before acceptance, bounded waiting, or a concrete
spill/recovery path. Missing a deadline does not itself permit abandoning the
obligation. Finite buffers cannot absorb a persistent service deficit.

Backup independence is relative to a named failure. Distinct relays may share
an uplink, zone or retained source. Show which failures the alternative avoids,
which resources it still shares, and whether it has capacity during failover.
A data-forwarding blackhole, host loss and zone loss have different effects on
payloads, receipts and retained state. Detection and repair consume time and
resources; correlated failures can defeat both the primary and its backup.

## Judge the whole policy

Compare fixed and adaptive policies against the same external offered demand
and disturbances, with the same observation access and resource ceilings.
Show completion, deadline misses, refusals and unfinished work alongside total
cost, peak retained bytes and transition traffic. A faster accepted subset can
be the consequence of refusing more work.

Vary capacity, delay, observation lag and failure dependence to find where the
preferred action changes. Include exhaustion and recovery: recovery can produce
another burst. Synthetic percentiles describe declared scenarios, without
establishing measured cloud tails or a production guarantee.

The [CFT cliff investigation](../cft-commit-latency/commit/cliff.md) explains why
queue growth and deadline outcomes belong beside throughput. Its measurements
are evidence about that experiment, not calibration of this routing model.

The [Orbital brief](../../../orbital/BRIEF.md) motivates propagation, while the
[consensus design](../../../orbital/CONSENSUS.md) owns authority and accepted
submission obligations. A route change establishes neither persistence,
admission nor clearance, and cannot grant serving eligibility or reduce a
quorum. Any composition with Orbital must preserve those obligations; this
spike does not choose their protocol.
