# Orbital allocation consumer

The maintained driver pushes an ordered packed container through Loom's actual
Worker/Pool/Allocation/Hold path. It compares identical deterministic logical
requests to actual LMDB, with an independent ordered-map oracle. This is the
allocation implementation stage: this mutable fixture does not implement or price
Engine, Shore, versioning, concurrent isolation or durability. The withdrawn
Stage 2 implementation and its row/column consumer are retained as a rejected
experimental baseline in the [Orbital MVCC spike](../../spikes/orbital-mvcc/README.md).

The [completed allocation comparison](evidence/selected-allocation-20261007/README.md)
measures source `595e07fa` with synchronous and same-CPU preparation controls.
Large point-hit throughput and medium/large insertion p99 remain gaps;
preparation did not repair the tails. These are leads for the Object composition.
The later closing code below has correctness qualification, not new hardware
measurements. [Earlier comparisons](evidence/allocation-residency-20261007/README.md)
retain their original source and limits.

[Measurement controls](measurement.md) own staged reads, preparation, accounting,
hardware diagnostics and lifecycle. [Carried-forward recipes](stage2.md) print
one experiment command; they select no new research priority.

## Run the composed comparison

Use the [pinned Linux toolchain](../../../BUILDING.md); prefix Linux commands
with `orb -m ubuntu` in the Mac workspace. LMDB headers/library are required
(`liblmdb-dev`; the worker adapter installs them if absent).

```sh
cmake -S . -B build/benchmarks/orbital-tooling -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_BENCHMARKS=orbital \
  -DSIXDB_MARCH=armv8.2-a+simd -DSIXDB_TUNE=neoverse-v2
cmake --build build/benchmarks/orbital-tooling \
  --target orbital_allocation_bench orbital_workload_check orbital_pack_growth_check -j1
build/benchmarks/orbital-tooling/workbench/benchmarks/orbital/orbital_workload_check \
  packed-owner build/reports/orbital/check-packed-owner
build/benchmarks/orbital-tooling/workbench/benchmarks/orbital/orbital_workload_check \
  lmdb build/reports/orbital/check-lmdb
python3 workbench/benchmarks/orbital/run.py \
  build/benchmarks/orbital-tooling/workbench/benchmarks/orbital/orbital_allocation_bench \
  --output build/reports/orbital/paired --cpu 0 --records 8192 \
  --workloads point-hit,point-update,scan-16,append,dependent-16 \
  --operations 20000 --repetitions 3
```

Choose an allowed CPU. Every output/database directory must be new or empty;
the driver does not delete existing data. For stable Zen 5 measurements, the
[worker helper](../../tools/workers.md) captures sources, binaries and machine
context, reuses compatible Spot workers, and collects results in S3:

```sh
python3 workbench/tools/worker.py run workbench/benchmarks/orbital/run.sh \
  --machine zen5 --instance-type c8a.xlarge --deadline 7200 --idle-seconds 600 \
  --env ORBITAL_RECORDS=8192,262144,4194304 --env ORBITAL_OPERATIONS=100000 \
  --env ORBITAL_REPETITIONS=3 --detach
```

`run.sh` selects `znver5`/`zen5`; change both `ORBITAL_MARCH` and `ORBITAL_TUNE`
for another architecture. Builds and timings are sequential, in a dedicated
build directory. Edit sources between captures, not during a measurement.
Keep selected comparisons with their source/host receipts; full outputs are
recoverable through the existing [retention tools](../../tools/artifacts.md).

## Operation and storage boundary

Every logical row has a sparse unsigned 64-bit key and a 16-byte record: dependent
next key, 20-bit numeric value and 32-bit tag. The candidate uses a sorted page
fence directory and actual TuplePack key binary search within 256-row pages.
TuplePack also stores links/tags; a compact Local20 SeriesPack column stores
values. There is no key-to-array-slot shortcut.

Payload is allocated from Orbital, 64-byte aligned with initialized readable
padding. A new page constructs every capacity slot through qualified pack bodies;
the allocator leaves the body uninitialized to avoid clearing it twice. These
6,304-byte pages use reserved Windows, so this driver does not measure finite-slot
selection. The private [page representation](packed-page.h) owns stable format
bindings and native reads; [packed-page.cpp](packed-page.cpp) binds that format
independently of store lookup and maintenance.
[Construction](packed-construction.h) takes explicit bytes and returns local
effects for owner adoption, without requiring a published Allocation. A separate
live count controls visibility. Appends initialize
spare slots through checked replacement. A sorted two-byte slot directory and
free bitmap live in the actual page allocation. Random inserts/deletes move
that directory, preserving unrelated physical packed rows; full-page splits
copy unchanged complete Tuple units through Ikea, rebuild selected Series values
in a new allocation, and repair fences. Scans retain
logical key order, using a physical SeriesPack range only when the selected
slots are contiguous and otherwise gathering the actual physical slots.
Live sums use replacement deltas; issued source-qualified pack effects are resolved
while stable views remain alive. Invisible Tuple slots retain initialized bytes;
reuse overwrites all fields before visibility. Series values are cleared when
retired because they contribute to the live sum. This is local owner bookkeeping,
not a journal.

