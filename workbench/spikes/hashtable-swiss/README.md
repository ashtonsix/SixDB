# Swiss overflow: tags, groups and native reads

One completed decision record for the three experiments behind Ikea's promoted
Swiss overflow behavior. Current contracts belong in
[Ikea usage](../../../ikea/docs/hashtable/usage.md) and
[rationale](../../../ikea/docs/hashtable/rationale.md); recurring peer comparisons
belong in the [Hashtable benchmark question map](../../benchmarks/hashtable/README.md#find-the-result-that-answers-your-question).
These chapters retain their own captured implementations and operating conditions.

## Three compatible decisions

| Question | Promoted choice | Alternatives and evidence |
| --- | --- | --- |
| Which fingerprints, continuation rule and density? | Eight-bit tags; retain tombstones and the 87.5% Swiss ceiling | [Tags and continuation](tags/README.md), [results](tags/RESULTS.md): filtered continuation and higher ceilings were not promoted |
| How wide is one logical probe group? | 16 slots | [Logical-group comparison](logical-groups/README.md), [results](logical-groups/RESULTS.md): actual 32/64-slot groups lost the complete comparison |
| How much metadata should an ISA read at once? | 64 bytes with AVX-512BW, 32 with AVX2, 16 otherwise | [Native read batches](read-batches/README.md), [promotion qualification](read-batches/GRADUATION.md): preserve 16-slot logical groups and their first-empty stopping rule |

Wider metadata reads do not mean wider logical groups. The early batch screen's
scalar recommendation was superseded by the later promotion; its
[rejected treatments](read-batches/RESULTS.md) remain available. None of these
studies keeps another live table owner or a production tuning option.

## Read the result for the operation

- **Whole-table reads, writes and peers:** the
  [batch qualification](read-batches/GRADUATION.md) retains Boost/Abseil,
  hot/cold and set/map controls, including losing cells.
- **Construction:** the [frozen-binary diagnosis](read-batches/GRADUATION.md#construction-anomaly-closed-with-frozen-binaries)
  measures whole-table initialization, reserve and insertion in ns/initial entry.
  Those numbers are not lookup timings or isolated overflow cost.
- **Density and continuation:** the [tag study](tags/RESULTS.md) keeps matched
  populations, mutation costs and the rejected denser variants.
- **Later read changes and memory budgets:** the
  [predictability qualification](../hashtable-primary-branchless/GRADUATION.md),
  [borrowed-inline promotion](../hashtable-wide-values/GRADUATION.md) and
  [memory-matched study](../hashtable-boost-adoption/MEMORY.md) answer subsequent
  questions. Their results do not retroactively replace these captured comparisons.

## Evidence, offline reports and recovery

Each chapter retains its original samples, compact reports, source identities
and S3 references. Offline auditors and summarizers keep separate schemas and
expected grids. Their code is unchanged; current report recipes point to the
new chapter paths. For example, on Linux (prefix with `orb -m ubuntu` on Mac):

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-swiss/read-batches/evidence/graduated \
  build/reports/swiss-read-batches
python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-swiss/logical-groups/evidence/zen5 \
  build/reports/swiss-logical-groups
```

The recipes recover their recorded full bundles and reproduce the existing
audits/summaries. They do not run new benchmarks. The tag chapter's
[recovery instructions](tags/README.md#recovery) cover its two density panels.

Original source snapshots and the chapter `archive.json` files retain their
historical study names and hashes. The [consolidation receipt](recovery.json)
pins the complete pre-merge Git trees. Measured evidence bytes and archive
references are unchanged; only current report recipe paths were retargeted.
