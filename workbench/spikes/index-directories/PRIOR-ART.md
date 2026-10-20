# Implementation prior art

Source review, 20 October 2026. The applications to SixDB below are proposals
for implementation experiments. Published performance figures are not SixDB
results. This survey concerns the supplied five-layer directory, its storage
and maintenance.

## Calico

Reviewed sibling source at commit
`ac83c82b9a0c8d76bea92829e471ebc0d98b1978`; no fresh build or timings.
Calico inspired the brief, but each mechanism needs assessment in the current
Ikea/Orbital/Loom composition.

| Mechanism and source | Strength | Cost or weakness to investigate in SixDB |
| --- | --- | --- |
| Prefix lookup and batching: [navigate.h](../../../../calico/arbor/include/arbor/navigate.h), [QHash report](../../../../calico/qhash/REPORT.md) | Derivable targets permit direct entry; shared-prefix batches amortize preparation and resolution. | The report also records poor isolated short-key cases and benefits disappearing against a Ring-equipped flat map. SixDB segment prefixes are at most four bytes; measure actual Ikea lookup. Calico's deepest-resident fallback belongs to its own representation rules and is not a SixDB navigation requirement. |
| Decision-site metadata: [segmeta.h](../../../../calico/arbor/include/arbor/segmeta.h) | Compact class/version metadata and shared schema can classify a segment before loading its body. | Hoisting creates interpretation dependencies and mutation coupling. Compare the six-locator dirent's direct selected-Part access and account for all schema/Control state. |
| Independent containers: [payload.h](../../../../calico/arbor/include/arbor/payload.h), [container design](../../../../calico/design/CONTAINERS.md) | Sibling containers can move independently; occupied-rank columns support compact scans. | Rank insertion can shift payload despite stable labels. The code documents a sibling-namespace collision and explicit reservation fix: arithmetic naming needs validated namespace allocation. |
| Physical continuation: [store.h](../../../../calico/arbor/include/arbor/store.h), [store_xmem.h](../../../../calico/arbor/include/arbor/store_xmem.h) | Reservation lengths and end markers allow sequential physical traversal without repeating a location lookup. | That store's mixed-depth physical preorder is a Calico storage choice. It supplies no same-layer directory cursor for SixDB. Keep layer explicit and compare the cost of per-layer continuation metadata. |
| Mutation and reuse: [store_xmem.h](../../../../calico/arbor/include/arbor/store_xmem.h) | Staged structural edits and generation-qualified references make relocation consequences explicit. | `commit_partition_repack_` constructs a future image from the whole store. `commit_watermark` invalidates memoized views coarsely. Test whether an interior SixDB edit grows with unrelated population, and measure reference repair and retained readers. |
| Summary fan-in: [fanin.h](../../../../calico/arbor/include/arbor/fanin.h), [source_arbor.h](../../../../calico/engine/include/engine/source_arbor.h) | Distinct upper-prefix work and ancestor claims avoid rescanning every input record. | Materialization and maintenance still cost work. Preserve the supplied overlapping windows when deriving affected summary entries; reuse current aggregate findings rather than assume less logical work means less CPU. |
| Reconstruction: [checkpoint export/open](../../../../calico/arbor/include/arbor/store_xmem.h), [Runtime checkpoint caller](../../../../calico/calico/src/physical_checkpoint.cpp) | A canonical live-entry roster reconstructs process-local lookup/extent indexes over a frozen image; host pointers and cached handles are excluded. | This is an implemented frozen checkpoint path, not general mutable recovery. Use changed-address reconstruction to test SixDB's locator encoding; durable installation still needs its actual Orbital protocol. |

The useful carry-forward is direct prefix addressing, batched resolution,
metadata at the access decision, selective container reads and explicit mutation
consequences. The practical cautions are resolver overhead, broad repair and
invalidation, payload movement hidden by stable labels, and process-local state
mistaken for portable storage. The existing
[SixDB remapping findings](../trie-remapping/FINDINGS.md) independently expose
the label/payload distinction, but their small unique-key fixtures and relaxed
ordering alternatives do not establish this brief's duplicate or full-order
behavior.

## Other implementation mechanisms

