# Independent logical cases

Two API-free workloads retain 8/12-byte logical keys, 26 command batches,
complete payload bytes, expected old/fresh states, duplicate-fence ranges and
membership/order results from the discarded directory check. The bundle holds
`generate.py`, `expected.json.gz` and its context; no backend, binary, Engine API
or storage layout is retained here. Its 42,756-byte recovery bundle was uploaded
and download-verified.

Use the data and independent expected results when useful for the replacement.
They do not prescribe its API, representation, fanout, allocations or test harness.
The generator checks full-key order, shared-prefix validity and consequential
range/cancellation/survivor results. This is correctness input, not performance
evidence or qualification of the replacement.

Recover with `python3 workbench/tools/artifacts.py fetch` using
[artifact.json](artifact.json) and a new ignored output directory.
