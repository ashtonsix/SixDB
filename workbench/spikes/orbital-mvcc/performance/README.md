# Versioned consumer performance and ABI

[Iteration feedback](FEEDBACK.md) retains paired read and publication results,
including negative tail/counter findings and source limits. The earlier
[thin-cursor findings](FINDINGS.md) remain a historical baseline comparison.
Each comparison belongs to its captured sources; it does not qualify later
directory, representation or Allocation changes.

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

The legacy read binding stores an 8-byte qualified Tuple cursor; direct16 and
scratch-free consume16 are separate whole-body treatments applied to both owned
Orbital and mapped LMDB. The common direct-mapped request cache has one slot for
scalar calls and 64 for batch calls, regardless of catalog size. It performs one
probe, rebinds collisions using actual opaque IDs, and initializes only used
slots. Its Schema/count remain fixture/control state. Generic row/column bindings
remain for updates and conversion; cursor size alone is not ownership cost.

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
ratio as isolated publication overhead. Store8/16/32 use the real candidate catalogs with the same Allocation/Pool and
charged body-owner records; their generic write control binds once and reuses
the fixed Shape. All clone identical complete Tuple Parts and apply the actual
native writer. The writer basis ends before install/drain; Prepared pins its
exact catalog. Baseline publication uses bounded drain, candidates full drain,
and physical polling is bounded. This policy difference remains in the output.

`--retention-age N` rotates a real old reader after every N publications and
first discovers an old Object immediately before that rotation, inside the
publication envelope. Zero holds the original reader throughout. Final retained
age is the remainder after periodic rotation, reported separately from N. Old
reader release, catalog drain and physical poll have a separate clock/footprint
after full old/current validation; that release is warm from validation. Remaining Arena charge
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

The serial runner captures sources, builds incrementally in its own
workspace, records host/compiler/affinity and combined build wall/CPU/largest
process memory, rotates codec and competitor order together, and uses
fresh processes/stores for throughput and latency. It checks matched outcomes,
logical requests, group counts and clock modes before summarizing. Its selected
measurements keep all repetitions, copy/preparation counters and initial,
old-reader-held and released footprints:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/performance/run.py \
  --output build/experiments/orbital-versioned/NEW-run --cpu 2 --repetitions 5 --profile churn
```

Profiles are bounded discriminators: `feedback` varies body admission/consumption;
`catalog` compares real Store8/16/32; `churn` measures actual writes with rolling
old readers; `groups` uses 192 rows in 64 Objects to exercise finite Allocations;
`resident` replays the earlier context/conversion controls. `quick` is a small
64-Object read/write feedback screen with two repetitions; tails are directional.
`faults` compares original versus equal sample-buffer preparation in both clock
modes; the long LMDB write-mode discrepancy remains unresolved. Corpus supports
1–4096 Objects; `--metadata-bytes` explicitly divides the fixed total limit.
The common comparison has one independent Tuple Part per Object. Full construction
Schema remains resident in Corpus: Pool/PMR totals exclude it while process
memory includes it.

`composed-inline` and `composed-directory` use the actual composed catalog,
shared Shape and native consumer. `lmdb-composed` receives the same Shape read
bridge over protected mapped values, plus the same private `write_batch` body.
The older `lmdb` and catalog arms remain comparison controls. `composed-quick`
keeps the 192-row / 64-Object read/write discriminator with both clock modes;
The retained first composed screen pairs current Allocation against the
cumulative compact, Storage16 and growth-core source treatment. Two repetitions give quick feedback;
their tails do not qualify a design.

`composed-context` adds 4096 rows in four Objects, batch1/64 updates and rolling
old readers. Tuple includes matched native LMDB; four independent Series Parts
are a contextual composed case. One repetition gives a modest directional
sample, with preparation, CPU, copies, capacity and reclamation recorded.

The separate `writer_law_screen.sh` requires an isolated source cut containing
the released [writer-law control](../metadata/consumer/shape-write.md) and its
integration patch. It fixes the first-three Allocation controls, Storage16 and
the full growth API in both cuts, then runs default composed before writer-law.
Caller-owned private state and later maintenance controls remain separate.
The shared writer law owns prepared controls in stationary inline slots; their
live-byte and slot views overlap the containing Backend/MappedShape records.
The fixture's original Schema remains resident and charged. Recover the exact
captured cuts and run each profile directly to repeat a retained comparison;
the screen script applies source treatments to the original isolated baseline.

`maintenance_screen.sh` starts from that measured writer-law cut with Allocation
already fixed. An unapplied caller/reporting patch adds a default full-drain
helper, detailed maintenance counters and publication-plus-final-settlement
cost. Candidate-header deltas compare original and new full drain; the separate
32-step control bounds each publication and reader-rotation maintenance call.
Final release still uses full drain and the same physical poll. Work steps do
not cap allocator, OS or backing-destructor time. Old unavailable counters stay
empty rather than becoming zero. The selected negative batch result in
[feedback](FEEDBACK.md) keeps full drain as the current default.

`allocation_routes_screen.sh` instead fixes that new full-drain writer cut and
compares Storage16, Group advice and Direct8/Direct4. The latter two share the
checked Hold API and count limit; Shape export refusal is carried through the
owner patch. A common cold accounting adapter observes each actual layout.
These remain pinned Clang/Linux address-routing controls with the owner's
[documented reachability limit](../metadata/orbital/allocation/routes/direct-domain/README.md).
The screen retains requested metadata, actual copies/capacity/reclaim work,
timings and all competitors; it does not measure foreign contention or heat
benefits under pressure.

Composed reads use the protected native Tuple bridge. `--from columns` instead
uses four real Series Parts and the generic consumer with its resident Schema;
this is a contextual Orbital case, without a fictitious mapped-LMDB counterpart.
Both directions of actual conversion, alternative adoption and old selected
bodies run through the enclosing driver. These paths have targeted oracle checks;
the prior caller sanitizer receipt does not qualify this new source cut.

Composed output separates PMR live/peak/calls, shared proof (already included in
PMR), borrowed Schema/writer controls, Pool setup/inventory, actual live Allocation
capacity and Arena charge. Optional held extent is `null` when a treatment removes
the diagnostic. Backend and caller-array record sizes are explicit; they do not
measure compiled frames. Mapped Shape proof bytes are reported separately from
LMDB's map/file footprint. No total-metadata target is established by these
component inventories.

Correctness qualification uses captured sources and meaningful lifetime/failure
cases, including sparse 64-Object writes with repeated keys, a failure after
earlier private clones, unchanged published bytes, successful retry, late old
discovery, no old reader accidentally held by the writer, and actual physical-capacity
refusal after an earlier clone followed by successful retry:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/performance/qualify.py \
  --sanitize --output build/orbital-mvcc/qualification/NEW-cut
```

ASan/UBSan/always-use-after-return/leak checks cover 120 CLI cases through
4096 Objects and four explicit 1 MiB metadata-capacity refusals. Clocks in
qualification are not performance evidence. `--sanitizer-qualification DIR` in
the performance runner validates actual compiled-input hashes and binary hashes
before attaching that receipt; another source cut is refused. New summaries
leave unavailable counters blank, and own-process VmHWM/VmRSS is separate from
possibly inherited rusage high-water.

Authored harness code and selected comparisons belong in Git. Full source
snapshots, binaries, disassembly, routine logs and per-process JSON belong in
ignored output and the verified artifact bundle. Disposable generated databases
are removed only after their trial passes; they are reproducible fixture state.
Use the [retention and recovery helper](../../../tools/artifacts.md) to retain a
chosen run; no benchmark clock establishes a cache-level or durability claim.
