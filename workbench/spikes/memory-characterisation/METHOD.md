# Method, interpretation and reading

## What initialization can know

CPU recognition and property lookup are different operations. `hardware.py`
reads the selected CPU's identity (including family/model/stepping or MIDR),
microcode where exposed, cache descriptors, sharing groups, SMT siblings,
NUMA topology, kernel and page policy. Its architecture name is only a label.
The lookup key includes the environment that qualified the measurement; a
similar marketing name does not establish identical behaviour.

Cache geometry and the base page size are reported facts. HugeTLB directories
advertise kernel-supported sizes, and their counters describe the current pool;
they do not promise an allocation will succeed. THP policy is permission to
attempt larger mappings. The probe records each mapping's `smaps` page fields,
`AnonHugePages`, `VmFlags` and `numa_maps`; a successful `MADV_HUGEPAGE` call alone
is never counted as a huge mapping. No global page policy is changed.

The initialization experiment has a lookup path and a bounded fallback. The
quick profile limits the largest footprint, repetitions, training patterns
and time per throughput sample. Total time includes allocation, prefaulting,
chain construction/validation and calibration. Scheduling delays and OS calls
mean this is a work bound, not a wall-clock deadline. A production caller could
supply a deadline and defer optional probes; that cancellation API is not built.
Storage/network measurements are explicitly optional and never imported as
CPU constants. They depend on volume, filesystem, instance size, provisioning,
route, congestion and endpoint, not just instruction-set identity.

## Memory-level parallelism

For each footprint, build a shuffled permutation of one node per reported
cache line and split it into K disjoint cycles, K = 1, 2, 4, 8, 12, 16, 24, 32,
48, 64. Every pointer is XOR-encoded, preserving a true address dependency
while avoiding a directly stored next-address hint. This reduces one possible
data-dependent prefetch confound; it does not prove all prefetchers are inactive.
Each link and cycle length is validated before timing. Specialized loops issue
one load for each chain per round; no software prefetch instructions are used.

The 16 KiB and 512 KiB controls expose instruction/register and cache-resident
cost. The 8 MiB and largest-footprint cases expose larger-cache, translation
and memory behaviour, without assuming a named residency tier from size alone.
The loop uses real loads, consumes the resulting pointers, calibrates a batch,
and retains three batch summaries per repetition. The 10%-of-best K is an
empirical scheduling candidate. Serial latency divided by best ns/load is an
effective overlap ratio, not an observed number of outstanding transactions.

Large K can incur register spills and extra address work. Cache residency,
TLB misses, bandwidth, speculative execution, arbitration and background loads
all influence the curve. An overlap ratio larger than K is especially strong
evidence that one fixed-latency MLP model does not explain the curve. Do not
turn it into a larger hardware miss-buffer count. Different seeds and repeated
runs test whether a near-optimal K interval transfers.

The TLB sweep compares a shuffled one-node-per-page cycle against tightly
packed nodes. Sparse nodes vary their within-page cache set rather than
hammering one set. Page counts range from 16 to 8192. Both cache footprint and
translation working set change, so this identifies candidate reach/cliffs,
not an exact TLB associativity or entry count. Base and THP-requested mappings
must be compared alongside the actual mapping receipts.

## Spatial fetch, learning, lookahead and retention

A single-load diagnostic needs independently separated hot and cold controls.
x86 uses fenced TSC reads, calibrated against `CLOCK_MONOTONIC_RAW`; the TSC
is a timebase, not CPU cycles. ARM uses the virtual counter and its reported
frequency. A coarse counter, virtualization or unsupported cache maintenance
can prevent inference. ARM cache maintenance is first attempted in a child
process; failure leaves the diagnostic unsupported rather than crashing the
survey. Demand-load ordering is explicit. Fences and timestamps add overhead;
paired controls include it too.

Each trial selects a random region, flushes the relevant demand/target lines,
then executes one of shuffled balanced cold, trained or hot arms. A single
noinline demand-load PC and fences impose the training sequence. A short loop
provides fixed processing slack before one target load. These are conditioned
access histories; flushing cache lines does not reset hidden prefetch state.
The cold arm flushes the target again after other activity so it remains a
cold baseline. Scores normalise the trained median between cold and hot.
They are latency savings, not cache-hit probabilities or identified cache levels.

