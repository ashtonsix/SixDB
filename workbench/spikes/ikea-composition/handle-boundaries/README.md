# Actual handle boundaries

This is the bounded TuplePack/SeriesPack witness in the
[reusable execution investigation](../reusable-execution.md). It changes no live
Ikea API. Its original ABI witnesses contain no timing loop; the later
[complete block commands](#complete-block-commands) include native measurements.
Independent translation units expose raw handle return/pass, checked factories,
stationary storage/reload, and crowded callers around the existing operations.

The completed [three-profile native comparison](PREDICATE-NATIVE.md) rejects
the cached unit-stride predicate candidate. The general two-pointer reader and
separately prepared predicate remain the endpoint.

The controls freeze the R3 diagnostic source, then change only the reader and
predicate cache placement:

| Control | General reader | Prepared predicate |
| --- | --- | --- |
| `cached24` | R3 pointer pair plus copied code | R3 private predicate |
| `generic16` | Pointer pair; predicates fetch code from its plan | No retained code |
| `predicate16` | Pointer pair | Separate pointer pair plus copied code |

These sizes are compiler observations in the receipt, not ABI predictions. The
raw return and the `expected` factory are inspected separately. A two-word raw
return can be direct while its checked result already needs result storage.

The live interface subsequently restored the general two-pointer reader and
exposed a separately prepared ordinary `predicate`. These immutable controls
retain the earlier spelling so that the experiment remains independent of that
implementation change. The final endpoint's separate qualification is recorded
below.

[Producer](producer.cpp) and [caller](caller.cpp) compile separately without LTO.
The noinline producer calls use real bound reads, checked writes with a real
journal, and encoded predicates. They are not empty kernel stand-ins. Crowded
callers retain seven bindings and six consumed scalar values across the opaque
calls. [SeriesPack boundaries](series.cpp) compile separately, so they do not
force a full width-matrix library build for the TuplePack checks.

[Native checks](check.cpp) reuse the maintained point fixture's four codes and
maps of one/two/four slots, and encoded widths one/five/eight at tight and strided
placement. Bit-by-bit oracles check physical bytes and untouched neighbours;
journals retain actual named sources. Successful and refused updates, output
preservation, guarded exact tails and protected empty activity are consequential
boundaries. These checks provide no timing result.

The frozen `extra_tuple_2.cpp` and its complete `run_extra` caller are also
available to inspect. Only `predicate16` changes its predicate type and prepares
it outside the existing loop. Existing window-index opacity is retained
equally; this study adds no per-value assembly barriers.

Prior work constrains interpretation: the [initial TuplePack study](../../tuple-layout/initial.md)
records its per-row barrier artifact and opaque tiny-operation cost;
[mixed-plan findings](../../tuple-layout/viability.md#what-the-analyser-must-learn-from-complete-workloads)
reject additive isolated-cost selection; [call-boundary findings](../../call-boundaries/README.md)
and the [wide ABI probe](../probes/ikea-blocks/abi/README.md) distinguish metadata,
native payload and checked results. The
[Ring follow-up](../../call-boundaries/ring-dispatch/README.md) retains context
hoisting and register-pressure negatives. Smaller records alone establish no
complete-consumer speedup.

From the repository root, using a frozen R3 `cohesion-inputs/after` as `SOURCE`:

```sh
orb -m ubuntu taskset -c 8 python3 \
  workbench/spikes/ikea-composition/handle-boundaries/run.py \
  --source SOURCE --output OUTPUT --stage abi
```

`--stage check --profile neon-v2` builds the nine owning TuplePack sources and
runs the native checks. `--stage fixture` emits the actual `extra_tuple_2`
objects. All stages are serial and retain dependency hashes, literal object
records, disassembly and commands under the selected ignored output. Reuse
requires the same commands, objects and consumed dependency contents. No cross
object is presented as an executed native check.

## Observed boundaries

The census agrees across NEON/V2, AVX2/Zen 5 and AVX-512/Zen 5: the generic
reader is 16 bytes, prepared predicate 24, ordinary/native writers 16, and
`expected<reader,error>` 24. The old unconditional cache made reader 24 and its
checked result 32. All measured handles have alignment eight and trivial copy
construction, move construction and destruction. These observations are not
portable ABI contracts.

Independent producer/caller objects confirm the raw two-pointer return in
X0/X1 on AArch64 and RAX/RDX on x86. The checked factory uses result memory in
both controls. The actual raw-read roundtrip falls from 19 to nine instructions
on NEON and 18 to eight on x86; the checked factory/read changes less, 44 to 40
and 32 to 29 respectively. Stored reloads and the seven-binding callers also
lose copying work. These are static boundary observations, not timing results.
The binding array and six consumed accumulators remain real caller state;
the probe does not force seven handles to reside in registers simultaneously.

SeriesPack's typed dense reader is likewise 16 bytes and returns in registers.
The selected whole-view expression reader/writer are 48 bytes; erased reader
40 and erased writer with sum maintenance 56. Their raw value returns use
memory. This does not establish that existing const-reference callers pay that
copy cost, or justify replacing all metadata with a universal handle.

## Complete filter caller

The maintained `extra_tuple_2` comparison retains complete admission, warmup,
timed-loop, fallback and result-consumption bodies. All three isolated controls
pass the native independent checks. The full-activity, tight, in-domain
`independent8` `less_equal` path in this actual translation unit has these
**static instructions per eight windows**:

| Profile | Cached reader24 | Generic reader16 | Reader16 + prepared predicate24 |
| --- | ---: | ---: | ---: |
| NEON / V2 | 371 | 379 | 379 |
| AVX2 / Zen 5 | 360 | 360 | 352 |
| AVX-512 / Zen 5 | 311 | 303 | 303 |

Every path retains eight SIMD comparisons and 33 activity/domain/stride/loop
conditions. Generic16 adds a dependent plan-to-code load; the prepared predicate
keeps direct code loads. NEON splits paired source/view loads differently, so
it executes more load instructions while fetching fewer scalar words than the
cached-reader control. All three retain the same 272-byte NEON measuring frame.
The architecture-specific scheduling and branch layout prevent an instruction
count alone from predicting latency or throughput. No wrapper call or control
preparation enters the timed path.

The initial ABI witnesses specify `-O3 -gline-tables-only` with full warnings
and `-Werror`; they are not labelled complete CMake Release commands. A separate
`--configured-release` reproof uses configured `-O3 -g`, all CMake warnings,
no `NDEBUG` or `-Werror`, and the real fixture's no-vectorization flags. Some
x86 complete bodies differ under `-g`; tracing those actual configured objects
independently gives the same path counts above. The records preserve those
differences. The pinned target/tuning and source-prefix-map substitutions remain
explicit in every command.

The separately frozen final public endpoint matches all 49 corresponding
complete fixture functions across the three targets, including their literal
bytes and relocations, plus every allocated data and unwind record. Its raw
producer/caller matches the isolated prepared-predicate control after the sole
private-to-public predicate type correspondence. The original NEON stored-read
witness reordered an independent MOV/LDP pair under line tables; recompiling
that one old caller with full `-g` gives the exact final body. The final endpoint
proof is distinct from the historical R3 performance result.

The final R4 native comparison still has losses: among its 240 TuplePack filter
records per profile, seven AVX2 cases and four AVX-512 cases exceed 5% by ratio
of medians; none on NEON exceeds that threshold. AVX2 tight width-five `less`
bound/fused independent8 loses about 10.9%; some native-chain and inline routes
also lose. The full TuplePack suite retains an AVX-512 two-code body-write loss
near 14.7%. Those measurements compare the complete cohesion revision with
f153, whereas this study isolates handle representation and public naming.
Their trial dispersion and unresolved route/placement questions remain in the
receipt; this study establishes neither overall performance parity nor a
causal explanation of those native losses.

For that proof, supply `--arm final16 --final-source FINAL_SOURCE` together with
`--configured-release`; `FINAL_SOURCE` is the frozen R4 `after` source root.
The old controls remain immutable. [The analyzer](analyse.py) records complete
function sizes, stack adjustments and literal-record hashes. [The path reader](paths.py)
extracts and annotates the specified path from the complete object, preserves
every tested predicate, and refuses an unexpected shape. Both take `OUTPUT` as
their positional argument. Detailed traces and dependencies remain ignored;
the [compact receipt](../evidence/interface-cohesion-v2-20261023/handle-boundaries.json)
retains object hashes, source recovery, flags, census, checks and limits.

## Linked R4 filter losses

[The linked audit](linked-filter.py) reads the four actual Zen R4 filter ELFs
and their disassemblies. It checks function-symbol extents against ELF bytes,
so padding between functions is excluded. It retains both addresses and actual
payloads when a RIP operand changes; anonymous data targets are not assumed
equivalent from their nearest disassembler label.

For widths five/eight and all five operators, the full-active, tight,
in-domain bound and one-stage paths retain the same instruction counts and
opcode multisets in both builds: 40 selected paths across the two targets.
Their addressed constants match exactly. The common entry is raw-identical
at 75 bytes/31 instructions. The common measuring function is 2,154 bytes/473
instructions on AVX2 and 2,151 bytes/470 instructions on AVX-512; only 13 RIP
address operands change in each profile. Every corresponding complete initial
object, nul-terminated string and scalar double matches. This verifies initial
ELF state, not the contents of mutable objects during execution.

The `one_stage` route uses the prepared encoded predicate, just as bound and
inline do. The `shared` route instead decodes through the native reader and
applies a joined decoded-value comparison. Its complete AVX-512 decode and
joined functions are raw-identical. AVX2 changes only ten RIP operands across
those bodies; their full 16/32-byte constants match. This makes `shared` a
useful route control without treating `one_stage` as independent of the API.

The inline path has one concrete extra-work case: AVX2 equality grows from
335 to 342 instructions per eight independent commands at both widths. The
final caller reloads loop-counter state from its stack between seven commands,
where the baseline retained it in a register. The other AVX2 operators retain
their total counts but trade some moves/immediate materialization for jumps.
Every AVX-512 inline path retains 303 instructions and the same opcode multiset.
All keep eight SIMD comparisons and 33 conditions. These facts preserve a
real allocation difference without identifying it as the cause of the observed
timing losses.

The borrowed-view load moves behind the full-activity branch, and the base/origin
loads move behind the stride branch, in inspected bound/one-stage paths.
The old native-reader and final ordinary-reader accessors are both trivial
pointer dereferences and already inline. Source nesting alone does not prove
the cause: CFG layout, alias metadata and register allocation may contribute.
On the actual width-five `less` paths, the Zen 5 scheduling model gives equal
steady block throughput: bound 9.7/9.3 cycles for AVX2/AVX-512 and stage 6.7/6.0.
That model omits fetch placement, predictors, cache behavior and real caller
dependencies; it is no measurement or causal explanation of the 6–11% losses.

The [compact linked receipt](../evidence/interface-cohesion-v2-20261023/linked-filter.json)
retains ELF/disassembly hashes, complete function hashes, literal payloads and
selected path hashes. Full traces remain in ignored output. To reproduce:

```sh
python3 workbench/spikes/ikea-composition/handle-boundaries/linked-filter.py \
  --results R4_ZEN_RESULTS --output OUTPUT/linked-routes.json \
  --compact OUTPUT/linked-filter.json
```

## Prepared-predicate controls

[The representation runner](predicate-storage.py) isolates three controls
against the exact R4 public endpoint. They keep the generic reader at 16 bytes
and the prepared predicate at 24; native kernels, admission, public methods,
and required stable plan/view values are unchanged. Direct pointers and the
first tight-cache attempt remain code-generation controls. The corrected
tight-cache candidate subsequently completed native comparison and was
[rejected](PREDICATE-NATIVE.md); it was never applied to live Ikea.

The direct-pointer control replaces the nested ordinary reader with its two
explicit plan/view pointer members, retaining their order and copied code.
All complete hot functions are strict-identical on all three targets. Only
the three `run_tuple` preparation bodies per target shrink, with corresponding
exception/unwind changes. All 24 allocated data records match. The
[negative receipt](../evidence/interface-cohesion-v2-20261023/predicate-storage.json)
therefore rejects the simple storage-wrapper explanation for lost hoisting.
The current native reader also delegates to the same private native read body,
so another two-pointer forwarding type has no established mechanism advantage.

The first tight-cache control stores `stride()==1` in predicate padding at
preparation and tests that byte during full-active filtering. This loses a
compiler fact: `row_unchecked(first)` still multiplies by the now-unproven
stride. It adds eight address multiplies per eight commands while removing
the old metadata guard; its total path counts tie R4. That
[fact-propagation negative](../evidence/interface-cohesion-v2-20261023/predicate-tight.json)
is retained rather than presented as successful preparation hoisting.

The corrected isolated control computes its full-tight address as
`row_unchecked(0)+first`, carrying the cached unit-stride proof into the address
expression. Active-zero returns before forming it; original-window admission
and stable view values still apply. Sparse, strided and native fallback source
paths remain unchanged. Its actual configured `less_equal` independent8 path
has these static counts:

| Profile | Exact R4 | Direct pointers | Tight flag only | Tight flag + unit-stride address |
| --- | ---: | ---: | ---: | ---: |
| NEON / V2 | 379 | 379 | 379 | 371 |
| AVX2 / Zen 5 | 352 | 352 | 352 | 344 |
| AVX-512 / Zen 5 | 303 | 303 | 303 | 295 |

Each retains eight SIMD comparisons and 33 conditions. The corrected version
saves one instruction per full-tight window and replaces a dependent
view-to-stride test with the prepared byte. Preparation gains a stride load,
comparison and stored byte; repeated execution and one-shot binding have
different tradeoffs. This is a bounded positive code-generation signal,
not evidence of timing recovery or of benefit on every route/operator.

The corrected control passes the independent optimized native NEON checks
above, including widths one/five/eight, strides one/seven, full/sparse/empty
activity, query boundaries and guarded tails. Its census confirms reader16,
predicate24, checked-reader24 and checked-predicate32 with trivial copying,
moving and destruction. Eight owning library objects were reused only after
every consumed dependency hash and the mapped compile command matched;
`initialize` and all three probe objects were compiled freshly. The
[candidate receipt](../evidence/interface-cohesion-v2-20261023/predicate-tight-address.json)
retains those source/command/object proofs. The completed native comparison
finds preparation and one-shot losses despite the shorter selected path. It
keeps the R4 endpoint and does not resolve the earlier f153-versus-R4 losses.

To reproduce a control, supply `--baseline` with the frozen R4 `final16` source
tree and `--baseline-objects` with its configured objects from this study:

```sh
orb -m ubuntu taskset -c 8 python3 \
  workbench/spikes/ikea-composition/handle-boundaries/predicate-storage.py \
  --baseline FINAL16_SOURCE --baseline-objects CONFIGURED_OBJECTS \
  --output OUTPUT --control cached-tight-address --compact OUTPUT/compact.json
```

The other control names are `direct-pointers` and `cached-tight`. Add
`--native-check` for the existing native correctness witness; it builds all
nine owning TuplePack sources unless `--reuse-library DIRECTORY` supplies
dependency-verified matching objects. This changes no timed harness. Full
objects, source closures and disassembly remain in ignored output.

## Complete block commands

[The block witness](blocks.cpp) measures checked read commands over a warm,
ordered 64-block fixture. Shared plans for widths one through eight support
retained predicates, per-block predicate preparation, reader convenience calls,
and fresh placement/reader/predicate binding. Exact tails, full/sparse activity,
three placements and three validated width traces produce 108 cases per
profile. The same scalar oracle checks every route before timing.

[Native methods and results](PREDICATE-NATIVE.md) retain all three profiles,
CPU ns/block and ns/query units, structural costs and byte-identical duplicate
controls. The candidate's ARM one-shot losses and preparation losses on every
profile reject the global change. The [linked ARM validator](blocks-linked.py)
checks the actual four-route bodies against the retained ELF hashes; fewer
selected filter instructions did not establish a complete-command speedup.

## Copied-projection qualification

[The projection controller](projection-native.py) prepares a separate paired
comparison for `predicate<Byte,Rows>`. Both arms share the strict active-unit
access fixes, final writer API correction and factory-inferred harness types;
only `filter.h` differs. The candidate copies its one-code projection and
borrows the named view. The preparation plan and intermediate reader may
expire after binding; the view's value, address and backing storage stay stable.
The source manifest records every input hash, the exact header patch and the
shared controller, fixture, benchmark and CMake wrapper hashes.

Before timing, the controller runs the candidate's 96 core/owner checks and
93 independent public headers on every profile supported by that worker. It
then builds and runs [the candidate-only boundary fixture](projection-check.cpp)
against the same configured library. That fixture destroys the actual
mmap-placed plan and protects its pages, checks all 11 valid packet shapes,
36 width/shift laws, offset 63, exact protected tails, sparse activity and all
five unsigned comparisons against an independent byte oracle. Protected payload
checks exercise empty/out-of-domain shortcuts and error admission order.
Its `IKEA_PROJECTION_CANDIDATE=1` target is enabled only for the candidate arm.

All native builds and both arms' ordinary oracles finish before the existing
five-order timing phase. The controller reuses [the paired executor](predicate-native.py)
for all 360 maintained filter cases and 108 complete block cases per profile,
including the byte-identical duplicate control, common argv0 and common working
directory. [The result reader](predicate-results.py) validates the original
units and structural counters. This protocol establishes no performance result
until the native output has been collected and checked.

After the frozen pair's source and fixture checks have passed, prepare a fresh
managed capture and run its entry point on the native workers:

```sh
python3 workbench/spikes/ikea-composition/handle-boundaries/projection-native.py \
  prepare CAPTURE --source-pair FROZEN_PAIR --metadata FRESH_METADATA_DIRECTORY
bash workbench/spikes/ikea-composition/handle-boundaries/projection-native.sh
```

The worker supplies `SIXDB_RESULTS`, `SIXDB_CPU` and `SIXDB_BUILD_JOBS=8` on the
16-vCPU/32-GiB hosts. Add `run --dry-run --inputs INPUTS --profile PROFILE`
to the Python controller to inspect commands without compiling or measuring.
