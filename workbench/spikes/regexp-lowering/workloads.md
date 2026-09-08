# Paired workloads

Retrieved 2026-09-08. [prepare.py](prepare.py) pins source revisions, verifies
downloads, records file hashes, and produces length-delimited UTF-8 strings
plus original regexp bytes and flags. No regexps were generated from the
selected strings. Each run carries its exact prepared input and source manifest.

## Accident descriptions and their extraction queries

[BLARE's repository](https://github.com/mush-zhang/Blare) publishes the US
Accidents December 2021 CSV through Git LFS and four queries in
[regexes_traffic.txt](https://github.com/mush-zhang/Blare/blob/b3c2a344307aed76e70ad1c40fd772c160d89425/data/regexes_traffic.txt).
The [BLARE paper, section 5.1.1](https://pages.cs.wisc.edu/~jignesh/publ/BLARE.pdf)
describes applying these queries to accident descriptions. Its production
DB-X/System-Y workloads are not published with the repository. The later
[REI repository](https://github.com/mush-zhang/REI-Regular-Expression-Indexing)
also points to this public workload.

Pin: `b3c2a344307aed76e70ad1c40fd772c160d89425`. The CSV is 1,154,730,978 bytes,
with SHA-256 `6029cb1eb91a607ba9e08f58ad46297129c7e11b1fd83dfbc20a585db2fca885`,
matching its published LFS pointer. CSV parsing observes 2,845,342 data records
(the paper lists 2,845,343). Use the actual checked file and parsed counts.

The default study samples 64 evenly spaced, nonoverlapping source blocks of
1,024 descriptions, totaling 65,536 values. It retains duplicate descriptions
and native order within each block; block starts are recorded in `inputs.json`.
This spreads the sample across the file while preserving local cohorts. It is
not random sampling or a claim that source order is timestamp order. Future
results can change the sample or run the full corpus without changing queries.

The four raw patterns are `At (.+)Exit (.+)`, `(.+) on (.+) at Exit (.+)`,
`on (.+) at (.+)`, and `Ramp to (.+)`. They are short, concrete extraction
queries over templated descriptions, useful for measuring order and gap
constraints. Four queries give very limited evidence of overall regex coverage.
This probe tests Boolean existence; BLARE's capture/extraction timings are not
being reproduced. Original dataset provenance is linked from BLARE to the
[US Accidents publisher](https://www.kaggle.com/datasets/sobhanmoosavi/us-accidents).

## User-agent rules and collected regression strings

[ua-parser/uap-core](https://github.com/ua-parser/uap-core) maintains the shared
rules used by language implementations and collected user-agent fixtures.
Its [specification](https://github.com/ua-parser/uap-core/blob/73e7340c3ed8055051607b296bf46ead7aa5f19e/docs/specification.md)
describes ordered, case-sensitive unanchored matching; device rules can carry
an explicit case-insensitive flag.

Pin: `73e7340c3ed8055051607b296bf46ead7aa5f19e`. Use every rule in
`regexes.yaml`: 433 user-agent, 204 OS, and 633 device rules, totaling 1,270.
Use all `user_agent_string` entries from `tests/test_ua.yaml`, `test_os.yaml`,
and `test_device.yaml`: 18,213 entries, 17,816 distinct strings. Source file and
row identities accompany the prepared data, and the upstream Apache license
travels with the input bundle. Explicit `i` flags are honored; unsupported
patterns are reported by the pinned RE2 oracle.

This is a maintained real application ruleset paired with its regression
inputs, not production access-log telemetry. Fixtures overrepresent unusual
devices and regressions; duplicates are not frequency estimates. Testing each
rule as an independent scan over the combined corpus resembles Boolean rules
over an access-log column, but differs from the classifier's first-matching-rule
execution and capture extraction. Neither equal rule weighting nor the native
fixture order is presented as a measured customer workload distribution.

This second workload supplies syntax diversity and genuine positive examples
missing from the four-query accident workload. Report exact coverage, declines,
queries with no hits, and per-rule rates as well as aggregates, so a high overall
rejection rate cannot hide ineffective or unexercised rules. A production
strings-plus-query-frequency trace remains an open dataset question.
