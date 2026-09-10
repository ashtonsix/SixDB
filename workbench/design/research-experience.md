# Lessons from the first research loops

Retrospective on aggregate maintenance, trie remapping, and regexp lowering,
2026-09-07–08. These observations explain the tooling's origins. Current commands
live in the [tools index](../tools/README.md); study results stay with their spikes.

## Friction that changed the tools

- **Small defaults made follow-ups cheap.** The dirty-buffer confirmation spent
  about 140 seconds on 85 comparisons, including repeated untimed initialization.
  A focused 30-comparison follow-up took about 30 seconds. Its runner now starts
  with a small selection. Setup outside the timer still costs researcher time.
  See the [probe](../spikes/aggregate-maintenance/dirty-buffer/README.md).
- **Editor setup needed one stable home.** The first probe's benchmark headers
  were missing from clangd because it read an empty dev configuration. A personal
  override fixed one file but would have repeated the chore for each study.
  [dev.py](../tools/dev.py) now preserves explicitly active studies in one database.
  Later, incomplete live edits showed that editor refresh must not block a run.
- **Source capture had to permit editing.** Initially, changing live sources
  prevented a successful receipt. Captured workspaces isolate measured code while
  preserving incremental compilation. In the shared-tool validation, live header
  edits did not affect execution and documentation edits did not trigger rebuilds.
- **Retention needed selection.** The first closeout staged about 42,000 added
  lines, including 31,000 of benchmark JSON and 3,000 of build caches/databases.
  Snapshots also recaptured older evidence. Full bundles moved to S3; Git keeps
  selected repetitions, counters, findings, and references. Snapshot exclusions
  prevent recursive capture. Broad screens need not produce broad Git exports.
- **Inputs outlived the question that first used them.** Regexp's ua-parser and
  accident preparation became [shared datasets](../datasets/README.md), with
  source records separate from study adapters. The factored adapter reproduced
  six historical input hashes; a warm call took 31 ms locally, without parsing
  or downloading. Retained runs reference prepared objects instead of copying
  them into every bundle. Counts also gained the same retention support as timings.

The [shared-tool validation](research-tools-20260908.json) retains measured source
and the full regexp result. Counters, examples, and tables reproduced byte-for-byte
while live editing continued; cold-cache recovery restored prepared inputs without
reparsing the source CSV. Feedback from the regexp task prompted public-source
fallback when S3 is unavailable and inclusion of preparation metadata in identity.

## Comparisons that needed repair

Hidden work and weak controls repeatedly changed the next experiment:

- [Aggregate maintenance](../spikes/aggregate-maintenance/FINDINGS.md) reduced
  ancestor adjustments but exposed construction cost. A dirty-buffer no-filter
  control then asked which clean reads repay filter maintenance. Separating reset
  from replay and inspecting cache-line placement changed the useful comparison.
- [Trie remapping](../spikes/trie-remapping/FINDINGS.md) saved locator repairs while
  rank-packed columns still moved. Credible controls needed bulk shifts, growth
  slack, and misses drawn from the same prefix domains as inserted keys. A small
  append-aware gap experiment separated a spacing policy from a layout limit.
- [Regexp lowering](../spikes/regexp-lowering/CONCLUSIONS.md) could remove RE2 calls
  while leaving other work. Shared datasets and receipts made follow-ups cheaper;
  they did not establish the value of the predicate strategy.

Failed case selections, an analyzer's mishandling of benchmark name suffixes,
and an omitted method registration also showed why useful receipts preserve
failures and why the actual comparison list deserves inspection. The studies'
findings contain the detailed corrections and limits.

## Keeping an investigation readable

One home per question replaced separate prototype and prior-art categories.
Reading, code, evidence, and interpretation can then develop together; incomplete
thoughts have the [notebook](../notebook/ideas.md). Calico's
[bytepack](../../../calico/workbench/prototypes/bytepack/README.md) and
[three-array](../../../calico/workbench/prototypes/three-array/README.md) were
useful examples, without becoming templates.

Reading behind this choice: [Oudeyer on lab notebooks](https://pyoudeyer.com/openLabNotebooks22.pdf)
for tentative ideas and failures; [Hamming](https://www.cs.jhu.edu/~kevinduh/projects/researchtips/hamming.pdf)
for revisiting unresolved questions; [Boettiger](https://www.carlboettiger.info/2012/09/28/Welcome-to-my-lab-notebook.html)
for linking loose notes to coherent project work; and
[Matuschak](https://notes.andymatuschak.org/A_writing_inbox_for_transient_and_incomplete_notes)
for revisiting incomplete notes. These are accounts of research practice, not
proof that a filing scheme improves research.
