# Execution witness: selected grouped reduction

This instrument asks how much knowledge of an encoding remains useful through
an Engine operation and into another Ikea container. It accompanies the broad
[Frame research](../REVIEW.md); it does not select Frame's format, schema API or
an optimiser. Engine owns this directory; Ikea owns the enclosing study, Claude
the encoding investigation, and Ikea's lifecycle investigation reuses the logical
inputs here.

## Why this question

The [layout-analyser synthesis](../../layout-analyser/design.md) proposes choosing
representations and their operation programs together. Its
[experiments](../../layout-analyser/experiments.md) distinguish rebinding unchanged
bytes from migration. The [TuplePack mixed-workload results](../../tuple-layout/viability.md)
show why isolated operation coefficients can choose a poor complete program.
These findings set the starting point: compare useful complete consumers over
the same image, then compare representations and their lifetimes.

The consumer groups selected rows by nullable category C and returns COUNT/SUM
of a nullable unsigned integer X. Different parts can assign different local
dictionary codes to the same category. The output uses actual logical keys in
the graduated [Ikea hash map](../../../../ikea/docs/hashtable/usage.md).

Several programs deserve comparison:

| Program | Opportunity and cost to establish |
| --- | --- |
| Reconstruct selected rows and hash their logical group keys | A useful straightforward fused control; handles broad cardinality without a large local histogram. |
| Classify the dictionary, reduce by local code, then translate active groups | Saves per-row translation/hash work; pays dictionary preparation and local counters. |
| Retain a structured partial result or reusable scratch | May share work with another operator/query; requires interpretation lifetime and a useful reuse horizon. |
| Reduce a conditioned or predicted residual stream | Exposed reconstruction laws might remove full-value reconstruction; weights, exceptions, nulls and overrides all count. |

Compare opaque getters, **curated opaque complete-operation kernels**, exposed
relations/program expansions, and independently fused complete consumers. A rich
schema must earn reusable composition or better choices against good whole-operation
kernels. A weak per-row accessor is insufficient evidence. Supported plain,
independent-column, TuplePack-grouped, conditioned and routed images remain
candidates; this logical input does not choose one.

Change query parameters, selected topology, cardinality and reuse while keeping
an encoded image unchanged. Later compare changed images separately. The current
generated X values are independent uniform u32, an authored control, not evidence
that residual encoding is worthwhile. Claude can derive correlated fixtures from
the same logical vocabulary without inheriting this distribution.

## Authored semantics

Each part has an exclusive `position_limit` from 0 through 65,536. Its `rows`
list contains occupied records at distinct original positions. Missing positions
are absent records. X and C can independently be null. Selection names original
positions; selecting an absent position creates no record. A missing part in a
query's selection excludes it, `null` selects all its occupied positions, and an
array selects those positions. An empty array selects none.

An independent optional `coarse_selection` supplies **one bit per block**. In
this private JSON witness its selected bits are listed as block indices:

```json
"coarse_selection": {
  "part-0": {"positions_per_block": 256, "blocks_per_group": 16, "blocks": [0, 1, 17]}
}
```

The numbers illustrate one experiment, not fixed Frame geometry. A block owns
original positions `[block*positions_per_block, min(limit,(block+1)*positions_per_block))`.
`blocks: null` selects all blocks; `[]` selects none. Omitted coarse entries
select all. Row admission intersects this mask with the existing original-row
selection; it does not renumber positions. Only the final physical block can
be partial. `blocks_per_group` is experimental traversal grouping; if omitted,
one group covers the part's blocks. **No independent group mask is required.**
The consumer derives skipped groups, whole active groups and mixed groups from
the block bits. It retains intervals through traversal rather than expanding
the mask into per-row activity. An expanded mask exists only as a test oracle.

A query admits a row exactly when it is selected, X is non-null and at least
`min_x`, and its category is admitted. Non-null categories pass either `all` or
membership in the explicit `categories` list. A null category passes exactly
when `include_null_category` is true. This is an explicit predicate, not a claim
that SQL `IN` admits NULL. Grouping maps non-null C to `C >> group_shift`; all
admitted null categories form a separate group. COUNT counts admitted rows; SUM
adds their X values exactly in u64. There are no empty output groups.

X is u32; categories and thresholds are u64, including the maximum key. The
admitted record count bounds even the all-row sum by UINT64_MAX. The bound of
65,536 is legal for counts/endpoints; the largest position is 65,535. JSON integer
tokens must be read without conversion through floating point.

The document has `format: frame-execution-logical-v1`, an explicit `kind`,
a `snapshot` label, `parts` and `queries`. The generator uses
`kind: authored-synthetic`. Real-corpus derivations use `kind: corpus-derived`
and retain a nonempty `source` provenance object. That object belongs to the
input preparer: identify original data/partitions/row mapping, projected columns
and transformations, including any injected nulls/presence as synthetic changes.
The validator retains it without claiming to verify provenance. A naturally
non-null corpus does not measure nullable behaviour; derived presence tests need
their own label. Values outside the declared integer law require another law or
an explicitly described derivation/subset, never silent truncation.
Each part carries `part_id`, `dictionary_edition`, `position_limit`, an ordered
list of distinct dictionary values and logical rows `{position,category,x}`.
The dictionary describes one input code assignment, not a Frame encoding law.
Each query carries the same snapshot label plus its predicate and selection.
Identity labels are nonempty printable ASCII for this fixture transport.
See `example()` in [workload.py](workload.py) for a small concrete document.

