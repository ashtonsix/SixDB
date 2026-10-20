# Orbital measurement controls

Start with the [consumer guide](README.md) for the ordinary comparison and its
implementation boundary. This reference covers staged reads, timing and effect
accounting, preparation, PMU/residency controls, and the separate lifecycle cohort.

## Bounded staged reads

`--batch-width 1..64` groups adjacent independent point-hit, point-miss,
YCSB-C or dependent-chain requests. The serial default executes each canonical
logical request. `--read-execution native|manual|ring` selects a synchronous
owner-only treatment with admitted pack readers. Its batch borrows initialized
pages, the rank-to-slot directory and stable named pack endpoints; it cannot
poll, reenter, grow, release or admit writes before all borrowed uses drain.

The staged body separates directory selection, Page/binding metadata,
rank-to-physical-slot loads,
actual TuplePack key probes and SeriesPack/TuplePack payload reads. A dependent
chain starts its next lookup from the returned next key. Point search stops at
the matching unique key; ordered scans retain lower-bound search. Fixed first-line hints
are shared by the native direct, manual and Ring controls; they do not establish
whole-page residency. The native direct control completes one request at a time.
Manual and real Loom Ring treatments keep the configured `--read-active` count
(default 16, range 1..64) within a bounded total inventory of 64. They expose actual dependent memory frontiers
rather than rotating complete `get` calls. No clock or diagnostic counter runs
at every stage visit.

Cold execution-bank preparation happens after loading each fresh store, before
counters and timing. Frame initialization, admission, all visits, output
consumption and retirement are timed. Empty batches perform no request. Explicit
staged width one measures underfill through the same batch path; the default
serial width-one request loop remains unchanged.

```sh
python3 workbench/benchmarks/orbital/run.py \
  build/benchmarks/orbital-tooling/workbench/benchmarks/orbital/orbital_allocation_bench \
  --output build/reports/orbital/ring64 --cpu 0 --records 4194304 \
  --workloads point-hit,point-miss,dependent-16 --operations 100000 \
  --backends packed-owner,lmdb,lmdb-read-batch64 --repetitions 3 \
  --batch-width 64 --read-execution ring
```

LMDB remains real serial requests with the selected transaction lifetime; its
batch64 control retains one actual snapshot across up to 64 adjacent requests.
Throughput counts logical requests for every treatment. Batch latency records
one whole-batch makespan in `batch-latencies.csv`, including the actual partial
tail width. `batch_p50_ns`/`batch_p99_ns` describe those makespans; request quantiles
remain zero for this treatment. Width-one staged makespans are still labeled
as batches. They must not be compared with, divided into, or relabeled as
individual request tail latency.

The worker adapter accepts `ORBITAL_READ_EXECUTIONS=native,manual,ring` to run
those controls sequentially from one captured build, under `paired-native`,
`paired-manual` and `paired-ring`. Pass the common width with
`--arg=--batch-width --arg=64`; each control retains its matched real LMDB runs.

The default comparison includes all listed ordinary cases, including insertion
and deletion. Successful deletion is capped at the initial record count; the
receipt distinguishes requested trace length, actually offered/completed
requests, requests with rows/changes and requests returning no rows/changes.
It does not turn a long deletion run into mostly absent-key timings.
Sparse insertion ranks and partial deletions span the full record population;
the mixed scan trace observes earlier insertions in that distributed population.
Independent distribution and scattered-delete/refill checks exercise sparse
retention and slot reuse. Older traces based on the operation-count domain
confined insertions to a prefix; their fragmentation comparisons need rerunning.
Older stock-range timings also opened a writer transaction for a threshold-only
scan and are invalid read-performance comparisons. Stock now opens a read transaction.

LMDB uses ordinary locking, ordered big-endian keys, COW and real transactions.
One logical request gets one transaction, including a scan or multi-row kernel.
`MDB_NOSYNC|MDB_NOMETASYNC` excludes durable flushes; the candidate has no durable
publication, so even this is not persistence parity. Values are copied into the
same logical record shape before consumption. The adapter avoids an extra
existence lookup when replacing a row already returned by that request.
`lmdb-read-batch64` additionally retains the real read snapshot for up to 64
adjacent read requests, closing it before a write and at final drain. Writes
remain one transaction per request. Comparing both LMDB lifetimes makes read
headroom independent of an assumed transaction setup cost. That control has
different snapshot lifetime from independent requests and one assigned caller.

