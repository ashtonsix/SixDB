# Packet size, loss recovery and FEC

Packet processing and recovery granularity can change the useful operating
range as much as routing. In these **synthetic** comparisons, larger packets
rescue a large-message CPU bottleneck but do nothing for tiny messages already
fitting one packet. Ideal FEC helps independent erasures and can lose under
correlated loss or insufficient capacity. Neither result selects a production
transport or establishes the 170/250 µs durable-effect target.

This follows network survey [N17–N19](SOURCE-SURVEY-NETWORK.md#n17),
[N58–N63](SOURCE-SURVEY-NETWORK.md#n58) and
[N74](SOURCE-SURVEY-NETWORK.md#n74). There are 24 finite-queue network comparisons
and 44 separate exact ideal-code calculations in the
[retained evidence](evidence/20260926/transport.json).

## Network experiment and boundaries

[transport_study.py](transport_study.py) sends 400 messages from one source to
one receiver, 80 µs apart geographically in the model. Both endpoints have
10 Gbit/s TX/RX, two CPU slots, 0.35 µs packet CPU plus 0.00002 µs per wire byte,
and 256 KiB limits on each modeled queue/reassembly resource. The two CPU-stress
profiles explicitly change CPU costs/slots. There is no persistence or witness.
Completion means the first complete usable message at the receiver. ACKs are
real reverse packets; retries send the whole message after 1,000 µs, then
2,000 µs, at most three attempts total. The existing model does not selectively
resend a missing fragment or reuse fragments between attempts.

MTU here is the model's **maximum packet size including 80 bytes of framing**,
with a further 16-byte per-transfer identity inside the packetized payload.
These are assumptions, not exact WireGuard encapsulation. MTUs 512, 1400 and
9000 are assumed to work throughout the path. This is neither IPv6 path-MTU
discovery nor evidence a public Internet path supports 9000-byte packets.

All offers are periodic, with a 100 ms drain. Loss cases use 2% independent
loss **per packet regardless of its size**, a periodic directed blackout, or
50% ACK-packet loss. MTUs therefore change the number of loss opportunities.
The shared seed makes each run reproducible; changed packet counts consume
different random draws, so this is not packet-by-packet paired loss across MTUs.
The burst case blocks source→receiver from 2,000 to 2,400 µs and every 5,000 µs
thereafter, through the offer period. No sender learns this fault schedule.

The counters include retransmissions, ACKs and traffic after useful delivery.
Queue overflow counts rejected resource submissions, **not unique messages**.
The p99 column below is conditional on completed messages; completion and
late/unfinished counts stay next to it. With only 400 offered messages, neither
p99 nor the retained p99.9 is a population-tail guarantee.

## Selected finite-queue comparisons

Wire MB is decimal MB, CPU ms sums both endpoints. Late/unfinished uses a
250 µs deadline except the large CPU case, which uses 500 µs.

| Workload | MTU | Completed / offered | Late or unfinished | Completed p99 µs | Wire MB | CPU ms | Retry messages |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 B, 2,000/s | all three | 400/400 | 0 | 80.83 | 0.112 | 0.564 | 0 |
| 64 B, 400,000/s, one CPU slot at 4 µs/packet | all three | 400/400 | 370 | 2,262.85 | 0.182 | 10.423 | 251 |
| 64 KiB, 500/s, no loss | 512 | 400/400 | 0 | 142.89 | 31.133 | 44.085 | 0 |
| same | 1400 | 400/400 | 0 | 136.40 | 27.869 | 15.395 | 0 |
| same | 9000 | 400/400 | 0 | 134.01 | 26.525 | 3.581 | 0 |
| 64 KiB, 8,000/s, two CPU slots at 4 µs/packet | 512 | 4/400 | 399 | 936.54 | 13.761 | 215.598 | 793 |
| same | 1400 | 400/400 | 0 | 189.84 | 27.869 | 164.315 | 0 |
| same | 9000 | 400/400 | 0 | 141.31 | 26.525 | 29.861 | 0 |
| 64 KiB, 2,000/s, 2% independent packet loss | 512 | 27/400 | 391 | 3,205.06 | 90.849 | 126.750 | 769 |
| same | 1400 | 200/400 | 309 | 3,192.03 | 65.542 | 35.364 | 542 |
| same | 9000 | 400/400 | 61 | 3,186.85 | 31.224 | 4.129 | 71 |
| 64 KiB, 2,000/s, periodic blackout | 512 | 400/400 | 40 | 1,205.06 | 34.241 | 46.275 | 40 |
| same | 1400 | 400/400 | 40 | 1,192.03 | 30.651 | 16.150 | 40 |
| same | 9000 | 400/400 | 40 | 1,186.85 | 29.172 | 3.746 | 40 |

The small-message overload is unchanged by MTU: every message already uses one
packet. It also produces 251 timeout retries with **zero queue overflows**;
queueing can make a fixed retry timer amplify load before a buffer overflows.
Batching/pacing/timer adaptation is the relevant next comparison there.

For 64 KiB, packet counts per initial message are 152, 50 and 8. This explains
the CPU difference, not a hardware measurement. The 512-byte large CPU case
hits its 256 KiB queue ceiling and records 154,738 overflow submissions. Its
lower total wire volume is failed service, not an efficiency win. At quiet
load, reducing CPU work by more than an order of magnitude moves p99 by only
8.88 µs because the remaining propagation/serialization dominates.

Whole-message retry couples losses to finite reassembly state. In the large
independent-loss cases, 512/1400/9000 have 654/343/0 overflow submissions.
Incomplete assemblies occupy retained bytes until expiry; loss of one fragment
can cause the whole message to be resent. These outcomes should motivate
packet-selective recovery and explicit reassembly admission, rather than a
general claim that the largest packet is optimal. With the periodic blackout,
every MTU has exactly 40 late messages and retries: changing packet count does
not remove the outage.

Lost ACKs expose another distinction. At MTU 1400, 50% reverse loss leaves all
400 messages delivered on time with the same 136.40 µs p99, but increases wire
from 27.869 to 47.795 MB and CPU from 15.395 to 26.285 ms. There are 286 retries
and 45 still-unacknowledged transfers after the retry limit. A useful-message
latency plot alone hides that work and the sender's unresolved knowledge.

There is also a discrete framing boundary: at MTU 1400, a 1,304-byte message
plus identity is one 1,400-byte packet; a 1,305-byte message requires two packets
and 1,481 wire bytes. Compression, enrichment and batch-size choices should
therefore count packets as well as bytes.

## Exact ideal-code comparison

The separate analytical section assumes an ideal systematic `(k+r,k)` erasure
code: any k received symbols suffice, matching the MDS recovery property
described in [RFC 5510](https://www.rfc-editor.org/rfc/rfc5510.html).
It does **not** encode/decode bytes, authenticate repair symbols, simulate
multi-receiver feedback or implement Reed–Solomon. It uses eight source symbols,
0/1/2/4 parity symbols, 1,400 charged wire bytes per symbol, and at most three
whole-block attempts. Attempts are independent even where losses *within* a
block are correlated. A persistent failed path violates that assumption.

Three exact loss distributions have the same marginal loss parameter p:

- IID Bernoulli packet erasure.
- A stationary two-state chain with `P(next lost | current lost)=0.8` and
  `P(next lost | current good)=p*(1-0.8)/(1-p)`.
- A common cut erasing the entire block with probability p.

For one-attempt success q, expected attempts are `1 + (1-q) + (1-q)^2`;
expected wire bytes multiply that by the complete coded block size. This
accounts for failure traffic, with perfect progress feedback before another
attempt. It is not the preceding Network ACK-loss model.

| Loss model, p=5% | Parity | First-attempt success | Expected wire bytes over at most 3 attempts |
| --- | ---: | ---: | ---: |
| IID | 0 | 66.342% | 16,238.49 |
| IID | 2 | 98.850% | 14,162.90 |
| Bursty chain | 0 | 88.217% | 12,675.17 |
| Bursty chain | 2 | 92.276% | 15,164.90 |
| Common cut | 0 | 95.000% | 11,788.00 |
| Common cut | 2 | 95.000% | 14,735.00 |

At IID loss, two parity packets improve first-attempt success while reducing
expected bytes because whole-block retransmission is so wasteful. At the same
marginal loss in bursts, the improvement is smaller and costs more bytes. A
whole-block cut gets no protection and pays exactly 25% more wire. Equal mean
packet loss does not imply equal redundancy benefit. Interleaving or path
diversity might change these distributions, but costs time and is not modeled.

The illustrative no-queue timing adds 80 µs propagation and serialization,
plus an assumed 5 µs encode and 8 µs decode for coded blocks. It waits for a
whole block slot rather than modeling early decode. Uncoded arrival is
88.96 µs; two-parity arrival is 104.20 µs. For a 110 µs first-attempt deadline,
the first-success column is also the deadline success fraction. At 100 µs,
this deliberately charged code misses even when all symbols arrive. These CPU
costs are assumptions, not a coding benchmark or a limit on optimized decoding.

Finally, redundancy can cause its own overload. At zero loss, an uncoded stream
using 85% of a 1,250 B/µs capacity becomes **106.25%** utilized with 8+2 symbols.
The excess 78.125 B/µs fills an additional 256 KiB of buffering in **3,355.44 µs**.
This exact capacity counterexample has no congestion controller or magic
capacity increase. FEC should be charged to the same service budget as data.

## What remains open

Selecting a supported packet size requires observed path behavior and recovery
from change, not setting one global constant. [RFC 8899](https://www.rfc-editor.org/info/rfc8899/)
specifies packetization-layer discovery for datagram transports. UDP guidance
also treats application fragmentation, congestion response, middleboxes and
probe costs as coupled concerns; it recommends independently recoverable
application fragments. [RFC 8085 §3.2](https://www.rfc-editor.org/rfc/rfc8085.html#section-3.2)

The useful follow-up is a bounded per-path controller with probe traffic,
stale MTU state, packet-selective repair, explicit reassembly credits and
congestion response. Then add an actual codec with measured encode/decode cost,
authenticated parameters, burst-aware parity/interleaving and common-cut
failures. Changing static MTU or calculating an ideal code is not that
controller. Batching, FEC block size, packet size and relay forwarding grain
must remain separate knobs.

## Reproduction and checks

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_transport_study.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/transport_study.py \
  --output build/workbench/orbital-dissemination/transport.json
```

Seven checks pass: independent enumeration of all Markov erasure patterns up
to eight packets, PMF mass/marginals, exact single-message/framing ledgers,
total packet loss, lost ACK versus logical completion, finite resource bounds,
parity overload and deterministic replay. No core simulator files changed.
The driver hashes itself and `simulator.py` before/after the campaign and
refuses to write a result if either changes.

```text
transport_study.py 1793a2939e85f46cab7becb9a5adb3e83c208f631ce067546f601879d7d19934
simulator.py       a3fd09a8d7985d27af6afd22fa3c43e73f7b8fd0fe9b1a6177f085af943bd3f0
```
