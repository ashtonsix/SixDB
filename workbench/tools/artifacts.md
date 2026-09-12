# Keeping and recovering experiment evidence

Git should explain what we learned and let us revisit useful comparisons.
Retain authored code, findings, small fixtures, selected measurements and their
provenance. Compact CSV or JSON is a representation, not a reason to keep a run.
The full bundle can preserve the experimental record without becoming source.

## Choose what earns a place

| Evidence | A useful default |
| --- | --- |
| Measurements behind a finding or recurring comparison | Keep the relevant case families, competitors, repetitions, counters and context together |
| A negative result, counterexample or before/after observation | Keep its distinct evidence; a later successful run may not replace it |
| A superseded broad sweep or routine successful check | Leave it local, or retain an S3 reference if recovery has value |
| Physical-control dumps, full disassembly, logs, binaries and source snapshots | Keep in the full bundle; select small witnesses that explain a finding |
| An existing dataset or unchanged evidence used again | Reference its existing home |

Select by the question, not by which cases win. For example, a layout-ranking
claim needs costs for every candidate considered, including rejected candidates
and actual workload weights. A preparation-cost comparison needs setup timings
as well as steady-state timings. A smaller table that loses these inputs changes
the claim. Some complete sweeps are worth retaining.

Give the selection a short explanation beside the finding or in the runner;
no separate retention report is needed. Revisit it when a study closes or its
question changes. Link old evidence instead of copying it into each follow-up.

## Make selection repeatable

A runner can name its offline inputs with `run.compact(files, regenerate_command)`.
Use an explicit list of useful outputs; a glob of every CSV and JSON will grow
as diagnostics accumulate. There is no required results schema.

For ordinary Google Benchmark data, the existing summary helper selects whole
name families and preserves repetitions in each case row. For example, inside a
runner that emitted `baseline.json` and `candidate.json`, before `run.finish()`:

```python
run.step("comparison", ["python3", str(run.source_root / "workbench/tools/evidence.py"),
    "summarize", f"baseline={run.output / 'baseline.json'}",
    f"candidate={run.output / 'candidate.json'}", "--filter", "^(read|update)/",
    "--counter", "encoded_bytes", "--output", str(run.output / "comparison")])
run.compact(["comparison/cases.csv", "comparison/provenance.json"], [])
```

Here both implementations and all repetitions for those families survive. The
selected counter is illustrative: keep what the comparison actually uses.
`[]` means there is no offline report command; otherwise record one using
`{evidence}` for the exported directory. The [aggregate runner](../spikes/aggregate-maintenance/run.py)
also illustrates raw iteration records with separate logical accounting.

## Retain a selected run

Commands run from the repository root on Linux with Python 3.10+ and AWS CLI v2;
prefix with `orb -m ubuntu` from the Mac. Use the directory printed by the runner:

```sh
python3 workbench/tools/artifacts.py retain build/experiments/STUDY/RUN \
  workbench/spikes/STUDY/evidence/NAME
```

The runner's recipe supplies the selection. Without one, the legacy timing
format retains all iteration rows plus accounting and summaries. Override the
file list when useful with repeated `--file PATH`, and the offline report with
`--regenerate 'python3 path/to/report.py {evidence}'`.

Retention automatically previews largest files, named CSV rows and their path
prefixes, repeated filenames across directories, identical bytes and ignore
rules. These describe contents, not scientific adequacy or compressed Git size.
Replace `retain` with `preview` to inspect without uploading or writing. Neither
uses a size threshold. Ignored export members are refused; choose a `.txt` name
for a selected diagnostic or a scoped ignore exception.

Retention checks the completed run and verifies its complete archive before
writing selected bytes with `provenance.json` and an `artifact.json` reference.
For collected worker studies it reuses the existing S3 archive, recording the
study's subdirectory; omitted local compiler output need not be fetched or
uploaded again. Other runs are bundled, uploaded and download-verified.
Failed retention leaves the source intact;
identical exports are safe to retry. Scoped `.gitattributes` protects hashed
line endings. For a generic or failed run, `artifacts.py put DIRECTORY REF.json`
keeps a bundle and reference without making an offline evidence export.

To check what a commit actually retains, use
`artifacts.py verify workbench/spikes/STUDY --staged` or `--tree HEAD`.
This checks manifests and hashes against Git blobs, not remote availability.
Study analyzers can call `verify_compact()` before reading their inputs.

## Recover or reduce retained evidence

```sh
python3 workbench/tools/artifacts.py fetch path/to/evidence-or-artifact.json \
  build/recovered/NAME
```

Fetch verifies the bundle hash, safe archive members and the original run receipt.
With the current helper, subdirectory references recover that study directly
into the requested directory.
Repeat `--file EXACT/MEMBER` to expand only selected files; the compressed bundle
is still downloaded and verified, but whole-run and input-dependency restoration
are omitted. Full fetch restores shared prepared inputs used by `run.input()`.
Recover into `build/`, not the prepared-data cache. The measured source is in
`source.tar.gz`; toolchain binaries are not embedded.

When moving existing evidence out of Git, first verify recovery of the exact
members and hashes. Preserve the original measured identity and S3 reference.
Keep advertised offline reports usable, or replace their commands with a tested
recovery path: fetching raw files alone may not recreate the compact provenance
an analyzer expects. Keep selection logic beside the study; changing selection
is not another experiment. Curation before committing avoids later history
rewrites, which need coordination with other users of the branch.

For local worker copies, [storage management](storage.md) reclaims older archived
compiler output and keeps recovery receipts. Other ignored `build/` directories
can contain unretained prototypes: remove inactive copies deliberately after
verified retention, recording removed paths, hashes and their recovery reference.

Bundles use content-addressed, conditionally written objects under
`s3://calico-fleet-artifacts/sixdb/artifacts/sha256/` in `us-east-1`; single PUTs
are limited to 5 GB. [Datasets](../datasets/README.md) keep their existing objects,
including Calico inputs. No expiry applied under bucket rules checked on
2026-09-07; garbage collection remains open. This is not Object Lock or a
separate backup. [Workers](workers.md) collect automatically because their
machines are temporary; that does not make every collected job worth retaining
in Git.