## What the measurements establish

Every request outcome and final ordered state is checked against the independent
map before reporting. Consequential checks cover fence gaps, capacity boundaries,
splits, deleted rows, ordered scan tails, RMW values, rejected over-width writes,
live sums and issued-byte coverage. Debug coverage checks run outside timings.

Malformed operation kinds, over-capacity scans and over-width write values
reject before opening a request or changing storage. Unexpected backend failure
after `begin` aborts the trial: discard the Store and report no timing result.
Packed has no transaction rollback. The wrapper deliberately does not call
`end` during unwinding, because that would commit partial LMDB writes; LMDB
destruction aborts its outstanding transaction. Fault fixtures that prove a
particular refusal preserves state use that narrower guarantee explicitly.
Batch checks compare each output index with the map, including zero-length
chains, cross-page links ending at misses, fragmented rank-to-slot maps, widths
1/3/16/63/64 and partial tails. Invalid batches must reject before accessing the
store or changing output; guard outcomes detect writes beyond the offered span.

Throughput and latency replay the same trace in separate fresh stores. Throughput
has no per-request clock. Latency samples every whole request, records timer-pair
overhead without subtracting it, and reports empirical p50/p99. Both include
final polling/drain and synchronous allocation/split/maintenance. Setup, input
generation and oracle checks are outside the timed interval. Trials reverse
backend order; paired identities and final states must match.
The runner measures a retained binary copy and verifies its hash before marking
the run complete. Disposable LMDB images are removed after verified replay;
trace identities, final state, samples and binaries remain recoverable.

The [selected frozen prefetch control](evidence/prefetch-control-20261007/README.md)
keeps native/manual/Ring repetitions beside the independent memory reference,
with their different units and unresolved load-service tier explicit.

These are closed-loop single-caller service latencies, not saturation latency or
queueing tails. Resident-set sizes are reported with measured host cache context;
a large footprint alone does not establish cold DRAM or disk I/O. Assess headroom **per workload** on throughput, p50 and p99 against the
omitted composed database costs. Retain gaps as composition leads rather than
claiming universal readiness from a correctness check, aggregate or mechanism
timing. Closing the allocation stage does not establish the eventual database
performance target.

`thread_cpu_ns` counts the assigned caller. `process_cpu_ns` additionally includes
all helper threads over the request/drain interval and PMU boundary calls;
startup, loading and validation remain outside it. This process total is not
producer-only CPU. Untimed modes and older results leave it unreported. PMU
events remain caller-only, with their own explicit scope.

Prepared-Page control is opt-in: `--prepared-pages 0` keeps synchronous creation;
a positive count allocates one stable preparation Slot bank sized at startup.
Preparations use Loom's actual Worker Pool and caller-selected service Lane.
Pages have separate ownership so adoption can outlive reuse of their preparation
Slots. Page creation harvests a ready page, adopts it once and submits replacement
work, or constructs inline
on a stockout. Ordinary Packed begin/get/end perform no preparation poll, refill
or flag reset. Existing staged execution continues to service its owner Lane.
The producer starts after loading, bank/sample/timer setup and conditioning.
`--prepared-prefill on|off` chooses whether startup waits for accepted inventory;
actual initial ready/inflight stock and startup wall/process/producer CPU are
reported separately. Storage baselines refresh after startup outside timing.

`--preparer-cpu -1` chooses within the caller's current affinity. With the runner's
single-CPU pin it uses the owner CPU. An explicit distinct producer CPU adds a
compute resource: `comparison.json` labels this, and whole-process/producer CPU
and actual placement must accompany throughput. It does not inherit a one-vCPU
headroom claim. The shell runner accepts `ORBITAL_PREPARED_PAGES`,
`ORBITAL_PREPARER_CPU` and `ORBITAL_PREPARED_PREFILL`; direct runner flags expose
the same controls. LMDB controls receive no prepared-page producer.

`preparation.json` records startup, before-drain and settled cumulative snapshots.
Ready, inflight and completion-pending work consume the same inventory bound.
Final drain stops refills, cancels unused stock, settles all accepted physical
records and joins the helper. Whole-Pool diagnostic walks remain outside timing;
the timed boundary observes only bounded stock, completed work, CPU and atomic
Arena charge. Complete storage statistics are explicitly observed or null.
Whole-Arena charge includes ordinary live/idle storage and core metadata;
private object heap bounds are separate, and these are boundary observations
rather than a continuously observed preparation-only peak.

