# Native Calico Frame controls

This front-A control turns the [source survey](../../../frame-phase-1/SURVEY.md#2-calico)
into executable controls at Calico `ac83c82b9a0c8d76bea92829e471ebc0d98b1978`.
The owning `frame/REPORT.md` retains narrow-reduction wins, G4 FastLanes cold
losses, patch-decode losses and stale coverage of later paths; `frame/SPEC.md`
retains the prepass drift failure. Those findings favor exercising the actual
current RawCodec and Engine operations across widths, layouts and escapes,
without relying on codebook selectivity or importing historical timings.

[Source provenance](source.json) pins 24 unchanged tracked files, 96,144 bytes
compressed, from the actual quoted-header closure plus owning findings and
tests as references. [Source recovery](source-artifact.json) excludes untracked
sibling experiments and the old harness. No absolute sibling path is required.

From the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/frame-phase-2/instruments/calico/run.py --prepare
orb -m ubuntu python3 workbench/spikes/frame-phase-2/instruments/calico/run.py
```

The runner recovers and verifies the archive, then uses incremental native
Clang 21.1.8 / C++23 builds, one job on CPU 8, `armv8-a+simd`, tuned for
Neoverse V2. It verifies every frozen upstream file before and after, records
both actual compile commands and linked SUM/NEON paths, and leaves full local
outputs in `build/frame-helper/calico/`. `--build-root` supports a fresh source
recovery/build location; `--cpu` changes the local affinity.

For parent integration, include `workbench/peers/frame-calico.cmake` and add
this directory. `frame_calico_controls` is the callable static target;
`frame_calico_check` is the functional witness. Both are excluded from the
default build. `WORKBENCH_FRAME_CALICO_ARCHIVE` can name a separately recovered
archive. No parent wiring is installed here.

## Callable owners and arithmetic

[control.h](control.h) exposes two distinct, noncopyable owners. Construct them
at a stable address, such as behind `unique_ptr`; their Type, codec, binding and
plans contain borrows. Input spans are needed only during construction.

| Owner | Actual operation | Logical domain and result |
|---|---|---|
| `signed_image` | `frame::RawCodec` / `RawStream`, `engine::raw_farray_sum_bind`, then bound `raw_farray_sum_trusted` | Signed int64 fields, symbols, bases, residuals and escape overrides; exact i128 SUM over complete uniform frames; zero on empty |
| `unsigned_image` | Engine Type-bound v1 store, checked load, then borrowed trusted view's `sum` | Unsigned physical widths 1/2/4/8, exact evidence, complete RawCodec frames plus the existing literal tail; exact u128 SUM; zero on empty; escapes and nullable fields remain unsupported |

The unsigned writer receives little-endian field bytes from the constructor's
logical uint64 spans. The actual Engine owner performs order encoding,
compression, evidence generation, main serialization and tail handling. The
wrapper does not implement Calico codec algebra. For u64 the actual Engine SUM
corrects its signed order encoding by adding `2^63 * complete_rows` and consumes
the unsigned literal tail. Every complete operation is checked against an
independent sum of original logical values. The largest fixture has 66,561
rows, so every unsigned total is below `2^81`; signed totals fit i128 as well.
Future callers must establish their own accumulator bound.

The separate `frame::aggregate::sum<T>` leaf witness uses u8/u16/u32 residuals,
base, optional mask and unique escape overrides. Its returned int64 follows
modulo `2^64`, including per-row base addition. It has 445 explicit cases where
the corresponding exact i128 sum differs. This leaf is not an exact whole-image
SUM control or a complete predicate-to-selected-SUM pipeline.

Both owners also forward the actual inclusive range-mask operation. Mask bit
`i` is byte `i/8`, bit `i%8`; the unsigned container appends literal-tail bits.
For narrow unsigned fields the tested query bounds remain inside the physical
field domain. Extreme wide predicates have the upstream limitation below.
No wrapper clamps, rewrites or falls back for an unsupported predicate.

## Preparation, query buffers and readable windows

Construction, encoding, validation and Type/plan preparation precede query
calls. Signed construction owns symbols, aligned body, patches, checkpoints and
frame offsets, validates the stream, and binds one exact-size symbol SUM plan
per field. Unsigned construction prepares the actual Type-bound plan, writes
metadata/body, independently re-proves exact evidence with checked load once,
then borrows those same immutable bytes. The transient checked/decompressed
view and input wire staging die before queries. The query view retains only
its real derived tables; it does not retain logical input copies.

Whole SUM walks encoded bytes on every call. It does not populate or use
`sum_frame`'s per-frame answer cache; the witness checks the cache remains empty.
The actual bound raw SUM has an 8 KiB stack residual scratch array per call.
Range predicates use the actual codec's stack scratch and caller-owned output
vector, whose capacity may be reused. Preparation time and allocator overhead
have not been measured.

`sum_ranges(field)` reports sufficient **same-owner SUM** ranges: owner and
plan headers, symbols/body or complete serialized container, selected escapes,
bound symbol table, and required width/byte-offset tables. The witness checks
that query borrows fall inside those ranges and that addresses/extents remain
stable after reuse. Whole-SUM does not read checkpoint/offset tables. The
container span includes evidence and other fields, so its size is not minimal
SUM traffic. Capacities are reported separately from logical extents; ranges
can overlap. They are not a complete retained-memory census (private codec
allocation capacities are unavailable) or predicate-conditioning coverage.
An integrator must declare which ranges it conditions and the state of plans,
outputs and scratch; these controls make no warm/cold claim.

No extra payload read slack is requested. Separate read-envelope fixtures copy
encoded symbols/body, complete container bodies and dense residuals so their
last admitted byte borders a protected page. Copied patch vectors have
`capacity == size`. Format alignment remains part of the actual byte image.
These tests cover the stated native cases and buffer ends, not every field
boundary, permitted grain, future ISA or hidden allocator capacity.

## Qualification and known negative

[qualification.json](qualification.json) and [recoverable selected evidence](evidence-artifact.json)
record the finished native result. Optimized and ASan/UBSan builds each pass
1,638 cases / 48,901,949 checks, 6,649 SUM calls, 5,538 range calls, 7,858
guarded calls and 1,042 refusals. The wrapper and executed upstream headers are instrumented at O1 in the
sanitizer build; the optimized build uses O3.
Fresh independent source recovery verifies all 24 files. The positive matrix
exercises:

- Bytepack, byteplane, lane256 and FastLanes at legal widths, including 0,
  8/16/32 transitions and 63/64; grains 256 and 1024 where applicable.
- Empty, 1/3/65 complete frames; unsigned empty, single, `z-1`, `z`, `z+1`,
  `3z+17`, `65z+1` rows; signed bases/extremes; no/sparse/dense/all escapes;
  five actually selected symbols and mixed widths; checkpoint fallback.
- Independent SUM/mask/point oracles, repeated calls, malformed checkpoint and
  row-count refusals, wrapper domain/width/permutation/escape refusals,
  same-owner ranges and protected-page read ends.
- Unmasked, all, empty and partial leaf masks; irregular reduction tails and
  explicit modulo-wide counterexamples.

**The pinned upstream extreme-range path is nonpassing under UBSan.**
`frame/prepass.h:168` in `res_window` subtracts signed endpoints. The reproducible
case `raw_lane256_k63_frames1_patch0` (`frame_calico_check raw 404`) reaches
`INT64_MAX - INT64_MIN`. This endpoint is outside the
sanitizer-qualified predicate matrix. The unchanged source,
reproducer and diagnostic are retained separately, and the runner requires
this negative to remain visible.

The passing `all-safe-ranges` matrix retains all wide SUM/leaf cases but omits
174 range queries: extreme equal endpoints for wide signed boxes (K >= 63),
and `UINT64_MAX` as the u64/K64 lower endpoint. This is a declared fixture
restriction, not a source fix or a complete classification of safe predicates.
The normal `all` command still exercises unrestricted original endpoints.

There are no query timings, worker results, native x86 qualification, nullable
controls, performance parity claim or inherited historical measurement here.
