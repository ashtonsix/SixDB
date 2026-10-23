# One code stream for two fields

2026-10-22. This exploratory Frame case changes both the stored relation and
its complete consumer. Shared pair codes earn a substantial grouped-query win
on OS/ResolutionWidth, while sparse demand, decorrelation, prepared state and
isolated field operations expose material losses. It selects no Frame format,
public interface or automatic policy.

## The operation and the representation

The grouped operation selects original-coordinate rows, filters nullable X by
`X >= threshold`, filters nullable category C, then returns COUNT/SUM grouped by
logical `C >> shift` through the maintained Ikea hash table. NULL categories
have a separate group; no integer key is reserved. X is admitted as u32 and
group sums are exact within u64 for these inputs. Missing records and NULL X
are excluded by their masks. The separate X-only operation returns the unsigned
predicated SUM with zero identity over the same row selection; it has no C
predicate. It is an aggregate, not full materialized projection or point access.

Independent images encode the local C indices and X separately. Shared images
encode distinct `(C-index, X)` pairs once, retaining a common code stream and
two native-u32 projected maps. For example:

```text
pair code       0   1   2   3
projected C     A   A   B   B
projected X    10  20  10  20
```

A histogram of selected codes can fold COUNT/SUM into C groups before translating
only the touched categories to logical hash keys. A query on X alone needs the
common codes and projected X, without reconstructing logical C. Part-local codes
and category editions remain local; the final output merges actual logical keys.
C is also preserved at rows where X is NULL. Validity remains separate, so a
NULL filler and a real zero never become the same logical value.

Both projected maps may repeat values. Existing owner forward reads and SUM
accept that; the current dictionary inverse-equality family resolves one code
and is unsuitable here. For X-only SUM, prepare
`D[i] = X[i] >= threshold ? X[i] : 0`, pack it with the owner and call its
selected coded SUM. This is a legal SUM transformation, not a COUNT shortcut.
Each prepared map has a coherent image/projection identity. Independent dictionary
X receives the same predicate lifting for nonzero thresholds. The original
decode/map X path remains a measured control.

## What the earlier work changed