Successful producer construction, including cancelled/unused pages, has separate
completed and discarded byte/span totals. Partial failed constructor writes are
unquantified. Producer CPU includes service/wake cost and does not attribute CPU
to individual unused pages. In latency mode `preparation-samples.csv` attaches
actual demand/ready-hit/stockout/inline-fallback flags to preallocated samples;
cumulative demand counters change only at page creation and are read after each
request clock stops. Fallback work is inside that clock. Throughput performs no
per-request observer reads. The summarizer verifies receipt identity, physical
settlement, sample ordering and exact latency joins before preserving fields in
`cases.csv`. Async readiness cannot be reconstructed from untimed replay.
Helper first touch and loaded/warmed histories still do not establish NUMA or
LLC/DRAM load service; retain the separate residency qualification.

`cases.csv` preserves every repetition. `comparison.json` reports ratios of
median trial values and absolute p50/p99 headroom, including deficits. Regenerate them offline:

```sh
python3 workbench/benchmarks/orbital/run.py --output build/reports/orbital/paired \
  --summarize-only
```

For request-level maintenance classification, the same binary's `--mode events`
replays scalar packed requests without clocks or PMU collection. It checks each
outcome against the oracle and records O(1) logical counter deltas by exact
request index: splits, page creation, pack/index writes and lease calls.
Physical Pool allocation/growth is not observed per request. Primary timing
loops remain unchanged; these labels come from a separate replay.

Pack-effect counters include page construction when the owner adopts that page.
With background preparation, they describe owner-accounted work: unused or
discarded construction and startup-produced pages need separate completed-work
totals at startup/request/drain boundaries. Adoption is not the time the writes
were issued. Whole-process CPU does not replace that accounting.

[request_events.py](request_events.py) runs that replay and joins it to a
validated scalar `latencies.csv`. It refuses changed composed implementation
inputs, mismatched trace/final-state/logical split totals and missing or reordered
request indices, and checks collected case bytes against an adjacent worker
collection receipt when available. Supply the capture used to build the event binary and the
measured worker's source manifest:

```sh
python3 workbench/benchmarks/orbital/request_events.py EVENT_BINARY \
  --source build/captures/EVENT_CAPTURE \
  --manifest build/workers/JOB/results/source-manifest.json \
  --measured build/workers/JOB/results/orbital-allocation/paired/CASE \
  --output build/reports/orbital/request-events
```

The event binary must use the measured composed sources, with only the driver's
untimed observer added. Retain its build provenance separately. Conditional
split latencies locate a useful class of slow requests; they do not separate
allocation, initialization, row transfer, index maintenance and publication
costs or establish cache service. Summed request samples include timer overhead
and must not be substituted for throughput elapsed time.
`--match-maintenance` additionally requires identical pack/index/lease totals
for synchronous construction. Background construction can legitimately change
those totals. Replay can classify deterministic structural splits; background
page creation, ready hits, stockouts and waits need flags from the actual measured
execution and cannot be inferred from another replay.

## Counter diagnostics

Primary throughput and latency runs leave PMU collection off. Use
the independent [guest availability tool](../../tools/pmu/README.md) first: it records
actual event types, sysfs formats/aliases, user/root permissions and unsupported
or unscheduled groups without changing global policy. Its short dependency loop
establishes availability only.

`run.py --modes throughput --pmu basic` records cycles and instructions;
`--pmu stalls` also attempts generic frontend/backend stall events. Repeated
`--pmu-event name:type:config` arguments add explicitly qualified events. Keep
the actual CPU/kernel semantics and source for raw encodings with the evidence;
generic cache-miss or stall names do not establish LLC service or DRAM residency.
These are separate diagnostic treatments, not primary timings.

`pmu.json` counts the caller thread's user-space request loop, result aggregation,
final drain and fixed boundary clocks; setup, validation, kernel execution and
helper threads are excluded. A fresh group maps exact event IDs, records enabled
and running time, and reports coverage/multiplexing. IPC is the observed ratio
from the same group. No scaled counts, clock-derived IPC or zero-valued substitutes
are generated for missing hardware. The sidecar links input/trace fingerprints.

