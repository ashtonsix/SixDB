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
| `orbital-objects/{DESIGN,SCENARIOS,SOURCES}.md`, `probe.py`, `evidence/probe.json` | [Physical brief](../../orbital/PHYSICAL.md) owns provisional scope. The smaller [Linux mapping probe](../spikes/orbital-objects/README.md) retains real UFFD/COW evidence and executable sources. |

Completed does not imply superseded. Aggregate maintenance, trie remapping,
regexp lowering and row-filter signatures still preserve distinct unanswered
questions. Tuple layout and layout analysis retain consumers and alternatives
beyond the implemented codecs. Hardware measurements remain independent evidence:
a simulator cannot replace CFT, memory or local-handoff measurements. The remaining
Orbital records retain scenarios and mechanisms the native models do not yet
cover. Their dated observations are research context, not current contracts.

## Earlier Orbital drafts

The former `orbital/stale-drafts/` is archived at commit
`1957910` rather than kept beside the live module. [BRIEF](../../orbital/BRIEF.md)
and [PHYSICAL](../../orbital/PHYSICAL.md) own current direction;
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
`BRIEF2.md` above. Neither is the current fixed-position design. Old witness
hierarchies, emergency quorum reductions, epoch-wide publication gates and
expanding retry reservations are not inherited requirements.

The verified local copy is Linux `~/sixdb-archive/orbital-drafts-20260927/`, with
`orbital-drafts-1957910.tar.gz` and a hash receipt. The four drafts and their old
source-map README retain their exact bytes; Git supplies shared recovery.

## Orbital verification campaign accounts

The September 27 [architecture retrospective](https://github.com/ashtonsix/SixDB/blob/e40e87c/orbital/spec/ARCHITECTURE.md)
and [design-change account](https://github.com/ashtonsix/SixDB/blob/e40e87c/orbital/spec/DESIGN-CHANGES.md)
remain recoverable at `e40e87c`. Their current design substance is consolidated in
[Orbital's architecture](../../orbital/ARCHITECTURE.md); the family reports retain
specific model findings. The dated [baseline](../../orbital/spec/evidence/baseline-before-design/README.md)
and [design selection](../../orbital/spec/evidence/design-revision/README.md) preserve
exact evidence. [RESULTS](../../orbital/spec/RESULTS.md) owns current coverage.
These two retired narratives are history, not additional current specifications.

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
