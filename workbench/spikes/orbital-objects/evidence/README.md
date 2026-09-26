# Initial object probe evidence

2026-09-26. [manifest.json](manifest.json) records source and executable SHA-256,
compiler/kernel context and the hashes of these three retained outputs:

- [probe.json](probe.json): 24 tiny installation histories, 90 mapping traces,
  48 copy/reconstruction count comparisons, eight exhaustive resource models and a
  coherence-ownership toy. Authored logical counts, no measured time.
- [capabilities.json](capabilities.json): Linux feature availability and a
  hardware read-only mapping check on the local aarch64 OrbStack guest.
- [views.jsonl](views.jsonl): four successful native one-page view tests,
  including actual UFFD missing, minor and write-protect/COW handling.

The native tests use Clang 21.1.8 and 4 KiB pages. This host is not the target
Zen 5 production environment. No platform settings changed; no EC2 resources
were provisioned. The exact bounded cases and limitations are described in the
[study](../README.md); none is a proof of the composed runtime or a latency
measurement. Binaries are rebuildable from the retained sources and the
documented build command; ignored build outputs need not be preserved.

Re-run the Python probe to compare its JSON with `probe.json`, whose own source
hash must match. Native output may legitimately differ on another kernel or
architecture. Unavailable optional features must remain explicit rather than
silently becoming a passing fault-path test.
