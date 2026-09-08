# Signature rollup

**Can a small block summary prove that no visible row in 16, 64, or 256 rows
can satisfy the filter, before reading their row signatures?** These are
candidate constructions and analytical examples. The first [resident study](FINDINGS.md)
now compares joint presence, marginal presence, and shared-mask OR at selected
block sizes; the remaining constructions are still open alternatives.

## Preserve the existential meaning

For a block `B`, rejection must prove `not exists r in B: filter(r)`.
Computing `exists A` and `exists B` separately does not compute
`exists (A AND B)`:

| Row | A | B |
| --- | --- | --- |
| 0 | true | false |
| 1 | false | true |

Both marginal summaries say possible, yet the conjunction has no match.
This is a precision loss even with collision-free predicate bits. Hash
collisions are another, separate source of false candidates.

For row signatures with **positive bit containment**, `R = OR_r S(r)` is
sound: if any row contains all required bits `Q`, then `R` contains them.
Therefore `(R & Q) != Q` rejects the block. A passing test supplies no same-row
witness. With OR alternatives, all branches must be rejected to reject the
whole expression; a single impossible atom only kills its containing conjunction.

Do not reuse this rule as equality on ORed packed tags. For tags `01` and
`10`, the rollup is `11`; testing equality against `01` would reject a block
that contains that tag. The encoding and the merge operation must agree.

## Candidate summaries

| Rollup | Merge/query rule | What it retains and loses |
| --- | --- | --- |
| OR of shared positive masks | OR compatible child masks; reject missing required bits | Very cheap, but more tokens/rows saturate bits and lose row correlation |
| AND plus OR of fixed-width codes | AND/OR across nonempty children; reject query bits inconsistent with a bit constant across the block | Cheap necessary equality test; unconstrained bits lose their correlations and quickly become uninformative |
| Per-feature tag/bin presence | Union sets of present codes; reject a disjoint allowed set | Exact membership in the encoded domain; correlations between fields/planes still lost |
| Joint pattern presence for one small plane | Union sets of complete plane codes; intersect with codes allowed by a query fragment | Retains within-plane combinations occurring on one row; loses correlation across separately summarized planes |
| Ordered interval or bin occupancy | Merge min/max or OR bin presence; compare with query range | Range-friendly control; min/max loses holes, occupancy retains some holes but has boundary ambiguity |
| Small distinct-pattern list / antichain | Merge, deduplicate, and conservatively coarsen on overflow | Can retain correlations under low diversity; variable size, overflow, query work, and rebuild costs matter |
| Positional bitmaps or transposed row bits | Keep row positions aligned and evaluate the Boolean filter with bitwise operations | Preserves same-row combinations, but may be another layout of the row signature rather than a smaller summary |

For AND/OR code bounds, let `A = AND_r code(r)` and `O = OR_r code(r)`.
A query code `q` is compatible only if `(q & A) == A` and `(q | O) == O`.
Empty blocks need an explicit identity/occupancy rule. Arbitrary sets of allowed
query codes require testing for any compatible code, not treating the set's
bitwise OR as a single code.

For positive containment, retaining only maximal observed row masks (an
antichain) preserves the encoded existential query: every dropped subset has
a retained superset. A size cap can OR groups of masks to remain conservative,
at the cost of introducing combinations that never occurred. This rule does
not transfer to equality or arbitrary negation. Deleting a dominating row also
requires recovering any still-live patterns it hid, or retaining a conservative
superset until rebuild.

## A particular opportunity for byte planes

An 8-bit plane has only 256 possible row codes. A 256-bit presence bitmap costs
32 bytes per block and records exactly which codes occur. Given an allowed-code
bitmap `Q`, `(presence & Q) == 0` proves no row in the block passes that plane's
fragment. The original predicate may still need exact refinement on surviving
rows. A conservative fragment table must account for unrepresented facts.

