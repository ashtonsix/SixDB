# Loom execution spike

Start with the [findings and minimum viable Loom recommendation](FINDINGS.md).
The prototype compares a small local memory ring, bounded worker channels and
large operations across cache-affinity seams. Measurements establish same-host
LLC behavior; NUMA and cross-host policies remain open. It is a spike, not a
scheduler contract.

Engine and Shore call Loom; Engine supplies pipelines to Ikea, and Ikea drives
them through Loom. Loom calls Orbital for resource and Port work. Existing Ikea
CPS and Bec256 consumers are comparison inputs, rather than inherited APIs.

The microtask prototype uses stable preallocated local storage, explicit
dependencies and a private uniform `musttail` callback family. A bounded ordinary
task spins it and then returns to owner service. Native allocated memory may
yield; a Port wait belongs to the ordinary task/completion machinery. Prefetches
are advisory, and resumption does not imply memory readiness.

The comparisons cover direct/manual/ring execution, live scalar and vector
state, dependent random memory versus streams, underfill and overflow; actual
queue geometries, worker groups, coordinators and pressure; lock contention and
useful polling; and immutable-build joins with skew, output expansion and
placement/copy costs. Timed runs use finite accepted-work accounting and retain
losing candidates as well as winners. Same-host measurements constrain same-host
choices; they do not establish a distributed transport or production policy.

| Frontend | Comparison |
| --- | --- |
| `loom_microtask`, `loom_microtask_ordinary` | Direct/shared/manual/ring, native grain, scalar/vector state, legacy/runtime/inline hint classes, typed chain control, current SeriesPack+Bec pair |
| `loom_abi`, `loom_abi_ordinary`, x86 `loom_abi_regcall` | Ten live scalar payloads and 0/2/8/12 vectors; real musttail families and independent value checks |
| `loom_messaging` | Dense/sparse SPSC, per-worker MPSC, local/coordinator groups, credit pressure, refusals/retry, complete observers |
| `loom_affinity` | Locks and owner request/ACK; real immutable-build joins, output expansion, placement, copies/replicas and actual retained-build reuse |
| `loom_service` | Causal peer request/ACK while the owner spins native memory work; count/time quanta and gap-observer controls |
| `loom_inventory` | Reusable worker storage serving two parents; same-family sharing, parent/family banks, short jobs, admission, selective cancellation and drain |
| `loom_pressure` | Actual bounded output spans and slow physical use; independent reserved receipts versus a credit-gated FIFO negative control |

Each frontend has `--check` and emits JSON lines. Use the pinned toolchain and
selected incremental targets, prefixing Linux commands with `orb -m ubuntu` in
the Mac workspace:

```sh
orb -m ubuntu cmake -S . -B build/loom-execution/dev -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=loom-execution
orb -m ubuntu cmake --build build/loom-execution/dev \
  --target loom_microtask loom_messaging loom_affinity loom_service -j 2
orb -m ubuntu build/loom-execution/dev/workbench/spikes/loom-execution/loom_microtask --check
```

`run.py` discovers actual topology, captures sources, records compiler flags and
placement, checks binaries and runs three shuffled repetitions. Every timed row
must verify; confirmation fixed-demand messaging also requires all planned
identities to complete. Select a family or case without rebuilding unrelated
targets:

```sh
orb -m ubuntu python3 workbench/spikes/loom-execution/run.py \
  --panel confirmation --families microtask --case-regex '^confirm-warm-' \
  --output build/experiments/loom-warm --march znver5 --tune zen5
orb -m ubuntu python3 workbench/tools/worker_group.py run \
  workbench/spikes/loom-execution/confirmation-workers.json
orb -m ubuntu python3 workbench/tools/worker_group.py run \
  workbench/spikes/loom-execution/extensions-workers.json
```

`--quick` reduces demand for development, not a retained native performance
result. `workers.json` runs the initial broad four-family panel with current
bytes; exact old bytes and executables live in the retained campaign archives.
The confirmation panel preserves the observed full-payload x86 preserve_none
compiler rejection separately and times ordinary/regcall there. To reproduce
that rejection, select `--families abi` on x86 in the initial panel. No smaller
payload is substituted for it.

The focused `extensions` panel keeps a warm native control, times short jobs at
1/2/8/64 visits, and exercises two parents, cancellation, reusable banks and
physical output drain. Its shared/parent-bank arms use the same scalar family;
the family-bank arm also performs real vector work and is not an isolated
storage-placement comparison. Worker-wide credits bound aggregate admission.
Reserved credit does not itself reserve an active home or guarantee service.
Selective cleanup scans concrete typed arguments at an owner spin-return boundary;
it adds no owner/refcount test to each yield. Whole shared discard intentionally
reports collateral abandonment. Cleanup timing isolates the boundary scan;
other inventory timing includes admission, outer service, native work, retirement
and early cancelled-parent destruction, with one B first-service timestamp/wave.

Pressure reserves writable output before native entry and stops outer admission
when no output credit remains. A submitted buffer stays unavailable through the
consumer's actual reads and the owner's receipt settlement. The negative control
gates a shared work/ACK FIFO on output credit, observes its cycle, then cancels
and physically drains. It does not show that every shared queue must deadlock.
The positive arm uses one preallocated receipt cell per retained buffer. Outer
clock reads, receipt scans and repeated volatile consumer checks are included
fixture costs. Qualified spans and generation receipts are real here; the
candidate Orbital Port/View admission and protection runtime is not implemented.

`--hint-path specialized --hint-class l1-1 --typed-chains` selects the known
one-node native body before timing. Runtime classes are already qualified;
`--varying-classes` explicitly exercises a varying prebound class. Multi-line
requests on 64-byte chains downgrade and are labeled in each record. Wider line
controls use qualified 16-line regions. Page8 at grain1 also downgrades; grain1024
retains its 17 fixed hints. Those differ from legacy variable page-loop volume.

Keep setup, physical completion and observer costs visible. Message retry retains
the same identity/timestamp; `--receiver-only --pattern fanin` removes receiver
production; `--pause-after 0` can pause before any offer. Join `--uses 1..16`
retains builds/copies/output storage across actual uses and joins workers each
time. `fixture_wall_ns` includes construction and final joins, while component
sums and execution CPU remain separate fields. The final service caller inlines
its no-hint/first-line body; reduced gap observation still includes sender clocks
and RTT collection. Its zero gap counter means unmeasured, with an explicit flag.

Retained campaigns keep all compared cases/repetitions, counters, hardware and
source manifests, including losing candidates and compiler rejection. They omit
duplicated per-case files/summary and full receipts/binaries/disassembly from
Git; `artifact.json` recovers those exact bytes from S3. `codegen.json` retains
fixed bodies and hot-path instruction evidence, with hashes of full disassembly.

```sh
orb -m ubuntu python3 workbench/spikes/loom-execution/report.py \
  zen=workbench/spikes/loom-execution/evidence/20261006-confirmation/zen/study/rows.jsonl \
  --case '^confirm-warm-specialized-' --metric ns_per_item
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/loom-execution/evidence/20261006-confirmation/zen \
  build/recovered/loom-confirmation-zen
```

Local native and ASan/UBSan checks cover refusal, overflow promotion, underfill,
discard boundaries, exact sampled-clock progress, argument/frame preservation,
unaligned/empty/bounded class binding, arbitrary queue indexing, unchanged
accepted/refused caller packets, full sample retention, pressure, routed identity,
join reuse/duplicates and output capacity. Native fleet checks accompany each
captured timing campaign. None proves a production scheduler or buffer pool.
