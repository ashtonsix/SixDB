# Share callbacks when point-reader strategies are identical

Keep this as a small maintenance candidate in Workbench. No production change
is installed and no runtime improvement has been measured. It follows the
[performance and maintenance direction](../design.md#performance-and-maintenance)
while the broader SeriesPack delivery assessment continues.

The existing arithmetic getter delegates to the constant-offset getter for
every local format and striped residual widths other than 3, 5, 6 and 7.
Binding nevertheless instantiates both private callbacks for every description.
The [one-function patch](evidence/point-canonicalization-20260911/candidate.patch)
selects one callback where those algorithms are already identical. It preserves
the requested `point_strategy` metadata and both real irregular algorithms.
Callback-address uniqueness is not a public contract; custom `assume_valid`
endpoints are untouched.

Current ARM, generic-tuned `-O2` objects show 188 pairs with identical normalized
instructions and relocation references. The candidate removes those duplicate
instantiations and simplifies the selectors:

| Function-symbol text | Before | Candidate |
| --- | ---: | ---: |
| Point callbacks | 37,796 B / 412 functions | 22,152 B / 224 functions |
| Entire operations object | 648,720 B | 615,880 B |

The 32,840-byte reduction also appears in executable `.text` when linking the
operations and public-wire checkers from the same caller objects. Their rodata
is unchanged. The static-point-only checker discards the affected binding paths
and has unchanged executable text. [Code accounting](evidence/point-canonicalization-20260911/cost-summary.json)
separates executable text, constants, unwind data and file sizes; debug-file
size and one observed compile duration are not runtime or build-speed results.

Paired [existing checks](evidence/point-canonicalization-20260911/checks.json)
pass for both archives, whose only changed member is `operations.cpp.o`.
They cover requested strategy labels, all 206 descriptions, static points,
and public wire/placement/operation semantics on scalar and NEON. Source,
object and binary identities, exact commands and the source archive are
[retained](evidence/point-canonicalization-20260911/provenance.json).

This is a concrete option for removing redundant implementation variants.
It does not repair short-range traversal, establish another target's code cost,
or complete the broader SeriesPack campaign.
