# Ikea Frame: consolidated proposal for the first landing

25 October 2026, for Ashton. Joint proposal by the stage side (Claude) and Ikea LEAD
(Codex). Evidence is in
[FINDINGS.md](FINDINGS.md), decisions and open items in
[CHECKLIST.md](CHECKLIST.md), and the history and full argument in the
[design history](DESIGN.md).

The read/build core is now implemented in Ikea; combined native qualification
is recorded in [GRADUATION.md](GRADUATION.md). The [consumer reference](../../../ikea/docs/frame/reference.md)
owns actual source names and public contracts. The map below is the accepted
extraction plan, retained with its rationale.

Engine research will settle much of Frame's eventual interface, and that
research needs a Frame to work on. So the first landing is a foundation:
**strong** where the evidence is good, **steerable** where we lack
confidence, and **extensible** where we know extensions are coming.

## What lands in Ikea

A column of non-null u64 values. Initial defaults use 65,536-row binding units,
16-block groups and 256-row fitting blocks. Child streams have explicit row
domains and placement; an encoding group, image section, allocation and Orbital
Part need not coincide. The supported recipes compose these optional stages:

- **`intervals`**: checkpoints every G rows (16 or 64), stored as a framed
  series of their own, and the rows between them as sizes (deltas) or
  relative ends (offsets from the checkpoint).
- **`bound`**: a per-block window [lo, lo + 2^width) with outliers in
  per-group exception areas; reserved capacity is an explicit fit/layout choice.
- **`pack`**: SeriesPack's packed fields, through its compiled-once
  operations.

Reads: get, read, select, filter, count, sum (whole and selected), and SUM,
COUNT, MIN and MAX together in one pass, with checked wrappers reconciled at
extraction. Construction, fitting and emission land; **point mutation and checked
private preparation do not**. `prepare_group`, its draft/copy/bind/rebuild machinery
and mutation scratch stay in research, per Ashton's 25 October scope decision.
Callers can construct replacement images from values and publish through their
owner; that is not a competitive point-write API. Read binding and construction
must not pay for the deferred mutation machinery.

### Strong (we are confident)

| Area | What is guaranteed | Evidence |
| --- | --- | --- |
| Meaning | Supported cells are exact; laws are optional fast rewrites. Unsupported images refuse | Original-value oracles, admitted trees and both interval forms |
| Composition | Runtime block-grain laws; per-part fusion and reusable selections | Fusion beats retained masks in every measured seam cell |
| Leaf | SeriesPack compiles its kernels once; Frame compiles none | Runtime leaf at timing parity, build bill halved |
| Skipping | Clustered predicates settle whole blocks from descriptors | 1.9–8.5× faster than front A's native pair |
| Interpretation and binding | Versioned meaning and explicit child domains; separate placement, contents and authority. Admit complete demand before output; retain immutable interpretation/backing | Canonical bytes, malformed/guarded images, sparse dependencies, stale-basis/teardown/conversion checks; unchanged decode can still hide a wrong SUM |

### Steerable (Engine chooses; defaults may change)

- The recipe per column: which stages, interval form and spacing, SeriesPack
  preset, bound's penalty, cap and slack. Relative ends trade bytes for SUM
  and point speed (SUM 1.1–8× faster, but UserID 29 → 48 bits per value), so
  no single default is right.
- Whether to use Frame at all. Where no recipe repays (URLHash), Engine keeps
  a native column; `pack` alone is not that choice.
- Execution plans by ISA and workload: the descriptor COUNT pass helps Zen 5;
  Graviton 4 is mixed and CounterID loses. Retaining masks can repay across
  several consumers even when one fused query is faster.
- Costs: return fit facts and capabilities; distinguish measured, estimated
  and unknown costs. Engine can probe alternatives; no complete model is required.
- Placement/preparation: Engine supplies budgets and protection, may retain
  masks/plans, and receives dependency closure. Policy/defaults stay separate
  from stored meaning. Prototype geometry/parser limits are not permanent
  whole-column restrictions.

### Extensible (known extensions)

- Stage/child domains and shared state, with described/refused RLE, head,
  TuplePack and presence/rank shapes.