- **Line / spatial:** offsets of 8..256 bytes at two starting-line parities.
  This corroborates a reported line size but cannot distinguish a larger line
  from adjacent fetching in isolation. Cross-core atomic offset sweeps provide
  an independent coherence-granule diagnostic.
- **Adjacent:** probes -2, -1, +1, +2 and +4 lines after one demand at both
  parities. This can distinguish forward fetch from a paired even/odd line
  effect. An observed benefit is conditional on this history and delay.
- **Learning:** sweep 1..16 demand accesses at strides +1, +2, +4, -1 and -2
  lines, excluding cases that would leave the page. A repeatable onset is a
  training-length candidate. One-access adjacent benefit is not stream learning.
- **Lookahead / boundaries:** after eight contiguous demands, probe 1, 2, 4, 8
  or 16 lines ahead, either within a page or beyond its boundary. Virtual page
  boundaries are known; physical contiguity is not assumed.
- **Retention:** train a victim at stride two, exercise 0..64 competing streams,
  flush its continuation and next target, then resume it once before probing
  the next target. This tests prediction after competition, removing an
  already-prefetched answer. Shared-PC and distinct-PC arms expose one
  possible PC-association effect. Eviction policy, aliases, retraining from
  the resumed access and prefetch distance still confound an exact table count.

The **serialized stream walk** is an additional capacity diagnostic. Shuffle the
order of pages in a 256 MiB mapping, visit K pages round-robin at a two-line
stride, then continue with a new group. All loads belong to one dependent cycle,
so demand MLP cannot explain a benefit. Compare against independently shuffled
within-page positions at the same K and footprint. Page working set, training
cost, cache effects and prefetching still interact; report a cliff interval,
not an internal table size. The dedicated `streams.sh` worker runner retains
both patterns and all repetitions.

The first version probed an already-prefetched victim target after competition.
It measured cache survival and had contaminated cold controls on Zen 5; those
rows are superseded by the resumed-victim experiment. They are not lookup input.
A significant inference must survive all repetitions, applicable controls and
another seed. Failure of those checks is a useful unknown, not a missing zero.

## Cores, caches and NUMA

Choose peers from the actual allowed affinity mask, separately for SMT siblings,
shared-cache groups and visible NUMA domains. Pin threads explicitly. Compare
an idle peer, a compute loop and a 64 MiB streaming reader. The memory stressor
can consume execution resources, cache, TLB capacity and bandwidth together;
a slowdown does not by itself identify prefetch-table sharing. A separate-core
control helps distinguish SMT-specific disruption from shared-cache pressure.

The handoff test flushes a payload, asks the peer to read it clean or write it
dirty, waits for a release/acquire acknowledgement on separate control lines,
then times a receiver load. Matched cold and local-hot controls go through the
same handshake. These are effective clean/dirty transfer costs. They do not
prove direct L2-to-L2 service, identify a snoop route, or exclude LLC/directory
participation. Sender-near-hot payloads target the small-cache case.

Two writers increment atomic words at separations from 0 to 512 bytes. Compare
the same word, different words sharing a line and separate lines; report wall
nanoseconds per increment across both writers. The sweep reveals the coherence
cost and an approximate granule cliff. It is not an ordinary-store bandwidth
benchmark. For NUMA, bind the probe's mappings to each visible node and retain
the actual node-placement receipt before and after measurement. An unavailable
`mbind` can fall back to first-touch on a CPU in the requested node; the receipt,
not the request, determines where pages actually ended up. NUMA claims need
multiple exposed nodes. The Linux syscall consumes a full nodemask using its
`get_nodes` maxnode convention; the spike's initial too-short mask was corrected
before retaining the successful NUMA campaign.

Useful next distinctions if these change a scheduling decision:

- Clean versus dirty producer payloads at L1-, L2- and LLC-sized footprints;
  deliberately evict the sender's private caches before handoff.
