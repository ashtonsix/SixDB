# Keeping and recovering experiment evidence

Git should explain findings and support useful comparisons. Keep authored code,
small fixtures, selected measurements and provenance. Full logs, disassembly,
binaries and source snapshots belong in the S3 bundle or ignored output.
Compact CSV/JSON is a representation, not a reason to retain a run.

Select by the question: keep relevant competitors, repetitions, counters and
context together, including negative results. A layout ranking needs rejected
candidates and workload weights; a preparation-cost comparison needs setup and
steady-state timings. A complete sweep can be the right selection. Superseded
sweeps and routine successful checks often need no Git export.

Explain the selection beside the finding or in the runner. Reuse existing
inputs and evidence by reference.

## Make selection repeatable

A runner can name offline inputs with `run.compact(files, regenerate_command)`.
An explicit list stays useful as diagnostics accumulate; there is no required
results schema. For Google Benchmark output, before `run.finish()`:

```python
run.step("comparison", ["python3", str(run.source_root / "workbench/tools/evidence.py"),
    "summarize", f"baseline={run.output / 'baseline.json'}",
    f"candidate={run.output / 'candidate.json'}", "--filter", "^(read|update)/",
    "--counter", "encoded_bytes", "--output", str(run.output / "comparison")])
run.compact(["comparison/cases.csv", "comparison/provenance.json"], [])
```

This keeps both implementations and all repetitions for the selected families.
Keep counters the comparison uses. `[]` means no offline report command; otherwise
record one using `{evidence}` for the report's working copy. The
[aggregate runner](../spikes/aggregate-maintenance/run.py) also illustrates separate
logical accounting.

## Retain a selected run

From the repository root on Linux with Python 3.10+ and AWS CLI v2 (prefix with
`orb -m ubuntu` from the Mac):

```sh
python3 workbench/tools/artifacts.py retain build/experiments/STUDY/RUN \
  workbench/spikes/STUDY/evidence/NAME
```

The recipe supplies the selection; the legacy timing format keeps iteration rows,
accounting and summaries. Override with repeated `--file PATH` and optionally
`--regenerate 'python3 path/to/report.py {evidence}'`. Plain script outputs need
an explicit selection, for example:

```sh
python3 workbench/tools/artifacts.py retain build/workers/JOB/results \
  workbench/spikes/STUDY/evidence/NAME --file samples.json --file worker-result.json
```

No `run.json` is needed. Provenance records selected bytes without inferring
success or source stability; the worker's status and host/source context remain
in its archive. Selected worker files must match that archive.

`retain` shows totals and the five largest files. Replace it with `preview` for
all files, CSV coverage, repeated names and identical bytes, without uploading.
Size is descriptive; ignored export members are refused so they cannot disappear
from a later commit. A selected diagnostic can use `.txt` or a scoped ignore rule.

Retention verifies any experiment receipt and the full archive, then installs selected
bytes with provenance and an `artifact.json` recovery reference. Worker studies
reuse their existing archive, including omitted local compiler output. Other runs
upload and download-verify a new bundle. Failed retention preserves the source;
identical exports are safe to retry. Scoped `.gitattributes` preserves hashed bytes.

`artifacts.py put DIRECTORY REF.json` archives a generic or failed run without
making a Git export. `artifacts.py verify STUDY --staged` (or `--tree HEAD`)
checks retained hashes against Git blobs. It does not contact S3.

## Recover or reduce retained evidence

```sh
python3 workbench/tools/artifacts.py fetch path/to/evidence-or-artifact.json \
  build/recovered/NAME
```

Choose a new destination, in ignored working space or on another volume. Fetch
verifies the archive and original run receipt, restores shared prepared inputs,
and recovers a subdirectory reference directly into that destination. Repeated
`--file EXACT/MEMBER` retrieves only those files and skips whole-run/input checks.
The compressed archive still downloads in full. Captured sources are in
`source.tar.gz`; toolchain binaries are not embedded.

To narrow an existing export, preview exact omissions, then repeat with `--apply`:

```sh
python3 workbench/tools/artifacts.py revise path/to/evidence \
  --drop blocks.csv --drop figure.svg
```

Apply recovers the named members and matches their hashes before removing local
copies and updating provenance. It preserves other files, measured identities and
the existing S3 reference; it neither uploads nor stages/commits. Only hashed,
byte-identical archived members can be removed this way. Derived files absent
from the archive need their own recoverable export first. Coordinate any later
history rewrite with branch users.

[Local worker storage](storage.md) reclaims older verified compiler output.
Other build directories can contain unretained prototypes; inspect them before
removing inactive copies.

Bundles are content-addressed under `s3://calico-fleet-artifacts/sixdb/artifacts/sha256/`
in `us-east-1`, with conditional writes and a 5 GB single-PUT limit. No bucket
expiry applied when checked on 2026-09-07; this is not Object Lock or a separate
backup. [Datasets](../datasets/README.md) reuse existing objects, including Calico's.

## Run a report recipe

```sh
python3 workbench/tools/artifacts.py report path/to/evidence build/reports/NAME
```

The recipe runs in fresh working space with the current checkout's scripts.
`--dry-run` shows commands and input source without fetching or executing.
By default inputs are copies of the hashed retained files. A recipe marked
`regenerate_from: archive` first fetches the existing `full_bundle`. The
[network cohorts](../spikes/cft-commit-latency/evidence.md#network-cohorts)
exercise both, including a report that recovers a second cohort.

Record a recipe during `retain`, or change one without uploading via `revise`:

```sh
python3 workbench/tools/artifacts.py revise path/to/evidence --apply \
  --regenerate-from archive \
  --regenerate 'python3 path/to/plot.py {evidence}'
```

Repeat `--regenerate` for ordered commands. `{evidence}` is the working input/output
directory; `{retained}` locates the original export, useful for another cohort's
reference. Commands are argv lists, run from the repository root without a shell;
`python3` steps use the interpreter invoking `artifacts.py`, including its venv.
Recipe commands should operate on working copies; scientific analysis and runtime
dependencies stay with their owning study. `report` executes the checkout's recipe,
never one discovered inside a downloaded archive.

The adjacent `NAME.report.json` records input identity, commands, entrypoint hashes
and completed steps. Failure preserves working files for inspection. This is a
report execution receipt, not a capture of all imports or an environment lock.
After revising a selection, run its report to check that the remaining inputs or
archive recipe suffice; byte recovery alone cannot establish that.
