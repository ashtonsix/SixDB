# Retired investigations

September 27, 2026 curation. These investigations have successors; their complete
source, findings and retained evidence remain in Git at
`275698524c58dea08f19961dbc2940cae68b405b`. The working tree keeps useful conclusions
with their current owners instead of preserving parallel implementations.

| Retired path under `workbench/spikes/` | What survives, and where |
| --- | --- |
| `packed-integer-kernels/` | Width/ISA/grain lessons in [SeriesPack history](../spikes/ikea-composition/seriespack-history.md); current kernels in Ikea. |
| `seriespack-range-execution/` | Rejected driver/edge machinery and the uninstalled store candidate in the same history; recurring workloads in the [SeriesPack suite](../benchmarks/seriespack/README.md). |
| `seriespack-head-projection/` | Dense versus independently placed/small-head tradeoff in SeriesPack history and [layout analysis](../spikes/layout-analyser/prior-art.md). |
| `bec-packed-metadata/` | Whole-consumer versus refill lesson in SeriesPack history; prepared windows and further comparison in [Bec256 composition](../spikes/bec256-composition/README.md). |
| `ikea-composition/{native-regions,call-boundaries,seriespack-predecessor}/`, `seriespack-{assessment,decisions}.md` | SeriesPack history, current module contracts, replacement campaign and [executable placement](../spikes/executable-placement/README.md). Frozen controls actually used by current benchmarks remain live. |
| `orbital-simulator/` | [Native simulator](../simulator/README.md), [research lessons and unported questions](orbital-simulation.md#lessons-from-the-learning-spike). The hardware calibration script and result moved to [local handoff](../spikes/orbital-local-handoff/calibration.md). |
| `orbital-scenarios/` | [Transaction workload repertoire](transactions/README.md), counterexamples and recovery questions in [Orbital research questions](orbital-ideas.md). Old arbitration/MVTO runtimes and browser are archived. |
| `orbital-dataflow/` | [Distributed workload repertoire](dataflow-workloads.md), retained-state and progress questions in the mining guide. Four toy frameworks are archived; their unanswered questions are not declared solved. |
| `orbital-dissemination/` | The full [223-entry source survey](dissemination/README.md) remains browsable in the notebook. Specialized routing, transport and resource comparisons remain recoverable from the checkpoint. |
| `orbital-objects/{DESIGN,SCENARIOS,SOURCES}.md`, `probe.py`, `evidence/probe.json` | [Physical brief](orbital/reference/PHYSICAL.md) records the earlier scope. The [object/view study](../spikes/orbital-mvcc/history/objects.md) retains native mapping and composition evidence. |

Completed does not imply superseded. Aggregate maintenance, trie remapping,
regexp lowering and row-filter signatures still preserve distinct unanswered
questions. Tuple layout and layout analysis retain consumers and alternatives
beyond the implemented codecs. Hardware measurements remain independent evidence:
a simulator cannot replace CFT, memory or local-handoff measurements. The remaining
Orbital records retain scenarios and mechanisms the native models do not yet
cover. Their dated observations are research context, not current contracts.

## Earlier Orbital drafts

The former `orbital/stale-drafts/` is archived at commit
`1957910` rather than kept beside the live module. [BRIEF](orbital/reference/BRIEF.md)
and [PHYSICAL](orbital/reference/PHYSICAL.md) retain subsequent research accounts;
[research questions](orbital-ideas.md) and the [dissemination survey](dissemination/README.md)
retain the useful questions, counterexamples and source references.

| Archived source | Historical role |
| --- | --- |
| [BRIEF.md](https://github.com/ashtonsix/SixDB/blob/1957910/orbital/stale-drafts/BRIEF.md) | Early broad narrative: witness hierarchy, extensions, networking and durable memory. Useful memory, sandboxing and compression ideas remain in the research questions. |
| [BRIEF2.md](https://github.com/ashtonsix/SixDB/blob/1957910/orbital/stale-drafts/BRIEF2.md) | Retained-lock C1/C2 preparation, retries and working notes, including A→B→A discovery and shared analysis. |
| [CONSENSUS.md](https://github.com/ashtonsix/SixDB/blob/1957910/orbital/stale-drafts/CONSENSUS.md) | September 18–19 specification, before the contention redesign. Its model-building instructions and completion criteria are historical. |
| [BRIEF-arbitration.md](https://github.com/ashtonsix/SixDB/blob/1957910/orbital/stale-drafts/BRIEF-arbitration.md) | Component-owned protection and dynamic preparation: baseline for the retired scenario workbench. |

The names are not a version sequence. In particular, “BRIEF2” in the historical
contention comparison names a later compute/promote/renew candidate, not the
`BRIEF2.md` above. Both precede the studied fixed-position protocol. Old witness
hierarchies, emergency quorum reductions, epoch-wide publication gates and
expanding retry reservations are not inherited requirements.

The verified local copy is Linux `~/sixdb-archive/orbital-drafts-20260927/`, with
`orbital-drafts-1957910.tar.gz` and a hash receipt. The four drafts and their old
source-map README retain their exact bytes; Git supplies shared recovery.

## Orbital verification campaign accounts

The September 27 [architecture retrospective](https://github.com/ashtonsix/SixDB/blob/e40e87c/orbital/spec/ARCHITECTURE.md)
and [design-change account](https://github.com/ashtonsix/SixDB/blob/e40e87c/orbital/spec/DESIGN-CHANGES.md)
remain recoverable at `e40e87c`. Their design findings are consolidated in
[Orbital's earlier architecture proposal](orbital/reference/ARCHITECTURE.md); the family reports retain
specific model findings. The dated [baseline](../../orbital/spec/evidence/baseline-before-design/README.md)
and [design selection](../../orbital/spec/evidence/design-revision/README.md) preserve
exact evidence. [RESULTS](../../orbital/spec/RESULTS.md) owns current coverage.
These two retired narratives are history, not additional current specifications.

## Hashtable consolidation — October 15, 2026

The separated-control/transposed-payload experiment is closed and archived.
[Findings and verified recovery references](retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
preserve its hot, cold and allocation comparisons. That consolidation retained
one top-level hashtable spike; the October 17 curation below supersedes that tree.
Its archives, rather than the September checkpoint below, recover the original
studies and paths.

## Hashtable promotion and curation — October 17, 2026

The old hierarchy (`workbench/spikes/hashtable` before this curation), the
Iceberg–Swiss design, and the mutable-write architecture are archived together
at Linux `~/sixdb-archive/hashtable-20261017/`. The archive also retains superseded
hashtable-specific grant and Loom heat/independence probes. Every one of 10,575
members was checked against its live SHA-256 before removal: 521,483,877 original
bytes, compressed to 65,787,987 bytes. Research archive SHA-256:
`4fe72a76551d0146fbed30c61200f5f5d376819c79d416f71410aea53e8bd66f`.
The directory contains the complete file/hash receipt and these verified archives:

| Archive | Recovery scope |
| --- | --- |
| `old-and-promoted-designs.tar.gz` | Retired studies, source and evidence at their original paths. |
| `promoted-runnable-source.tar.gz` | Full project snapshot from that promotion, including sibling modules and tools. It predates the later mutable-accumulation promotion. |

The maintained [set/map usage](../../ikea/docs/hashtable/usage.md) and
[rationale](../../ikea/docs/hashtable/rationale.md) own the design and contracts.
The [benchmark suite](../benchmarks/hashtable/README.md) owns recurring Abseil
comparisons and promotion evidence; superseded controls are recovered from its
archive rather than kept as live implementations.
The [multimap frontier](../spikes/hashtable/README.md) retains its candidate,
executable dependencies and four captured comparison panels;
[K-way traversal](../spikes/hashtable-merge/README.md) remains separate research.
The [compact receipt](../spikes/hashtable/archive.json) and archived-comparison
hashes locate the earlier findings, including negative results. Historical paths
in immutable evidence resolve in the recovered snapshot, not necessarily in the
current checkout.

Recover the exact original tree into a fresh directory rather than extracting
over current sources:

```sh
mkdir -p ~/sixdb-archive/recovered-hashtable
tar -xzf ~/sixdb-archive/hashtable-20261017/old-and-promoted-designs.tar.gz \
  -C ~/sixdb-archive/recovered-hashtable
```

## Index directory spikes — October 2026

Two earlier directory studies are archived on Linux under `~/sixdb-archive/`.
[`engine::directory`](../../engine/README.md#segment-directory) and the
[segment-directory study](../spikes/segment-directory/README.md) succeed both.

- **`index-directories/`, removed 21 October** with its Engine directory
  implementation and API-bound drivers: `index-directories-20261021/`. The
  archive holds the complete `f021d381` source checkpoint and an exact
  pre-deletion overlay with the uncommitted audit. All 14,611 base Git blob
  members and 187 overlay members were verified before removal; `receipt.json`
  records archive hashes and retained oracle identities.
- **`directory/`, removed 22 October**: `directory-spike-20261022/`. This was
  the complete-consumer experiment that followed: layer-specific routing,
  sparse and duplicate traversal, packed Segments, publication, native LMDB
  peers, and memory and drain evidence. It was never committed and linked
  against the since-deleted `engine::material` adapter. `directory.tar.gz`
  holds all 4,272 files, `manifest.sha256` per-file hashes (all verified after
  extraction before removal), and `base-commit.txt` the matching SixDB commit.
  Notebook links to its documents name paths inside that archive; its evidence
  READMEs reference further S3 archives of frozen sources, ELFs and raw results.

```sh
mkdir -p ~/sixdb-archive/recovered-directory
tar -xzf ~/sixdb-archive/directory-spike-20261022/directory.tar.gz \
  -C ~/sixdb-archive/recovered-directory
```

## Object prototypes and first formal suite — October 21, 2026

Four superseded source trees were removed from the live spike catalog:
`orbital-selected-access`, `orbital-representation-conversion`,
`orbital-stage2-composition` and `orbital-formal-first-pass`.
Every one of their 133 tracked files (470,720 bytes) matched Git commit
`35e63b8416d377f216575d6f8d97668b9bce9ab3` byte for byte before removal.
No unique untracked material was removed. The
[compact recovery receipt](../spikes/orbital-mvcc/history/recovery.json)
records the four original Git tree IDs; the full checkpoint recovers sources,
run instructions, selected receipts and sibling modules together.

The [Object history guide](../spikes/orbital-mvcc/history/README.md) keeps the
three native studies' findings, lifetime falsifiers and diagnostic costs.
Their retained consequences now run in the
[production integration checks](../benchmarks/retired-drivers.md#current-integration-checks).
The [first formal findings](orbital/reference/formal-first-pass.md) preserve
counterexamples, graph growth and the original 80-case classification; current
models and coverage belong in [Orbital specifications](../../orbital/spec/README.md).
The old leaf suite is not a second verification plan.

Links from retained findings to deleted source/evidence are pinned to that
checkpoint. Original measured bundles still identify their own captured bytes.
Recover into a separate directory, not over current modules.

## Object/view retirement and Swiss consolidation — October 22, 2026

The earlier `orbital-objects/` composition is retired. All 34 tracked files
matched `35e63b8416d377f216575d6f8d97668b9bce9ab3` byte for byte before removal.
Its [findings](../spikes/orbital-mvcc/history/objects.md) preserve physical-use,
padding/stability, stale-rebase and cached-mapping lessons, plus exact recovery
of the standalone UFFD and capability probes. The [Object history receipt](../spikes/orbital-mvcc/history/recovery.json)
records its original tree. Production consequence checks remain with Orbital
integration; the old prototype did not establish durable commit or recovery.

`hashtable-overflow`, `hashtable-swiss-probes` and `hashtable-swiss-batch` are
now chapters of [Swiss overflow decisions](../spikes/hashtable-swiss/README.md).
The [merge receipt](../spikes/hashtable-swiss/recovery.json) pins their original
Git trees at `1b9866b717a3911ae826ca16389d1257a56e38cd`. All 297 tracked files were verified before moving.
Measured evidence and original archive references retain their exact bytes;
current offline report recipes point to relocated, unchanged scripts.
Eight-bit tags, 16-slot logical groups and 32/64-byte ISA reads remain distinct
decisions, with their losing controls and source epochs preserved.

## Paused Frame adapters (26 October 2026)

Three unpublished extensions were paused with the phase-2 shared-image direction
and later superseded. Ikea LEAD confirmed they have no active dependency:

- Phase-1 scan pairs: complete predicate(A) → SUM/COUNT(B), with staged/fused,
  native and Polars controls.
- Phase-2 cells' optional Calico SUM adapter: actual pinned controls in the same
  runner, with private interpretation owners and separate conditioning/retention
  accounts. Its two native scripts failed before timing.
- Phase-2 retained-memory ledger: physical unions of object/vector capacities
  and old/current views, never integrated into the driver.

The [recovery archive](../spikes/frame-phase-2/paused-adapters.json) preserves all
18 exact working files, ten committed originals, their patch, per-file hashes,
and selected old checks/results and recovery pointers. Publication and an
independent full fetch verified every member before removing the inactive copies.
Those old receipts do not qualify the final unpublished sources. Committed
controls and their existing findings retain their own source/evidence identities;
current Frame use starts with [Ikea](../../ikea/docs/frame/usage.md).

Recover into a fresh ignored directory:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/frame-phase-2/paused-adapters.json \
  build/recovered/paused-frame-adapters
```

Read `README.md` and `state.json` there before reproducing an old experiment.

## Recovery

Browse the [complete checkpoint](https://github.com/ashtonsix/SixDB/tree/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes)
or recover source from Git. For one file, from the repository root:

```sh
git show 275698524c58dea08f19961dbc2940cae68b405b:workbench/spikes/orbital-simulator/GAUNTLET.md
```

For a runnable snapshot with sibling modules and tools, extract the complete
checkpoint into a fresh directory; historical runners assume repository-relative
paths. Use the pinned Linux toolchain and the original artifact reference when
reproducing a particular measured binary. Full worker outputs remain in their
existing S3 archives; this curation changes no artifact bytes or S3 objects.

A convenience copy is in the Linux `~/sixdb-archive/spikes-20260927/`:
`sixdb-2756985.tar.gz`, with `receipt.json`. Every archive member was checked
against its Git blob, and every removed tracked file against that checkpoint.
The receipt records the archive SHA-256 and retired paths. This local copy is
optional; the pinned Git checkpoint is the shared recovery source.

Historical citations in surviving documents use commit-pinned links. Retained
evidence keeps its original bytes and relative paths, which resolve within the
recovered source snapshot. New investigations should use the owning live guide
or start from the question, rather than revive an obsolete launcher by accident.

## Boundlist retirement — October 22, 2026

[Intervals](../../ikea/docs/intervals/usage.md) succeeds common borrowed ordered
search/geometry; [StreamPack](../../ikea/docs/streampack/usage.md) owns compact
image, admission, bitmap and retirement/value behavior. Public Boundlist headers,
targets, docs and examples are removed. Useful literal-cut/domain tests run under
Intervals; compact-image/edit checks run under StreamPack. Teaching diagrams and
the packed composition example were rewritten into the maintained Intervals guide.

The [historical study](../spikes/boundlist/README.md) keeps findings, losing
controls, offline reports and measured cohort references. Its [recovery receipt](../spikes/boundlist/recovery.json)
pins the complete pre-retirement snapshot at
`719b0f5709c9190bd592888cf8fbbbd379bce960`, including original Git trees. All
80 retired documentation/experimental source files (547,413 bytes) matched that
checkpoint before removal. Original measured source epochs remain in their own
S3 bundles, unchanged. Recover a full snapshot in a fresh directory for old native
runners; the maintained tree no longer configures `SIXDB_SPIKES=boundlist`.
