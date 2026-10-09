# Versioned consumer performance and ABI

The [2026-10-09 findings](FINDINGS.md) retain the qualified thin-cursor panel,
including remaining deficits, old-reader capacity and exact native call frames.

This is the rejected Stage 2 implementation retained as an experimental
comparison baseline, alongside the fixed-record [consumer](../consumer/).
Architecture, implementation and performance must now be varied and measured
together. The primary comparison must select actual Object state, decode real
Ikea backing and publish real versions.
Codec-only calls and deliberately outlined seams cannot stand in for it.

The primary local comparator stores identical encoded Tuple bodies under opaque
Object/Part identifiers in Orbital and as LMDB values. Both resolve those IDs
and execute the same real Ikea body directly over legal bytes. LMDB values
stay in their mapped transaction; no adapter copies them. The common footprint
is the unaligned, non-overreading 23-byte Tuple representation. Series columns
need stronger alignment and are excluded from this common-payload arm; their
actual Orbital conversion remains a separate case.

The primary read binding caches an 8-byte qualified Tuple cursor per resolved
Object. Its Schema and row count belong to the enclosing fixture/control, and
its actual snapshot protects the bytes. Both adapters open the same ephemeral
24-byte inline native Tuple body; no endpoint points into a former cursor or
wrapper. Qualification proves the actual description/count matches that control.
Generic 56-byte row/column bindings remain for updates and conversion; a small
cursor alone provides no ownership or publication authority.

The primary fixture's external prepared Schema, parent interpretation and Part
layout are fixed for all logical publications. Orbital still selects real
descriptions and qualifies its actual interpretation; the mapped-value arm uses
that same fixed physical law. Changing interpretation/Part count is exercised
separately through actual baseline conversion and old selected descriptors.

A native row-key/Record LMDB arm is a contextual logical-work control, separately
labeled. It cannot attribute differences to Object/Part access, and does not
replace the common-payload comparator.

Identical sparse-key records and request traces drive both bodies. One coherent
selection/read transaction serves
each offered group (1 or 64 requests, with the final tail explicit). Dependently
decoded next keys remain in that same snapshot. Snapshot acquisition, binding
and retirement are inside the batch clock. Throughput and batch latency use
separate fresh stores/replays; batch64 p99 is not a request p99. An independent
ordered-map oracle owns expected fields, outcomes and final state.

LMDB uses its native C API, ordinary locking/COW transactions, `MDB_NOTLS` for
old/new read transactions on the same caller, and `MDB_NOSYNC|MDB_NOMETASYNC`
for this embedded comparison. It keeps real publication and old snapshots.
No durability or multiworker throughput claim is made. New database directories
are required; the harness never clears existing data.

The retained C2 leads are large point-hit throughput and medium/large insertion
tails. This fixed-membership consumer can examine point/dependent reads,
scan16, private field updates, publication with an old reader retained, and
representation conversion. Insert/split costs remain unpriced. Conversion is a
separate actual enclosing operation, with source/destination coexistence and
codec effects visible; LMDB has no fictitious counterpart for it.

The independent baseline check covers atomic multi-Object publication,
future discovery through an old snapshot, an abandoned private group, moved
native bindings, conversion in both directions, and the named complete callers
with batch1/64 and a short final tail. The CLI checks every timed outcome and
all final/old logical rows outside its clocks. Native row-key and directly
mapped-body checks remain separately runnable.

Updates are **adapter-inclusive**. Orbital's wrapper qualifies its source to
preserve `next`, then the actual consumer qualifies it again and constructs a
dynamic Shape; mapped LMDB binds once and reuses the fixture's prepared Shape.
The output reports those logical preparation counts. Do not interpret a write
ratio as isolated publication overhead. Both clone the same complete Tuple
Parts and apply the same actual native field writer. Each publication includes
bounded Orbital drain/poll work; old-reader release plus subsequent catalog drain
and physical polling have a separate clock and footprint. Remaining Arena charge
can be idle Pool backing; successful shutdown independently requires zero charge.
LMDB file-used bytes are a high-water footprint, not a live-payload refund.

Enable the spike and build only the needed targets:

```sh
orb -m ubuntu cmake -S . -B build/orbital-mvcc/performance -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=orbital-mvcc
orb -m ubuntu cmake --build build/orbital-mvcc/performance --target \
  orbital_versioned_performance_check orbital_versioned_performance_bench \
  orbital_versioned_lmdb_check orbital_versioned_body_check -j2
orb -m ubuntu build/orbital-mvcc/performance/workbench/spikes/orbital-mvcc/performance/orbital_versioned_performance_check \
  build/orbital-mvcc/performance/NEW-check-db
```

For ASan/UBSan use a separate build directory with
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fsanitize-address-use-after-return=always -fno-omit-frame-pointer'`
and `-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`, then run the same
checks with `ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1:halt_on_error=1`
and `UBSAN_OPTIONS=halt_on_error=1`. Sanitized clocks are not performance evidence.

The bounded serial runner captures sources, builds incrementally in its own
workspace, records host/compiler/affinity, rotates competitor order and uses
fresh processes/stores for throughput and latency. It checks matched outcomes,
logical requests, group counts and clock modes before summarizing. Its selected
measurements keep all repetitions, copy/preparation counters and initial,
old-reader-held and released footprints:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/performance/run.py \
  --output build/experiments/orbital-versioned/NEW-run --cpu 2 --repetitions 3
```

Authored harness code and selected comparisons belong in Git. Full source
snapshots, binaries, disassembly, routine logs and per-process JSON belong in
ignored output and the verified artifact bundle. Disposable generated databases
are removed only after their trial passes; they are reproducible fixture state.
Use the [retention and recovery helper](../../../tools/artifacts.md) to retain a
chosen run; no benchmark clock establishes a cache-level or durability claim.
