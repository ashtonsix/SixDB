# Scan4 callsite-inline follow-up: keep the baseline

The 10 September V2 follow-up rejects the inline arm and independently repeats
the original candidate's short public-call regression. None of these Scan4
changes is integrated. Larger-call wins do not clear the 256-value loss.

[All repetitions](../evidence/scan4-inline-20260910/cases.csv) and their
[provenance/recovery reference](../evidence/scan4-inline-20260910/provenance.json)
retain worker `20260910T200445Z-b69bf135`. Three exact binaries ran in process
order before/candidate/inline/inline/candidate/before: 42 cases, five sequential
50 ms repetitions per process, CPU0. Six native public/protected-extent checks
passed before timing, and the exact inventories and executable hashes match.

The third arm adds only a force-inline annotation at the candidate's W4/u8
bound callsite. Clang removes the call to `encode_typed` but outlines
`encode_payload` instead; bound/callee frames change from 32/176 to 96/128 bytes.
This is a concrete resulting implementation, not an isolated call-cost test.

| Public encode | Before | Candidate | Inline | Candidate/before | Inline/before |
| --- | ---: | ---: | ---: | ---: | ---: |
| Bound, 256 values | .026319 | .027282 | .030526 | 1.0366 | 1.1598 |
| Bound, 8,192 | .014077 | .011296 | .011393 | .8024 | .8093 |
| Bound, 65,536 | .015711 | .014102 | .014086 | .8975 | .8966 |
| Bulk driver, 8,192 | .014249 | .011458 | .011546 | .8041 | .8103 |

Numbers are pooled medians in ns/value. At 256 the ranges are
[.026056,.026364], [.027251,.027336] and [.030328,.030799]; every sample
separates in the same order in both process groups. Candidate/before block
ratios are 1.0403 and 1.0362; inline/before ratios are 1.1578 and 1.1647.
The original public regression therefore repeats as 3.66% after the earlier
3.51% result. Inlining worsens it to 15.98%.

The raw helper still improves at every extent, while its inline/candidate
ratios stay around one. Unchanged Scan6/Local4 controls and the Scan4 predecessor
encoder remain stable. Public candidate and inline encoders still trail their
same-arm predecessor at every measured extent. The decoder's context-dependent
movement is retained in the complete records; it is not an encoder-algorithm
benefit. The next change must recover useful grain without retaining this
short-call loss; this result does not justify a universal inlining policy.
