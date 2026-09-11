# Results explorer

A small browser UI generated from retained evidence. Browse investigations and
runs, filter cases, inspect recorded controls and repetitions, compare selected
runs, and open original tables, build costs and measurement context.

From the repository root (Python 3, no frontend install):

```sh
python3 workbench/tools/results.py --serve
```

Open `http://127.0.0.1:8766`. This is a snapshot; rerun the command to include new
evidence. To refresh a running server, run it again without `--serve`. The generated
site lives in ignored `build/results/`; it can also be served by any static host.

**Export this run** downloads a self-contained HTML file that opens offline,
including tables, context and recorded comparisons. It exports the selected run,
not an additional cross-run baseline. For a curated set, generate a smaller site:

```sh
python3 workbench/tools/results.py --output build/results-review \
  --include ikea2/bench/evidence/20260911-test-reuse-zen5
```

Repeat `--include` for several directories. Review the selection before sharing
with an evaluator; the viewer carries research notes and artifact references.

## How evidence reaches the UI

The generator discovers Git-visible CSV and build records beneath `evidence/`
in Workbench and `ikea2/bench`. New runs appear without editing an index. It reads
compact files locally; it neither downloads S3 artifacts nor launches workloads.

Adapters currently understand the shared `evidence.py` case tables, Google
Benchmark CSV, Ikea2 samples and recorded controls, and retained before/after
and store comparisons. Other schemas remain searchable, downloadable original
tables. A new useful shape can get a small adapter in
[results.py](../tools/results.py); no universal result schema is required.

Units and raw repetitions survive normalization. Cross-run comparison matches
case, input label and unit; these alone do not establish equivalent conditions.
The UI calls out differing workload counters and exposes both contexts. Hash
checks establish agreement with retained manifests, not experimental validity.

Keep short interpretations and decisions beside the investigation. Tables and
plots can come from this viewer instead of being maintained again in Markdown.
The authored UI is three dependency-free files here; generated data stays out
of Git. Check adapters and offline export handling with
`python3 workbench/tools/check.py results` (also needs Node).
