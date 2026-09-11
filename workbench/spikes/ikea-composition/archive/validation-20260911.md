# Recovering the former validation directory

The September 11 curation separated recurring workloads from studies of native
composition, call boundaries, range execution, head projection, codec kernels,
BEC metadata and executable placement. The [file inventory](validation-20260911.csv)
records each original path, decision, destination, size and pre-move SHA-256.
It is a historical lookup, not a catalog to maintain for future work. Eight
authored source/check summaries were accidentally ignored as `run.json` despite
being linked from findings; these now live beside their evidence as `run-facts.json`.

Git keeps findings, paired comparisons (including losses and repetitions),
focused correctness/assembly witnesses, useful prototypes and measured identities.
Full checkpoint sweeps, duplicate per-block samples and one-off campaign wrappers
were removed from the Git selection. Fifteen audit records also omit replicated
full-run file-hash inventories; their other fields remain intact, with the original
audit hash and archive member recorded beside them. Retained comparisons are not
a substitute for the full sweep when asking a new question.

The [verified archive](validation-20260911.json) holds the original 647-file
validation tree. Its members are relative to that old directory. For example,
from the repository root:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/ikea-composition/archive/validation-20260911.json \
  build/recovered/validation-20260911 --file evidence/coherent-20260910/cases.csv
```

This restores old Git material, not a runnable SixDB checkout. Original worker
artifact references beside each study's evidence recover measured sources,
objects and binaries; historical paths and hashes inside those records remain
unchanged. Removed members are also identified in their retained provenance.
The latest baseline's full table has its own
[analysis artifact](../../../benchmarks/seriespack/evidence/delivery-baseline-20260911/full-analysis-artifact.json).
The final store campaign arrived after this archive and retains its own
[verified artifact](../../seriespack-range-execution/evidence/delivery-store-20260911/artifact.json).
