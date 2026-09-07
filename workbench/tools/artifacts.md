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
