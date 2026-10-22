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
dictionary entries examined, actual hash updates and null updates. The local
counter-vector peak excludes all other scratch, dictionary, plan and hash memory;
it is not an RSS or complete footprint measurement.

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
A native fleet campaign has not been scheduled.
No timings or encoded-execution wins are claimed by this preparation.

The checks compare full groups, not just totals/checksums. They cover a
hand-calculated example, reversed dictionary editions, null-only and empty parts,
selected holes, full population and maximum values, out-of-domain thresholds,
regrouping collisions, generated populations and deliberately wrong code mappings.
They are evidence about the instrument's result boundary, not Frame graduation.

Local qualification on 2026-10-22: all eleven checks passed with the pinned Clang
21.1.8 development build. Both consumers also matched all eight queries in each
of the lifecycle investigation's 13 generated snapshots: 208 query/program
comparisons. These were ordinary local checks with no timing or physical
retained-reader claim. Generated snapshots remain in ignored `build/` storage.
