# Representation conversion: local qualification

Historical findings; the prototype and its launchers were retired during the
October 21 curation. The text below describes the captured experiment, including
its original API and qualifications. Current interfaces belong in
[Orbital material](../../../../orbital/docs/material.md);
[current integration checks](../../../benchmarks/retired-drivers.md#current-integration-checks)
retain the production consequences. [Recovery](README.md) pins the original tree.

9 October 2026. This bounded fixture composes existing SeriesPack and TuplePack
with actual Orbital allocation. No production Ikea or Orbital interface changed.
It supplies a completed, immutable descriptor closure for the separate
selected-access caller; it does not implement production Objects, Views or
publication.

## Correctness and ownership

Pinned Linux Clang 21.1.8 native and ASan/UBSan builds passed the same 560
schema/count cases: 1,129 constructions, 2,246 conversions, 2,378,554 logical
field checks and 37,737,544 independent wire-bit checks. Coverage includes empty
and single records, eight-row tile boundaries and ragged tails, all supported
field widths, wide Tuple fragments including the upper packet, and both
directions and roundtrips. Series planes have separate offsets/strides in one
Part per column, including initialized final-tile slack.

Eleven rejected construction cases cover invalid late values, allocation
refusal, actual partial construction and incomplete completion. Fourteen
malformed descriptions are rejected. The source remains readable when target
construction fails. Copied completed owners retain both interpretation and
encoded backing after ordinary owners and input metadata disappear. Final Pool
close drains all charge and storage; the check records 7,226 allocation requests.

The runner audited all 22 project translation units reached by the check for
ASan/UBSan and always-on use-after-return instrumentation. Native and sanitizer
captures have the same input-manifest SHA-256:
`1372bbce8b42c46f91996e1989fbb21ee5ee8b65876769be176a70781424c886`.
Base checkout was `00e861a69c87ec8a0b51bbc6e093de50b7155799`; the new fixture
remains an unstaged spike. Complete commands, input/binary identities and output
are in ignored `build/ikea-representation-conversion/{native,sanitize}/receipt/`.

## Bounded cost diagnostic

The six-field schema is `{1,7,9,20,31,64}`. Five trials of ten conversions per
case follow one untimed warm-up. Each clock interval includes source decoding,
target allocation, reencoding and immutable capture. Oracle readback, release
and Pool settlement are outside it. These are local sequential repeated-source
diagnostics on OrbStack AArch64, not target-hardware or database results.

| Records | Direction | Median trial mean CPU / wall | Target allocations | Target body / padding | Staging estimate | Retained descriptor estimate |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1,025 | Columns → rows | 64.43 / 65.23 µs | 1 | 17,425 / 64 B | 73,800 B | 496 B |
| 1,025 | Rows → columns | 65.75 / 66.00 µs | 6 | 36,224 / 384 B | 90,312 B | 1,776 B |
| 4,097 | Columns → rows | 259.13 / 259.54 µs | 1 | 69,649 / 64 B | 294,984 B | 496 B |
| 4,097 | Rows → columns | 267.89 / 268.28 µs | 6 | 140,672 / 384 B | 360,648 B | 1,776 B |

Each conversion decodes and constructs `records × 6` logical values. Row
construction binds once but invokes the constructor once per record; column
construction binds and invokes once per column. At 4,097 rows the corresponding
issued coverage is 69,649 B in 4,097 spans and 67,716 B in 6,669 spans. Coverage
counts issued stores, not memory-bus traffic. Body sentinel initialization is
included in the timing and counted separately. The intentionally separated
Series plane gaps explain its larger body here; this is not a compact-layout
comparison or an inherent SeriesPack size penalty.

Source and target coexist through completion. Last simultaneous Arena charges
were 109,760 / 139,088 B at 1,025 rows and 239,864 / 359,632 B at 4,097 rows
(columns→rows / rows→columns). Idle inventory and allocator metadata remain
charged after closure release. Every diagnostic case reaches zero charged,
mapped, reserved and metadata bytes after explicit Pool close. Close/poll/remove
wall time ranged from 47.6 to 65.2 µs in this run; Arena/helper teardown is
excluded and thread CPU omits helper CPU.

## What this establishes

Existing Ikea storage placement, constructors, serialization and borrowed native
bindings suffice for this representation change without production adapters.
Wide logical fields require a fixture-owned mapping to Tuple's byte-contained
codes. Series plane placement and the logical field schema also belong to the
descriptor's interpretation closure. Part identifiers are descriptor-local:
changing the Part directory need not change the logical Object/version label.

The converter deliberately stages fixed eight-u64 records, builds a private
target and only exposes a completed immutable owner. A failed target cannot be
captured as complete. A Hold keeps physical bytes alive; fixture encapsulation
supplies immutability. Selection, authority, asynchronous readiness, cache
replacement and publication remain the selected-access work's responsibilities.

Staging/descriptor estimates exclude native stack/plans, malloc overhead and
shared-pointer control blocks. They are not RSS or a measured total peak.
No optimized conversion, request-tail, cache-residency, durability, distributed
semantics or production container claim follows from this diagnostic.
