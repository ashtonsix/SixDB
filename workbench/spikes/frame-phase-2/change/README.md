# Before-mutation physical envelopes

Ikea HELPER owns this bounded phase-2 change witness. Ikea LEAD owns
instruments and baselines; Claude owns the substrate. This is an experiment,
not a public Frame interface, MVCC protocol or lock manager.

The [phase-1 repair findings](../../frame-phase-1/lifecycle/REPAIR.md) already
qualify adjacent residual repair and show that physical copies and owner work
can dominate fewer logical edits. The [lifecycle account](../../frame-phase-1/lifecycle/README.md)
keeps logical contributions separate from stored deltas and requires coarse
exclusions to avoid payload access. Current [owner integration](../../../../ikea/docs/integration.md)
states that effects hooks precede local store groups, not a whole-operation
admission barrier. We reuse those laws and current Ikea primitives; the old
owner/publication machinery is not part of this witness.

The question is what the caller can declare **before the first payload access
or mutation**: old-data read authority, possible stores and a serialization
envelope. Afterwards actual effects remain source-qualified issued stores,
including preserved neighbours. Logical aggregate contribution is another
quantity. A lease, a logical row lock and a store journal do not individually
establish all four facts.

## The concrete caller composition

[The declaration/admission prototype](envelope.h) uses current APIs. No
maintained Ikea interface or Frame read path changes.

1. Keep the interpretation, named views and backing stable. For each admitted
   coarse block, declare the potential store spans with
   `sp::author::visit_writes_point` and the logical old-value dependencies with
   `sp::visit_point_reads`. Excluded blocks are skipped before acquiring their
   command or payload. For the scalar preserving setter, the prototype admits
   the store footprint as a conservative additional old-read footprint.
2. The owner supplies readable, writable and serialized byte grants, plus
   enough effect slots for the whole command. The experiment conservatively
   serializes the union of reads and possible stores. Backing byte addresses
   unify named sources over the same mapping; named effect-source identities
   alone cannot establish disjointness. Separate virtual mappings of shared
   storage require the owner's alias knowledge.
3. Only after that admission, acquire payload-dependent proof: old logical
   values, both fitting residual replacements and any checkpoint correction.
   Refuse a nonfitting repair before either residual is stored. Packed/FOR
   commands can check their new value against the immutable basis earlier.
4. Invoke maintained checked setters. Resolve actual journal entries through
   the live source's `plane_storage`, then compare their union with the
   declaration and account for the exact logical contribution independently.

`full_grant` is a fixture assertion. It does not acquire a lock, retain a lease,
authenticate a version or make a page readable. Mapping stability, initialized
bytes and real isolation remain owner obligations. The exclusive byte-union
policy is a sufficient experimental rule, rather than a selected concurrency
policy. The prototype allocates vectors during cold declaration/admission;
execution is not a production writer-performance baseline.

The chosen cases need no missing maintained primitive. The owner still needs
an operation-specific declaration above them: a point store footprint cannot
describe a predicted value's reconstruction dependencies. This does not
establish a generic envelope interface for every composed writer or summary.

## What the executable establishes

[The witness](check.cpp) passes on native Linux NEON with pinned Clang 21.1.8:

- 1,536 fitting packed/FOR cases: every row of a 256-row block, Local13 and
  Striped12, across four coarse blocks with all, none and sparse admission.
  A separate bit-wire encoder/decoder checks exact bytes, all logical values
  and preserved neighbours. Excluded payload pages are inaccessible.
- 1,020 fixed-basis delta repairs: both signs of a one-unit change at every
  non-anchor row, with no checkpoints or plain-u64 checkpoints every 16 rows.
  Independent reconstruction verifies every logical value and exact wide SUM;
  checkpoint bytes are checked separately. Unused checkpoint pages are
  inaccessible in the no-checkpoint case.
- Eighteen counted range/value/grant/capacity refusals, plus the delta
  store-only read-grant refusals and TuplePack read-grant check. Protected
  payloads prove metadata-only declarations and early refusals do not acquire
  old bytes. Nonfitting delta proof leaves both residuals, checkpoints and
  effects unchanged. Coarse none also accepts an inaccessible command array.

The independent negative controls expose four different failures:

