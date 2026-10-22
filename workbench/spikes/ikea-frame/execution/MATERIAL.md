# Borrowed encoded material across physical versions

Engine contribution to [Frame's lifecycle investigation](../lifecycle/README.md),
2026-10-22. This private witness asks whether existing encoded operations remain
correct when their bytes live in actual Orbital Parts, survive a successor
publication and escape the Object catalog. It does not choose a Frame format,
ownership grain or mutation policy. It supplies no timing result.

## Why this belongs in the schema-fidelity investigation

Retaining payload is insufficient if its interpretation is lost. A delta body
needs its checkpoint mode and reconstruction parameters. A code body needs the
right dictionary, including its order, base, width, population and identity.
Those facts can change independently of the native operation implementing the
read. Frame's description must preserve these dependencies without retaining an
entire encoder object or its prepared address-bound readers.

The [fidelity review](FIDELITY.md) separates representation laws, address discovery,
evidence, execution and maintenance. Here the forcing case is interpretation
across real physical lifetimes. The [composed-material study](../../orbital-mvcc/metadata/composed/README.md)
already distinguishes proof, binding and allocation capacity. Its
[transient-Schema negative](../../orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase)
also cautions that smaller resident metadata can make repeated tiny construction
much slower. Neither fine ownership nor small metadata is an automatic win.

## Reuse the existing ownership mechanisms

[Orbital material composition](../../../../orbital/docs/material.md) admits a
successor assembled from retained certified source bytes and fresh Allocations.
The public [extension adapter](../../../../orbital/test/extension_material.cpp)
and [Object exercise](../../../../orbital/test/extension.cpp) demonstrate
publication, retained old Scopes and an escaping selected Part. The
[source checks](../../../../orbital/test/part_sources.cpp) cover slices, padding,
grouped/independent owners and refusal. This witness uses those mechanisms; it
adds no allocator or ownership wrapper.

Three obligations remain separate:

- **Bytes and read grants.** A PartSource carries a certified contiguous body
  and backing. Its body plus readable padding must fit the original certificate;
  spare allocation capacity does not extend it. Source, proof and resource
  remain alive and stationary through capture.
- **Interpretation.** A successor preserves its own descriptors, coordinates,
  checkpoint mode and dictionary dependency. Capturing a source retains backing,
  not an ancestor Control or its incidental pointers. A selected copy must retain
  the selected interpretation after the catalog disappears.
- **Reclamation.** Distinct Parts need not have distinct physical owners.
  Retaining one slice can retain its obsolete sibling. Logical body extent,
  referenced owner capacity, metadata, Pool idle capacity and Arena charge answer
  different questions. Idle does not mean unmapped or nonresident.

Capturing `sizeof(frame_encoding::column)` would capture vector headers rather
than their nested material. A shared pointer to that column would still not
make each nested range an independently protected or reclaimable Part.

## A small witness using the owner's actual operations

[`material-witness/material.h`](material-witness/material.h),
[`material.cpp`](material-witness/material.cpp) and
[`check.cpp`](material-witness/check.cpp) contain the fixture. One Object has
two regions of 16 blocks × 256 original positions. These fixed capacities are
fixture limits, not proposed Frame limits.

The existing encoder constructs temporary columns. The fixture copies their
actual block bodies and optional dictionary plane into native Orbital
Allocations, using the owner's reported extents and grants. It records
pointer-free interpretation in an inline Control and destroys the columns before
retained reads. These copies are real work and are counted. Encoder scratch,
internal encoder copies and test oracles are outside that counter.

The [private borrowed-block seam](../encoding/README.md#borrowed-blocks) supplies
`extent_of`, `admits`, point/decode and logical/stored SUM/COUNT operations.
The fixture resolves addresses within the selected Part and supplies that view.
It implements no bit packing, delta reconstruction, patch handling or dictionary
translation. The body descriptor's former column offset is normalized away;
the explicit local offset locates the relocated body.

A dictionary plane resides in the same Part as its code bodies. Its identity
comes from the actual column factory. Equal local part indices in distinct
column images must remain distinct identities. Reusing one SUM or COUNT cache
across old/new/old reads therefore exercises the factory's identity contract,
rather than assigning convenient globally distinct fixture tags. The unchanged
region preserves its identity; its changed sibling obtains a new identity and
switches sorted/frequency order. No original column pointer survives.

The fixture admits initialized native bytes before ownership transfer. Physical
capture then uses Orbital's own source validation; it does not parse a forged
source span before its ownership certificate is validated. Attach refusal owns
nothing and consumes nothing. The owner's structural admission checks extents
and body shape; it is not content authentication or a substitute for the trusted
encoder's code-to-dictionary provenance. Matching metadata identities alone
cannot prove that arbitrary bytes are the right dictionary.

### The lifetime exercised

```text
publish two old regions; open old Scope
  -> retain certified source for region 0
  -> encode/copy replacement region 1 into fresh Allocation
  -> capture successor; Prepared publication; open current Scope
  -> read current / old / current with shared query caches
  -> release old Scope and drain; read current again
  -> export both selected Parts; destroy Scopes and catalog
  -> read both exports; release changed export; reread retained export
  -> release final export; close Pool; require zero Arena charge
```

The independent-owner arm allocates each old region separately. The contrasting
arm puts both old regions in one original owner. Both reuse region 0 without
copying it into the successor. Only the independent arm can retire the old
region 1 owner while region 0 remains live. Random full-width filler blocks make
these native allocations large enough for Window backing, so the public Pool
idle counters can expose that difference without relying on finite-slot reuse.

Every selected region is compared with original logical inputs: every point,
full decode, a partial range crossing a block boundary, exact 128-bit SUM and
COUNT thresholds including extrema. Reused code-domain caches must also match
those independent oracles. Checks include coordinates, unchanged source pointer,
changed body separation and untouched initialized padding. Attachment rejects
bad population, short grants, malformed patch headers and dictionary identity
mismatch. Physical capture rejects an expanded source certificate and metadata
exhaustion while preserving fresh ownership and published answers.

### Coverage and qualification

The current extension covers constant, packed-FOR, runs, patched,
ascending/descending delta and delta-patched bodies; delta and delta-patched
exercise all four existing checkpoint modes. Sorted and frequency dictionaries
include raw and coded blocks in the same region. The code paths, parameters and
dictionary laws come from the owner, rather than a parallel fixture decoder.
Arrival-order bodies, presence/NULL masks, cross-Part dictionaries and external
predictor dependencies are outside this bounded witness.

All 15 scenarios × two ownership arms pass, including refusal and final Pool
closure. The [all-kind qualification](evidence/material-integration/all-kinds.json)
records all 120 phase observations, source/binary identities and flags on the
committed `d2a81e80` encoding core. The same build also passes the separately
owned 32,640-edit [delta-repair check](../lifecycle/delta_repair_check.cpp),
qualifying fitted residual/checkpoint edits against the newer monotone COUNT
search. Historical timings are unchanged.

Representative owner capacities still referenced, bytes:

| Body case and ownership | Old reader held | Old reader released | Only exported region 0 |
| --- | ---: | ---: | ---: |
| Packed, independent | 98,304 | 65,536 | 32,768 |
| Packed, shared original owner | 98,304 | 98,304 | 65,536 |
| Sorted dictionary, independent | 98,304 | 65,536 | 32,768 |
| Sorted dictionary, shared original owner | 94,208 | 94,208 | 61,440 |

The dictionary cases also retain 29,384 bytes of dynamic query-cache capacity.
Those decoded-value vectors are reported separately from material/catalog
metadata and do not retain Orbital backing. The selected exported Part uses
2,008 metadata bytes in this deliberately fixed-capacity inline fixture; that
is not a production minimum or the historical witness's 136-byte metadata
layout. Encoder/oracle scratch is excluded. All material/catalog metadata and
nonidle payload reach zero after final Part release; Pool closure separately
requires zero Arena charge. No RSS, throughput or latency claim follows from
these correctness and physical-lifetime observations.

## Historical packed-FOR result

The first witness, committed as `b2a9acb8` and qualified in `a91479a9`, encoded
two 4,096-position FOR planes directly in native Allocations with widths 13/9;
its replacement used width 21 and another base. Its native payload-copy count
was zero. It used the isolated `f760ca39` encoding core; it did not yet use the
broader borrowed-body seam. The source and results remain a historical control,
not a description of the current all-kind fixture.

Observed capacity of distinct physical owners still referenced, bytes:

| Ownership | Old reader held | Old reader released | Only exported region 0 remains |
| --- | ---: | ---: | ---: |
| Independent owners | 28,672 | 20,480 | 8,192 |
| Two slices of one original owner | 24,576 | 24,576 | 12,288 |

Public Pool counters corroborated 8,192 bytes becoming idle only in the
independent arm after the old Scope ended. Each exported Part used 136 metadata
bytes **in that earlier fixture**. All catalog/material metadata reached zero;
final Pool closure required zero Arena charge. Grouping saved 4 KiB while both
versions were held but retained 4 KiB more afterwards. Both effects matter.

The unchanged [qualification record](evidence/material-integration/qualification.json)
contains eight phase observations, source/binary identity and flags. The
[grouped checks](evidence/material-integration/grouped-checks.json) record the
passing 15 logical, 420 authored encoded and 93 corpus query/program comparisons
on that cut. Earlier grouped timing cohorts remain unchanged.

## What this does and does not decide

The useful result is a concrete lifetime and interpretation seam. It does not
choose independent allocation for every block, a serialized metadata layout or
an update/cleaning policy. Distinct owner entries, rounding, retained preparation
and dictionaries can overwhelm a small repair's saved copying. Grouped backing
can remain preferable even though it retains obsolete siblings.

Choosing that policy requires complete construction/publication, query and
retirement measurements, including simultaneous old/new backing and failure.
Ikea LEAD owns those lifecycle experiments and their performance claims. Engine
owns this material witness; the encoding owner owns the borrowed operations.
No public Frame API follows from the fixture.

Build `frame_execution_material_check` through the ordinary Frame spike
configuration and run it on Linux. From macOS prefix commands with `orb -m ubuntu`:

```sh
cmake --build build/clang/frame-execution --target frame_execution_material_check --parallel 1
build/clang/frame-execution/workbench/spikes/ikea-frame/execution/frame_execution_material_check
```
