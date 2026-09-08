# Keeping and recovering experiment evidence

Run experiments locally under ignored `build/`. Retain the runs that inform a
finding with one command, using the run directory printed by its runner:

```sh
python3 workbench/tools/artifacts.py retain \
  build/experiments/aggregate-maintenance/RUN \
  workbench/spikes/aggregate-maintenance/evidence/NAME
```

This checks the completed run's artifact hashes, packages the entire directory
including binaries and measured source, uploads to
`s3://calico-fleet-artifacts/sixdb/artifacts/sha256/`, downloads it, and verifies
its SHA-256. Only then does it install compact evidence in the chosen directory:

- `samples.csv`: every individual repetition and its counters, without the
  framework's duplicated names or derived statistical rows.
- `accounting.csv`, `summary.csv`, `summary.md`: counters and readable results.
- `provenance.json`: measured source identity, compiler/configuration, machine
  context, and hashes of the compact inputs.
- `artifact.json`: full bundle's S3 location, SHA-256, byte size, and file count.

The study's analyzer accepts this directory as well as the original raw run;
tables can be regenerated without S3 access. Keep findings and interpretation
beside the evidence. The run is still local if upload or verification fails;
rerun the same retention command to retry. Repeating a successful command with
identical inputs is safe. An existing, different evidence directory is refused.

## Counts and other evidence

The same command handles studies without Google Benchmark output. A runner may
call `run.compact(files, regenerate_command)` to name the files worth keeping
and record how to regenerate tables. Alternatively, select files at retention:

```sh
python3 workbench/tools/artifacts.py retain build/experiments/STUDY/RUN \
  workbench/spikes/STUDY/evidence/NAME --file counts.csv --file summary.md \
  --regenerate 'python3 workbench/spikes/STUDY/analyze.py {evidence}'
```

This preserves the selected bytes and writes hashes, source identity, available
compiler/configuration details, and the regeneration command. A scoped
`.gitattributes` prevents Git from changing already-hashed line endings. The
study chooses its counters, examples, and interpretation; there is no common
results schema. `verify_compact()` checks files before an analyzer uses them.
Google Benchmark extraction remains the default for existing timing studies.

## Shared inputs and captured sources

Runs using `run.input()` reference [cached prepared data](../datasets/README.md).
Retention publishes each input once before bundling the run's references; it
does not copy those bytes into every S3 run object. Fetch retrieves the input
objects, verifies their identities, and materializes them under the restored
run. Keep these dependency objects while retaining the runs that reference them.
Custom input directories embedded by older runners remain supported.

Recover the full bundle into a new ignored directory:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/aggregate-maintenance/evidence/NAME \
  build/recovered/NAME
```

Fetch verifies the downloaded SHA-256 before extracting, rejects unsafe archive
members, and checks the restored run against its original receipt. Its
`source.tar.gz` contains the measured files, which can differ from the Git
commit named by the receipt. Unpack source into a separate build/ directory and
use the recorded compiler and flags; toolchain/dependency binaries are not
included in the source archive.

The current runners build from captured sources in a per-study workspace.
Live checkout edits can continue during a run. The workspace has a stable path,
preserves unchanged source mtimes, and reuses Ninja's compiled objects. Runs
sharing that workspace serialize; separate studies have separate workspaces.
The lock covers the captured build and run, not the development checkout.
Refreshing the live editor configuration is best-effort; its failure is logged
but does not stop the captured experiment.
The receipt checks the captured sources and prepared input bytes actually used.
Direct incremental development builds and standalone prototype commands remain
available; this adds no required research stages.

For a validation or other non-benchmark bundle, use `artifacts.py put DIRECTORY
REFERENCE.json`. Fetch accepts that reference file directly. This preserves
failed runs and detailed validation logs when they matter without requiring a
benchmark result or inventing measurements.

Use Python 3.10+ and AWS CLI v2 with access to the existing bucket in
`us-east-1`. From the macOS workspace, prefix commands with `orb -m ubuntu`.
The scripts use the CLI's existing credentials. Content-addressed keys and
conditional writes avoid overwriting existing objects; integrity is checked
from downloaded bytes. No bucket policy, lifecycle, ACL, dataset, or Calico
object is changed. These objects have no expiry under the bucket rules checked
on 2026-09-07; this is not Object Lock or a separate backup service.

## What belongs where

Git holds code, questions, findings, small correctness fixtures, selected
samples/counters, and compact provenance. Full logs, caches, compilation
databases, source archives, binaries, profiles, traces, and broad raw sweeps
belong in local output or S3. Existing datasets remain referenced in place.
Source capture explicitly excludes `build/` and spike `evidence/` directories,
including tracked files, so evidence cannot recursively enter later snapshots.

This is a retention mechanism, not a requirement to upload every experiment.
The current uploader handles bundles below S3's single-PUT limit (5 GB);
larger datasets should stay referenced separately. Remote experiment execution
and retention/garbage-collection policy remain open.

`python3 workbench/tools/check_artifacts.py` checks failure/retry behavior,
compact-input integrity, safe restoration, and source exclusions offline.
