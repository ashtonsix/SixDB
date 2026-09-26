# Constrained families and equivalent senders

2026-09-26. Two bounded network-backed comparisons, extending
[the algorithm counterexamples](ALGORITHMS.md). Their purpose is to expose
tradeoffs, not select one topology or turn suggested overlaps into requirements.
The [study](adaptation_study.py) reuses the finite-resource simulator and records
all offered, late and unfinished work alongside CPU service, wire traffic,
queue peaks, retries and duplicate logical deliveries.

These are **authored synthetic values**. The 250 µs route deadline means delivery
to all sixteen receivers; the sender comparison uses a 120 µs diagnostic deadline.
Neither is the client's durable extension-effect target, and 600 messages do not
establish a production p99.9 estimate.

## Small route families need constraints and useful selection granularity

The source is in one AZ, sixteen recipients in another. Remote propagation is
80 µs and local propagation 8 µs. Each host has 1,250 B/µs TX and RX capacity,
256 KiB per-resource buffers and two CPU slots charging 0.35 µs per packet plus
the simulator's byte cost. No additional shared AZ-wide uplink is assumed.

The cheapest tree pays one remote crossing then uses a local star. Noise produces
four cost-perturbed trees with depths 2, 10, 12 and 10. The constructed shallow
family uses four different regional gateways, one remote crossing per tree and
local forwarding with fanout at most four and total depth at most three. This is
a feasible construction, not an optimum constrained-tree solver.

Every family explicitly installs receiver child lists before the timed workload;
the setup traffic is included in work totals. One cheapest tree installs 376
descriptor bytes and uses 3,832 wire bytes including ACKs. Four trees install
1,504 descriptor bytes and use 4,960 wire bytes. The model does not claim that
delegating regional routing automatically eliminates this control state.

At 256 B and 20,000 messages/s, all 600 messages complete. Direct fanout has
85.22 µs p99, the cheapest tree 94.03 µs, noise 181.71 µs and the shallow family
100.31 µs. Direct fanout's configured egress charge is about sixteen times larger;
noise adds latency without reducing steady traffic relative to the shallow tree.

At 4 KiB and 100,000 messages/s, sender/gateway work becomes the problem:

| Policy | Completed / 600 | Late or unfinished at 250 µs | Completed p99, µs | Wire MB | CPU service, ms | Peak queue KiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Direct | 4 | 597 | 277.65 | 9.32 | 19.91 | 256.0 |
| Fixed cheapest | 10 | 597 | 2,588.93 | 12.75 | 21.12 | 256.0 |
| Oblivious noise | 505 | 234 | 1,491.11 | 42.91 | 39.62 | 256.0 |
| Shallow, stable message hash | 600 | 0 | 140.92 | 43.70 | 35.37 | 36.2 |
| Shallow, interleaved stripes | 600 | 0 | 133.10 | 43.70 | 35.37 | 17.7 |
| Shallow, blocks of 32 messages | 600 | 54 | 261.10 | 43.70 | 35.37 | 177.5 |

MB is decimal; KiB is binary. The low wire/CPU totals of failed policies reflect
unfinished deliveries, not economical completion. Per-message stripes divide
short windows evenly; coarse stripes repeatedly load the same gateway even
though the long-run shares and total successful bytes match. Stable hashing
preserves replayable choices but gives no short-window balance guarantee.

An independent packet-processing stress uses 32 B, 50,000 messages/s, one CPU
slot and an authored 4 µs fixed packet charge. NIC capacity is unchanged:

| Policy | Completed / 600 | Late or unfinished at 250 µs | Completed p99, µs | Wire MB | CPU service, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Direct | 78 | 598 | 16,219.06 | 0.92 | 59.63 |
| Fixed cheapest | 62 | 599 | 27,327.04 | 1.07 | 66.43 |
| Oblivious noise | 406 | 599 | 24,167.76 | 3.15 | 201.44 |
| Shallow, stable message hash | 600 | 125 | 354.49 | 2.39 | 153.95 |
| Shallow, interleaved stripes | 600 | 0 | 180.55 | 2.39 | 153.95 |
| Shallow, blocks of 32 messages | 600 | 595 | 15,947.39 | 3.98 | 257.14 |

The block policy triggers 6,445 transport retries despite zero buffer-overflow
events: queued work delays receipt beyond the timeout, and retries add more
packet work. The model exposes that feedback; it does not establish that these
timer settings or CPU costs describe a real NIC. A retry policy aware of
outstanding local work deserves a separate comparison.

