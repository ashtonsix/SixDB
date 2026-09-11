# Keeping and recovering experiment evidence

Git holds code, questions, findings, small correctness fixtures, selected
samples/counters, and compact provenance. Full logs, caches, binaries, source
archives, profiles, traces, and broad raw sweeps belong in ignored output or S3.
Keep useful repetitions in compact evidence. For code-generation studies,
retain the measured binary and its source/flags in the bundle, with selected
assembly explaining a finding. Full disassembly expands cheaply from that binary;
duplicating it for every check executable and archive can dominate disk use.
Link existing evidence when a follow-up reuses it. Retain the runs worth returning to.

Commands run from the repository root on Linux, with Python 3.10+ and AWS CLI v2
using existing credentials. Prefix with `orb -m ubuntu` from the Mac. Bundles
use `s3://calico-fleet-artifacts/sixdb/artifacts/sha256/` in `us-east-1`;
existing Calico datasets stay referenced in place.

## Retain a run

Use the directory printed by the study's runner:

```sh
python3 workbench/tools/artifacts.py retain \
  build/experiments/aggregate-maintenance/RUN \
  workbench/spikes/aggregate-maintenance/evidence/NAME
```

Retention checks the completed run's hashes, packages its full directory
(including binaries and measured source), uploads, downloads, and verifies it.
Only then does it write the compact export. The timing-study default includes:

- `samples.csv`: individual repetitions and counters, without duplicated framework rows.
- `accounting.csv`, `summary.csv`, `summary.md`: counters and readable results.
- `provenance.json`: source identity, compiler/configuration, machine context, and input hashes.
- `artifact.json`: full bundle's S3 location, SHA-256, size, and file count.

The analyzer can regenerate tables from this directory without S3. Failed
retention leaves the local run intact; repeat the command to retry. Identical
exports are safe to repeat; an existing different export is refused.

Replace `retain` with `preview` to see selected files, bytes, lines, and ignore
rules without writing or uploading. Retain also previews and refuses ignored
exports. Rename a selected diagnostic to `.txt` or use a scoped ignore exception.
Selection is the study's choice; there is no export-size threshold.

After staging, verify what Git will actually keep:

```sh
python3 workbench/tools/artifacts.py verify workbench/spikes/STUDY --staged
```

This checks manifests, members, and hashes against staged blobs; ignored or
unstaged files cannot fill gaps. Use `--tree HEAD` for a commit, or neither
option for local files. Verification does not download bundles.

## Counts and other evidence

A runner can call `run.compact(files, regenerate_command)` or select files
at retention. There is no required results schema:

```sh
python3 workbench/tools/artifacts.py retain build/experiments/STUDY/RUN \
  workbench/spikes/STUDY/evidence/NAME --file counts.csv --file summary.md \
  --regenerate 'python3 workbench/spikes/STUDY/analyze.py {evidence}'
```

Selected bytes, hashes, source identity, and available compiler/configuration
are preserved. Scoped `.gitattributes` protects hashed line endings.
`verify_compact()` checks inputs before analysis. `--regenerate` records an
**offline report**, not another experiment; omit it if there is no analyzer.
It can also override the runner's command without changing file selection.
When reducing a historical export, preserve its measured source/run identity
and S3 reference; a new selection is not a new measurement.

For validation logs or another generic bundle, use
`artifacts.py put DIRECTORY REFERENCE.json`. It also accepts failed runs.

## Recover a run

Choose a new directory under this checkout's `build/`, such as
`build/recovered/NAME`. The CLI does not restore bundles into `SIXDB_DATA_CACHE`;
that cache is for prepared inputs.

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/aggregate-maintenance/evidence/NAME \
  build/recovered/NAME
```

Fetch also accepts a standalone reference file. It verifies the bundle hash,
rejects unsafe archive members, and checks the restored run's original receipt.
For a code-generation question, recover just the binary and needed context:

```sh
python3 workbench/tools/artifacts.py fetch path/to/artifact.json \
  build/recovered/diagnostic --file relative/path/to/binary --file host.json
```

Repeat `--file` for exact member paths. This still downloads and hash-verifies the
compressed bundle, but expands only those files and leaves input datasets alone.
It reports a partial recovery, not a complete experiment or whole-run validation.
Use the full fetch above when replaying a run with its dependencies.
`source.tar.gz` holds the measured files, which may differ from Git HEAD; use the
recorded compiler/flags when rebuilding in a separate ignored directory (see
[captured execution](README.md#captured-experiment-runs)). Toolchain binaries are not embedded.

Runs using `run.input()` reference [prepared datasets](../datasets/README.md).
Retention publishes each input once; a full fetch verifies and restores dependencies
under the recovered run. Keep input objects as long as retained runs reference
them. Older runners' embedded input directories remain supported.

## Local copies and storage limits

After verified retention, an inactive expanded run can be removed while keeping
its compact evidence and `artifact.json` outside that directory. For a partial
eviction, record removed paths, verified hashes and the recovery reference (for
example, `local-evictions.json`). Sources, live builds and unretained results
need different treatment: ignored `build/` directories can contain prototypes
as well as caches. Inactive package/editor downloads and compiler intermediates
are usually easier first targets.

Content-addressed keys and conditional writes avoid overwriting existing objects.
The uploader uses single PUTs, limited to 5 GB; reference larger datasets separately.
No expiry applied under bucket rules checked on 2026-09-07; retention and garbage
collection policy remain open. This is not Object Lock or a separate backup.
[Workers](workers.md) collect automatically because their machines are temporary;
local retention remains a choice.
