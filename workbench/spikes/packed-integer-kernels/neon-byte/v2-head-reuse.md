# Zero-shift all-head reuse: exact V2 replay

The exact paired-binary ABBA replay supports reusing the measured W8/u64 dense
64-value encoder in zero-shift u64 head runs. All seven cases passed in both
process orders, five sequential 50 ms repetitions per process on pinned V2
CPU0. Each before/candidate column below pools its ten CPU ns/value samples.

| Case | Before | Candidate | Candidate / before |
|---|---:|---:|---:|
| `byte_grain/bound_head8/256` | 0.125384 | 0.094382 | 0.7527 |
| `byte_grain/bound_head8/8192` | 0.113607 | 0.088275 | 0.7770 |
| `byte_grain/bound_head8/65536` | 0.153391 | 0.111667 | 0.7280 |
| `bulk/series/neon/local/k8/h8/u64/encode` | 0.114458 | 0.090075 | 0.7870 |
| `bulk/series/neon/local/k16/h8/u64/encode` | 0.224118 | 0.220847 | 0.9854 |
| `bulk/series/neon/local/k16/h16/u64/encode` | 0.228713 | 0.213314 | 0.9327 |
| `bulk/series/neon/local/k24/h16/u64/encode` | 0.338299 | 0.336881 | 0.9958 |

The K8/H8 whole encoder improves about 21.3%; K16/H16 improves about 6.7%
because only its zero-shift low head plane changes. Shifted K16/H8 and K24/H16
controls remain close (about −1.5% and −0.4%). The shared-buffer bound_head8
cases improve at all three extents by about 22–27%. This is now actual head
traversal evidence, distinct from the earlier byte-body-only measurement.

The underlying byte-body primitive is unchanged: the head patch adds one
64-value prefix in Shift0/u64 runs and reuses the existing byte-body helper.
Its exact remainder and strided tile behavior stay in the head traversal.
No shifted-head or all-shift policy follows from this result.

The measured source predates the coherent range rewrite: native.cpp before is
`869f0504`, candidate `89241f53`, and both use payload header `24ca6129`.
The [compact repetitions and medians](evidence/v2-head-replay-20260910/cases.csv)
and [provenance](evidence/v2-head-replay-20260910/provenance.json) retain all four
processes and exact identities. The [full replay artifact](evidence/v2-head-replay-20260910/artifact.json)
retains recovery references for the measured binaries. A separately checked
rebase onto coherent native.cpp `1abf641b` is required before integration; these
measurements are not relabeled as measurements of that new combined source.
