# Swiss SIMD batches

Graduated ordinary overflow reads: **64-byte comparisons with AVX-512BW,
32 bytes with AVX2, 16 otherwise**. Logical groups remain 16 bytes; tag encoding,
placement and the 87.5% ceiling are unchanged. This is a compiled ISA default,
with no new caller option.

[Graduation](GRADUATION.md) records the maintained implementation, costs removed,
whole-table reads/writes, actual Boost/Abseil controls and accepted losing cases.
The [initial screen](RESULTS.md) retains the earlier packet/first-line comparisons;
its recommendation to retain scalar probing was superseded. The maintained
implementation and boundary checks are in Ikea.

The prototype implementations and runners have been archived. This directory
keeps offline reports and evidence; it does not maintain another table owner.

## Evidence and recovery

- [Graduated reader](evidence/graduated/provenance.json): 300 native callers,
  23,100 raw rows, full set/map8/map64 hot and set/map8 cold comparison.
- [Construction replay](evidence/construction/provenance.json): 345 frozen binary
  executions qualify the first-use construction anomaly; no recompilation.
- [Static proof](evidence/graduation-static/provenance.json): final sanitizer
  checks, complete primary read/write assembly comparisons, scalar-control
  preservation and literal metadata scheduling estimates.
- Earlier [ordered](evidence/lookahead/provenance.json),
  [bounded first-line](evidence/bounded/provenance.json) and
  [read-only](evidence/readonly/provenance.json) cohorts retain the rejected
  treatments and the reason for separating reads from mutation resolution.

From Linux with the pinned toolchain (prefix with `orb -m ubuntu` from macOS):

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-swiss-batch/evidence/graduated build/swiss-batch-report
```

The recipe restores the full S3 bundle and reproduces the audit and summaries.
Every native cohort contains captured source and binaries. Exact replay uses
that source, including its runner/check versions, rather than applying old
experimental deltas to future production headers. A verified source copy is
also in Linux `~/sixdb-archive/hashtable-swiss-batch-20261020/`
([receipt](archive.json)).
