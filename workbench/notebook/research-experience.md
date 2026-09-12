# Lessons from the first research loops

Retrospective on aggregate maintenance, trie remapping and regexp lowering,
2026-09-07–08. Current commands live in the [tools guide](../tools/README.md).

## What changed the tools

- **Small default runs.** A dirty-buffer confirmation spent about 140 seconds
  on 85 comparisons, including untimed initialization; a focused 30-comparison
  follow-up took about 30 seconds. Its [runner](../spikes/aggregate-maintenance/dirty-buffer/README.md)
  adopted a small default. Setup outside the timer still costs researcher time.
- **Editor and experiment separation.** Missing benchmark headers prompted a
  shared editor configuration. Refreshing it during experiments then tripped on
  unfinished live edits. Editor setup now has its own [command](../tools/editors.md).
- **Captured incremental builds.** Freezing measured sources allowed continued
  editing. Validation showed live header edits could leave a captured run intact
  and documentation edits could avoid rebuilding its TUs.
- **Selective retention.** The first closeout staged about 42,000 added lines:
  roughly 31,000 benchmark JSON and 3,000 build caches/databases. Full bundles
  moved to S3; selected evidence stayed in Git. Snapshots stopped recapturing it.
- **Shared inputs.** Regexp's ua-parser and accident preparation became reusable
  [datasets](../datasets/README.md). The adapter reproduced six historical hashes;
  a warm local call took 31 ms without downloading or parsing. Input references
  replaced copies in each bundle.

The [shared-tool validation](research-tools-20260908.json) retains measured
sources and the full regexp result. It reproduced counters and tables while
live editing continued and recovered prepared inputs without reparsing the CSV.

## What the tools did not establish

[Aggregate maintenance](../spikes/aggregate-maintenance/FINDINGS.md) exposed buffer
construction and filter costs after reducing ancestor updates.
[Trie remapping](../spikes/trie-remapping/FINDINGS.md) still moved rank-packed
columns despite fewer locator repairs. [Regexp lowering](../spikes/regexp-lowering/CONCLUSIONS.md)
could remove RE2 calls while leaving other work. Better controls changed all
three comparisons; receipts and reusable inputs only made that work easier.

One home per question replaced separate prototype and prior-art categories.
Code, reading and interpretation could develop together, while incomplete ideas
stayed in the [notebook](ideas.md). This was a useful arrangement for these loops,
not evidence of a universally better research process.
