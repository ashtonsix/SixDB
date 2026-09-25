# Separating hosts, flow tuples, roles and time

The [network question](README.md) is how to find a repeatable outgoing
leader→follower edge. The controlled experiments compare tuples on fixed hosts,
then revisit those exact tuples, then compare hosts within an AZ pair.
[Independent clock calibration](clocks.md) bounds the two directions without
assuming their symmetry. Offset-independent RTT is the primary check that a
large tuple effect is real even when its directional split is uncertain.

## What each comparison isolates

| Comparison | Held fixed / varied | What remains unresolved |
| --- | --- | --- |
| Ports on one host pair | Same IPs/machines; shuffled explicit UDP tuples | Tuple-sensitive NIC, virtual-network and physical-route mechanisms are not separated |
| Same tuple and initiating role in another round | Both IP/port endpoints and role retained; time and socket lifetime change | Closing/reopening and elapsed time occur together; no independent estimate of each |
| Reversed initiator on the same tuple | Endpoint ports stay attached to their hosts | Role changes with round parity, so this is not a pure time repeat |
| Different host pairs in one AZ pair | Same AZ IDs, protocol and sample budget | Host, rack, path and shared time-reference effects are not individually assigned |
| Different AZ pairs | Same campaign and controlled sampling design | Finite host/port candidates do not identify an intrinsic AZ latency distribution |

The request is the closer analogue of a leader send. A reply follows receipt
of the other packet and can encounter a different receive/scheduling state.
Both legs are retained, with role explicit. A lower RTT identifies a promising
pair, but cannot select its initiating side without directional evidence.

The original [TCP study](studies/tcp-method.md) used one host per AZ and new
connections between cases. The first two [one-way cohorts](studies/oneway-initial.md)
used new ephemeral tuples between passes while alternating initiators. The
initial cohort did not retain its ephemeral port numbers; the confirmation
did. Those studies exposed variation but mixed flow choice, time and role.
Their pass changes must not be described as one fixed flow changing latency
over time.

## Common capture boundary

Each exchange is a 64-byte UDP request and same-size reply over private IPv4,
MTU 1500, default VPC, without a placement group or ENA Express. Kernel software
TX/RX stamps measure the two network legs; source elapsed time minus peer
turnaround supplies RTT. There are no PLP writes, throughput load or simultaneous
two-follower sends. Hosts have one active peer at a time in disjoint matchings.

Exchanges are paced 5 ms apart with no catch-up burst. The first 20 of every
block are retained as warmup and excluded from statistics. Each fixed-tuple
block opens new sockets with recorded endpoint ports and closes them afterwards. Matching and
tuple order are shuffled; control barriers and uploads occur between blocks.
Probe, responder and reference collection use the recorded CPU placement.
[Clock references and boundary diagnostics](clocks.md) are collected throughout.

| September 24 cohort | Host coverage | Tuples / rounds / exchanges per block | Initiator and selection rule |
| --- | --- | --- | --- |
| [Initial one-way](studies/oneway-initial.md) | Two per AZ; all 66 host pairs | New ephemeral tuple / 4 / 1,000 | Alternate roles; descriptive, no holdout |
| [Fresh confirmation](studies/oneway-confirmation.md) | Two fresh per AZ; all 66 pairs | New ephemeral tuple / 4 / 500 | Alternate roles; descriptive, no holdout |
| [Dense control](studies/tuple-dense.md) | Four per AZ in az2 and az4; 16 cross-AZ pairs | 16 fixed tuples / 4 / 100 | Train 0–2 on all legs; hold out 3 |
| [Broad comparison](selection.md) | Two fresh per AZ; 60 cross-AZ pairs and 6 same-AZ controls | 4 fixed tuples / 5 / 80 | Train 0–2 on requests; hold out 3–4 |

