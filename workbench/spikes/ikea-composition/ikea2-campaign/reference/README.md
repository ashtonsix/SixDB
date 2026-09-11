# Frozen predecessor comparator

This is the Ikea implementation measured immediately before the candidate's
source-boundary pass. `provenance.json` records original and transformed source
hashes. Headers and namespace use `ikea_predecessor`, with private include paths,
so a comparator can include both implementations without include-order tricks.
Run `check.py` to verify the retained files. It exists
only for named, optional performance comparisons; it is not a module to copy or
extend. Ikea tests use their smaller independent wire reference instead.

The original implementation is preserved by source checkpoint
`4ab2b64cc56b841e798da0088e24e04a216d9c48`. Rebuilt isolated comparators have a new
binary identity: original measured binaries and source receipts remain the
authority for historical performance numbers. Namespace changes alone can alter
code placement.
