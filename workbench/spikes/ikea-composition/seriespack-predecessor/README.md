# Retired SeriesPack implementation and workloads

The first Ikea implementation and its recurring suite are preserved in Git.
The active module is [Ikea](../../../../ikea/README.md); its maintained workload
entry is [Workbench SeriesPack](../../../benchmarks/seriespack/README.md).
The replacement was developed in the [Ikea2 campaign](../ikea2-campaign/README.md).

## Recover the authored source

- Exact retiring module and suite checkpoint:
  `4ab2b64cc56b841e798da0088e24e04a216d9c48`.
- Complete pre-switchover checkout, including shared tools, sibling studies and
  the candidate/campaign: `08187281bb59f81d0b3b5fbcc0f9689ce7019c1d`.

From the repository root, create a separate checkout for the old implementation:

```sh
git worktree add --detach build/recovered/seriespack-predecessor \
  08187281bb59f81d0b3b5fbcc0f9689ce7019c1d
```

Use that checkout's pinned Linux toolchain and runners. For example, its
`workbench/benchmarks/seriespack/run.py --help` describes the retired suite.
The full checkpoint preserves authored source, tests, examples, documentation,
workloads and their supporting research context. A particular historical timing
may refer to an earlier source capture: recover its recorded artifact and exact
source/binary hashes instead of substituting this checkpoint.

## What remains in the working tree

[Workload definitions](workloads.md), [findings](findings/history.md) and the compact
`evidence/` projections retain the old suite's research value. Findings and
evidence moved here without changing their measured values, source identities,
original command strings or artifact references. Their historical relative source
paths refer to the recovered checkout. Original executables, full raw logs and
captures remain in the linked retained artifacts.

The old suite includes variable resident footprints, dependent-point access and
Calico controls beyond the replacement suite's current coverage. Their preservation
does not imply those workloads have been ported or measured on the new Ikea.

Two deliberately small references remain usable without recovery:

- The [independent scalar wire oracle](../../../../ikea/test/seriespack/reference/README.md)
  stays with active correctness tests, with checked provenance.
- One [isolated frozen comparator](../ikea2-campaign/reference/README.md) remains
  an optional benchmark control. It uses `ikea_predecessor` headers/namespace and
  campaign-owned adapters. Its rebuilt binary has a new identity; replay original
  artifacts when reproducing the old measurements.

Closed studies keep their question-based homes. Their overlays and relink tools
must use an explicitly recovered source/build satisfying their hash checks.
Obsolete live launch routes are retired rather than silently redirected to the
replacement API. The historical name `ikea2-campaign` is intentionally retained;
active consumers use `ikea::seriespack`, `<ikea/...>` and the Workbench suite.

## Switchover verification (2026-09-11)

The 45 replacement implementation headers/TUs differ from the preserved candidate
only in module identifiers and include paths. The scalar oracle keeps its original
bytes and private namespace; the isolated comparator verifies all 22 transformed
source hashes. Historical evidence receipts and numeric tables are unchanged.

The renamed NEON build passed 76 headless and 206 placed/composed/mutation formats,
ownership checks and all four executable guides. All 21 ordinary/author headers
compile independently on NEON, AVX2 and AVX-512; x86 compiled sources, examples and
comparison adapters also passed cross-compilation syntax checks. The maintained
quick suite and optional-comparison runner passed local pinned smoke runs. These
short runs validate the new routes, not a new performance comparison.

The updated analyzer reproduces 29 historical datasets / 8,446 comparison rows
unchanged and produces equivalent ratios with the new endpoint names. The main
checkout configures the module and optional suite through the standard selectors.
