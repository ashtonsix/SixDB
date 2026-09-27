# Locality, recovery and retained obligations

These experiments use the maintained actor library to compose cases the learning
spike made awkward: regional placement independent of transaction shape, faults
between packet receipt and callback delivery, and multiple durable retention
obligations sharing finite resources. Costs are authored nanoseconds and bytes.
The comparisons test mechanisms; they are not production latency estimates.

The current provisional default is older-conflicts-drain. The
[420-history policy study](../../spikes/orbital-reservation-policy/NATIVE.md)
compares it with ordered, eligible-first and oldest-live admission, including
its younger-WAN regression and operation-count overhead. `regional_main` accepts
`--policy drain|ordered|eligible|head`; policy stays fixed through replay.

The following captures used the explicitly ordered historical baseline unless
an eligible-first comparison is named (2026-09-27):

| Comparison | Histories | Completed / offered | Unfinished |
| --- | ---: | ---: | ---: |
| Regional baseline, bursts and retries | 102 | 3,996 / 3,996 | 0 |
| Queue policy and broad-writer progress | 84 | 2,913 / 2,988 | 75 |
| Hosted consumer/checker recovery | 27 | 252 / 270 | 18 |
| Competing retention roots | 18 | 900 / 900 | 0 |

All 231 histories have zero reported safety violations, missing required incidents
or dispatch-budget exhaustion. Unfinished work remains explicit. All eight native
validation groups pass normally and under ASan/UBSan; those checks separately
include deliberately incorrect protocol variants and corrupted observer evidence.

## A WAN transaction among local writers

[Regional assembly](regional.hpp) places each shard's witnesses and consumers on
local links. Only traffic between the two regions gets 20 ms propagation. A rare
two-shard transfer runs alongside three waves of local writes. All comparisons
preserve the authored arrival times. The shared-host arm collocates the European
coordinator, leader and primary consumer; its follower witnesses stay on separate
hosts. One European client retains and retries all offers.

The decisive case is a chain of reservation conflicts. The WAN transaction holds
key 0. An earlier broad writer requests keys 0/1/2 and waits. A later small writer
needs only key 2. The historical ordered rule prevents it from overtaking the broad waiter,
so the delay spreads beyond the WAN transaction's own coverage. Key 3 is a separate
control. A second control changes the broad writer to keys 1/2/99: the same put,
arrival, operation count and declaration width, cutting only its connection to the
WAN transaction while preserving its overlap with the key-2 writer.

The [102-history baseline](../evidence/regional-v1/regional/summary.json) includes
seeds 1/7/19, LAN/WAN controls, both placements, both overlap controls, denser bursts
and a common-window retry comparison. All **3,996 offers complete**, with no reported
safety violations, missed incidents or dispatch-budget censoring.

| Seed 7, 12 local offers, 100 µs spacing | Key-2 maximum latency | Key-3 maximum latency |
| --- | ---: | ---: |
| No broad writer | 0.104 ms | 0.104 ms |
| Broad writer connected to WAN transaction | 70.338 ms | 0.104 ms |
| Same broad writer with that connection removed | 0.104 ms | 0.104 ms |

The key-2 offer reaches durable coordinator input at 50.106 ms in every arm. Its
reservation is granted at 50.125 ms without the connection and at 120.259 ms with
it. The WAN transaction locally fixes and releases at 120.211 ms. This identifies
reservation waiting, rather than frontend admission or an unexplained client
latency change. All phases come from real persisted input and consumer transitions.

With 192 local offers in three bursts, maximum key-3 latency rises to 8.803 ms on
separate hosts and 7.003 ms with sharing. Key-2 latency reaches 72.895/72.989 ms with
the bridge and 8.800/7.037 ms after removing that connection. Physical backlog and
the reservation convoy are distinct. Sharing can help by removing hops; it is not
intrinsically the worse placement.

This substantiates the brief's qualification about queueing behind distributed
work. Local release avoids a shard-wide transaction-completion barrier; it does
not eliminate transitive waiting through overlapping reservations. The case uses
five logical scopes, not a measurement of collection cardinality. The model also
lacks complete-replacement supersession, so delays of dependent reads must not be
interpreted as irreducible costs of the brief's full design.