Depth limits remove the demonstrated long chains. They do not themselves bound
loaded latency: the cheapest depth-two tree overloads, and the same shallow
family succeeds or misses its budget depending on selection granularity.

## Equivalent copies: release times, notices and fallback

Three origins independently acquire the same usable bytes at 0, 30 and 80 µs.
Their one-way propagation to a recipient is 80, 10 and 30 µs. Those releases are
fixture inputs: the cost and protocol that created the copies are outside this
comparison. A live recipient records each message once and applies one logical
effect. First acceptance sends real suppression notices to all origins;
duplicates refresh only their sender's notice. Transport ACKs also consume work.

`single_ranked` chooses one stable hash-ranked origin. `timed_ranked` gives ranked
backups timers of 60 µs per rank, never earlier than their own release.
`readiness_ranked` orders those timers by the authored release-plus-propagation
estimate. `eager` sends at each origin's release unless a suppression notice has
actually arrived. No policy reads the recipient's acceptance set to cancel work
remotely. In-flight copies cannot vanish on another origin's success.

| Healthy policy, 512 B at 20,000/s | Completed / 600 | p99, µs | Wire MB | CPU service, ms | Duplicate equivalent deliveries |
| --- | ---: | ---: | ---: | ---: | ---: |
| Single ranked | 600 | 111.21 | 0.87 | 3.39 | 0 |
| Timed ranked | 600 | 111.21 | 1.63 | 5.64 | 790 |
| Readiness ranked | 600 | 41.93 | 1.45 | 5.10 | 600 |
| Eager | 600 | 41.21 | 1.45 | 5.10 | 600 |

Ranking is not automatically cheaper than eager sending: receipt propagation
can outlast a backup timer. Here the early origin's slow-path packet is already
in flight when the later origin delivers sooner. Both readiness-ranked and
eager policies therefore pay one duplicate per message.

When the preferred low-latency origin crashes for 35% of the workload, single
ranked leaves 72/600 messages unfinished. Timed ranked completes all but misses
the 120 µs deadline for 40; readiness ranked misses it for 210. Eager completes
all within that deadline, with 81.21 µs p99. The advantage comes from earlier
duplicate work, not instantaneous failure knowledge. Three-recipient cases also
complete without repeated logical effects; their traffic and CPU are separately
retained rather than inferred from the one-recipient result.

Losing half the reverse-direction packets affects both transport ACKs and
suppression notices. All logical messages still complete in this finite case,
but eager traffic grows from 1.45 to 2.78 MB and duplicate equivalent deliveries
from 600 to 909. Some receipt transfers exhaust their retry budgets after the
logical effect has completed. Accordingly, retry exhaustion is not equated with
an unfinished logical message, and completed effects are not equated with every
sender knowing the outcome.

The receiver ledger remains alive throughout. This establishes neither durable
deduplication after receiver loss nor exactly-once external effects. Origin
crashes cannot restart old-incarnation transfers magically; surviving equivalent
copies provide the fallback used here.

## Scope and reproduction

The comparison does not relocate witnesses, discover membership, optimize
changing graphs or implement a hysteretic controller. Candidate routes remain
fixed during each workload. Useful next contrasts are a shared uplink bottleneck,
hot destination partitions, flow-sized rather than fixed-count stripes, observed
gateway queues, and switching only after measured gain repays state distribution
and in-flight overlap. Such adaptation must use delayed observable information;
the result above does not license reading simulator fault state as an oracle.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_adaptation.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/adaptation_study.py \
  --count 600 --output build/workbench/orbital-dissemination/adaptation.json
```

The [compact results](evidence/20260926/adaptation.json)
retain 34 cases and executable source hashes. The runner refuses to write
mixed-source results; larger exploratory runs stay in ignored build storage.
The retained run used simulator hash beginning `a3fd09a8d798` and study hash
beginning `2817a77e76aa`; the JSON stores full identities.

Seven [checks](check_adaptation.py) cover reachable acyclic constrained families,
one remote crossing, replayable selection, queue growth at identical successful
byte counts, failed-origin fallback, actual-notice suppression timing, duplicate
effect exclusion within the live ledger, lost-receipt amplification and outcome
accounting. They do not assert the displayed percentiles as permanent regressions.
