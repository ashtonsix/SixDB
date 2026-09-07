# Aggregate-delta validation

The [verified bundle reference](artifact.json) preserves the eleven original
validation files. Fetch it with `artifacts.py fetch REFERENCE build/recovered/NAME`.

Recorded results:

- ASan/UBSan: 146,880 exhaustive range/batch-boundary expectations across four
  methods in both accounting modes, plus 13 afterimage-oracle scenarios; passed.
- Build checks: isolation, shared objects, source/header/flag invalidation,
  compiler pin, release artifacts, and tuning/ISA separation; passed.
- Editor checks: additive activation/removal, flags/includes, inactive-study
  isolation, empty-database cleanup, and configure-only behavior; passed.
- clangd: benchmark checks before and after relocation; zero errors.
- Workflow: successful smoke receipts before and after relocation.
- Rejected-case receipt: the deliberate invalid selection failed as expected.

The bundle retains `aggregate-deltas-sanitize.*`, `check-build-spikes.log`,
`check-dev*.log`, `clangd-*.log`, `workflow-*.json`, and `rejected-case.json`.
These historical validations do not replace future checks after code changes.