The [first encoded consumer](ENCODED.md) primarily compared preparation of the
same aggregation algorithm and retained more decoder capacity than body bytes.
This case compares different images/programs, disables address-bound per-block
decoder vectors and charges the remaining preparation. The
[schema-fidelity review](FIDELITY.md) and
[layout study](../../layout-analyser/case-study.md#follow-changes-back-through-the-same-design)
require isolated demand and maintenance beside compression. The
[composition review](../../ikea-composition/predictor-review.md) also cautions
against treating renamed opaque recipes as broader composition evidence.

The shared operation is itself a serious opaque complete kernel. Its gains
establish useful structure and programs; they do not prove a general graph
interface outperforms an owner supplying those same complete operations.

## Inputs and controls

OS/ResolutionWidth and RegionID/ConnectTiming each contain three original-order
65,536-row ClickBench parts. Timing zero is interpreted as NULL in the latter,
explicitly synthetic; only 16,631 of its 196,608 X values remain valid. IMDb
uses 4,096 original title rows with 205 real NULL years. Original order favours
runs. The existing retained transports and dictionary editions are reused.

The authored functional case has 196,608 rows: `X = 20000 + 257*C_latent`, with
256 categories and rotated local C dictionaries. Its decorrelated partner
preserves the exact C sequence and each part’s X multiset, shuffling only their
pairing. Shared dictionary cardinality grows from 768 entries across the three
parts to 124,079. This is an authored mechanism contrast, not query traffic.

All programs create, consume and retire the actual hash output. Independent
kernels use constant C, constant X, merged run boundaries and owner selected
SUM where useful, otherwise actual owner decoders. Sorted-dictionary X is a
separate control with a charged u32 dictionary cache. Shared grouping counts
common codes, then folds the two maps. Native controls use u8/u16 local category
indices plus u32 X, or u8/u16 shared codes plus native projected maps. Their
separate TU contains explicit NEON filtering/reduction and scalar indexed
accumulation, with flat and four-row equal-value coalescing variants. Native
hot loops call no Frame decoder. These are measured native loop controls, not
an exhaustive native/global-FOR or same-packed-bits fused reference.

## Prepared complete-query costs

Numbers are median **ns per complete query**, all-row selection and threshold
zero. They are warmed local AArch64 OrbStack VM observations on vCPU6. They
are not dependent point latency, interleaved reciprocal throughput, cache-level
or DRAM/Loom qualification. Preparation is outside these clocks and reported
below. Native columns show the faster measured flat/coalesced loop for that
cell, not an implemented selector. Full alternatives/selectivities remain in
[the query table](evidence/cross-columns-local/queries.csv).

| Input | Independent | Independent + X dictionary | Shared | Native C-index/u32-X | Native shared codes |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS / width | 176,966 | 180,718 | 69,007 | 219,707 | 130,327 |
| Region / timing | 321,748 | 334,106 | 166,773 | 154,641 | 157,976 |
| IMDb / year | 13,175 | 14,427 | 5,675 | 8,818 | 3,123 |
| Functional control | 332,976 | 405,916 | 215,098 | 274,898 | 73,504 |
| Decorrelated control | 331,346 | 412,564 | 390,190 | 273,680 | 235,940 |

OS benefits from merging the two columns’ run structure. Coalescing makes its
native shared-code control much stronger than the flat loop; the encoded
shared operation still leads. Region is faster than independent encoded columns
but loses to native controls. IMDb and the functional case leave a substantial
native shared-code gap. Decorrelation loses both runtime and storage. None of
these establishes a universal shared-code choice.

Selectivity changes the program comparison too. In the decorrelated scattered
case, the shared histogram loses sharply to independent processing. A large
pair domain is expensive to clear/fold when few rows need it. Four-row native
coalescing helps OS but slows unpredictable authored data; both paths are kept.

### X-only predicated SUM

| Input | Independent | Independent + X dictionary | Shared owner SUM | Shared decode/map | Native u32 X | Native shared map |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OS / width | 28,598 | 40,436 | 24,881 | 66,132 | 48,816 | 179,294 |
| Region / timing | 77,664 | 77,967 | 101,806 | 152,151 | 97,787 | 15,305 |
| IMDb / year | 2,084 | 5,430 | 5,396 | 5,260 | 969 | 3,804 |
| Functional control | 102,295 | 245,446 | 122,304 | 232,281 | 49,126 | 173,030 |
| Decorrelated control | 101,918 | 245,586 | 145,786 | 254,960 | 48,802 | 181,266 |

The owner’s forward-map operation changes the X-only verdict on OS and narrows
other losses. Treating the slower decode/map composition as an inherent shared
format tax would have been wrong. Region, IMDb, functional and decorrelated
inputs still retain important isolated-demand gaps. This cohort did not measure
narrower native/global-FOR X arrays. The separate
[native-domain follow-up](NATIVE-DOMAINS.md) adds those controls without pooling
these samples; materialized X projection remains unmeasured.

## Memory and preparation

Bytes below distinguish mutually separate image capacity, common keys/masks
and group-plan/workspace capacity. They count vector/control storage, not RSS,
allocator rounding, stack, executable code or the retained logical test oracles.
The common harness keeps all three presence/validity planes even on all-valid
data; that is a control, not a proposed mandatory Frame encoding. These shared
planes materially reduce OS’s whole-inventory saving. The native-u32 forward
maps are also one tested layout, not a lower bound for joint representations.

| Input | Independent image | Shared image | Common keys/masks | Independent group state | Shared group state |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS / width | 81,532 | 68,222 | 74,752 | 26,479 | 30,251 |
| Region / timing | 146,512 | 201,148 | 81,992 | 41,864 | 65,400 |
| IMDb / year | 8,420 | 7,280 | 1,760 | 806 | 1,690 |
| Functional control | 616,056 | 215,976 | 80,160 | 37,971 | 41,043 |
| Decorrelated control | 616,056 | 2,118,532 | 80,160 | 37,971 | 534,287 |

Per-query forward weights and their decoded owner cache add further state for
X-only SUM. Preparation/first-call values are **single diagnostic observations**,
not stable cold latency estimates or an amortization model. The first call
includes both lazy cache work and actual SUM. The common row-mask preparer is
shared by all arms and also prepares C flags for the grouped operation; it is
not a minimal standalone X-only binder. Component clocks are not summed into
a claimed one-shot query result.

| Input | Common mask/flag preparation, ns | Extra X weights preparation, ns | First owner X call, ns | Extra X capacity, B |
| --- | ---: | ---: | ---: | ---: |
| OS / width | 176,717 | 11,045 | 36,010 | 9,468 |
| Region / timing | 32,343 | 18,338 | 108,030 | 58,568 |
| IMDb / year | 4,084 | 2,083 | 6,875 | 2,380 |
| Functional control | 177,374 | 13,500 | 129,124 | 8,232 |
| Decorrelated control | 175,374 | 167,541 | 194,831 | 1,256,758 |

The retained independent dictionary also demonstrates why body bytes alone
are insufficient: its functional body is smaller than plain, but spare capacity
and prepared dictionary state erase that resident advantage while grouping
slows. Construction component clocks are retained in the
[material inventory](evidence/cross-columns-local/material.csv), with different
component scopes; native shared-code construction reuses the prepared pair map.
No from-logical encoding-time winner is inferred from those observations.

## Changing C while preserving X

Each part receives 64 dispersed existing-domain C edits. Independent storage
re-encodes C and retains X. Shared storage looks up/appends `(new C, old X)`
pairs and re-encodes common codes. Every live original position is checked for
the requested C and unchanged X; validity and old const images remain intact.
The optional mutable reverse index is measured separately through a counting
PMR resource. It is absent from the read-only image and is not an Orbital layer.

These are **whole-part private repair-and-retirement controls**: decode, change,
encode, temporary dictionary copying, index append/undo and private disposal.
They are not the minimum repair grain or publication/MVCC measurements. Real
publication and retained-reader policy belong to
[Ikea’s lifecycle work](../lifecycle/REPAIR.md). Timings alternate the two arms
across six repetitions. Representative first-part observations follow; all
[28 part/arm records](evidence/cross-columns-local/repairs.csv) are retained.

| Input | Independent batch, ns | Shared batch, ns | New pair entries | Optional shared reverse index, B |
| --- | ---: | ---: | ---: | ---: |
| OS / width | 817,066 | 967,963 | 14 | 10,952 |
| Region / timing | 878,264 | 1,206,220 | 6 | 57,824 |
| IMDb / year | 22,125 | 43,188 | 7 | 4,048 |
| Functional control | 2,190,335 | 2,248,965 | 61 | 8,840 |
| Decorrelated control | 2,266,965 | 2,052,025 | 21 | 1,327,616 |

Shared repair adds a real mapping dependency even though logical X did not
change. It is not uniformly slower: owner candidate work depends on the new
code stream. Appended entries are not reclaimed in this single-batch control;
new logical C-domain values, sustained churn, dictionary cleaning and selective
block repair need further assessment.

## Consequence for Frame

Preserve common-code identity, per-field forward relations and the operations
licensed by those relations. Order, injectivity and logical value meaning are
separate facts: forward SUM can remain valid after a substitution that
invalidates a one-code equality inverse. Query-derived maps/cache editions
belong to preparation, while their actual code stream and interpretation must
remain coherent. Read dependencies and C-only repair dependencies also differ.

The main unresolved execution gap is substantial: the functional shared owner
histogram is about three times the native shared-code loop. Linked inspection
confirms native X SUM uses explicit `cmhs`/`uadalp`, coalescing uses
`cmeq`/`uminv`, and native loops call no Frame reconstruction. The Frame complete
group includes owner decoder calls and widened stack temporaries. Those facts
do not isolate the cause of the gap: storage, dispatch, carrier width, loop
shape and placement differ. Native-width/fused packed consumers merit comparison
before turning this decomposition into an execution boundary.

Compact/factorized forward maps and direct projected grouping can also change
the high-cardinality trade-off. Conditioned bases and routed streams remain
other representations to investigate. This case earns keeping shared structure
in the research space; it does not close those alternatives or choose a default.

## Qualification and recovery

The final [qualification](evidence/cross-columns-local/qualification.json)
freezes actual code, body operations, compiler/flags and inputs. The consumer
uses Clang 21.1.8, Release/O3, native ISA and generic tuning; its auto-vectorization
is disabled. The owner library retains its ordinary flags. Encoding code is
`3c3b5824`; the owner later qualified 417 cases at `df8c262d`, whose difference
is only documentation and its NULL cost driver, not the library used here.

Five authored documents pass 684 reported logical group/SUM comparisons, covering
partial/coarse masks, holes, NULLs, extremes and exceptional bodies. The real
and coupling inputs add 432 comparisons and 7,776 timed samples. Repair checks
compare all live original positions. Checks use full key/count/sum outputs or
logical X sums, not digest equality alone. The authored counterexample’s exact
marginals and unchanged C sequence are independently asserted.

Eighteen Williams-order rows balance each ordered within-row treatment pair
twice, with equal repetition counts across arms for each query/operation.
Exact same-code/input/workspace duplicates have 0.404% median and 2.493% maximum
absolute median difference. Close rankings remain observations, not significance
claims. Earlier cyclic pilots stay separate; they are not mixed into the table.
This controls first-order carryover, not arbitrary machine interference.

[CSV selections](evidence/cross-columns-local/queries.csv),
[calibration](evidence/cross-columns-local/calibration.csv),
[source/input identities](evidence/cross-columns-local/inputs.json) and
[linked-code facts](evidence/cross-columns-local/codegen.json) remain small.
The [recovery instructions](evidence/cross-columns-local/REPRODUCE.md) and
[artifact](evidence/cross-columns-local/artifact.json) retain frozen source, ELF,
raw samples, disassembly and exact authored inputs. Real logical inputs reuse
the earlier encoded-consumer artifact by verified member/hash. Run
`columns_report.py RECOVERED NEW_REPORT` for the offline tables. The measured
entry is `frame_execution_columns`; current source can run through
`columns_run.py --binary PATH --output NEW --trials 18`. Historical reproduction
uses the frozen source and flags, rather than whichever owner implementation
happens to be current.
