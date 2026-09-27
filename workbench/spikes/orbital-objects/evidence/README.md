# Native mapping evidence

September 26, 2026, local AArch64 OrbStack, Clang 21.1.8, 4 KiB pages:

- [capabilities.json](capabilities.json): feature availability and read-only protection.
- [views.jsonl](views.jsonl): four successful one-page native view tests, including
  actual UFFD missing/minor handling and write-protect COW.
- [manifest.json](manifest.json): unchanged source, binary and output hashes.

The manifest also identifies the archived Python toy and its `probe.json` output;
recover those from the [pre-curation checkpoint](../../../notebook/retired-spikes.md#recovery).
They are not missing native-test dependencies. The two C++ sources and CMake file
still match this receipt. Rebuild and run through the [probe guide](../README.md);
results can legitimately differ on another kernel or architecture. Missing optional
facilities must not silently become successful fault-path checks.

These are bounded mechanism observations, not proof of a composed runtime or
latency measurements. No platform settings changed and no EC2 resources were used.
