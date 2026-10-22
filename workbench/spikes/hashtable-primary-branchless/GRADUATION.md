# Maintained lookup specialization and selection

Ikea now exposes `get<lookup_hint::predictable>(key)` and
`get<lookup_hint::unpredictable>(key)` on concrete owners and borrowed native
views. **Predictable is the default.** Both return copied `{value, found}`;
absence produces exactly zero value bytes. Neither hint supplies membership
knowledge. Geometry, encoding, occupancy, writer and storage contracts are unchanged.

`lookup_selector` is a caller-owned 64-byte controller for a stream or operator.
`run(count, operation)` calls complete compiled operations on ordered disjoint
ranges, not each getter through a runtime dispatch. It times two useful ranges
every 64 ranges, with at most 256 requests/range, and favors predictable unless
its per-request cost exceeds 1.10 times unpredictable's. State persists across
calls; no request is replayed. Keep the same caller's full-input prefetch horizon
across internal ranges. Use the static default for point calls and tiny batches.

The [usage guide](../../../ikea/docs/hashtable/usage.md#select-lookup-and-consumer-together)
and [complete example](../../../ikea/examples/hashtable/adaptive.cpp) show native
binding, both compiled consumers, persistent selection, PF32 and tails. A getter
alone cannot remove an unpredictable branch in arbitrary consumer side effects.
The fast unpredictable kernel covers AVX-512+BMI1 generic/small-exact set/map8
buckets without overflow; other surfaces retain correct ordinary lookup.

## Verification

The [maintained-source scorecard](evidence/graduation-zen5/tables.md) retains
all hot/cold hit/miss mixes, predictable patterns, phase shifts, actual vendor
controls, and short-batch costs. Native Zen 5 AVX-512 independent reads,
ns/lookup, hit / random50 / miss:

| W32 surface | Original | Default predictable | Automatic | Boost | Abseil |
| --- | --- | --- | --- | --- | --- |
| Set | 1.005 / 7.150 / 1.149 | 0.978 / 7.178 / 1.067 | 1.044 / 1.627 / 1.120 | 1.473 / 6.578 / 1.211 | 1.286 / 6.616 / 1.599 |
| Map8 | 1.464 / 7.354 / 1.196 | 1.389 / 7.246 / 1.156 | 1.453 / 2.583 / 1.223 | 1.864 / 6.551 / 1.192 | 1.774 / 6.860 / 1.846 |

Automatic random throughput is 615M QPS for sets and 387M for map8. All-hit stays
below 1.6 ns. Relative to fresh original controls, the worst predictable-pattern
or cold regression is 3.9% for automatic (grouped W32 map8), and 1.4% for the
static default. Cold PF32 map8 hit/random/miss is 17.643/15.894/5.216 ns at W40
and 32.448/28.403/6.959 ns at W50, preserving the prior gains.

Existing deficits remain visible: predictable hot W40 endpoints still trail
Boost/Abseil at the tested geometries, and cold W50 map8 hit/mixed trails Abseil's
21.660/20.009 ns. This promotion addresses the random branch cliff; those separate gaps remain.
No new AVX2/NEON timing claim is made.

The maintained external-batch check includes real calls and controller cost.
Map8 automatic all-hit is 1.53 ns at batch256, matching the original 1.53 ns;
batch64 is 1.63 versus 1.54 ns. Per-point automatic is 13.07 versus 6.38 ns, so do not
put a selector around each getter. Prefer 256 or more requests/operator for the
small predictable tax; use the static default for tiny calls.

- Full cohort: 1,440 fresh processes / 6,264 complete, unscaled, full-coverage PMU
  intervals; three seeds and two fresh trials/arm/case. Exact ordered input,
  hit/key/full-value answers and Ikea-owned bytes agree across arms.
- Native and local portable: all 29 maintained checks/examples pass, including
  all key widths, wide values, mutation/growth, retained captures and both hints.
- Native ASan/UBSan: getter/selector/example checks, 462 tail processes, then
  expanded no-overflow live-primary checks including zero-valued hits, zero/max
  quotient, every row, first/last bucket, overflow 255 and erase/refill.
- Persistent controller: six fresh processes / 5,040 intervals, identical useful
  answers across methods and batch sizes. A retained native capture survives
  rebuild/erase under both getter hints.
- Promoted runtime headers exactly match the passing native source capture.
  Four predictable W32 map8 cross-compiled range instruction streams also match
  the qualified prototype after local-label normalization. Native linked loops
  confirm selection/clocks outside per-key work and a real zero-word load on
  the unpredictable map path.

`native-graduation.sh full` reuses the exact input/order/geometry controls of the
[qualified experiment](HINTS.md) with the maintained public selector. It builds
actual Boost 1.92 and pinned Abseil, collects calling-thread PMU intervals, and
runs maintained checks and ASan/UBSan getter/selector/tail qualification.
`native-graduation-stream.sh` checks the maintained microbatch seam and extra
primary/capture cases. `graduation-report.py` audits exact ordered sequences,
full useful answers, owned memory and counter coverage before rendering results. The original
rejected overlay generators and duplicate checks are archived, rather than
kept beside the maintained implementation. Linux
`~/sixdb-archive/hashtable-primary-read-20261021/prototype-83b4393d.tar.gz` contains
commit `83b4393d` (124 members, 118,364 bytes), verified SHA256
`90619ddc59b719edc53f70e8de3c2cea208dc12c0cc67997f384a24f905b5674`.
The earlier S3 evidence archives also recover their exact sources. The remaining
harness uses the maintained getter/controller and emits byte-identical measured
C++ after curation.

## Historical Engine consumer

At the captured checkpoint, Engine's [shared-prefix HashLayer](https://github.com/ashtonsix/SixDB/blob/a5f6a1a8/engine/include/engine/directory_hash.h)
supplied Scope-borrowed native groups to its
[complete equality consumer](https://github.com/ashtonsix/SixDB/blob/a5f6a1a8/engine/examples/segment_driver.cpp).
`Producer::equality_selector` persists across calls. Each Scope64 invocation
selects a compiled loop including `HashGroup::get<Hint>`, summary narrowing,
record refinement, afterimage selection and full-row consumption. The Scope
protects every borrowed pointer through completion. The static unpredictable
consumer uses a readable zero-count header on absence.

The [independent hash checks](https://github.com/ashtonsix/SixDB/blob/a5f6a1a8/engine/test/directory_hash.cpp) cover
197 consecutive 64-request batches, both methods, full 64-byte values, zero-valued hits,
sparse misses, dense same-group cloning, old/fresh versions, late discovery,
discard/abort and coupled Segment/locator publication. Normal and ASan/UBSan
suites pass 5/5. Additional actual-consumer sanitizer runs cover two Segments
with all 65,536 slots and 80×128 dispersed rows, afterimages and 25% refinement
misses. Explicit wide absent-prefix IDs exercise true Hashtable misses through
both consumers, including the zero-header/null-cell path.

Engine qualification source capture:
`14d45d74f761ab52b886ce73dcf3c97f2b2828972e80895cc5d1bb719bf39887`.
The consumer, hash implementation, test and three promoted runtime headers
match its consumed-source identities. Engine committed the integration as
`a5f6a1a8`; its [qualified evidence](https://github.com/ashtonsix/SixDB/blob/a5f6a1a8/workbench/spikes/index-directories/evidence/logical-hash-20261021/README.md)
retains source/ELF identities, checks, complete-consumer comparisons and verified
archive recovery. The [implementation record](https://github.com/ashtonsix/SixDB/blob/a5f6a1a8/workbench/spikes/index-directories/IMPLEMENTATION.md)
describes that checkpoint's broader scope. This directory implementation was
subsequently removed ([archive notice](../../notebook/retired-spikes.md#index-directory-spikes--october-2026));
[`engine::directory`](../../../engine/README.md#segment-directory) supersedes its integration status.

The first Engine recipe is a bitmap with 64-byte values; both getters use the
ordinary fallback there. The W32 map8 speedup above is a separate measured
surface. Engine's broader directory acceptance and performance work remain
with its owner.
