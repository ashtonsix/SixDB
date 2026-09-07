# Tools

Development and experiment scripts, including benchmark-instance provisioning
and S3 artifact storage.

[check_build.py](check_build.py) verifies incremental compilation and release
packaging using a disposable fixture. Run it with Python 3 on Linux, with the
pinned Clang, CMake, Ninja, matching LLVM objcopy/strip tools, and `readelf`.
It does not compile Calico or a database implementation.

[experiment.py](experiment.py) supplies a small local-run receipt helper:
source snapshots excluding evidence, logged commands, hashes, and success/failure recording.
The first caller is the [aggregate delta runner](../spikes/aggregate-maintenance/run.py).
Study-specific execution and analysis remain in the study. This does not yet
provide remote execution or an imposed research process.

[artifacts.py](artifacts.py) retains a selected run with one command: upload
and verify its full bundle, then write compact evidence into the spike.
It also fetches and verifies bundles. See the [retention and recovery commands](artifacts.md).
[evidence.py](evidence.py) supplies compact samples and their reader;
[check_artifacts.py](check_artifacts.py) exercises retention failures and recovery offline.

[dev.py](dev.py) maintains the stable dev compilation database for explicitly
active spikes. It preserves existing selections, supports `--add`,
`--remove`, and `--list`, and configures without building study targets.
[check_dev.py](check_dev.py) checks that behavior with disposable targets and
an intentionally broken inactive study. See the
[first research-experience notes](../design/research-experience.md).

SixDB uses Calico's S3 bucket, `calico-fleet-artifacts`, and reuses its
existing datasets in place. Its [fleet tooling](../../../calico/tools/fleet/README.md)
is a reference for source snapshots, worker provisioning, remote recipes,
collection, and cleanup. SixDB's implementation should be fresh and draw on
those lessons. No SixDB provisioning command exists yet.

Live bucket access and lifecycle rules were checked on 2026-09-07. Run bundles
use the separate `sixdb/artifacts/sha256/` prefix in `us-east-1`; dataset
references point to existing objects. Worker provisioning remains to develop.