| Control | What actually happens | Consequence |
| --- | --- | --- |
| A two-byte native-row grant for Local13 row zero | The real checked setter changes five residual-plane bytes outside that grant. | Logical row shape is insufficient to predict physical packed stores. |
| TuplePack native partial-field write, only its store byte readable | The real native body faults across a protected page; its declared possible reads cover 64 bytes and its issued store covers one. | Store coverage does not grant wider old-data reads. |
| A before-summary with inaccessible old data | The point mutation faults before the first effect callback. The summary output is independently observable. | Admission at the first effect hook can already be too late. |
| No-summary overwrite of a read-only byte | The effect callback is observed before the store faults. | The callback marker works, and the hook still precedes its local store. |

The TuplePack case explicitly uses the supported **native author body** with
its plan. The ordinary buffered sparse setter can choose a narrower scalar
route. `read_bytes()` is a possible envelope; it does not assert that every
caller performs every possible read. The linked NEON body contains the wider
old-unit loads. No forbidden inactive-unit window grant is introduced.

The initial callback fixture used an unconsumed `sum_change` accumulator. The
compiler removed its old-value read, so that fixture was rejected. The final
observer writes a value-dependent atomic marker; its result cannot disappear.
That is a fixture correction, not evidence of an Ikea ordering defect.

## Dependencies and costs

Unique physical byte unions from the passing native executable:

| Operation | Declared reads | Possible stores | Serialization union | Additional record bound |
| --- | ---: | ---: | ---: | ---: |
| Local13 point | 6 | 6 | 6 | 2 |
| Striped12 point | 2 | 2 | 2 | 2 |
| Delta row 31, no checkpoints | 57 | 12 | 57 | 4 |
| Delta row 31, checkpoints every 16 | 40 | 20 | 48 | 5 |
| Native TuplePack partial field | 64 | 1 | 64 | 1 |

These are declarations, unique byte footprints and conservative effect-slot
bounds. Repeated loads, overlapping loads, repeated preserving stores and
journal coalescing mean they are neither issued traffic nor event counts.
There are no latency/throughput measurements here.

For delta `s_i = x_i - x_(i-1) - rbase`, changing an interior logical value by
`d` changes `s_j` by `d` and `s_(j+1)` by `-d`. A checkpoint exactly through
`j` also changes by `d`; later prefixes cancel. In the interior, the maintained
packed `sum_change` therefore finishes at zero while the exact logical SUM
changes by `d`. At the last row only one residual changes. Immutable anchor
and basis metadata remain part of the admitted interpretation.

The actual linked NEON `apply_delta` calls admission before its first checkpoint
load or residual load, checks both replacements before its first setter, and
records the checkpoint effect before its store. Its prefix path and local
setters remain real work. The new proof has no pristine-query branch, dirty bit
or metadata lookup on Frame's existing read paths; it changes none of them.

## Qualification and scope

[The qualification receipt](qualification.json) records source/command/binary
hashes, exact results and linked inspection. The native optimized executable
and an ASan/UBSan executable both pass. The sanitizer instruments this witness
and its inline payload operations; linked library preparation code is not
presented as a fully instrumented Ikea build. AVX2/Zen5 and AVX-512/Zen5 objects
compile successfully; these foreign objects have **not** executed.

Run from the repo root on Linux (prefix with `orb -m ubuntu` from the Mac):

```sh
cmake -S workbench/spikes/frame-phase-2/change -B build/frame-helper/envelope-neon \
  -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSIXDB_SPIKES= -DSIXDB_BENCHMARKS= \
  -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2
cmake --build build/frame-helper/envelope-neon \
  --target frame_change_envelope_check frame_change_envelope_sanitize -j1
build/frame-helper/envelope-neon/frame_change_envelope_check
build/frame-helper/envelope-neon/frame_change_envelope_sanitize
```

The same directory can be included by the phase-2 CMake owner. Bulky local
objects, binaries, dependency maps and full disassembly remain under
`build/frame-helper/`; source and commands reproduce them.

This is a fixed-basis, initialized, isolated write witness with empty initial
journals. It covers no anchor changes, exceptions, coded or nullable data,
summary repair beyond the stated exact integer law, retargeting across unresolved
effects, old readers or concurrent execution. Resolve the whole journal before
retargeting: coalescing can alter an earlier last record. The wider phase-1
repair/publication findings keep their own qualification. No public API,
geometry, write policy, fold cadence or full MVCC/journal protocol is selected.