- New operations with optional laws, and bind-time plans for critical paths.
- Separate change capabilities, interpretation versions and evidence. The initial
  Frame mutation capability is unavailable. Future edits must distinguish logical
  contributions, issued bytes and evidence invalidation, with explicit access and
  publication boundaries; no afterimage or in-place protocol is fixed by landing.

## Source, namespace and class map

One compiled `ikea::frame` target contains the initial implementation. Header
paths below are relative to `ikea/include/ikea/frame/`; TUs to `ikea/src/frame/`.
The [consumer reference](../../../ikea/docs/frame/reference.md#source-and-extension-map)
owns the full map and contracts.

| Namespace / header | Main types or entries | Compiled TUs and responsibility |
| --- | --- | --- |
| `ikea::frame`, `types.h`, `description.h` | geometry, `summary`, `error`; `description`, `node`, `edge` | `description.cpp`: versioned meaning, child domains/sharing and executable-shape admission |
| `ikea::frame`, `image.h` | `stream_manifest`, `image_view`, `validated_image`; `required_sections` | `manifest.cpp`: placement/closure; `group_image.cpp`: parent/leaf proof; `image.cpp`: granted attachment and cross-stream proof |
| `ikea::frame`, `reader.h` | `reader`, `bind_reader` | `reader.cpp`: complete-request admission and selection adaptation; retains execution/proof, borrows payload |
| `ikea::frame`, `build.h` | `fitting_policy`, `stream_images`, `built_unit`, `build` | `build.cpp`: admitted stages, fit/emit one unit and fit census |
| `ikea::frame::author`, `author/stage.h` | `stage`, `fit_input`, `census`, `point_leaf` | `stage.cpp`: stream traversal and defined decode fallbacks |
| `ikea::frame::detail`, `detail/stages.h` | factories and private pack/bound/interval stages | `stages/pack.cpp`, `bound.cpp`, `intervals.cpp`: each stage's fitting, section law and algebra; leaf mechanisms delegated to SeriesPack/Intervals |
| `ikea::frame::detail`, `detail/execution.h` | internal read plan | `execution.cpp`: bind-time point entries |

`ikea/frame.h` is the ordinary umbrella. Public headers expose contracts rather
than width-specific kernels. A new stage adds its description/image law, hooks
and conformance together; an arbitrary new description is not automatically
executable. There is no binary plugin ABI and no initial `change.h`/`change.cpp`
or public `prepare_group`. The actual Orbital owner example lives separately
from the library and links Orbital only in that example.

Three seams govern the split:

- **Interpretation → placed image → execution.** Attachment checks metadata and
  placement without consuming ungranted payload. Validation establishes the
  stage/child content premises; binding prepares operations against that immutable
  basis. Image byte validity, availability and query selection remain distinct.
- **Stage → children.** Each stage's fitting, section emission/validation and
  algebra live together. Common drivers do admission/traversal, not a second copy
  of each encoding. Adding a stage supplies those hooks and capability tests;
  its versioned description and image law are added together. Adding an operation
  supplies its semantics and optional laws. A defined decode
  fallback can fill a supported cell; an unknown shape still refuses.
- **Frame → existing modules and owners.** SeriesPack owns packing geometry,
  exact reductions and native kernels; Intervals owns reconstruction/search;
  Ikea selections, description framing and journals are reused. Frame owns the
  cross-child proof and compensation. Caller/Orbital retains backing and publishes
  replacements; Loom acquires/yields around declared dependencies. Neither becomes
  a dependency of the Frame library.

This reorganises the spike's shared implementation rather than creating another:
`owner/description.*`, `manifest.*` and `group_image.*` feed description/image;
the read portions of `binding.*` and `stream_binding.*` converge on one checked
reader. Private preparation and mutable-driver code remain in the spike.
`proto/frame.h`, `stages.h`, `runtime_width.h` and `trees.h` split into the author
contract, private drivers and stage TUs. The typed `widths_*.cpp` remain comparison
controls; Frame adds no per-width kernel instantiations. Ordinary examples live
under `ikea/examples/frame/`. The actual Orbital `Control`/`interpretation_owner`/
`Physical` material and history checks become a separate integration fixture,
linking Frame and Orbital for retained interpretation/readers and externally built
replacements. Mutation-specific histories remain research; native/system controls
stay in Workbench.

## Measured weaknesses and initial limits

These are the qualified spike results, before the queued reuse changes:

| Work | Current evidence | Implication |
| --- | --- | --- |
| Independent points | 2.6–6.4× slower than native gather after an eviction sweep | Extra dependent acquisition remains exposed; no exact DRAM-line count or irreducible floor is proved |
| Dispersed predicate → SUM | Up to 1.6× slower on Zen 5, 2.9–3.4× on Graviton 4 | Selected fraction is insufficient; touched-block topology matters |
| COUNT | OS/EventTime lose 1.5× on Zen 5 | Descriptor pruning is valuable, not universal |
| Wide SUM/dispersed work | Roughly 2–4× slower than typed native in the local six-cell panel | Exact-wide leaf improvements target part of the gap; recheck changed cells. Wins over DuckDB/Polars include larger query/result boundaries |

Memory is conditional too: URLHash's G16 image is 528,864 B against 524,288 B
native payload, OS 74,721 B against 65,536 B. Neither account is full retained heap.
The sizes form currently decodes SUM; the module work below targets that route.

The initial domain is non-null u64. Fine-row read authority, retained asynchronous
preparation and general in-place mutation are not established. Transaction/lock
ownership and recovery remain with their existing modules.

[Scorecard](controls/SCORECARD.md) and [owner evidence](owner/EVIDENCE.md)
record source/operation boundaries. Native comparisons cover Zen 5 AVX-512 and
Graviton 4 NEON; newer owner conformance/sanitizers and actual-system cells are
local AArch64. Repeated execution and a 2 GiB eviction sweep prove no cache tier.
Module changes inherit no old timing or qualification automatically.

## Resolve before landing

1. **Submodule reuse** ([REUSE-AUDIT.md](REUSE-AUDIT.md)):
   - Intervals' searches are routed through `ikea::intervals`.
   - Intervals gains a source over decoded wide fields, so delta
     reconstruction goes through it too.
   - SeriesPack gains an exact weighted SUM (periods 16 and 64) and an
     exact wide SUM, replacing Frame's masked law and its decode above
     55-bit widths.
   - `bitset256` gains its iteration and intersection helpers.
   - Physical H8/H16 planes use SeriesPack heads. A fixed logical split is a
     different child mapping and must not be aliased to that packing option.
   - SeriesPack supplies runtime geometry/required-plane extents, replacing the
     owner validator's duplicate tile-size formula.
2. **Extraction**: one implementation behind Ikea's lifecycle; the prototype
   driver stays internal. Frame owns canonical composition/image validation and
   checked operations. Attachment, validation and binding keep Ikea's existing
   meanings; selections reuse its types with explicit block/row coordinates.
   Orbital material/history stays an integration example and conformance test,
   keeping owner retention/publication outside Ikea. Preserve old-reader meaning,
   child domains and conversion equivalence; no final Engine interface is required.
3. **Explicit capabilities and defaults**: which cells have laws, which
   operations each stage supports, what refuses. Mutation/private preparation is
   excluded; retaining its design obligations does not add an initial write API.
4. **Qualification** of the combined implementation on the workers, against
   front A's controls.

## Stays in research

- **Writes and checked private preparation.** The current `prepare_group` costs
  21–26 µs for one edit and 57–71 µs for 64 edits in the local cohort, excluding
  publication/disposal. It is not the initial write design. Revisit the architecture
  and complete-operation cost against a strong adaptive native owner later;
  preserve the existing correctness counterexamples and receipts. Scratch/proof
  reuse and right-sizing are research questions, not a promise that tuning this
  implementation will make it competitive. Both current paths request 128 KiB of
  journal scratch, whose cost is not isolated. No new write study blocks landing.
- Engine's choice of recipe judged by regret (B11), with a real native
  candidate and a workload reversal: the first Engine-side work on the core.
- Points: entries stored with their bodies.
- Executable RLE, TuplePack-shared records, logical split, map, routing and
  domain stages; Frame image placement/filtering for SeriesPack head planes.
- Scans across groups through one prepared operation (B10).
- Bound's MAX law is built and checked but not timed, so it lands only as an
  internal law.