The Python oracle reads logical rows and never evaluates through dictionary
codes. The C++ bridge instead receives local codes, resolves each part's dictionary,
and builds the actual Ikea hash output. A null group uses a separate accumulator;
no u64 category is reserved as a null sentinel. The compact text bridge is private
test transport, not a persisted Frame or common encoded-byte manifest.

## Reuse and current preparation

[workload.py](workload.py) supplies validation, a deterministic authored generator,
the logical oracle and transport. Lifecycle can reuse ordinary JSON rows/queries
and author later logical snapshots. Encoding experiments can use the same
expected results and add their own encoded material. A label does not establish
Orbital version authority: this directory currently supplies no retained-reader
or publication implementation.

[hash_check.cpp](hash_check.cpp) supplies two **untimed boundary controls**,
`rowwise` and `by-code`, both feeding the real hash table. They are scalar
logical controls, not Frame kernels or strong performance baselines. The fixed
hash geometry is not a tuning decision. Work counters explain row visits,
dictionary entry accesses (classification and later logical-key translation),
actual hash updates and null updates. Coarse work
adds selected/excluded block bits, skipped/full/mixed groups, entered address
ranges and probes into a separate prepared position directory. An all-zero
mask bypasses rows, dictionary preparation, local counters and scratch hash
construction. Full groups use whole ranges; mixed groups visit active blocks.
Fine selection is checked against the position directory before reading a row
body. A query can still pay directory probes outside the selected body ranges.

The loader prepares that directory once and loads all logical fixture rows;
those are input-preparation costs, not work avoided by a query. It is not an
encoded Frame descriptor. `row_record_bytes_modeled` is the sum of visited C++
record sizes, including padding, **not** issued load bytes, cache-line demand,
resident bytes or measured traffic. Dictionary-entry and position-probe counts
also describe logical accesses. Encoded-body experiments must record their
actual plane/span demands before making physical-work claims. The local
counter-vector peak excludes all other scratch, dictionary, plan and hash memory;
it is not an RSS or complete footprint measurement.

The eager `by-code` control classifies the whole part dictionary on its first
use, even if only one block survives. This is a chosen program cost, not a law
requiring every masked operation to visit every dictionary value. Lazy active-code
classification is an alternative for sparse masks. Empty parts and wholly
excluded parts need no dictionary classification.

## Coarse masks across the research surface

Ashton's requirement applies across most operation families, not only this
reduction. Investigate whether a mask can stay coarse through filtering,
projection, gather, transforms, reductions, packing/conversion and mutation,
skipping descriptors and payload movement before the first native body read.
Keep the empty/full fast paths beside sparse, clustered and mixed masks. Compare
different row masks under the same block bits, and different block arrangements
with the same active population. Preserve original coordinates and the declared
output order; an omitted block does not become a different row number.

Dependencies make some work necessary outside the output mask: predicted values
may need preceding residuals/checkpoints, routed values may need prefix counts,
and variable bodies may need earlier geometry. A shared dictionary or reused
query preparation has its own lifetime and acquisition cost. Expose that halo
and compare better restart/addressing/preparation choices rather than charging
every inactive payload or claiming the whole dependency disappears.

Masked writes exclude logical destinations, while preserving stores can still
cover adjacent packed bits under an admitted physical grant. Reorder/compaction
can change ranks and downstream geometry; a source block mask alone does not
promise zero movement elsewhere. For conversion, define whether inactive target
regions are retained, left unchanged or constructed separately. Carry these
questions into lifecycle and encoded consumers before choosing a public mask API.

From Linux at the repository root (prefix with `orb -m ubuntu` from macOS):

```sh
python3 workbench/spikes/ikea-frame/execution/check.py
cmake --preset dev -B build/clang/frame-execution -DSIXDB_SPIKES=ikea-frame
cmake --build build/clang/frame-execution --target frame_execution_hash_check
python3 workbench/spikes/ikea-frame/execution/check.py \
  --binary build/clang/frame-execution/workbench/spikes/ikea-frame/execution/frame_execution_hash_check
```

Generate a larger ordinary input in an existing ignored directory with
`workload.py generate --output PATH`, then use `workload.py oracle PATH` or
`workload.py transport PATH`. Pass `--input PATH...` to the compiled check command
to compare lifecycle or encoding snapshots through both hash consumers.
`workload.mask_variants(document, positions_per_block=..., blocks_per_group=...)`
adds authored all/none/clustered/scattered/tail controls to one query while reusing
unchanged logical rows and retaining their provenance. It selects no production
block geometry. The private text bridge is versioned independently as
`frame-execution-transport-v2`; rebuild the reader when changing that transport.
A native fleet campaign has not been scheduled.
No timings or encoded-execution wins are claimed by this preparation.

The checks compare full groups, not just totals/checksums. They cover a
hand-calculated example, reversed dictionary editions, null-only and empty parts,
selected holes, full population and maximum values, out-of-domain thresholds,
regrouping collisions, generated populations and deliberately wrong code mappings.
They are evidence about the instrument's result boundary, not Frame graduation.

Local qualification on 2026-10-22 passes fifteen checks with pinned Clang
21.1.8, including coarse exact-output and skipped-work checks.
Both original consumers also matched all eight queries in each
of the lifecycle investigation's 13 generated snapshots: 208 query/program
comparisons. These were ordinary local checks with no timing or physical
retained-reader claim. Generated snapshots remain in ignored `build/` storage.
The coarse follow-up also checked both ClickBench logical inputs and the small
IMDb year input with real nulls, at 256 positions/block × 16 blocks/group and
127 × 5: 124 query/program comparisons agreed. These are two experimental
geometries, not a default selection. All-zero masks recorded zero row visits,
dictionary accesses, address probes and hash updates in both controls.
