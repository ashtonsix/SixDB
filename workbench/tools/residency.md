# Memory workload setup and observation

We have controlled memory-system probes and host receipts. A composed workload
still owns its address set and conditioning; no helper can declare its timed
loads L1-, LLC- or DRAM-served from allocated bytes alone.

| Need | Existing tool / evidence |
| --- | --- |
| CPU identity, allowed cores, cache geometry/sharing, page policy | [Memory hardware collector](../spikes/memory-characterisation/hardware.py) and each worker's context |
| Dependent loads, working-set and independent-chain sweeps | [Memory-characterisation runner](../spikes/memory-characterisation/README.md), with validated shuffled cycles and retained repetitions |
| Controlled hot/cold/trained lines, prefetch history and peer-cache handoff | [Memory probe method](../spikes/memory-characterisation/METHOD.md#spatial-fetch-learning-lookahead-and-retention); cache maintenance is capability-tested |
| Actual THP and NUMA placement | The same probe's per-mapping `smaps` / `numa_maps` receipts; requested advice is recorded separately |
| Guest PMU availability and permissions | [Counter availability tool](pmu/README.md), retaining sysfs types, formats, aliases, status/errno and group coverage |
| Counter sensitivity over real memory accesses | [Memory MLP diagnostics](../spikes/memory-characterisation/pmu.sh), with a fresh group over each calibrated dependent chase and below-/above-cache footprint controls |
| Counters over an exact composed request interval | [Historical Orbital example](../benchmarks/orbital/measurement.md#counter-diagnostics); a new caller supplies its own interval using the shared PMU helper |
| Composed representation and actual page mappings | The same consumer's opt-in `--memory-receipt on`, collected after measurement; overlapping post-state extents are not an accessed-line set |

For cache-sensitive workload comparisons, retain both the setup history and
the observation. Name which input, directory/index, binding metadata, execution
frames and output ranges are touched. Distinguish allocated/mapped bytes from
the accessed cache-line/page set and its reuse: a short trace or skew can make
the effective hot set much smaller. First-touch, validation, loading and prior
repetitions can warm it before the clock starts. The workload must put its
conditioning after those steps, outside the measured request interval, and use
the same logical trace and setup policy for each competitor.

A useful comparison includes a deliberately warmed trace, a footprint sweep
across the measured host's caches, and a capability-tested flushed or explicitly
evicted control where the question requires it. Record the actual mechanism,
ranges, pass count, delay and page/affinity conditions. A buffer named `cold`, a
fresh process, or a footprint larger than LLC is not a residency observation.
Flush/eviction also changes writeback, translation and prefetch history; the
[existing method](../spikes/memory-characterisation/METHOD.md) explains its controls.

Collect level-specific load/refill/miss events when the guest exposes them;
keep raw encodings and their CPU/kernel meanings with the measurement. Generic
`cache-misses`, IPC or stalled cycles alone do not determine where a load was
served. Unsupported events, inadequate counter coverage and unavailable load
source sampling leave that distinction unresolved. In particular, software
prefetch can turn a DRAM fetch into a later demand-cache hit: count prefetch
traffic as well as demand misses before attributing the mechanism.

Event-open success is also insufficient: qualify a miss/refill signal against
the controlled footprint sweep. A flat zero across a real above-cache dependent
chase remains unqualified, even with successful reads and full running coverage.
Preserve those raw zeros as an observation; do not treat them as proof of hits.

RSS/PSS, `smaps` and [mincore](https://man7.org/linux/man-pages/man2/mincore.2.html)
describe virtual-memory/page residency, not CPU-cache temperature. `mincore` is
only a snapshot. Keep faults, actual huge mappings and NUMA placement alongside
cache observations rather than using them as substitutes.

Run the cold availability probe on a worker before choosing a PMU campaign:

```sh
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/tools/pmu/run.sh --machine zen5 --deadline 900
```

For the configured Graviton 4 worker, use `--machine neoverse-v2`. The runner compares
ordinary user and `sudo -n` access on the disposable guest without changing
global perf policy. Successful probe counts establish counter availability,
not the composed workload's IPC or residency.

For real-memory sensitivity on an ARM worker, submit
`workbench/spikes/memory-characterisation/pmu.sh` with
`--env MEMORY_ARM_CACHE_EVENTS=1`. It retains the guest's PMU catalog and attempts
the architectural L1/L2/L3 refill encodings through both the raw and dynamic
device types. The tested small Graviton 4 guest exposed a responsive generic L1
signal but refused these raw events, even when sysfs advertised their aliases.
Its measurements leave LLC versus DRAM service unresolved. The Zen 5 guest's
generic L1 event returned zero throughout an above-cache chase and remains
unqualified. Inspect sensitivity and errors before selecting counters for a
composed run.

Instance size matters as well as architecture. [AWS's PMU support table](https://github.com/aws/aws-graviton-getting-started/blob/main/perfrunbook/debug_hw_perf.md#how-to-collect-pmu-counters)
lists basic support on small current-generation guests and full event support
on selected large sizes (for Graviton 4, 24xlarge/48xlarge). Verify the required
events on the chosen instance before committing to a residency campaign.
