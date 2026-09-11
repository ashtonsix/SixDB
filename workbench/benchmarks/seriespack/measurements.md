# SeriesPack implementation measurements

The production implementation covers k=1..64. These workloads ask where its
physical kernels and public seams spend time; correctness coverage and timing
coverage are separate. The [hardware runner](README.md#hardware-profiles-and-retained-results) owns capture,
feature profiles, affinity and result retention. The [reference](../../../ikea/seriespack/reference.md)
owns the promised representation and access contracts.

The [current baseline](findings/baseline-20260911.md) and
[delivery assessment](../../spikes/ikea-composition/seriespack-assessment.md)
reconcile the completed campaign. Earlier findings remain observations of their
captured checkpoints; the current workload source is not their measured binary.

## Workloads and comparisons

`ikea_seriespack_bench` uses the repository's pinned Google Benchmark. Cases and
repetitions run sequentially. Its shared main pins one allowed CPU before
construction or timing; `SIXDB_CPU` can select it. Allocation, prefaulting,
construction and independent value checks precede the timed loop. A compiler
memory barrier prevents repeated encodes/decodes from disappearing.

| Family | Meaning and current coverage |
| --- | --- |
| `runtime/series/...` | Arbitrary ranges over independent dense/gapped placements; u32/u64 outputs, a once-bound callback, full output/canary checks outside timing. |
| `boundary/series/...` | Short, crossing and full ranges with independently encoded wire bytes and timed-pointer output guards. |
| `head-placement/...` | Bound H16 construction under independent payload/head gaps, with separate issued-write-effect cases. |
| `checked-point/...` | Checked versus once-bound scalar reads over six gapped descriptions using an independent wire oracle. |
| `bulk/series/TARGET/LAYOUT/kK/hH/uU/encode` or `decode` | Trusted bound construction/materialization over all 206 legal descriptions, each compiled target, u64 and the smallest adequate unsigned output type. |
| `bulk/predecessor/LAYOUT/kK/h0/u8/...` | Direct LocalPack/ScanPack kernels for k=1..7, including the repaired ScanPack5/7 map. These are the primary immediate-predecessor controls. |
| `bulk/predecessor/local/k56/h0/u64/...` | The wider-body probe's native packet operations in an inline whole-array loop. |
| `bulk/predecessor-region32/local/k56/h0/u64/...` | Its optional AVX-512 32-value encoding region; decode is the unchanged packet control. |
| `bulk/calico/LAYOUT/kK/h0/u64/...` | Constant-shape prior control for every width and both original residual layouts. Its placement and wire format differ from SeriesPack. |
| `bulk/calico/.../u8/...` | Direct residual kernels for k=1..7, avoiding the prior planes API's compulsory u64 adapter. |
| `bulk/plain/uU/copy` | A memcpy control for each native element size. This has no packing work and does not have the same stored extent as a narrower packed width. |
| `resident/.../point` | Independent original-index scalar lookups through an admitted bound endpoint. |
| `resident/.../get16` | Materialize 16 values as u64, starting at an index divisible by 16. |
| `resident/.../dependent` | Each value participates in generation of the next lookup index, together with the previous index and a persistent Weyl nonce. Every provider pays the same index generation work. |
| `resident/predecessor/...` | The same three access operations for LocalPack/ScanPack k=1..7 and the width-56 body probe, with u64 materialized output. |
| `resident/series/.../static-arithmetic/point` or `static-offsets/point` | Bind one whole query region and inline the public static getter within it. These measure the alternative caller seam to an indirect call for each point. |
| `composition/TARGET/.../MASK/authored` or `direct` | Immediate native decode, unsigned comparison and modulo-u64 sum over identical fragments. Eight representative formats, full and 75% prefilter masks; both paths consume the same meaning without materialization. |
| `composition/TARGET/.../MASK/materialized` | Bound native decode into the smallest sufficient unsigned scratch array, then an ordinary filtering/reduction loop over the same original positions. Decode, buffer stores/reads and consumption are timed. |
| `composition/TARGET/.../MASK/dense32-authored` or `dense64-authored` | The same authored operation over admitted contiguous headless Local k5/k7 regions, using 32/64-value native readers. |
| `composition/TARGET/.../MASK/authored-u64-carrier` | The ordinary tile producer and authored operation return native u64 partial sums; the driver combines them and performs one final horizontal reduction. This benchmark-only result representation is separate from the dense read-grain alternative. |
| `casing/TARGET/.../nN/gapG/inU/OPERATION` | Placement admission, binding, checked/bound encode/decode, effect reporting and checked 25% selected mutation over six representative layouts/extents, plus two narrower-source cases. Items are calls; outputs are u64. |

Bulk calls default to 8,192 values (`SERIESPACK_BULK_VALUES`, a multiple of
256). The main baseline uses complete cells/tiles, dense payload streams and
separate dense head streams. Both source and output element bytes are reported.
The trusted encoder/reader is bound before timing; validation, binding, effects,
publication and allocation are excluded from these cases.

The initial random-access matrix covers all 64 unheaded Local layouts and all
12 supported striped payload widths. Calico has both original residual
layouts at every width. Immediate-predecessor controls add both narrow layouts
and the width-56 local body. `SERIESPACK_RESIDENT_BYTES` requests the encoded
footprint, default 128 MiB; the logical count rounds down to a whole 256-value
cell. Construction uses bounded staging, so the k=1 fixture does not allocate
an accompanying multi-GiB u64 array. The query stream spans the entire logical
array and advances across batches. Its own resident bytes are reported. A tiny
replayed set of query addresses would measure a different workload.

The dependent walk starts at index 0 and nonce 0 per fixture. Both states persist
across batches and repeated invocations using that fixture; equal returned values
and initial state define the same query prefix. Independently calibrated providers
can consume different prefix lengths. The former index-only recurrence entered
short cycles (99 or 559 positions for width 56 at a 1 GiB requested extent), so its
timings do not establish broad random access. The persistent Weyl contribution
passes independent address-coverage and partitioned-replay checks at all 64 widths;
this is evidence about the generated stream, not measured cache misses. The
captured `dependent_access_contract` identifies which driver a result used.

**A resident-size label does not establish cache residency.** Interpret results
with the captured machine's cache geometry, the query stream and the operation's
actual read footprint. Heads and stride gaps change that footprint. Small
resident settings are useful correctness/smoke runs, not evidence for cold
random access. For static regions, the driver skips at most one incomplete
query batch at wraparound; indices remain valid original coordinates.
Provider selection, query-stream pointers and extent are retained outside the
query loop. Controls call their selected endpoint indirectly; SeriesPack calls
the admitted bound reader. The static SeriesPack alternative instead selects
one endpoint for a complete query region.

The composition cases are Local k=5,7,12,31,56,64; Local k=60 with an 8-bit head;
and striped k=12. A separately retained scalar source supplies the correctness
oracle outside timing. Authored and direct controls use the same native leaves,
head/body/tail joins, active mask, comparison and reduction. Matching those
controls measures authoring cost; it does not prove that the shared physical
kernel is competitive with every other implementation.

The materialized control supplies another whole-operation plan with the same
values, predicate and reduction. Binding, allocation and the scalar oracle stay
outside timing; the scratch array is reused and its size is reported. The
consumer loop may autovectorize. This control can expose a fused region whose
small work size or frequent reductions outweigh avoiding materialization. It
does not establish an optimal alternative or model suspension and buffer-pool
acquisition. Each case checks the full decoded array and result before and after
timing.

The dense and [deferred-reduction](../../spikes/ikea-composition/native-regions/carriers/README.md)
alternatives keep those controls in the same benchmark. Dense execution changes
the admitted read grain; the carrier case retains tile reads and changes the
representation of one logical modulo-u64 sum. They are independent experiments,
not a combined optimization. Carrier checks include runtime cutoff boundaries
and selected sums exceeding 2^64. These inline measurements do not establish a
carrier ABI across erasure, CPS, or suspension. Existing isolated results retain
their original source captures; the combined workload requires its own timings.

The casing cases cover Local k=12 at n=17 and 8,193; striped k=28/h=8 at
n=257 with dense or 32-byte tile gaps; Local k=60/h=8 at n=8,193 with 32-byte
gaps; and dense Local k=64 at n=8,192. Decode and selected mutation use the
original-coordinate range `[3,n-2)`. Selection picks every fourth position from
that origin, with uncompacted replacement slots. Each checked/bound pair has
the same target, input/output types, placement and result. Selected mutation
currently has one physical implementation and is registered only once per case.
Two additional encode/mutation cases use u8 sources for a wider stored domain:
Local k=9/h=8 at n=17, and striped k=28/h=8 at n=257 with 32-byte gaps. They
retain byte input storage and prepared typed descriptors throughout timing;
the u64 correctness oracle is separate. These also exercise checked encoding
when the source type itself proves value fit, so a validation scan is unnecessary.

Binding cases charge the binding call separately. Other cases bind before timing;
checked endpoints then pay their own validation/selection on each timed call.
Effect cases reuse prepared caller-owned storage and reset its output length
each iteration. Allocation and capacity preparation are excluded; emitted record
count and reserved bytes are reported. Independent value, coverage and foreign-byte
checks run before and after timing. These resident fixtures price specific caller
seams; they do not establish random-access or concurrent publication costs.
Timed writes repeatedly install the same prepared values after the preflight
write. This measures repeated overwrite, not content-sensitive no-op detection.
The checked/bound difference includes repeated validation and endpoint selection;
it is not a direct-physical-body comparison. Byte comparisons validate values,
foreign bytes and reported coverage, while the module's access checks supply
stronger evidence for exact access footprints.

## Keeping the immediate predecessors visible

[`probe_controls.cpp`](probe_controls.cpp) includes the probe's
[LocalPack](../../spikes/ikea-composition/probes/ikea-integers/local.h),
[NEON LocalPack regions](../../spikes/ikea-composition/probes/ikea-integers/local_neon.h),
[ScanPack](../../spikes/ikea-composition/probes/ikea-integers/scan.h), and
[width-56 native body](../../spikes/ikea-composition/probes/ikea-integers/wide56/kernels.h) as Workbench
controls. These are the primary reference where they implement the same work.
Production Ikea has no dependency on them. Their headers and exact selected
variants travel in the worker source capture; no independent copy can drift.

The narrow controls call their inline codecs over successive complete 256-value
regions. The wider control repeats their native packet bodies within the array
call, keeping an extra external call per 256 values out of the comparison. The
optional [32-value encoder](../../spikes/ikea-composition/probes/ikea-integers/wide56/region_encode.h) is a
separately named case. These controls use ordinary benchmark compiler flags.
The narrow adapters check every bit address at compile time against SeriesPack;
all predecessor encoders also compare complete bytes against SeriesPack's scalar
construction during preflight, followed by a value check after decoding.

The random-access controls call original point and 16-value readers, reducing
global indices to the predecessor's bounded 256-value cell first. ScanPack uses
its `fragment_classes` reader. Narrow get16 results widen in registers from the
native byte result to the workload's u64 output; this adapter work is timed.
The width-56 control already produces u64. The bridges have no scratch-byte
array, and their inspected NEON/AVX2/AVX-512 bodies contain no calls or stack
accesses. The optional width-56 region32 changes only bulk encoding and adds no
duplicate random-access case.
The [adapter check](checks/probe-access/README.md) retains the
independent wire/value and guarded-extent verification used for these controls.

LocalPack/ScanPack accept width-valid u8 inputs. SeriesPack's low-field helpers
also support arbitrary higher bits, which headed composition needs. Both timed
whole-array callers supply values fitting K; any retained projection work is
part of the current lowering, not a license to weaken the low-field contract.
No u64 residual or general k=8..64 predecessor is invented: those probe kernels
do not exist. Calico remains an additional, broader-width control.

## Making the Calico control useful

With `--calico`, the runner restores the existing BytePack source capsule; it does not copy prior code into Ikea.
`SERIESPACK_PRIOR_DIR` points at the restored include directory. `BP_CPU` follows
`SIXDB_TUNE` independently of compiler ISA features. The source reference,
prepared-input identity, adapter source and compiler arguments belong in the
same receipt as the result.

`calico_target` records the implementation selected by that control. A full
feature build may register both AVX2 and AVX-512 SeriesPack endpoints against
one AVX-512 Calico control. Comparing those different targets can inform endpoint
selection on that machine, but is not an ISA-matched algorithm comparison.
Execution-family names also do not establish an instruction ceiling: an AVX2
family compiled in the full-feature profile may use additional admitted features.
The captured build profile specifies that ceiling; the runner's AVX2-only profile
supplies the narrower comparison. Both dimensions belong with a result.

[The adapter](calico.cpp) binds a constexpr `planes::Shape` per width
and calls Calico's own native APIs. Simply supplying that constant left outlined
plane lambdas and runtime plane dispatch in Clang 21 output. The measured
adapter therefore adds a scoped `always_inline` annotation to the `planes.h`
include and uses two **adapter-only** LLVM settings:

```text
-mllvm -unroll-threshold=100000 -mllvm -unroll-full-max-count=8
```

This lets the short, constant plane loop specialize. It changes the compilation
of the control, not its format or algorithm. Production Ikea does not inherit
these flags. The adapter's 852 endpoints were inspected for retained Shape
objects and Calico helper calls; the corrected build removes both. Bulk u64
decoders still have their API's zero-initialization work, and can call memset.
NEON and AVX2 guard-page/value checks cover all six operations, both layouts,
u8 residual controls, empty/multiple cells and high 64-bit values. AVX-512 also
needs execution on the actual hardware profile.

This stronger control prevents an old dynamic dispatch cost from becoming the
performance ceiling for the new implementation. It is labeled Calico with a
specialized adapter, not an untouched historical binary or old retained timing.

## Reading results

Google Benchmark emits raw repetitions plus aggregate statistics. Compare
matched operation, logical extent and I/O type. `items_per_second` counts values
for bulk and native composition, but queries for point/get16; `values_per_item`
identifies the 16-value result. Report time per query or per value explicitly.
Keep encoded bytes and materialized output bytes distinct.

[`summarize_bulk.py`](analysis/bulk.py) converts each raw repetition to time per
value, then retains the median and observed range for every SeriesPack case.
It attaches available same-layout prior, fastest-prior-layout and scalar controls;
`--before` adds an exact-name checkpoint comparison. It rejects differing logical
extents and records the input hashes. For example:

```sh
python3 workbench/benchmarks/seriespack/analysis/bulk.py \
  build/candidate/samples.json --before build/baseline/samples.json \
--output build/seriespack-comparison.json
```

`--table build/seriespack-comparison.csv` also emits every SeriesPack case and
its available controls as a flat table. Missing controls stay empty; the JSON
retains the source identities and full machine context.

Older captures need `--prior-target` from their build receipt. The summary tags
cross-target comparisons and excludes them from the ranked same-target output.
Available immediate-predecessor comparisons appear as `same_wire_predecessor`.

[`summarize_access.py`](analysis/access.py) provides the corresponding resident
comparison. Supply `--profile` from the build receipt and optionally `--receipt`
to retain and check it. It verifies logical, encoded and query extents, reports
get16 both per query and per materialized value, and keeps static-region versus
bound-point comparisons distinct. For example:

```sh
python3 workbench/benchmarks/seriespack/analysis/access.py \
  build/resident/samples.json --profile neon \
  --output build/seriespack-access-comparison.json
```

The [caller/composition findings](../../spikes/ikea-composition/native-regions/findings.md) own interpretation of those
separate workloads and their materialized-plan and effect-reporting controls.

Ratios above one mean the candidate takes more time. A missing carrier control
stays missing; a u64 control cannot stand in for u16 materialization. Prior
comparisons at nonzero H use the same full K-bit values with Calico's fixed
progressive-plane layout. Its benchmark label `h0` is not evidence that it lacks
leading byte planes: its captured Shape chooses up to two for every width.
Their placement and traversal differ, so these ratios do not isolate the cost
of optional head separation. The fastest layout
column also changes representation. Neither repetition ranges nor this summary
alone establish statistical significance or equivalent cache behavior.

Use broad sweeps to locate material losses, then repeat the relevant cases with
longer runs and inspect generated code. A short smoke run only establishes that
the workload built and its embedded checks passed. Compare code/table sizes and
compilation costs in the scope affected by a proposed optimization. No fixed
percentage alone decides whether a change is worthwhile.

The casing family separately charges representative partial boundaries, strided
embedding, interface validation/binding and effect output. Sparse mutation is a
different operation from full construction. Do not infer unmeasured combinations
from the dense trusted baseline, or require their full cross-product before
investigating a concrete kernel regression found by that baseline.
