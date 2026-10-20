# Hashtable evidence and recovery

[Broad results](../ACCUMULATION.md) owns the integration and maintenance
comparison before the October 20 tag promotion. The later
[overflow evidence](../../../spikes/hashtable-swiss-probes/README.md#evidence-and-recovery)
owns the tag/width qualification and its recovery recipe. Start with these files
for the broad panel:

| Evidence | Contents |
| --- | --- |
| [Core scorecard](accumulation-promotion-core.csv) | 990 cells: complete reads, construction, writes, RMW and growth; ns, byte counters, MADs and samples for Ikea/Abseil |
| [Accumulation scorecard](accumulation-promotion-accumulation.csv) | 306 cells: preparation, fixed-backing fold, total time and destination backing |
| [Earlier controls](accumulation-promotion-before.csv) | 126 cells from the separately run historical four-control harness |
| [Maintenance receipt](accumulation-promotion.json) | Native/RMW job and S3 identities, source hashes, checks, codegen/profile preservation and source footprint |

The core scorecard applies the receipt's RMW-only supplement after the payload
mixer correction. Original raw rows remain in their job archive. Earlier reports
below keep their own captured source and harness; do not substitute their numbers
for the broad scorecard. [Metric definitions](../README.md#what-a-number-means)
distinguish throughput, preparation, byte counters and unmeasured latency.

## Original promotion

[Qualification](../QUALIFICATION.md) owns the promotion-cut performance account.
[Generation transfer](../ZEN-GENERATIONS.md) and [RMW](../RMW.md) own subsequent
experiments. Reports retain every measured cell; original raw rows, disassembly,
compiler/topology and source
receipts are recoverable from each archive reference. `jobs.json` records job IDs,
source capture digests and archive identities. No losing rows are discarded.

| CPU / ISA | Full paired screen | Final growth |
| --- | --- | --- |
| Zen 5 AVX-512 | [Cells](zen5-avx512-promotion.json), [archive](zen5-avx512-artifact.json) | [Cells](zen5-avx512-growth.json), [archive](zen5-avx512-growth-artifact.json) |
| Granite Rapids AVX-512 | [Cells](granite-avx512-promotion.json), [archive](granite-avx512-artifact.json) | [Cells](granite-avx512-growth.json), [archive](granite-avx512-growth-artifact.json) |
| Neoverse V2 NEON | [Cells](neoverse-neon-promotion.json), [archive](neoverse-neon-artifact.json) | [Cells](neoverse-neon-growth.json), [archive](neoverse-neon-growth-artifact.json) |
| Zen 5 AVX2 | [Cells](zen5-avx2-promotion.json), [archive](zen5-avx2-artifact.json) | [Cells](zen5-avx2-growth.json), [archive](zen5-avx2-growth-artifact.json) |
| Granite Rapids AVX2 | [Cells](granite-avx2-promotion.json), [archive](granite-avx2-artifact.json) | [Cells](granite-avx2-growth.json), [archive](granite-avx2-growth-artifact.json) |

The final AVX2 archives also retain the focused K30 set/50% miss reader
instantiation confirmation. Its complete summaries are
[Zen](zen5-avx2-reader-confirmation.json) and
[Granite](granite-avx2-reader-confirmation.json). It is diagnostic evidence;
it does not replace the original full-screen cell with a faster instantiation.

Local final checks: [19 sanitizer histories](local-sanitizers.txt),
[11 standalone headers](local-headers.txt). [Header identities](final-source-identity.json)
show that the growth-mask repair and internal include relocation are the only
executable differences after the full screen. Final growth-capture header bytes
were the promotion-cut bytes; the later RMW additions and synchronous batch
retention repair are described in the follow-up report. Exact pre-promotion
controls, their original hashes and the old runners are recovered through
[the maintenance archive receipt](../archive.json); the former live `reference`
tree has been removed.

## Recover a run or its historical tools

Recover a native bundle into ignored storage, using the repository tool:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/benchmarks/hashtable/evidence/zen5-avx512-artifact.json \
  build/recovered/hashtable-promotion-zen5
```

For a maintained run, use the active `native-results.py` described in the
[suite guide](../README.md#run-and-audit-the-complete-suite). Historical
`promotion-results.py` and `rmw-results.py` are archived, not live commands in
this checkout. The local archive named by `../archive.json` restores their
original repository-relative paths:

```sh
orb -m ubuntu mkdir -p build/recovered/hashtable-tools
orb -m ubuntu tar -xzf \
  /home/ashtonsix/sixdb-archive/hashtable-maintenance-20261017/reference-and-promotion.tar.gz \
  -C build/recovered/hashtable-tools
```

Verify the archive SHA-256 against the receipt before extracting a different
copy. From the recovered tree, the promotion auditor accepts
`RESULTS --source CAPTURE --output SUMMARY.json`, or `--growth-only` for the
600-operation confirmations. `jobs.json` identifies each source capture;
native bundles retain submitted sources and runtime receipts. A recovered
auditor requires its matching historical inputs, not the maintained runner's
new file grid.

## October 17 RMW and generation follow-ups

[Generation cells](zen-generations.json), [code hashes](generation-codegen.json),
[RMW and preservation cells](rmw.json), [jobs and captures](followup-jobs.json).
The reports link all five raw archive references. Final local checks retain
[20 sanitizer histories](rmw-local-sanitizers.txt),
[standalone headers](rmw-local-headers.txt), and
[six curated frontier checks](frontier-local-sanitizers.txt).
The [RMW code excerpt](rmw-primary-codegen.txt) shows direct primary value addressing. The
[fallible-storage failure before repair](batch-retention-before.txt),
[one-header source delta](followup-source-identity.json), and
[unchanged complete Neoverse code](batch-retention-codegen.json) account for the
final mutable-binding lifetime correction.

Superseded research is recoverable through the
[archive receipt](../../../spikes/hashtable/archive.json); its verified Linux
destination is ~/sixdb-archive/hashtable-20261017. The current frontier retains
only the owning-multimap sources and selected full evidence panels.
