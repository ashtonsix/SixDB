# Origin placement and directed network economics

2026-09-26. The original [brief](../../../orbital/stale-drafts/BRIEF.md), lines
7 and 76–82, contains several separate proposals. Their specificity was lost
in the initial MINING-based survey. This comparison restores them individually;
the [source catalog](CATALOG.md) preserves the wider corpus.

## Edge-origin broadcast is different from cheap routing after cloud egress

The [40-case study](edge_study.py) uses the same finite-resource packet simulator
as the other comparisons. Eight recipients occupy four regions, two regions in
each of two cloud providers. A separate edge and a cheap-egress provider can
relay. The input originates either at the edge or at one cloud recipient.
Payload-only cases complete when all eight recipients possess the payload;
the cloud origin already possesses its own copy. Hash cases additionally require
every digest comparison; reply cases require every return at the edge. The
information is equivalent, while its
initial possession/location deliberately differs. Creation/prepositioning at
that origin is not simulated.

All tariffs and service times are **authored coefficients, not provider prices**.
Cloud cross-provider/public outgoing bytes cost .09 units/decimal GB, cloud
cross-region bytes .02, cheap-provider outgoing bytes .01, and edge outgoing
bytes .004. Within-region bytes have zero marginal tariff. These coefficients
include any intended per-byte receiver levy; the model does not assume that only
senders can be charged. Reverse ACKs use their own directed tariffs. All hosts
still consume CPU, NIC and finite buffers even on zero-tariff links.

Propagation is 8µs locally, 4ms within a provider between regions, and 10ms
between providers/edge. Normal NICs provide 1,250B/µs with 1MiB resource queues;
the default workload offers 100 messages at 200/s. It is a region/provider
dissemination comparison, **not the 170/250µs durable-effect target**. Small
cohorts and constant service do not establish tail probabilities.

For 16KiB payloads, all these healthy cases complete 100/100:

| Origin and route | Completed p99, ms | Transfer cost, millionths of an illustrative unit | Wire MB |
| --- | ---: | ---: | ---: |
| Cloud, direct to peers | 10.098 | 702.400 | 12.292 |
| Cloud, one trunk per remote region then local fanout | 10.079 | 351.200 | 12.292 |
| Cloud, edge relay then regional fanout | 20.080 | 181.176 | 14.048 |
| Cloud, cheap-provider relay then regional fanout | 20.080 | 212.640 | 14.048 |
| Edge, direct to every recipient | 10.112 | 64.448 | 14.048 |
| Edge, one trunk per region then local fanout | 10.079 | 32.224 | 14.048 |
| Edge, cheap-provider relay then regional fanout | 20.094 | 81.176 | 15.804 |

The first regional step halves expensive crossings, without reducing total
successful wire bytes. Cloud-to-edge relay adds bytes and another inter-provider
hop but lowers the configured cost. Originating at the edge avoids that initial
cloud-egress transfer. Sending from an already-cheaper edge through the cheap
provider instead makes both price and latency worse in this fixture. No provider
or universal relay topology is selected by these numbers.

For 64-byte cloud-origin records, edge relay costs 4.920 versus direct 11.200
millionths, while direct edge-origin fanout costs 9.152 versus regional 4.576.
Tiny records make the 120-byte transport ACKs and framing a substantial share
of cost: a cheap forward payload edge does not make reverse control free.
The same reasoning applies to receipts, membership control, certificates,
heartbeats and short extension invocations.

## Direct verification, replies, capacity and failure can change the result

**Cheap payload plus direct small hashes.** The model charges one origin hash,
sends a 32-byte digest directly to each recipient, and charges recipient verification
only after both payload and digest have arrived. Hash CPU and actual digest/ACK
packets are charged. This adds 173,600 wire bytes for the 100 cloud-origin
broadcasts. The cheap payload route remains cheaper in the healthy case, but
verification is now an additional required path; a lost required hash can hold
completion after all payloads arrived. This models equality verification against
a supplied trusted digest; actual digest correctness is a fixture assumption.
It does not establish arbitrary extension semantics or admission authority.

**Ring replies reverse the byte direction.** Each recipient optionally sends its
result directly to the edge, independently of ingress. For edge-origin regional
fanout, 32-byte results change transfer cost from 32.224 to 41.824 millionths;
64KiB results raise it to **5,040.352** millionths. For cloud-origin direct and
edge-relay routes, large-result cost is 5,710.528 and 5,189.304 millionths, with
20.357ms and 30.476ms p99 respectively. Cheap request dissemination is much less
important once cloud-origin result bytes dominate. A small request can therefore
have an expensive round trip even when its edge ingress is cheap.