| Primary source | Mechanism | Experiment in this spike |
| --- | --- | --- |
| Leis, Kemper, Neumann, **ART**, ICDE 2013, section III: [paper](https://www.db.in.tum.de/~leis/papers/ART.pdf) | Adaptive dense/sparse layouts, including byte-index-to-packed-child indirection. | Compare presence metadata and packed addressing where directory pages are sparse. Charge the extra decode and transitions. Keep the supplied segments and layer selection fixed. |
| Wu, Ni, Jiang, **Wormhole**, EuroSys 2019, sections 2–3: [paper](https://wuxb45.github.io/papers/wormhole.pdf) | Hash-assisted prefix search combined with ordered anchors and continuation. | Inspect missing-key seek and prefix-preparation costs. Its anchor ordering requires care with equal logical keys; import no duplicate or cross-layer semantics. |
| Mao, Kohler, Morris, **Masstree**, EuroSys 2012, section 4.6.2: [paper](https://pdos.csail.mit.edu/papers/masstree:eurosys12.pdf) | A small permutation publishes order over separately placed slots. | Compare moving compact directory order metadata with moving 64-byte entries. Its fifteen-key permutation is an example of the mechanism, not a capacity or publication rule for SixDB. |
| Levandoski, Lomet, Sengupta, **Bw-tree**, ICDE 2013, sections II.B–C: [paper](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/bw-tree-icde2013-final.pdf) | Location indirection permits physical relocation without rewriting every referencing node; deltas are periodically consolidated. | Account for locator repair, dependent reads and consolidation versus copied directory pages. SixDB already has prefix-defined segments and Orbital versions; its snapshot/install protocol remains the integration target. |
| Bender, Demaine, Farach-Colton, **Cache-Oblivious B-Trees**, section 2.3: [paper](https://erikdemaine.org/papers/CacheObliviousBTrees_SICOMP/paper.pdf) | Packed-memory arrays use density thresholds at several scales to select redistribution windows, including lower thresholds for deletion. | Implement a bounded hierarchical redistribution control alongside simple shifts/tail slack. Charge changed segment prefixes, summaries and bytes, and do not transfer its amortized bounds to a modified SixDB policy. |
| Bender, Hu, **An Adaptive Packed-Memory Array**, 2007, section 2: [paper](https://www3.cs.stonybrook.edu/~bender/newpub/BenderHu07-TODS.pdf) | Recent insertion markers guide uneven redistribution, leaving more room near predicted inserts. | Compare local/frontier padding with measured skew and moving hotspots. Predictor storage and repair are costs. Unordered duplicate ties also permit boundary-record moves outside the paper's prescribed-rank model. |

Boundlist's [production qualification](../boundlist/GRADUATION.md) and
[module guide](../../../ikea/docs/boundlist/usage.md) now supersede its extraction
recommendation for integration. They cover search-array layout, SIMD,
interleaving, opt-in numeric binding, cold staged access and retained-reader
histories. Direct/Width and compact typed bodies are available in the module;
the flattened prototype adapter was rejected. The final
[StreamPack parity evidence](../boundlist/evidence/parity/README.md) covers
complete readers, issued-effect mutations and packing. Reuse those selected
controls, including losses, rather than repeat the survey or infer complete
directory economics from leaf measurements. The
[hashtable benchmarks](../../benchmarks/hashtable/README.md) provide an existing
actual Abseil control if a loss needs resolver attribution.

The [memory-locator comparison](LOCATORS.md) now exercises the indirection
mechanism with actual Orbital-selected segment Parts. It removes address-only
directory repair at the measured cost of dependent reads and cell ownership.
Its compact and full-cell controls retain that tradeoff rather than selecting
indirection from the prior-art description alone. Same-Scope selection and
proof retention come from the current composition; Calico and Bw-tree do not
establish those obligations for SixDB.

The [schema experiment](SCHEMA.md) measures decision-site representation IDs
against an actual retained historical-definition tail. Native-source conversion
avoids rebuilding unaffected columns, while flat schema extension exposes its
full-copy cost. These are current composition results; the Calico metadata
mechanism supplies motivation without imposing its class/version encoding.

The first comparisons follow directly: plain per-layer arrays, the proposed
composition, coarse versus local directory edits, compact ordering metadata,
and simple versus hierarchical placement. Add a borrowed mechanism when one
of those comparisons exposes a concrete cost; no reimplementation of the
surveyed trees is needed to evaluate these implementation choices.
