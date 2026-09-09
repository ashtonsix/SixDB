# Tools

Development and experiment scripts, including benchmark-instance provisioning
and S3 artifact storage.

[worker.py](worker.py) runs a repository script on one temporary Spot or
On-Demand EC2 worker, uploads its results, and terminates it. Start with the
[worker guide](workers.md) for one-command runs, machine/environment overrides,
and detached recovery. [check_worker.py](check_worker.py) tests its lifecycle offline.

[check_build.py](check_build.py) verifies incremental compilation and release
packaging using a disposable fixture. Run it with Python 3 on Linux, with the
pinned Clang, CMake, Ninja, matching LLVM objcopy/strip tools, and `readelf`.
It does not compile Calico or a database implementation.

[experiment.py](experiment.py) supplies a small local-run receipt helper:
source snapshots excluding evidence, logged commands, hashes, and success/failure recording.
The [aggregate delta runner](../spikes/aggregate-maintenance/run.py) is an example.
Study-specific execution and analysis remain in the study. The worker command
can run these scripts remotely and collect their output.

Its optional `workspace` argument builds from captured sources while the live
checkout remains editable. Stable workspace paths preserve incremental builds.
[check_experiment.py](check_experiment.py) verifies source isolation and Ninja
reuse with an actual build. `input()` records a reusable prepared dependency;
`compact()` declares study-selected evidence for later retention.

[artifacts.py](artifacts.py) retains a selected run with one command: upload
and verify its full bundle, then write compact evidence into the spike.
It also fetches and verifies bundles. See the [retention and recovery commands](artifacts.md).
[evidence.py](evidence.py) supplies compact samples and their reader;
[check_artifacts.py](check_artifacts.py) exercises retention failures and recovery offline.
Retention also supports arbitrary compact files and recorded regeneration
commands, with shared inputs restored automatically.

[datasets.py](datasets.py) resolves pinned source data and caches prepared
variants independently of any one spike. Its [catalog and examples](../datasets/README.md)
start with ua-parser and accident descriptions. [check_datasets.py](check_datasets.py)
checks cache reuse, concurrent callers, preparation retry, and source hashes.

[dev.py](dev.py) maintains the stable dev compilation database for explicitly
active spikes. It preserves existing selections, supports `--add`,
`--remove`, and `--list`, and configures without building study targets.
[check_dev.py](check_dev.py) checks that behavior with disposable targets and
an intentionally broken inactive study. See the
[first research-experience notes](../design/research-experience.md).

SixDB uses Calico's S3 bucket, `calico-fleet-artifacts`, and reuses its
existing datasets in place. Its [fleet tooling](../../../calico/tools/fleet/README.md)
is a reference for source snapshots, worker provisioning, remote recipes,
collection, and cleanup. The SixDB worker implementation draws on those lessons
with its own bootstrap, minimal scripts, and dedicated instance role/network group.

Live bucket access and lifecycle rules were checked on 2026-09-07. Run bundles
use the separate `sixdb/artifacts/sha256/` prefix in `us-east-1`; dataset
references point to existing objects. Worker submissions have separate
`sixdb/workers/` prefixes; their final bundles use the same artifact store.
