# SeriesPack initial-scope review, 11 September 2026

This working assessment reconciles the original implementation sketch/specification with the current component. It is not a promotion decision or performance sign-off. The physical task owns the fresh hardware reconciliation.

| Initial obligation | Current implementation and evidence | Limit |
| --- | --- | --- |
| Headless unsigned arrays, widths 1..64, empty/partial/large lengths | layout.h, view.h, reference.md; 206 descriptions, checked capacity/alignment/occupied-range admission; current layout/view checks pass | Semantic domains above unsigned values and serialization envelopes belong to later compositions |
| Curated geometry plus independent placement | Compact, bulk, ARM and head-split recipes resolve actual versioned descriptions; payload/head spans retain independent strides; static/dynamic views share bytes | Recipe names are not fastest-layout claims; no runtime data-dependent format choice |
| Ordinary calls with reusable binding | Checked get/set/encode/decode/selected write; bound reader/encoder and static trusted point operations | Bound calls trust future call bounds, values, capacities and lifetime; selected write has no separate bound writer object |
| Mutation effects without engine context in kernels | Caller-owned append-only physical byte-span coverage, exact foreign-byte exclusions and pre-mutation rejection; static writes also support effects | Physical coverage is not a semantic delta, snapshot, transaction publication or summary-maintenance protocol |
| Shared native composition and introspection | Body/tail/head expressions, own-domain joins, named source dependencies, explicit ISA executors, incoming original-coordinate masks, owned values and symbolic Ops over the authored function | Named-view object lifetime is additional to byte ownership; general recording/optimizer framework is intentionally absent |
| Useful grain and reuse | Tile, dense and grouped execution; deferred sum carrier where supplied; original-coordinate and independent-leaf checks | Known geometry/grain must be admitted. No turnkey general native-range driver or portable CPS ABI is claimed |
| Usable contribution surface | Component links directly as ikea::seriespack without a spike selection; all three current examples run; logical/composition tests pass | This local ARM dev result is not all-profile native or performance coverage |

The native query and two-source examples passed with the expected results, including empty and out-of-domain query constants. Temporary describe calls are rejected at compile time; expressions still need rebuilding after moving their named source objects. Inspection of native tile/dense/grouped readers shows each leaf resolves its actual source and placement. The existing tests exercise independent/mixed leaves rather than inferring common ownership from equal types.

The original sketch explicitly deferred the general CPS ABI and many-recipe economics, Engine schema/discovery and semantic transforms, and Loom suspension/publication integration. These stay visible as design work; inventing them now would enlarge the delivery scope. Current materializing operations accept arbitrary ranges; native fragments leave traversal, skipping, clipping and retained state to the caller's driver. Workbench runtime-driver candidates remain experiments.

The current performance conclusion cannot be assembled by splicing old baseline ratios and isolated candidate wins. The last complete seven-profile checkpoint predates retained changes. The repaired 1 GiB dependent pilot exists on V2/GNR and establishes broad generated access/footprint, not individual cache misses. The physical task is preparing the remaining current-source comparisons and will distinguish significant residual gaps, usable tradeoffs and unmeasured cells.

Local commands and binary/source identities are in checks.json. No production source was changed by this review. The point-strategy duplication review is a separate ignored candidate exploration, not an installed optimization.
