# Observed health and future proposal timing

2026-09-26. During the survey, Orbital LEAD relayed Ashton's suggestion to explore
switching between early conditional proposals and eligibility-before-proposal
according to infrastructure health. This [comparison](proposal_study.py) changes
**when future proposals start**, preserving the write fixture's payload evidence,
publication conditions and every already allocated journal position. Conditional
entries remain an unadopted protocol candidate; this is not a composed consensus
proof or an amendment to the current brief.

The central limitation is visible in the execution history: a later switch to
strict scheduling cannot remove an early entry whose missing payload blocks a
shared prefix. Even one such entry can cause an unbounded wait if nobody can
repair it. Limiting an early window bounds exposure count, not stall duration.
Eligibility-before-proposal can also have downstream follower-prefix waits;
changing timing is not a universal cure for every ordering or dissemination
dependency in the fixture.

## An observable, bounded candidate

Each producer independently observes actual durable-payload replies arriving
over the network. It knows its own send/start time and local timers, not remote
durable sets, failed hosts, prefix state or another producer's history. After a
300µs missing-receipt deadline or a received slow reply it selects strict timing
for at least 500µs. An overdue unanswered sample also keeps it strict after
cooldown; expiration alone does not establish recovery. A policy change is sampled
when that operation's local payload persistence finishes, then fixed for that
operation. No already submitted slot is withdrawn, skipped or relabeled.

The controller tracks at most 128 unanswered samples per producer. Once full,
new operations select strict timing without allocating more watch state;
application work is still offered and uses the ordinary fixture's finite queues.
Duplicate replies naturally find no pending sample after first receipt. Forty
diagnostic transition records are retained. A permanently lost reply can leave
this conservative policy strict forever; a production design would need an
explicit bounded progress/reprobe policy rather than claiming recovery from
silence. Existing durable identities and required effect state are separate from
this temporary controller state.

Early-mode observations require additional payload-receipt traffic, which this
comparison charges. Strict mode already needs those replies. Optional warning
inputs travel from `l0` to producers as real 32-byte control messages. Warning
times are authored exogenous inputs, independently of the fault declarations;
we test correct, expired and false warnings. Their upstream detection process
is absent. This tests what received information enables, not whether a real
detector would predict a partition.

This is a payload-readiness symptom policy. It cannot identify the underlying
cause as a network, device, host or producer failure. A fast reply from another
qualifying holder may hide the degradation of the preferred follower; that can
still leave a slow target path. A future controller needs operation-specific
observations, with false-alarm and probe costs included, rather than one global
“healthy” bit.

## Retained comparison

All 26 authored cases offer 300 writes at 100,000/s from two alternating
producers. They use the same synthetic default write fixture; all complete
during the 10ms drain in this campaign. Percentiles are finite model results,
not hardware tail estimates. P1 is the independent producer in the targeted
P0-path failures; in shared-device and false-alarm cases P1 is also exposed to
the corresponding shared symptom or warning.

| Incident / timing | Overall p99, µs | P1 p99, µs | Early / strict choices |
| --- | ---: | ---: | ---: |
| Healthy, always strict | 372.93 | 372.93 | 0 / 300 |
| Healthy, always early | 159.46 | 159.46 | 300 / 0 |
| Healthy, observed adaptive | 159.30 | 159.34 | 300 / 0 |
| Sudden P0 payload partition, strict | 1,944.92 | 847.74 | 0 / 300 |
| Sudden P0 payload partition, early | 1,341.65 | 1,332.93 | 300 / 0 |
| Sudden P0 payload partition, adaptive | 1,533.17 | 1,332.01 | 173 / 127 |
| Advance warning, strict | 1,945.78 | 840.83 | 0 / 300 |
| Advance warning, early | 1,344.16 | 1,334.28 | 300 / 0 |
| Advance warning, adaptive | 1,945.41 | 618.22 | 163 / 137 |
| False warnings, adaptive | 400.84 | 403.67 | 150 / 150 |
| Shared fast-follower device slowdown, strict | 2,968.00 | 2,973.09 | 0 / 300 |
| Shared fast-follower device slowdown, early | 3,294.63 | 3,294.44 | 300 / 0 |
| Shared fast-follower device slowdown, adaptive | 2,516.09 | 2,516.42 | 49 / 251 |

The sudden partition starts at 300µs, after early slots have been fixed, and ends
at 1,000µs. The adaptive policy reduces subsequent speculation but retains about
1,172µs maximum admitted-prefix wait, almost identical to always early. It does
not recover the independent producer's strict-path p99. Always early improves
the overall cohort's p99 while harming P1: “better during distress” is not one
scalar property of a policy.

For the advance-warning case, the partition runs 600–1,300µs; the warning is
sent at 400µs. Adaptive protects future allocation early enough to improve P1's
p99 to 618µs, versus 841 strict and 1,334 early. P1 completes 57 operations by
fault end, versus 46 strict and 25 early; its fault-window work has no unfinished
effects one millisecond after repair, versus 12 for always early. Overall p99
remains close to strict because the stalled producer still pays for repair.

A warning sent at 100µs instead expires around the beginning of the same fault.
That **expired-warning** adaptive case reaches 2,248.51µs overall and 1,566.25µs
P1 p99—worse than either fixed policy. Prior strict scheduling and renewed early
allocation interact with order and repair. Predictive warnings need a useful
horizon, not merely a true association with a later incident.

Three false warnings in a healthy run produce strict-path latency and transient
ordering waits: 403.67µs P1 p99 versus 159.46 always early. Setting the watchdog
to 150µs, below the healthy durable-reply path, creates 300 false timeout samples
and selects strict for 284 of 300 writes. Thresholds and cooldown are authored
controls, not recommended defaults. Flapping payload paths also retain old
holes despite repeated mode changes.

Windows of one and four unanswered payload receipts select early for only two
and eight operations respectively in the sudden-partition case; the rest run
strict. Their later behavior approaches always strict. This buys little useful
healthy concurrency at the offered rate, and does not prove a bounded stall if
one of those initial early entries is the one that loses its payload.

## Adversarial checks and reproduction

[Focused checks](check_proposal.py) include a lost early payload with later
strict entries, reverse-only receipt loss, warning propagation, actor-local
timers, frozen operation choices, bounded sampling and unchanged fixed-policy
behavior. In the no-retry early-hole history, two later payloads become ready
but no effects complete: the old position remains owed. This is deliberate
absence of a retroactive skip/abort oracle.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_proposal.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/proposal_study.py \
  --count 300 --output build/workbench/orbital-dissemination/proposal.json
```

[Evidence](evidence/20260926/proposal.json) retains all configurations, source
hashes, payload/effect/response endpoints, per-producer completed and unfinished
work, chosen modes, observed alarms and finite resource work. The hook in
`experiments.py` only supplies local start and actually received payload-reply
events. The existing strict and early fixtures keep their original behavior
when no observer is installed. Adaptive timing is worth considering where its
observations and transition costs fit the workload; these cases do not establish
a generally superior controller or make conditional-entry consensus safe.