**Burst buffers matter below average link capacity.** Reducing resource queues
from 1MiB to 256KiB in the same 64KiB-reply workload leaves 0/100 edge-origin
whole broadcasts complete under either route, and 87/100 cloud-direct versus
4/100 cloud-edge-relay complete. Edge regional traffic grows from 69.786MB to
180.291MB with 1,590 retries. Average offered result payload is about 105MB/s,
below a 1.25GB/s NIC, but synchronized eight-way fan-in overwhelms finite queues.
These failures call for credits, pacing, staggering or bounded result streaming;
raising average bandwidth alone does not explain the cliff.

**A cheap relay can be the overloaded resource.** At 2,000 messages/s with an
edge NIC limited to 10B/µs and 256KiB queues, cloud-direct still completes all
100; cloud-edge-relay completes 42. Edge-origin direct and regional routes
complete only 3 and 7. Lower total charged bytes in these failed cases are not
savings at equivalent delivered service. Byte tariff and available capacity are
different properties of an edge.

**A relay and an origin fail differently.** Crashing the edge from 50–150ms
leaves cloud-direct unaffected. Cloud-edge-relay completes all 100 through
surviving-origin retries, but p99 rises to 170.122ms and transfer cost to 228.264
millionths. With the edge as the origin, only 80 complete: the simulator has no
durable upstream queue supplying the 20 inputs born while that origin is down.
This is a deliberate input-possession limit, not a universal claim that a
well-provisioned edge service must lose them. No receipt, retry or route change
can recreate absent information; adding an upstream durable holder is different
work and must be charged.

## Private links, peer retrieval and NAT bypass have distinct break-even terms

These are explicit algebraic comparisons in `economics()`, separate from the
packet simulation. All terms use the same declared billing horizon and preserve
the requested result. They are cost boundaries, not complete deployment models.

- **Private interconnect.** If fixed/setup/reserved-capacity cost is F and the
  variable saving is Δc per GB, break-even volume is F/Δc when Δc is positive.
  With illustrative F=200 and .09→.02 per GB, break-even is 2,857GB over that
  horizon. At 100GB, public cost is 9 versus private 202; at 10,000GB it is 900
  versus 400. Idle/redundant capacity, provider/colocation charges, path capacity,
  commitment length and failure exposure belong in F or additional terms. A
  fixed-price facility cannot generally be represented by an independent linear
  cost on every message edge; it couples multiple workloads.
- **Peer before blob.** Let blob request cost be q, blob transfer bB, peer lookup
  cost l, peer-hit transfer pB and exact-version hit probability h. Peer-first
  cost is l + h·pB + (1−h)·(q+bB). Its saving is h·(q+bB−pB)−l. With q=4e−7,
  l=2e−8, b=0 and p=.01/GB, a 128-byte object with 90% hits saves about 85%;
  zero hits add lookup cost, while 1MiB objects lose even at 100% hits. A false
  positive locator, stale version, slow peer or peer miss can also add latency
  and control work. A maintained cache/directory and its retention are not free.
- **Gateway filter/direct destination.** Move the authorized packet filtering or
  small program near the database, establish a direct permitted connection, and
  keep bulk bytes away from a metered NAT path. A simple comparison uses .04/GB
  processing versus .005/GB extra direct-path cost plus 10 fixed units: direct
  loses at 100GB (10.5 versus 4), wins at 1,000GB (15 versus 40). The application
  still needs authentication, filtering, addressing, connection setup/repair and
  an allowed reply path. This does not imply bypassing required policy, or that
  NAT traversal always succeeds.

Actual tariffs are often **directed, tiered, service-specific and nonseparable**.
As source checks on 2026-09-26, the official [EC2 pricing page](https://aws.amazon.com/ec2/pricing/on-demand/)
distinguishes inbound/outbound transfer and aggregate outbound tiers;
[VPC pricing](https://aws.amazon.com/vpc/pricing/) separates NAT provisioned hours,
processed bytes and ordinary transfer; and [Direct Connect pricing](https://aws.amazon.com/directconnect/pricing/)
separates capacity, port hours and data transfer. [Cloudflare's platform pricing](https://developers.cloudflare.com/workers/platform/pricing/)
also distinguishes product-level operation, compute and egress charges: a
product with zero egress does not imply every product at that provider has it.
Those pages establish the need for these terms, not the authored coefficients
above or permission to use any particular product as an unrestricted relay.

## Reproduce

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_edge.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/edge_study.py \
  --count 100 --output build/workbench/orbital-dissemination/edge.json
```

The source-hashed [retained evidence](evidence/20260926/edge.json) contains every
selected configuration, route, directed tariff, completed/unfinished count,
queue state, resource service, actual completed-TX bytes and retries. Seven
checks cover directed prices, route coverage, both origins, paid extra paths,
source loss and economic sign changes. A transmission still active at the final
drain is not yet included in the completed-TX byte counter, as in the shared
model. Routing is static; tariff discovery, live interconnect purchase, placement
control and secure network setup are not implemented.