This works for two four-bit field tags sharing a byte: joint pattern presence
retains their correlation, while two 16-bit marginal presence maps do not.
The trade is 32 bytes versus 4 bytes per block. Compare the additional pruning
with the extra stored/scanned bytes. It also works for eight exact flags or
an ordered byte prefix, with the appropriate allowed-code calculation.

| Rows per block | One 256-bit pattern set, bytes per row |
| --- | ---: |
| 16 | 2 |
| 64 | 0.5 |
| 256 | 0.125 |

These are single-level payload costs for one plane, excluding alignment and
metadata. At 16 rows the set exceeds the byte plane itself; a list of up to
16 codes or simply reading row bytes is a serious control. A full 16-bit-domain
presence map costs 8 KiB **per block**; a 32-bit-domain map costs 512 MiB.
Sparse lists, coarse bins, selected tuple hashes, or smaller groups are more
credible comparisons there. A tuple hash preserves a necessary equality test
for its selected tuple, not an arbitrary Boolean program over its fields.

Separate plane summaries can each find a code on a different row. Intersecting
their block-level “possible” flags is safe but may be much weaker than
intersecting row masks. Compare one joint byte plane, independently rolled-up
bytes, and a few chosen cross-plane combinations under the same total budget.

## Block size, saturation, and organization

For an ideal uniform `b`-bit tag and a fixed absent equality, the chance that
none of `g` independently tagged rows collides is `(1 - 2^-b)^g`. With eight
bits it is about 94%, 78%, and 37% at 16, 64, and 256 rows. This is a model
for tag presence, not the false-positive formula for an ORed token signature.
Duplicates, hot values, multiple query tags, and clustering change it.

For a positive mask with per-row bit occupancy `rho`, an IID row model gives
rollup bit occupancy `1 - (1 - rho)^g`; even moderately occupied rows can
quickly make OR summaries useless. Query bits need not be independent, so
this occupancy expression alone does not predict a conjunction's pass rate.
Count encoded diversity, occupied bits, same-row candidates, and no-match
blocks admitted separately.

Test flat 16/64/256 summaries and selected hierarchies such as 256 → 64 → 16.
Charge **all** levels and descent overhead, including the rows where every
level passes. Larger blocks amortize summary bytes and can save larger reads;
smaller blocks retain precision. SIMD batch size, signature block size, payload
page size, and storage tree fanout need not be identical.

Use the same rows in random, clustered, and adversarial arrangements to isolate
grouping. Logical key order, column correlation, and
[physical remapping](../trie-remapping/README.md) may change rollup utility
without changing global selectivity. A summary covering a superset of the
incoming row mask can safely reject that mask, but often loses its selectivity;
positional summaries or direct sparse probes may then be preferable.

## Maintenance and visibility

Compatible OR/presence summaries merge naturally; merging different hashes,
bin boundaries, plane meanings, or editions requires translation or bypass.
A rebuild or split cannot publish negative evidence until it covers all rows
visible through that block. Row moves must repair both membership and summaries.

For rejection-only OR/presence summaries, retaining deleted contributions
remains conservative. Clearing them needs per-bit/per-pattern multiplicities,
reconstruction, or proof of the last witness. AND/OR bounds must widen when
new rows disagree with formerly constant bits. Min/max deletion likewise may
require repair or conservative bounds. Measure counter space and contention
against rebuild-on-dirty-block and immutable-edition alternatives.

Current conservative supersets can cover both old and new values, but latest
only summaries are unsafe for old snapshots. Inserts/pending updates cannot be
hidden by a clean-looking ancestor; propagate evidence or force descent/bypass.
Dirty markers can borrow ideas from
[aggregate maintenance](../aggregate-maintenance/shared-dirty-buffer.md), while
their exact publication and reset contract remains part of this investigation.
False “all rows match” certificates are a different risk from false candidates;
they need their own coverage proof if explored.

The result to seek is avoided row-signature and payload work per maintained
summary byte, including refresh debt. A rollup that merely repeats a cheap
row-plane scan after first adding its own scan is a valid negative result.
