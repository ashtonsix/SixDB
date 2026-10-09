# One preparation pass

[`prepare-pass.patch`](prepare-pass.patch) is an unapplied control against
`0acd622de2e0219f2029843ec6b553ffc640e435`. Each History now reserves its own
debt capacity inside `prepare`; Store no longer calls a separate `reserve`.
NodeHistory keeps its existing debt-count pass. InlineHistory counts new debts
and old tips together, removing one explicit directory lookup per changed
Object. Its later `ensure` remains. No new state, allocation or policy is added.

Debt reservation still happens before output/node allocation, entry creation,
physical retention or pinning, outside the layout's mutation-cleanup handler.
Store validation and its capacity-error conversion retain their order. The
combined counting pass reads owner-local state; the current final `Memory`
resource does not invoke owner callbacks. This does not establish a protocol
for concurrent directory mutation or reentrant custom allocators.

The patch applies cleanly to the named commit. In a private incremental
projection, pinned Linux AArch64 Clang 21.1.8, Release with assertions, `-j2`,
both existing checks pass: `orbital_mvcc_candidates_check` and
`orbital_mvcc_bounded_check`. Their 14 Store profiles cover allocation refusal,
delete/recreate, accepted work, abort callbacks and bounded maintenance with
both history layouts. Live implementation and index are unchanged.

Exact patch/source hashes, commands and native logs are recorded in ignored
`build/orbital-mvcc/metadata-projected-integration/prepare-pass-receipt.json`.
No timing or sanitizer run was added; reduced explicit work is not a measured
latency improvement.