### One simpler rule, and its cost

The [84-history comparison](../evidence/ordering-v1/ordering/selected.json) changes
one rule: visit waiters in agreed order and grant an entire local group whenever
no **live reservation** conflicts. A blocked waiter no longer excludes otherwise
eligible writers. Canonical shard order, position assignment, local release,
pending reads and publication stay unchanged. These historical cases explicitly
select ordered or eligible-first; `--eligible-first` remains a compatibility alias
for the latter, and the current provisional default is drain.

This restores the bridge-connected key-2 writer from 70.338 ms to 0.104 ms in the
matched case above. The counterexample is also executable: two initial holders
occupy different keys, a broad writer queues for both, and alternating younger
writers keep at least one occupied. Each younger grant is derived from actual
agreed input; the harness supplies neither grants nor positions.

| Younger narrow offers, seed 7 | Broad wait, no overtaking | Broad wait, eligible first | Younger grants ahead, eligible first |
| --- | ---: | ---: | ---: |
| 16 | 0.540 ms | 2.891 ms | 16 |
| 32 | 0.540 ms | 5.489 ms | 32 |
| 64 | 0.540 ms | 10.971 ms | 64 |

In the 64-offer case the broad writer is still ungranted at the 10 ms observation
deadline, while narrow writers have made progress. Both policies still have twelve
narrow offers unfinished at that deadline: aggregate throughput would obscure
the change in who waits. All work drains within the separately declared 40 ms
window. This finite family shows growing broad-writer delay; it is not an
infinite-execution starvation proof.

All 84 histories have no reported safety violations or dispatch-budget exhaustion.
They contain 2,988 offers: 2,913 complete and 75 remain unfinished in six short-window
cases. The experiment establishes a locality/fairness tradeoff, not a replacement
recommendation. Removing waiter exclusion alone is insufficient if broad work
must make dependable progress under continuing narrow traffic. This earlier two-policy comparison did not promote a replacement; the later
[four-policy study](../../spikes/orbital-reservation-policy/NATIVE.md) informed the
provisional drain default.

### Retry pressure needs a common observation window

The regional comparison also finishes both retry candidates within the same
400 ms window. For the seed-7 bridge case, a 100 µs retry period produces 5,586,471
wire bytes and 718,059 dispatches; 80 ms produces 242,104 bytes and 12,805 dispatches.
Both complete all 14 offers. The short retry period raises disjoint-write maximum
latency from 0.104 to 0.262 ms in this case.

Thus the earlier dispatch-budget failure is not itself a demonstrated service
collapse. With enough laboratory budget the transaction still finishes; the
observed cost is unnecessary traffic and some collateral latency. These healthy
cases do not establish a good timeout after real packet loss. A single fixed retry
period for every role remains an experimental policy, not a recommendation.

## Failure while a packet is already resident

[Hosted recovery](hosted_recovery.cpp) collocates a required checker with one
consumer, leaving its witness quorum and the other consumers elsewhere. It pauses
after an actual witness packet reaches the consumer's host, before the posted
actor callback, while the checker is executing. It compares no failure, a consumer
process crash, and a host reset that also stops the checker. Recovery uses the
ordinary journals and messages, with a declared 5 ms outage.

The [27 histories](../evidence/hosted-recovery-v1/hosted/summary.json) exercise all
three modes at 16/24/64 KiB hosted memory, seeds 1/7/19. All requested boundaries
and actions occur; no safety violation or old packet escaping into the replacement
incarnation is reported. At 24/64 KiB all 180 offers complete. At 16 KiB the 72
source and independent writes complete while 18 checked/dependent offers remain
unfinished. Safe nonpublication is recorded separately from successful recovery.

At 24 KiB, refusals and dropped receives prolong recovery but it finishes. At
16 KiB the no-failure arm also runs out of retained-history capacity; after restart,
the model's conservative 16 KiB recovery destination cannot coexist with existing
resident state. These are distinct physical constraints. Neither byte threshold
is an irreducible Orbital requirement. The first delayed read is checked against
the old cut after all four continuing source writes have completed.