All hosts in the dense control are M7i.xlarge with ENA PHC. Each all-six-AZ
cohort uses ten M7i.xlarge and two I4i.xlarge in az3, where the captured offerings
query found no supported PHC family. Az3 remains in the result with broader
NTP bounds. The cohorts use distinct machines; repeated host labels are aliases
within a cohort. [Evidence and accounting](../evidence.md#network-cohorts)
retain exact timestamps, completed populations and traffic budgets.

## Dense control: change both ports, preserve the tuple on reversal

The sixteen lower-AZ ports are 48000–48015; higher-AZ ports are 48100–48115.
Tuple index chooses the corresponding port at each endpoint. Four disjoint
matchings cover the sixteen host pairs. Lower AZ initiates rounds 0 and 2;
higher AZ initiates 1 and 3. Ports remain attached to their hosts on reversal.

Before inspecting results, the declared rule selected the lowest **worst
training-round median** over rounds 0–2, with worst p90 as tie-breaker. All
256 host/tuple candidates per direction are retained. Round 3 is withheld
from selection, reporting p50/p90/p99, clock bounds and RTT.

That single holdout observes lower-AZ outgoing legs as **replies**, and higher-AZ
outgoing legs as **requests**. It cannot validate both as leader requests.
With 80 post-warmup packets per block, p99 is close to the maximum and is
only descriptive. Selection by median does not establish a tail budget.

## Broad comparison: change one port, hold out both initiating roles

Only the lower-numbered host's port changes (48000–48003); the other endpoint
stays on 48100. The lower host initiates rounds 0, 2 and 4; the higher host
initiates 1 and 3. Eleven disjoint matchings cover all 66 host pairs. This
isolates whether changing **one endpoint port** suffices for a different class.
For reverse leader directions, that changed endpoint is the follower.

The declared rule trains on rounds **0–2** and validates on **3–4**, so each
direction receives one held-out request and one held-out reply. The primary
ranking uses the lowest worst training **request-role median**, then worst
request p90. The all-leg ranking is retained in separate output files for
comparison with the dense design. Candidates have only one or two training
request blocks and one validation request block, each with 60 post-warmup
packets; this is an exploratory screen, especially at p99.

Selection is performed at two levels:

- **Fixed directed host pair:** choose one of four flows in training; compare
  its held-out request with flow 0 on those same hosts. This isolates the
  observed value of port selection without replacing machines. Flow 0 is a
  specified comparator, not a random production baseline.
- **AZ direction:** choose among sixteen host/flow candidates (four host pairs
  × four tuples), then assess that candidate's held-out request. Opposite
  directions may choose different hosts and ports; subtracting their winners
  would not measure paired asymmetry.

Held-out packet delays never enter selection. Independent clock fits use the
full capture's local references, however, so this is an **offline holdout**,
not a demonstrated online selection available at the end of round 2.

## Validation and limits of inference

Analysis verifies the complete expected host/flow/round/direction grid, unique
physical tuples, endpoint ports and role parity before reporting stability.
Same-role temporal diagnostics include every adjacent comparison: 0→2, 1→3,
and for the broad cohort 2→4. Different-role pairs are reported separately.
Clock analysis rejects contradictory references, invalid packet joins, missing
coverage and point estimates outside packet-causality bounds.

The complete candidate tables retain unfavorable and unselected results.
Quantile error bounds apply to observed samples under the stated clock model;
they are not confidence intervals or guarantees about unseen traffic. More
hosts expose variation, but shared clock bias cannot be averaged away.
Separate link marginals cannot recover the joint two-follower race or assume
independent tails. There is no production-load, long-duration, host-restart or
QUIC-migration experiment here.

The [selection findings](selection.md#a-cheap-reroll-and-what-retaining-it-means)
distinguish short-window tuple repeatability from IID rerolls and stationarity.
The [capture and analysis tools](../variance/README.md) retain their original
paths so archived sources and recovery commands stay usable. Recovery fetches
existing archives and does not launch workers; a new capture is a new cohort.
