# Tools

Development and experiment scripts, including benchmark-instance provisioning
and S3 artifact storage.

[check_build.py](check_build.py) verifies incremental compilation and release
packaging using a disposable fixture. Run it with Python 3 on Linux, with the
pinned Clang, CMake, Ninja, matching LLVM objcopy/strip tools, and `readelf`.
It does not compile Calico or a database implementation.

SixDB will repurpose Calico's S3 bucket, `calico-fleet-artifacts`, and reuse its
existing datasets in place. Its [fleet tooling](../../../calico/tools/fleet/README.md)
is a reference for source snapshots, worker provisioning, remote recipes,
collection, and cleanup. SixDB's implementation should be fresh and draw on
those lessons. No SixDB provisioning command exists yet.

The bucket name comes from Calico's local registry; live AWS configuration has
not been checked in this scaffold. New run/source artifacts need a SixDB prefix
while dataset references should point to existing objects. Artifact-recording
process, worker settings, and implementation are still to develop.