## Old readers, replay and reclamation under pressure

[The retention composition](../models/retention_pressure.hpp) registers two
different obligations before an overwrite stream: a reader at an old cut and a
replay consumer that needs the operation tail. One source owns the durable root
and collector. Readers communicate with it through messages; they receive no
privileged access to another actor's storage. An independent small-write service
shares the source host.

After later writes and a source restart, a temporary memory holder forces actual
reconstruction-read refusals. Small independent writes can still finish. Releasing
the holder permits the retained continuation to retry; releasing the old reader
does not release the replay root. The replay cursor advances durably before its
tail is collected. A final restart reconstructs the latest bytes after collection.
The special fault arms cut after checkpoint persistence or replay-cursor persistence
but before the corresponding callback; the baseline also includes ordinary source
restarts before delayed access and after final collection.

The [18-history comparison](../evidence/retention-pressure-v1/retention/selected.json)
crosses two memory/storage budgets, three incident settings and seeds 1/7/19.
All **900 obligations complete** with no safety violations or missing milestones.
Each history accounts for 24 source writes, 24 independent writes, the old reader
and the replay obligation. For seed 7 at 4 KiB memory/8 KiB storage with the
checkpoint cut, there are ten reconstruction refusals and seven independent
completions while pressure is held. Peak resident use is 4,054 bytes; collection
reclaims 8,712 durable bytes and leaves 338 bytes, from which current state is
actually reconstructed after restart. These are fixture byte counts.

Independent checks pair write intentions with physical storage receipts. They
verify every erased record against both roots, every terminal result against
durable authored evidence, and every recovery against its incarnation's actual
manifest, decoder, checkpoint and suffix reads. Oracle corruption controls reject
invented completions, omitted reads and correct-looking self-reported values
without the physical reads needed to reconstruct them.

The incorrect collector that ignores the replay root allows the reader to finish
but makes replay fail on a genuinely missing base. The deliberately lost retry
continuation leaves thirteen source writes plus reader/replay unfinished after
pressure is relieved, while all 24 independent writes finish. Its result is
unfinished work, not a fabricated safety violation or a successful experiment.

This fixture uses serialized root authority, known record-size bounds and roots
registered before the stream. It does not settle concurrent root admission,
distributed collection, UFFD/COW, generic operation replay or indefinite capacity.
Its synthetic workspaces and explicit buffers do not measure native allocator
overhead. The useful result is that separate durable obligations and recoverable
refusal continuations can be expressed and challenged through the ordinary ports.

## Reproduce and extend

Build the experiment executable and call it directly, or capture a campaign:

```sh
orb -m ubuntu python3 workbench/simulator/investigate.py --suite regional \
  --output build/experiments/simulator/regional-next
orb -m ubuntu python3 workbench/simulator/investigate.py --suite hosted \
  --output build/experiments/simulator/hosted-next
orb -m ubuntu python3 workbench/simulator/investigate.py --suite ordering \
  --output build/experiments/simulator/ordering-next
orb -m ubuntu python3 workbench/simulator/investigate.py --suite retention \
  --output build/experiments/simulator/retention-next
```

`--binary` selects an explicitly existing executable. Python callers can import
`cases` and `campaign.run_cases`; native callers can construct the regional case
and use `orbital::assemble` for their own inputs and incidents. CLI evidence handling
is shared so path aliases, failed writes and exact replay have the same safeguards.

The baseline [regional archive](../evidence/regional-v1/artifact.json) and
[hosted archive](../evidence/hosted-recovery-v1/artifact.json), plus the
[ordering](../evidence/ordering-v1/artifact.json) and
[retention](../evidence/retention-pressure-v1/artifact.json) archives, recover captured sources,
per-case arguments, raw output and exact choices. Their summaries retain every
competitor and unfinished obligation. New captures run [the selector](select.py)
before closing their receipt; it keeps all cases and phase maxima plus causal
examples, leaving the remaining per-transaction rows in the archive.

All workloads here are finite. The Orbital transaction composition and its
independent observer still retain growing history; these experiments do not establish sustainable capacity, general
liveness, authority replacement or production percentile latency.