`packed-owner` is the primary synchronous ready path. The real Allocation owns
its bytes throughout the uninterrupted assigned-lane call, and checked stable
Ikea bindings qualify the footprint. No pointer escapes and no poll, reentry,
growth or release intervenes. Object version selection and publication costs are
absent from this path. `packed` separately acquires a genuine Hold at each page access.
Stable named pack bindings remain admitted while placement is unchanged.
`packed-retained` is a separate
native-region control: each page's actual Hold remains live for the bounded
trial. It performs the same lookup, checked pack calls, effects and maintenance.
Neither path simulates Objects or silently adds a version cache. The slot
directory/bitmap's issued writes are counted separately. Descriptor and
fence metadata use the fixture's C++ heap. Logical metadata totals also include
the allocated slot directory/bitmap and, when prepared, the real Ring bank.
Those portions already occupy Pool storage, so the metadata total must not be
added to Pool mapped/reserved bytes. This is not a completed allocator for all
Engine state. Pool mapped,
reserved, idle and metadata bytes are also recorded, with initial and final live
payload so append expansion and deletion are visible. Cumulative allocated
payload is distinct from final live payload.
These payload counters describe initialized page capacity. `logical_live_rows`
and `logical_live_field_bytes` separately count visible rows and their logical
key/link/value/tag fields (24 bytes per row, excluding C++ padding). Sparse live
rows can retain a whole capacity-backed page; this is a representation choice,
not an allocator leak or a claim that the unused capacity is useful data.

## Workloads and limits

| Family | Requests |
| --- | --- |
| Ordinary | Hit/miss, partial-field update, insert with duplicate rejection, erase and value/tag RMW |
| Range/append | Ordered lower-bound scans, live tails, arbitrary insertion/splits and monotonic append |
| Fragmented mixed scans | 50/50 nonmonotonic sparse-key insertion and 16-row ordered scans, observing prior insertions |
| Dependent | 16 actual next-key lookups through a random cyclic record chain |
| YCSB-inspired A/B/C | 50/50 read/update, 95/5 read/update, all reads |
| YCSB-inspired D/E/F | Latest-region reads/inserts, short scans/inserts, read/RMW mix |
| Partial order/status/stock kernels | Eight stock-like decrements plus a new record; 16-row status scan; 32-row threshold scan |

The [YCSB core workloads](https://github.com/brianfrankcooper/YCSB/wiki/Core-Workloads)
motivate the mixes. This driver uses its own 80/20 hot-set generator, numeric
16-byte records and explicit dependent links, rather than the YCSB client,
Zipfian generator or standard record schema. Partial order/status/stock kernels
only exercise a few common database operations: they omit the tables, joins,
transaction rules, terminals and reporting required by [TPC-C](https://www.tpc.org/tpcc/). These are inspired
component scenarios, not published YCSB or TPC benchmark results.

## Correctness and evidence

`orbital_workload_check` compares complete outcomes and final ordered rows with
an independent map, including all 19 scenarios, boundary keys, fragmented slots,
batch tails and native/manual/Ring agreement. The focused `orbital_split_check`,
`orbital_slot_maintenance_check`, `orbital_construction_check`,
`orbital_read_bounds_check` and `orbital_inventory_check` exercise publication
refusal, initialized bytes and effects, malformed slots, helper construction,
stockout and physical drain. Build/run only the checks needed for changed behavior.
`orbital_validate` and `loom_validate` own module lifetime and failure histories;
`ikea_tuplepack_transfer_check` owns complete-unit transfer semantics.

The current consumer keeps its format bindings inside the stable Page and uses
bounded journals for split maintenance. Those are composition simplifications,
not an Engine layout or a measured database performance claim. Old request
samples and binary/source receipts remain immutable when code changes.

The [closing qualification record](evidence/20261008-closeout.json) identifies
the later `d059bf8a` runtime cut and distinguishes earlier implementation checks.
Its [verified local-evidence archive](evidence/20261008-local-qualification-artifact.json)
keeps selected captured code, original qualification/ABI receipts and diagnostics.
Members retain their repository-relative `build/...` paths; `selection.json`
records their hashes. Recover into ignored storage with:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/benchmarks/orbital/evidence/20261008-local-qualification-artifact.json \
  build/recovered/orbital-local-qualification-20261008
```

Throughput replays the deterministic trace without per-request clocks; latency
replays it in a fresh store and times each whole request. Outcome checks, setup
and loading are outside the interval; allocation, split, maintenance and final
drain are inside. Timer overhead is retained. These are single-caller service
latencies, with no arrival queue. Staged batches report whole-batch makespans,
never request-tail estimates. Prepared work reports its actual CPU resources,
unused construction, stockouts and settlement separately.

The container has no snapshots, atomic publication or transaction rollback.
LMDB retains real transactions, snapshots and copy-on-write even with durable
flushes disabled. Matching logical outcomes does not establish semantic or
persistence parity. Ordinary borrowed pointers remain valid only within the
uninterrupted owner visit; physical Holds do not make old values immutable.

Insertion trials load full pages and measure the first growth wave; they are
not steady-state insertion measurements. Flat fence-directory movement is not
included in the page index-write counters. Footprint, prefaulting and cache
conditioning do not prove DRAM service. Compare headroom per workload and
retain deficits alongside wins: the eventual composed database still has
Engine/Shore, selected-version, isolation and publication costs to pay.