- Reader fanout followed by one writer: invalidation cost versus sharer count.
- Batched ownership transfer and producer/consumer ring distance: line handoff
  amortization versus extra queueing latency, with backpressure charged.
- Same LLC versus different LLC versus remote NUMA, for home memory and peer
  cached data; add model-specific HITM/snoop/offcore PMU events when accessible.
- Cache pollution and victim retention under scans; directory/snoop-filter
  pressure, LLC slicing/hash aliases and inclusion behaviour need separate
  address and capacity controls before assigning internal mechanisms.

## Storage and transport

The portable system probe creates and removes a 64 MiB temporary file. It
measures sequential buffered reads after `DONTNEED` and warm, 4 KiB random
`O_DIRECT` reads, 4 KiB buffered writes plus `fdatasync`, and application
concurrency 1..16 for direct reads. These are distinct operations. Direct
requests use aligned anonymous mmap buffers; unsupported direct I/O is
reported. Thread creation/dispatch and syscalls are charged in concurrency
throughput. This is not an io_uring or block-device queue-depth benchmark.

`fdatasync` latency describes the acknowledged filesystem durability path,
not proof of media persistence under failure. Root-volume work may compete
with the guest OS. A short sweep does not establish sustained EBS/instance
limits, deplete burst budgets, measure SSD steady-state write amplification,
or justify a p99.9 claim. Record devices, sector sizes, scheduler, read-ahead,
mounts and NUMA placement before interpreting the numbers.

TCP loopback echo covers 1 byte through 1 MiB messages. It includes the Python
runtime, scheduler, copying and both local socket endpoints. Three fresh HTTPS
HEAD connections to the regional S3 endpoint split DNS, connect, TLS and first
response time. They do not measure peer-to-peer bandwidth, fabric RTT, tail
latency under load or cross-AZ traffic. The companion [CFT study](../cft-commit-latency/README.md)
uses [network measurements and selection](../cft-commit-latency/network/README.md), explicit
[persistence paths](../cft-commit-latency/persistence/README.md) and
[arrival-driven replication](../cft-commit-latency/commit/README.md) to address
those questions, including message sizes, batching and ENA counters. Its finite
runs still do not establish long-term credit behaviour or isolate IRQ placement.
There is no throughput inference from an HTTPS handshake.

## Sources and prior work

- [Calico AMAC report](../../../../calico/loom/report/AMAC.md): prior ring and
  prefetch-probe observations; SixDB does not inherit its numeric defaults.
- [FetchBench, CCS 2023](https://tschlueter.com/research/publications/23-fetchbench/)
  and the authors' [artifact](https://github.com/scy-phy/FetchBench): systematic
  cross-platform prefetch characterization, including PC/history-sensitive tests.
  This spike borrows questions and controls, not code or security case studies.
- [Intel architecture and optimization manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html):
  instruction/ordering and model-specific cache/prefetch documentation. A stream
  count documented for an older core is not a Granite Rapids constant.
- [AMD optimization-guide index](https://docs.amd.com/r/en-US/57368-uProf-user-guide/Useful-URLs):
  primary guide entry points, including Zen 5 publication 58455. No undocumented
  miss-slot or stream count is seeded from a marketing architecture name.
- Linux [THP](https://docs.kernel.org/admin-guide/mm/transhuge.html) and
  [HugeTLB](https://docs.kernel.org/admin-guide/mm/hugetlbpage.html): page policy,
  supported sizes and allocation/pool distinctions.
- AWS [EBS I/O characteristics](https://docs.aws.amazon.com/ebs/latest/userguide/ebs-io-characteristics.html)
  and [benchmarking](https://docs.aws.amazon.com/ebs/latest/userguide/benchmark_procedures.html):
  size, queueing and provisioned/instance limits affect observed I/O performance.
- AWS [network bandwidth](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-instance-network-bandwidth.html)
  and [ENA monitoring](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/monitoring-network-performance-ena.html):
  instance/flow limits and burst behaviour qualify transport measurements.
