# Copied predicate projections: complete native commands

The copied-projection candidate improves every complete block case on the three
tested profiles. It also leaves seven predicate-route losses above 5% in the
separate x86 filter suite, plus twelve shared-decode control losses. Those are
retained executable outcomes. The plan-independent endpoint is adopted for its
simpler lifetime contract and complete-command gains; these results establish
neither blanket parity nor a general speedup for Ikea.

The candidate keeps TuplePack's general reader at two pointers. Its separate
`predicate<Byte, Rows>` copies the selected code and borrows only the named view.
The preparation plan and intermediate reader may expire. Removing irrelevant
packet capacity `N` from this type permits the same predicate representation for
equivalent projections from different packet capacities. The observed predicate
size falls from 24 to 16 bytes on all three targets; its checked result still
uses return memory. View values, address and backing must remain stable.

[The source-qualified receipt](../evidence/interface-cohesion-v2-20261023/projection-native.json)
and [all 1,404 canonical records](../evidence/interface-cohesion-v2-20261023/projection-native-cases.csv)
retain every case and all five samples. This is a one-header comparison against
the shared strict-access/final-writer control. It is separate from original
f153-to-R4 cohesion timings and the rejected [cached-stride experiment](PREDICATE-NATIVE.md).

## What was measured

The comparison reuses the [complete block fixture and native protocol](PREDICATE-NATIVE.md#complete-commands-and-units):
64 named blocks, eight shared plans, exact 13-row tails, full and sparse activity,
three placements, three width traces and one/eight/64 queries per block. The four
routes retain the predicate, prepare it per block, use reader convenience per
query, or freshly admit placement and bind operations per block. All visit the
same blocks and issue the same queries in the same order.

Plans and fixture allocation precede timing. Fresh includes placement admission
and reader/predicate binding, while retaining the same fixture slots as the other
routes. It does not measure cold plan construction or a minimal resident block
descriptor. Retained predicate storage falls from 1,536 to 1,024 bytes; shared
plan, fixture and payload grant counters remain unchanged. This demonstrates no
Orbital allocation-class saving. Ikea has no allocator dependency in these loops.

The after arm passes 96 core/owner checks and 93 standalone public headers per
profile before timing. The candidate-only fixture destroys the actual mmap-placed
plan and protects its pages, checks all 11 valid packet shapes and exact tails,
and verifies unsigned comparisons and admission independently. All three timed
arms also pass their ordinary independent preflight. A separate
[fully instrumented access proof](../evidence/interface-cohesion-v2-20261023/predicate-projection-access.json)
qualifies poisoned inactive interior bytes for the compact Rows64 predicate.
Both arms use strict active-unit access; the explicit native common-window grant
is a separate operation permission.

The [adoption bridge](../evidence/interface-cohesion-v2-20261023/predicate-projection-adoption.json)
then verifies all 193 live Ikea header/source files against the native-qualified
after tree and passes 19 current integration checks with the merged allocator.
Five [Frame owner checks](../evidence/interface-cohesion-v2-20261023/frame-current-interface.json)
pass separately at its settled 4d0c7b32 source. Those exercise the SeriesPack and
Intervals boundary rather than the TuplePack predicate, and establish no Frame
throughput result.

Five balanced trial orders use the same argv0, working directory, arguments and
result pathname. Duplicate is a copied before ELF. Uploads occur after timing.
Blocks report CPU ns per 64-block traversal and checked derived ns/query; filters
report wall ns per 16-row window. No metrics are pooled or duplicate-corrected.

## Results and their limits

Ranges below are median paired after/before ratios, expressed as percentage
changes. Negative means less time. All 27 cases for each route are included.

| Profile | Retained | Prepare per block | Convenience per query | Fresh placement/binding |
| --- | ---: | ---: | ---: | ---: |
| NEON / Neoverse V2 | −12.9% to −3.1% | −53.1% to −8.2% | −52.0% to −4.8% | −13.2% to −2.7% |
| AVX2 / Zen 5 | −18.5% to −7.8% | −70.6% to −43.2% | −73.3% to −35.4% | −51.6% to −15.2% |
| AVX-512 / Zen 5 | −17.7% to −6.5% | −70.8% to −9.7% | −64.2% to −26.6% | −55.6% to −8.6% |

All 108 block cases improve per profile; 97 NEON and all 108 cases on each x86
profile exceed a 5% improvement. Block duplicate paired medians stay within
approximately 1.2% of before. Display precision does not imply corresponding
measurement accuracy.

The separate maintained filter suite has 360 cases per profile. No NEON case
loses 5%; nine AVX2 and ten AVX-512 cases do. Four AVX2 losses use dependent
`inline_fused` comparisons, at widths five/eight for equality and less-or-equal:
approximately 5.5–7.6%. Three AVX-512 losses use independent-eight `one_stage`
comparisons, approximately 6.7–6.9%. The other twelve losses use shared decoding,
which bypasses the prepared predicate. The receipt and CSV preserve all of them.

Four x86 shared-route cases also lose at least 5% in the identical-binary
duplicate. One full-active width-eight AVX-512 shared case has candidate and
duplicate paired ratios of approximately 1.140 and 1.138. This is a useful
execution control, not a reason to divide candidate results by duplicate results
or dismiss losses in the seven predicate routes.

These warm read comparisons do not qualify writes, cold data, owner publication
or complete Frame throughput. Ratios from this cohort cannot be multiplied by
earlier R4 ratios to declare the f153 regressions closed.

## Actual NEON caller mechanisms

[The linked audit](../evidence/interface-cohesion-v2-20261023/projection-blocks-linked-arm.json)
verifies symbol extents and actual instruction bytes in the
native ELFs. The shared checked predicate shrinks from 9,852 to 960 bytes by
removing general packet-plan/shuffle work from its one-code fallback. Selected
width-five paths retain the same comparison and physical grant:

| Path | Before instructions | Candidate instructions |
| --- | ---: | ---: |
| Full, tight | 54 | 53 |
| Full, strided | 140 | 99 |
| Sparse activity | 161 | 123 |
| Exact 13-row tail | 170 | 126 |

Preparation per block loses an overlapping aggregate reconstruction. The
convenience route previously stored the three-byte code with two stores, then
loaded a wider eight-byte aggregate across them and copied it into the consumed
predicate on every query. The candidate writes the code directly into its
consumed handle. Removing this partial-store/wider-load dependency supplies a
concrete store-forwarding explanation alongside the smaller fallback; no
isolated causal timing measurement assigns the observed gains between them.

The retained query loop grows from 26 to 28 instructions despite improving in
the measured cases. Fresh keeps its 25-instruction query loop and real per-block
admission. These negatives prevent treating a selected instruction count or
smaller handle as a general timing model.

## Actual x86 losses

[The linked filter audit](../evidence/interface-cohesion-v2-20261023/projection-linked-filter.json)
checks 39 complete selected functions per profile against ELF bytes and symbol
extents, plus addressed constants and initial objects. In the four slower AVX2
dependent inline paths, equality falls from 43 to 39 instructions and
less-or-equal from 45 to 41. The activity reloads and a back-jump/cursor store
disappear; all five conditions remain. The signed-comparison mask changes from
an addressed 16-byte constant to a hoisted scalar broadcast with the same value.

The three slower AVX-512 stage paths fall from 36 to 35 instructions and from
150 to 137 executed instruction bytes. They retain four predicate guards and
one encoded comparison. Mask extraction changes from `kmovw` to `kmovd`; one
register copy disappears. Their candidate mask extraction crosses a 64-byte
boundary, but a width-eight less-or-equal control has the same boundary pattern
and improves. Placement geometry therefore supplies no causal explanation or
basis for a blanket alignment change.

Shared native-decode and joined-comparison functions keep their instruction
skeletons and addressed data. On AVX-512 those bodies are raw-byte exact despite
moving. The common entry and actual completion are exact at the same addresses;
the common indirect timer changes only RIP references to equal initial payloads.
This constrains an added-work explanation without establishing equal execution
conditions or dismissing the measured losses.

The actual linked AVX-512 less-or-equal independent-eight path is **303 to 298**
instructions. The earlier unlinked configured object was **303 to 322**. Both
source-qualified observations remain in their receipts; the unlinked allocation
must not be substituted for the native caller's actual allocation.

## How to use the result

Keep a prepared predicate around repeated one-code work. It owns the projection,
so a predicate-only owner can release its preparation plan while retaining the
named view. For a different placement, bind another operation after admitting
that view; replacing a view's value underneath a retained binding violates its
contract. The fresh route measures this useful admission/binding boundary.

General TuplePack readers still borrow shared plans for packet projection and
native transfer. StreamPack can reuse its existing typed same-layout access;
Directory's direct endpoint/comparator path needs no plan indirection. Frame's
operation-major width catalogue supplies a concrete starting point for shared
SeriesPack controls. These different operation grains support common lifecycle
roles, not a universal handle or a requirement to retain every control per block.

The [execution survey](../reusable-execution.md) recommends a direct
validated-layout/code-index predicate binder and shared SeriesPack operation
columns as distinct next candidates. They are not implemented or measured by
this comparison. In particular, the block fixture excludes cold plan preparation
and cannot establish that direct binding's break-even point.

## Reproduction

The source-pinned [controller](projection-native.py) and
[result reader](predicate-results.py) retain protocol and qualification checks.
The receipt identifies both complete worker artifacts, source archives, source
maps, binaries and raw trials. Recover the jobs with the existing worker helper,
then recompute all 2,052 original metrics:

```sh
python3 workbench/spikes/ikea-composition/handle-boundaries/predicate-results.py \
  neon=NEON_RESULTS zen=ZEN_RESULTS --output FRESH_OUTPUT
```

The canonical CSV keeps filter wall ns/window and block CPU ns/query. The other
two validated block metrics derive from the same traversal and remain recoverable
from the worker output; they are not additional timing observations.