The [residency guide](../../tools/residency.md) links existing conditioning and
mapping probes. Current default composed runs load a fresh store before timing
and do not flush/evict caches or run an explicit warm-trace pass. Their accessed
cache level remains unresolved even when mapped bytes exceed reported LLC.

`--cache-setup warm-trace --warmup-passes 1` replays the same validated read-only
trace after timer/counter preparation and before measurement. Setup outcomes
are checked, its time/passes are recorded separately, and its requests are not
included in timing or hardware counts. This controls access history; the trace
can still exceed cache capacity, so it promises no particular hit level. Mutating
traces reject this treatment. Match cache setup and pass count across competitors.

`--read-active 1|16|32|64` controls the staged candidate's active chases separately
from offered `--batch-width`. The native and LMDB controls retain their original
serial execution. Matched comparisons preserve both factors, conditioning and
counter mode; default active16/loaded/off preserves the original treatment.

`--modes throughput --memory-receipt on` is an optional, separate mapping
diagnostic. After the request loop, drain and PMU stop, while the store is live,
it records labeled representation extents, initial input/trace ranges and raw
kernel `smaps` / `numa_maps`. Collection precedes final validation and never
warms metadata before the measured interval. Packed ranges overlap: union them
instead of summing capacity, field and index extents. They describe post-interval
representation, not an observed accessed-line set; mutating traces can change
it during measurement. Stack/output scratch and Ring-bank backing remain
unreported. LMDB's file mapping is visible in `smaps`; an empty store-region list
means its private extents were not enumerated. Page residency and cache service
remain distinct. Default runs leave this receipt off.

## Allocation lifecycle and pressure

`orbital_lifecycle_bench` is a separate cohort. It builds real SeriesPack values
and TuplePack keys/links/tags in a bounded Arena, grows seven-row prefixes to
128 rows, retires each packed batch and refills the same Pool. Both columns stay
within the current finite allocation ceiling in that treatment. Finite allocations
exercise relocation and fresh bindings; reserved windows exercise disjoint
in-place growth. A live Hold must refuse finite relocation without changing the
prefix. Every fourth cycle also leaves a complete packed batch with a foreign
reader, releases the Allocation owners, then handles its final return Messages.
The operation has its own completion handshake: retiring one Hold while an
Allocation remains live need not pulse Pool capacity.

The selected policy explicitly releases cold batches and trims every four
cycles. It polls through actual VM completion, joins the foreign reader and
drains Worker shutdown; final Arena charge must be zero. This prices a chosen
batch lifecycle and pressure policy, including instrumentation and service
waits. It does not insert arbitrary trim work into ordinary ready requests or
claim transaction parity with LMDB. The main comparison owns logical key edits.

```sh
cmake --build build/benchmarks/orbital-tooling --target orbital_lifecycle_bench -j1
python3 workbench/benchmarks/orbital/pressure.py \
  build/benchmarks/orbital-tooling/workbench/benchmarks/orbital/orbital_lifecycle_bench \
  --output build/reports/orbital/pressure --cpu 0 --helper-cpu 1 \
  --rounds 64 --slots 16 --arena-bytes 8388608 --repetitions 3
```

Choose allowed CPUs; use the same CPU if only one is available. Placement is
set before timing and recorded for the caller, foreign reader and actual Arena
VM helper. The campaign retains an immutable measured binary and rotates finite,
window and mixed case order. Full constructed rows are consumed and checked
against independently generated values; pack effects are resolved before their
bindings retire. Receipts include offered/completed cycles, whole-cycle p50/p99,
physical drain time, faults, relocations, foreign returns, trim counts and final
inventory. Peaks are **observed at allocation/cycle boundaries**, so they do not
claim to capture a transient charge inside `grow`. The Arena enforces its byte
limit throughout. The default footprint is well below that limit; this is a
bounded lifecycle with explicit trim, not a capacity-exhaustion benchmark. The
128-row window grows within its already prepared page. Module growth/fault checks
cover additional page preparation; this cohort does not price that treatment.
These lifecycle-cycle quantiles are separate from database
request quantiles. The worker adapter runs this cohort after ready comparisons.
For a lifecycle-only rerun, submit [pressure.sh](pressure.sh) through the same
worker helper; it builds only the lifecycle executable and accepts the same
pressure-runner arguments. This avoids repeating the scalar campaign when only
allocator internals have changed.
