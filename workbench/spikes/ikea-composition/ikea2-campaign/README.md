# Ikea2 campaign evidence

This directory retains the comparative investigation behind the
[Ikea implementation](../../../../ikea/README.md). Current guides and supported behavior live there. This is the home for historical alternatives,
measured exceptions and the frozen predecessor comparison.

The [candidate account](candidate.md) closes the implementation, performance and
compilation evidence on one source snapshot. It records the retained exceptions
and distinguishes supported behavior from owner implementations still outside
this candidate. Earlier measurements below are intermediate snapshots.

Final full sweeps, with the same candidate source digest:

- [Zen 5 AVX-512](evidence/20260911-candidate-zen5/summary.md).
- [Zen 5 AVX2](evidence/20260911-candidate-avx2/summary.md).
- [Neoverse V2 NEON](evidence/20260911-candidate-neon/summary.md).

## Recover and compare

Each `evidence/NAME` directory contains compact CSV, interpretation and source/host
provenance. Its `artifact.json` identifies the retained source, binary and raw logs
in S3. Follow the [artifact guide](../../../tools/artifacts.md) to recover selected
members. Regenerate comparisons with
`python3 workbench/benchmarks/seriespack/summarize.py workbench/spikes/ikea-composition/ikea2-campaign/evidence/NAME`.

`bench.cpp`, `mutation.cpp` and `fixture.h` retain the broader comparison workload,
including historical execution alternatives and all-width sweeps. The
[frozen predecessor](reference/README.md) supplies the `ikea` controls. Specialized
narrow/wide56 controls remain with their originating probes through the shared
[SeriesPack benchmark adapter](../../../benchmarks/seriespack/README.md).
Build only when needed with `IKEA_COMPARISONS=ON` / `ikea_seriespack_compare`; this code is not
a candidate API or a normal consumer dependency.

## Earlier findings

[assessment.md](assessment.md) is the historical account, including failed
approaches, loss of inner-kernel gains at ordinary boundaries, and the test-only
compilation-cost reduction. Its “latest” numbers are historical, not the candidate
closure snapshot.

Useful intermediate evidence includes:

- [Bound pipelines and catalogue, Zen AVX-512](evidence/20260911-bound-catalog-zen5/summary.md).
- [Bound pipelines and catalogue, Neoverse V2](evidence/20260911-bound-catalog-v2/summary.md).
- [Native 32-row AVX2 writes and broader reads](evidence/20260911-native32-avx2/summary.md).

The final placed-consumer comparison strengthens materialized controls to use the
same native read grain and prepared physical bulk writer. Earlier placed ratios
must not be treated as measurements against those strengthened controls.

[Retirement and recovery](../seriespack-predecessor/README.md) identifies the old
source checkpoints and the relocated predecessor workload findings. The active
comparator uses current/predecessor labels; historical CSV/JSON case names remain
unchanged and the analyzer understands both.
