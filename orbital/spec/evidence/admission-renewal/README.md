# Orbital campaign after Admission renewal

This current-source selection accepts **613 of 613 maintained cases**:
254 complete graphs, 195 expected defects and 164 witnesses, with no conflicting
result. It closes the Admission item left open in the dated
[design revision](../design-revision/README.md) and
[original baseline](../baseline-before-design/README.md); those exports are unchanged.

`Admission-renewal-capability` completed 925,794 distinct states and 9,580,369
generated successors at depth 64, with all five safety properties and `Completes`
passing. The two-loss configuration is byte-identical to the original. The
[reduction account](reduction.md) explains the reviewed representation map,
its finite audits, unsafe controls and limits. Thirteen supporting cases accompany
the replacement. The original larger reference's stopped prefix is not counted
as a completed case.

`checks.csv` records each selected outcome, state count, execution settings and
receipt/log digest. `inputs.json` identifies exact parsed dependencies, catalogs,
checker and TLC build. The full archive preserves `selection.json`, raw receipts,
logs/progress, each selected source closure and the reports at collection. Two
completed prior-reduction comparisons and their exact inputs are retained
separately, including the two-loss comparison's checkpoint lineage; they do not
inflate the 613-case count. Solver scratch is omitted.

Recover the archive through `artifact.json` using the
[retention tools](../../../../workbench/tools/artifacts.md). Against these same
maintained inputs, re-run `orbital/spec/evidence.py --search RESTORED/runs
--output NEW_DIRECTORY` to recheck the selected receipts. The
[model guide](../../README.md) explains case execution and collection.

These are finite configured checks under their stated fault, service, reduction
and authored-schedule assumptions. The complete reduced graph is not an
exhaustive traversal of the unreduced graph, an arbitrary-size composition proof,
an implementation proof or a performance measurement. TLC's fingerprint-collision
qualifications remain in the raw logs. Legacy receipts did not authenticate prior
log edits; collection rechecks and hashes the retained diagnostics. Timings from
shared hosts and resumed attempts are not a clean full-suite wall clock.
