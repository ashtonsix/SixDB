# Model and assumptions

The model compares network delivery plans under declared synthetic conditions.
Its outputs are conditional predictions, not measurements of AWS, WireGuard,
TCP, UDP, storage, consensus or database transactions. Figures and the HTML
workbench retain that distinction beside their results.

## Delivery and capacity

An object is released according to a seeded Poisson arrival schedule. Every
policy in a scenario receives the same arrivals and required recipients. Online
policies may refuse work before admission; those objects retain outcome rows,
count as incomplete, and remain in every offered-work denominator.
The burst fixture scales alternate groups of 16 sampled interarrival gaps;
it is an exogenous burst trace, not a claim to implement a continuous-time
Markov-modulated process. All objects drain or remain explicitly incomplete;
waiting after the last offered arrival stays in latency and cost.

At the source, all object bytes are initially available. An edge sends a copy
of each payload chunk. A relay forwards a chunk only after all its bytes arrive
and receive-side CPU work completes. It can receive the next chunk while
forwarding the previous one. The source retains bytes for the duration of the
simulation, including repair. There is no storage durability operation.

Each directed edge names capacity pools: sender TX, receiver RX, its own link,
and any shared corridor/uplink. TX and RX are separate full-duplex budgets.
The private-corridor fixture deliberately has one aggregate pool shared in both
directions. A modeled pool need not correspond to one physical cable.

For active stream rates `x_i`, capacities `C_r` and positive weights `w_i`:

```text
sum(x_i for streams i using resource r) <= C_r
```

Progressive filling gives a weighted max-min allocation over all these
constraints. Data and control have equal network weights in the retained
campaign. `control_weight` can change this. Chunks serialize FIFO within an
`(object, directed edge, traffic kind)` stream; otherwise equal-sharing every
chunk would artificially delay the first chunk until almost the whole object
was sent. Different objects and outgoing edges are different competing streams.

Serialization consumes all named pools concurrently at the allocated rate.
Propagation consumes no bandwidth capacity after serialization finishes.
Resource counters integrate serviced wire bytes over time and assert capacity
conservation at each event. This is a fluid abstraction: allocations can change
inside a chunk. It is not packet-level scheduling, TCP congestion control or a
modeled reliable transport window.

CPU work is a single serialized queue per host, shared between send and receive:

```text
CPU microseconds = chunk overhead + wire bytes / crypto bytes per microsecond
                   + packet count * per-packet overhead
```

It is non-preemptive per chunk. Small metadata, request, repair and confirmation
jobs precede queued payload jobs. These costs are assumptions, not encryption
benchmarks. Optional edge setup delay gates the whole stream before its first
serialization; subsequent chunks cannot bypass it. It defaults to zero.

## Parameters and their status

All exact inputs are in the retained `config.json`. These values make scenarios
plausible enough to expose mechanisms; their precision does not imply calibration.

| Input | Synthetic default / alternatives | Basis and interpretation |
| --- | --- | --- |
| Host TX and RX | 10 Gbit/s each; edge source 2.5 Gbit/s | Explicit capacity assumptions, not an EC2 SKU or burst-credit claim. |
| Shared outbound WAN | 2.5 Gbit/s per source region | Forces distinct overlay edges to share a real budget in the model. |
| Private corridor | 1 Gbit/s aggregate, $1/hour | Assumed provisioned resource; hourly charge is shown separately. |
| Same-zone one-way delay | 25 µs | Assumption. |
| Cross-zone one-way delay | 125, 175, 200 µs | Plausible scale motivated by the CFT study, not half-RTT measurements or one-way percentile estimates. |
| Cross-region one-way delay | 35, 40, 65 ms | Illustrative regional distances; host coordinates only draw the schematic. |
| Per-host CPU | 32 Gbit/s byte processing, 1 µs/chunk, 0.08 µs/packet | Assumptions shared by all policies; edge source uses 16 Gbit/s. |
| Blob service | 1.5 ms each way plus 3 ms service delay, 5 Gbit/s | Constant service delay plus network queues; service worker saturation is not modeled. |
| Cross-zone transfer | $0.02/decimal GB, aggregate | Assumed combined charge on the directed transfer; no extra receiver charge is added later. |
| Same-provider inter-region transfer | $0.02/GB | Assumption. |
| Public egress | $0.09/GB from A, $0.04/GB from B, $0.005/GB from edge | Deliberately asymmetric illustrative rates. |
| NAT processing | Additional $0.045/GB on public A↔B routes | The same byte can incur both transfer and processing charges. |
| Private transfer | $0.015/GB | Variable charge conditional on the provisioned link. |
| Blob retrieval | $0.0000004/response, zero regional byte charge | Illustrative request/byte tradeoff. |

