# Frame control reuse and execution grain

This source/research survey is separate from the isolated lifecycle port and its
native qualification. It considers the settled lifecycle source and later live
Frame readers without changing either. These are candidate mechanisms, not a
chosen interface or new performance result.

The strongest existing witness is the [shared plane catalogue](../../../../ikea-frame/encoding/kernels.h).
It separates reusable width-specialized code from each block's storage and
logical count. The [owner migration](../../../../ikea-frame/encoding/README.md#owner-operations-on-the-shared-catalogue)
already found no measurable local slowdown across 117 configurations while
removing 232 B of binding objects per block by `sizeof` (about 308 B with the
observed vector capacities). The
[point integration](../../../../ikea-frame/point/INTEGRATION.md) still found
large gaps to native arrays after the prefix improvement. A small handle alone
does not remove prediction dependencies, run search or kind dispatch.

## What the callers actually reuse

| Caller | Stable control | Changes at the next unit of work | Existing grain |
| --- | --- | --- | --- |
| `encoding/kernels.h:51-73`, `material.h:505-529` | One pointer column per operation and width; compile-time Local packing law and output lane | Body address, width, row count, requested edge/tail | A point, exact-tail range, or a whole reduction |
| `lifecycle/phase_material.cpp:149-185` | Query threshold and dictionary helper state | Each selected block's descriptor/body/grant; validity determines empty/full/mixed path | One 256-row block inside a 4096-row owner group |
| `encoding/fused.h:310-342` | Narrow output lane and chosen range function for this block | Plane pointer, selected 64-row word, decoded scratch contents | One whole block or selected 64-row words, then several reductions |
| `encoding/material.h:1015-1049,1234-1320` | Decoded dictionary or translated threshold while the dictionary edition matches | Dictionary edition, threshold, block code plane | One edition across ascending selected blocks |
| `point/compact_read.h:35-108` | Shared width catalogue; one stationary image | Random point's block, kind, exception rank, checkpoint and residual prefix | One original-coordinate point |
| `encoding/packing.cpp:21-45`, `material.cpp:224-324,566` | The specialized packing operation for a width | Destination address, logical count, input and exact byte grant | One plane: residuals, short run/exception values, checkpoints or dictionary |

The catalogue is a structure of operation-specific pointer arrays, with an
8-byte index stride. The inline `plane_kernels` bundle is only a view; unused
operation loads can disappear. Replacing it with a mandatory large operation
bundle would need a better emitted-code result. The actual width transition
frequencies have not been counted in this survey; neighbouring blocks are not
assumed to have equal or adjacent widths.

## Useful candidate mechanisms

1. **Reusable physical control, with storage supplied per call.** A supported
   Ikea equivalent of the existing catalogue could let a consumer select a
   width/layout/output-lane operation and call it on successive admitted
   buffers. Frame already has the dynamic count and body offset in its parent
   metadata. Its useful hot path need not construct or retain a complete bound
   reader for every block. Selection of a different width should be another
   table lookup, not rebuilding an unrelated general plan.

   The typed wrappers in `encoding/kernels.cpp:14-22` already bind an admitted
   compile-time dense source and immediately call one operation. Such transient
   objects can disappear after inlining; a new handle must not be credited with
   removing allocations or retained state that this path never had. Its useful
   contribution may instead be exposing the shared runtime control without a
   Frame-owned width matrix. The finished caller disassembly must decide this.

2. **Keep a compact borrowed handle where the caller actually needs one.**
   An operations-table pointer plus a resource pointer is two words when the
   resource owns the count and placement metadata. A dense resource represented
   only by its byte pointer also needs a logical count for the ordinary checked
   API: pointer, operations and count are three words. Removing that count
   would weaken the checked contract; borrowing a stable named view can avoid
   duplicating it. The choice should remain explicit so Frame does not have to
   retain extra metadata merely to satisfy a handle-size target.

3. **Select the operation at a useful grain.** Current fused reads choose a
   narrow range function once before their selected-word loop. SUM and COUNT
   sharing a decoded plane can pay less than two separately dispatched passes.
   A native-packet fused leaf is a candidate for avoiding that scratch, provided
   it retains the real full-window and exact-tail grants. Per-row erasure or
   repeated checked plan construction inside a packet loop would move in the
   opposite direction.

4. **Reuse content-derived work under its real identity.** Dictionary threshold
   translation and decoded entries have a different lifetime from a packing
   width's control. Existing `code_sum`, `code_count` and the cache's last-edition
   fast path already make this distinction. A buffer rebind must not retain a
   dictionary translation when its edition changes, or retain the old body
   pointer after a new owner publication.

5. **Consider reusable writer control separately from the write grant.** Frame's
   pack adapter currently dispatches across 64 compile-time widths, attaches a
   destination, binds/erases a writer and allocates its effect journal for every
   plane. A shared initialize operation selected by width could remove repeated
   control selection, and caller scratch could reuse journal storage. Admission
   of the new buffer/count, input fit and available effects still belongs to
   each operation. This survey has not attributed complete encoding cost to
   those steps; policy search, emitted candidates and allocation remain
   competing costs. The isolated port's existing NEON object comparison gives
   a concrete code-footprint witness: the all-width dispatcher is about 30 KB,
   with successive width comparisons and repeated setup. Its 309 static calls
   span the whole function, not one invocation. A control lookup should be
   compared under clustered and unpredictable widths rather than credited with
   the aggregate static code reduction as if every instruction ran per block.

## Counterexamples that constrain generalisation

- **Short planes are not full blocks.** Run values, exception values,
  dictionaries and packed checkpoints require exact-tail operations.
  `kernels.h:9-15,28-30` distinguishes those from the full-window native SUM.
  Reusing the same width does not grant another 16-row window.
- **Empty work must remain empty.** Coarse masks and an empty fine selection
  return before descriptor/body/dictionary access (`selected.h:633-679`).
  Eagerly rebinding or preparing every block would break both useful work and
  protected-access laws.
- **Fusion loses when one answer is already cheap.** The
  [fused study](../../../../ikea-frame/encoding/README.md#fused-aggregates)
  found no gain on UserID where bounds decide COUNT, and its decoded gather
  could lose to arrival SUM's sequential literals plus COUNT's truth bits.
  Its gains and negative cases were local indicative measurements, not native
  fleet qualification.
- **A larger cache is not free reuse.** The
  [dictionary cache study](../../../../ikea-frame/encoding/README.md#dictionary-caches-across-calls)
  found random point reads 1.4–1.7 times slower through the cache and sparse
  SUM losses. Retaining decoded values can exceed the column's bytes; repeated
  random edition lookup and memory residence matter more than avoiding decode.
- **Binding size is only one cost.** The
  [complete program study](../../../../ikea-frame/lifecycle/PROGRAM-COSTS.md)
  removed most repair-plan allocations but improved pressure updates by only
  about 5%. Native arrays, closed forms, prefix reconstruction and unpaid
  folding remain relevant controls. Do not infer a complete owner speed gain
  from a smaller handle or fewer allocator calls.
- **Width reuse does not erase indirect-target pressure.** A shared table saves
  duplicated metadata but still selects among specialized code bodies. If a
  workload changes width unpredictably, instruction residence and indirect
  branch prediction can matter. Reordering blocks by width is not a free fix:
  current scans preserve original coordinates and benefit from ascending
  dictionary editions. No transition or branch-miss measurements are claimed
  here.

The next comparison should preserve the actual owner algorithms and compare
the existing private catalogue, any supported shared-control form and ordinary
binding at the same point/range/block grain. Count retained state separately
from transient objects, and inspect the completed caller's indirect calls,
constant loads, spills and instruction footprint. New width/lane/plan reuse
needs evidence from those callers before it becomes a common interface.

The [runtime-control proposal](runtime-control.md) develops the smallest shared
SeriesPack control surface suggested by these callers, with explicit binding,
grant and source-identity costs. It remains a proposal for the reopened study.
