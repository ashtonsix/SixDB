# Factored-filter evidence

Run `20260908T120746.015181Z`, complete with unchanged source. Source digest:
`3bc4b8e5233934be1aa82d37ca15bfaa8063ac0c732bd56f734cd5cb37499175`.
The prepared inputs are identical to the preceding raw study. Its 64-branch
baseline agrees query by query with this run.

- [Summary](summary.md): candidate counts and separate work categories.
- [Per-query counters](factor_summary.csv): every query, including exact routes.
- [Structure and training](structure.csv): representation and paid observations.
- [Examples](examples.csv): selected complete regexps and plans.
- [Correctness](correctness.json): differential, positional, sanitizer, and
  corpus counters. Detailed logs remain in the bundle.
- [Provenance](provenance.json): measured source, baseline identity, compact hashes.
- [Full bundle](artifact.json): S3 reference, SHA-256, size, and file count.

The full bundle was uploaded and downloaded with SHA-256 verification:
6,408,174 bytes, 53 files. It includes exact prepared strings/patterns, measured
source, executables, full plans, raw pair outcomes, compiler details, and logs.
Durations in the receipt are operational logs, not a timing comparison.

Regenerate the tables without downloading data or running RE2:

```sh
python3 workbench/spikes/regexp-lowering/factor_analyze.py workbench/spikes/regexp-lowering/evidence/factored-20260908
```

Recover the full bundle into a new ignored directory:

```sh
python3 workbench/tools/artifacts.py fetch workbench/spikes/regexp-lowering/evidence/factored-20260908/artifact.json build/recovered/regexp-factored
```

Use `orb -m ubuntu` before either command from macOS. The [method](../../factored-filters.md)
describes a fresh run; the [findings](../../FACTORED_FINDINGS.md) interpret these
counts and their limits. Neither file selects a compressed implementation.