Pricing shapes are informed by primary public examples, checked 18 September
2026. AWS's [VPC pricing examples](https://aws.amazon.com/vpc/pricing/) combine
NAT processing, hourly NAT charges and internet egress; its
[S3 pricing](https://aws.amazon.com/s3/pricing/) includes request charges and
same-region transfer exceptions. [EC2 pricing](https://aws.amazon.com/ec2/pricing/on-demand/)
depends on endpoints, tiers and exceptions. This simplified ledger omits free
allowances, tiering, account placement and provider-specific byte definitions;
it is not a bill estimator. Values for providers A/B are not attributed to real
vendors. No live price API is required to replay a run.

Fixed host, relay and provisioned-link charges are **excluded from variable
USD/GiB**. The UI displays a used private corridor's fixed obligation. Compare
fixed and variable costs at an explicit traffic volume before choosing whether
to provision it. For one otherwise identical charged leg:

```text
break-even GB/day = 24 * added hourly cost / saved USD per GB
```

This one-leg equation does not include topology changes or duplicated transfers.

## Framing, jitter, losses and repair

The framing example uses an IPv6/UDP outer header, WireGuard-style data header
and authentication tag, and an inner IPv6/UDP packet with 32 bytes of application
framing. Outer overhead is 80 bytes; inner overhead is 80 bytes. The encrypted
inner packet rounds up to 16 bytes. `wire()` splits each forwarding chunk to fit
outer MTU 1500 or 9000 and accounts for the final partial packet. See
[WireGuard's protocol description](https://www.wireguard.com/protocol/).
This supplies byte and packet counts; it does not emulate WireGuard or FEC.

Delay jitter contains an exponential component shared by an object's directed
zone pair, plus a smaller edge/chunk component. Regional scale is 2 µs;
long-distance scale is 1% of nominal delay. This models some correlated
variation, not a measured distribution or long-lived congestion process.
Counter-based random draws identify seed, object, edge/domain, chunk and retry;
algorithm event order cannot consume another policy's random draws.

For independent packet loss probability `p` and `n` packets, a chunk is lost
with probability `1-(1-p)^n`. A lost chunk retransmits in full after a declared
timer, up to the retry limit. This conservative chunk retry rule intentionally
differs from a transport that selectively resends only missing packets. A chunk's
metadata/control category participates in the same capacity and cost accounting.

The forwarding-failure fixture blackholes **data egress** from one modeled
relay during the specified serialization interval. It is not process, host or
AZ death: incoming payloads, control traffic and chunks already in propagation
remain alive. The common-cut fixture instead blackholes data using a named
shared capacity pool. Blackholed chunks consume their full modeled wire service
and price; no delivered partial chunk can be forwarded.

Optional repair is receiver-driven. Direct 256-byte metadata announces each
object. After `fallback_us`, an incomplete recipient sends a request naming its
missing chunks to the retained root. The root responds using the requested
snapshot, even if an original copy has arrived meanwhile. All request, duplicate
response and confirmation bytes count. A failed primary and backup using the
same failed pool remain jointly vulnerable. One repair timer is modeled; this
is not an indefinitely self-healing transport.

## Candidate algorithms

| Candidate | What it optimizes or selects | What it does not promise |
| --- | --- | --- |
| Price + latency | Search legal reparentings using price, shared-resource service, pipeline and queue proxies; simulate a shortlist with independent planning arrivals; first minimize incomplete planning objects, then choose lowest price within the configured allowance of the best planning percentile | Best found in the simulated shortlist, not a global optimum or an evaluation-tail guarantee. |
| Fastest planning candidate | Fastest planning percentile among shortlist candidates with minimum incompletion | The planning horizon and synthetic inputs can misrank held-out tails. |
| Same-price depth reduction | Remove depth through equal-or-lower-price reparenting | Diagnostic seed; shared TX contention can justify a deeper pipelined tree. |
| Robust design + adaptive repair | Minimize worst-design incompletion, then worst-design deadline failures; choose lowest design-mean variable price within an allowance of best worst-design p90 | Declared design cases are not probabilities or an exhaustive failure model. |
| Direct | Fastest nominal direct edge from source to each recipient | It does not share copies across expensive boundaries. |
| Shortest | Dijkstra tree using unloaded whole-object serialization plus propagation/setup | Shared capacity, CPU queues and chunk overlap can reverse its ranking. |
| Cost | Rooted directed minimum spanning arborescence, edge price = chunked wire bytes plus request fee | No completion-time or failure bound; zero-price ties can form relay chains. |
| Bounded | Start at shortest tree; greedily apply the largest cost-saving reparenting that preserves per-target nominal stretch | Best found by this heuristic; neither a constrained optimum nor a simulated deadline guarantee. |
| Peer / blob | Fixed eligible holder for request and response | Does not imply semantic serving eligibility. |
| Cheapest / bounded fetch | Minimum variable price including request, response and receipt; bounded variant filters by unloaded response deadline | Does not predict queueing or service saturation. |

The [NetworkX directed-arborescence implementation](https://networkx.org/documentation/stable/reference/algorithms/generated/networkx.algorithms.tree.branchings.minimum_spanning_arborescence.html)
supplies the cost optimizer. Removing all incoming edges to the root fixes the
root. On the retained propagation graphs every included non-root node is a
required recipient. If a custom graph includes optional relays, the implementation
spans them and prunes unused leaves; that is a heuristic for the directed Steiner
problem, with no minimum-cost claim for the optional-relay choice.

The price/latency search retains multiple epsilon-constrained tradeoffs rather
than adding incomparable dollars and microseconds. Its proxy is only a proposal
mechanism: per-object wire demand divided by each pool's effective capacity,
CPU demand, a first-chunk path estimate, and a queue penalty. The queue proxy is
M/D/1-inspired below saturation and a finite-backlog penalty above it; it is not
a tail prediction. A beam applies up to five rounds of legal parent changes.
Direct, nominal-shortest, price-only, bounded and shallow trees remain explicit
seeds. The shortlist is then replayed with seed 271828, normally 12 objects.
Bursts use at least 32 so both declared phases occur. The default selection
allows 10% above the best shortlist planning p90, then minimizes variable price,
completion time, mean recipient completion and depth. Incomplete planning
objects rank before those quantities. No finite bound is claimed when its
planning percentile is infinite. Search/shortlist sizes and the horizon are
retained with every selection. Ordinary CPU and shared-network queues are in
that replay. Known repair timers remain enabled; future evaluation failures,
capacity changes, packet losses and jitter do not enter planning.

Robust selection uses a second independent seed, 314159, and a common declared
ensemble for every candidate: nominal, a 30% capacity haircut, and one named
forwarding source's data-egress blackhole at a time. The haircut multiplies the
existing capacity scale and preserves background traffic. The exact risk tuple
is `(maximum incomplete/offered, maximum missed-deadline/offered)` across cases.
Among the best risk tuple, a latency allowance bounds worst-design p90; the
lowest equal-design-mean variable price wins. Those equal weights describe a
sensitivity experiment, not estimated failure probabilities. Rejections count
as incomplete. Trial outcomes, price, repair, duplicates and rewrites are
retained. These design faults are available to simulator physics only; the
controller still has to observe their effects. Neither optimizer calls itself
recursively from the online controller.

## Online controller and bounded admission

The static baseline uses a fixed tree and absolute one-shot repair timer.
The adaptive policy instead waits for a configured interval without new chunk
progress. It sends a missing-chunk request through the same modeled network.
Only when that request reaches the root can it affect routing knowledge.
The root follows the object's original path back to the first unacknowledged
holder after a known holder; it does not blame a downstream relay that may
never have received data. Failed repairs subsequently implicate their actual
chosen repair edge. Two distinct suspected paths sharing a non-endpoint pool
can trigger a temporary pool suspicion. This is a fallible inference with a
TTL, not access to the simulator's fault truth.

The controller queues a modeled decision job on the source CPU (50 µs in the
online fixtures). After a minimum rewrite interval, it evaluates legal one-parent
changes, prioritizes fewer suspected paths, then compares the price/latency
proxy. Pending decisions collect intervening reports. The default hold interval
is 5 ms and suspicion TTL is 20 ms. These constants are assumptions, not measured
optimizer runtimes. They expose detection and reaction delays rather than
making rewrites instantaneous. The initial offline search runtime is outside
delivery latency; the online decision cost is an explicit input.

New objects capture the current tree. Already admitted objects retain their
original acyclic forwarding tree. Repair adds direct copy requests from holders
whose full-copy receipts have reached the root; the root itself retains the
payload. Requesting another holder sends a real command before that holder can
send the requested chunks. The holder checks possession locally. Duplicate
chunks do not forward again, including when repairs overlap original traffic.
No old transmission is canceled for free. Requests, commands, retries and all
serviced duplicate bytes consume CPU, network capacity and charges. The policy
performs at most three repair rounds in the online fixtures. It does not
proactively probe/rejoin recovered paths or change replica membership.

The in-flight window reserves one immutable payload per included node for each
admitted object. A full window refuses a new delivery before accepting it;
refusals stay in the offered sample population. An admitted obligation stays
active until every receipt is back **and** all queued/in-flight transmissions,
including old duplicates, drain. An unfinished object does not release its
reservation simply because a deadline passes. This caps concurrent transport
payload reservations and bounds generated chunk/control work by the window and
retry limits. It is a conservative shared object window, not independent
per-host buffer allocation, spill-to-disk, a connection limit or a model of
Python's own simulation bookkeeping memory. Payload sends reference immutable
chunks rather than allocating a full extra retained object per outgoing edge.

External capacity steps modify physical service in the simulation, without
updating the controller's assumed graph directly. It reacts to resulting stalls.
The workbench shows initial and final trees plus timed observation, repair and
rewrite events. Failures remain data-forwarding faults; dead required recipients,
source loss and source durability require a different obligation/recovery model.

No static tree models fan-in aggregation or request batching. Metadata is sent
directly by default; the control-tree scenario follows the payload tree instead.
Every delivered object returns a 128-byte direct confirmation from each required
recipient. Confirmation latency is a separate metric from payload availability.

## Reading the results

Primary latency is object release to the last required recipient's complete
payload. Fetch includes the request path and service delay before the response.
The report also retains first chunk, first full remote-zone copy, direct metadata
and returned-confirmation latencies. These events are transport facts only.

Quantiles use linear interpolation over all offered objects, representing
incomplete delivery as infinity. A displayed incomplete quantile is never
replaced with a successful-only estimate. Deadline success also includes all
offered objects. Small scenario populations make p99.9 little more than a view
near the simulated maximum; the UI exposes sample counts and seeds.

The campaign compares finite arrival cohorts, not a measured steady-state service
capacity. Vary capacities, workload volume, delay, CPU, chunk size, MTU and failure
assumptions before transferring an algorithm preference. Planning runtime is
outside modeled delivery and is not benchmarked here. The bounded window is a transport admission experiment; independent buffer pools,
connection limits, multi-source coordination, tenant fairness, durable spill and
recovery remain outside this implementation. There is no empirical calibration
of observation noise, controller costs, failure probabilities or tail dependence.
