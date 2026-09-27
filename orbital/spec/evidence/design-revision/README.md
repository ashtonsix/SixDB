# Checked Orbital design revision

This selection accepts 599 of 600 maintained cases: 248 completed graphs,
188 expected defects and 163 witnesses, with no conflicting result. The single
missing case is Admission-renewal-capability; its separate investigation remains
open. This is a completed check of the design revision, not a completed claim for
the entire verification plan.

Compared with the historical pre-revision selection, 276 accepted baseline
closures changed (213 import TxKernel, 118 import RecoveryKernel, 55 import both).
All have current accepted replacements. The other 209 have unchanged dependencies.
The 114 new maintained cases cover policy boundaries and actual transaction
binding, cold-owner recovery, repeatable ordinary requests and prepared-root
derivation. Separate study archives retain the full policy/fusion comparisons
and native workload histories.

checks.csv records each outcome, state count, execution settings and receipt/log
digest. inputs.json identifies exact parsed dependencies, catalogs, checker and
TLC jar. invalidated.csv maps each changed baseline case to its changed imports.
The full archive preserves selection.json, original receipts/logs/progress, each
selected source closure and the reports/catalogs at collection. No stopped run is
counted as complete.

These are finite configured model checks with stated fault, service, reduction
and authored-schedule assumptions. They are not an arbitrary-size composition
proof, implementation proof or performance measurement. TLC fingerprint collision
qualifications remain in the raw logs. Legacy receipts did not authenticate prior
edits to their logs; collection checks and hashes the retained evidence.
