# ua-parser rules and regression fixtures

Pinned uap-core revision `73e7340c3ed8055051607b296bf46ead7aa5f19e`, originally
used by the [regexp-lowering spike](../../spikes/regexp-lowering/workloads.md).
[dataset.json](dataset.json) records the URL, exact source hash, and size.

`datasets.py get uap-core` supplies `rules.jsonl`, `fixtures.jsonl`, and `LICENSE`.
Rules retain their source records, IDs, groups, flags, replacement fields, and
order. Fixtures retain their source file, row index, and full test record.
There are 1,270 rules and 18,213 fixture entries (17,816 distinct user-agent
strings). Duplicates and native source order are preserved.

These are collected regressions, with unusual cases deliberately represented.
Their frequency and ordering do not describe production traffic. Evaluating
every rule independently against every string, as the regexp spike does, is
a separate workload choice from the original first-match classifier.

The shared recipe does not select RE2 semantics or a binary input format.
Install PyYAML to prepare from source; a published prepared variant can be
restored without importing it. See the [dataset helper](../README.md) for caching
and selected prepared references.
