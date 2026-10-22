# Joint Frame investigation

Current collaboration map, 2026-10-22. The [question index](QUESTIONS.md) owns
the open questions; this page connects people, witnesses and active work. It
is not a staged roadmap or a chosen Frame interface.

## Shared purpose

Find representations and programs which materially reduce useful work for
Engine, and preserve that advantage through writes and changing workloads.
Encoding, decoding and compression receive the initial emphasis. The wider
operation families and the [review's architectural alternatives](REVIEW.md)
remain open while those experiments develop.

Ashton's coarse-selection requirement applies across all or most operation
families. The working shape is one bit per block, with a whole group skipped
when all its block bits are clear. Excluded regions avoid work and data movement;
fully selected groups can retain the unmasked fast path. Derive group emptiness
from the block selection without requiring a second caller-supplied mask.
Preserve that selection at its own granularity rather than expanding it into
row masks and traversing every row. Fine row selection,
record presence and NULL validity remain independent. No fixed group size or
public mask interface is selected by this requirement.

Ashton asks Ikea LEAD to drive the effort collaboratively with Claude- and
Codex-based peers. Responsibilities below are current working ownership, not a
ranking of capability or a partition of future public APIs. Challenge framing,
bring counterexamples, and revise the division as the work reveals strengths.

| Frontier | Current lead | Active contribution and connection |
| --- | --- | --- |
| Real-column encoding and compression | Claude, Ikea frame design | [Encoding inventory](encoding/README.md): existing corpora and controls, useful stages/kinds, complete bytes and analysis cost; supply representative images to Engine early |
| Representation algebra and structured intermediates | Claude, with Ikea/Engine | Masked and overridden reductions, weighted delta/checkpoint alternatives and dictionary/run results; use the same images and consumer witness as schema-fidelity work |
| Mutation and representation lifecycle | Ikea LEAD | [Lifecycle investigation](lifecycle/README.md): repeated versus dispersed writes, novelty, selective folding, thaw/reseal and retained-reader economics |
| Point reconstruction on encoded images | Ikea LEAD | [Point consumer](point/README.md): actual body readers, per-image checkpoints, retained readers and coarse masks; separate from geometry primitive redesign |
| Separated filter heads and predicate programs | Ikea LEAD, with Claude | [Head-plane consumer](filter/README.md): head8/head16, transform cuts with independent predicate meaning, direct packed comparisons and complete masked consumers |
| Schema fidelity and complete execution programs | Engine LEAD, jointly with Ikea/Claude | [Execution witness](execution/README.md): different programs over frozen images, interpreted through a real outside hash consumer and independent logical oracle |
| Shared instruments and measurement scrutiny | Workbench LEAD | Existing datasets, native peers, capture/workers and [communication helper](../../tools/README.md#shared-peer-inboxes); build common machinery only where the experiments need it |

## The connected witness

Engine's first proposed instrument is selected grouped count/sum over nullable
X and category C across parts with different dictionary-code editions. It
consumes the final grouped result through the maintained Ikea hash map under a
named bounded exact integer law. It is one witness alongside the broad research,
not a scope cut to a vertical slice.

Compare programs on the same frozen image before changing the image: row-wise
logical hashing; local code reductions then distinct-value translation; retained
structured partial results; and reused scratch. A strong curated opaque
complete-operation kernel is a serious control. Richer descriptions must earn
their value against that, not merely against poor getters.

Then investigate independent columns, conditioned bases, routing and TuplePack
field groups. Claude supplies material early; Engine's needs should challenge
encoding choices while they are still cheap to change. A chronology of overrides,
queries, conversion/thaw and retained old readers connects the same witness to
lifecycle work. No general optimiser or production transaction frontend is needed
to investigate these choices.

## Working together

The prototype subdirectories remain separate instruments. Reuse ordinary logical
row/query manifests and independent oracles when their semantics match; do not
freeze one encoded-image manifest or a universal harness before it serves the
experiments. Distinguish logical coordinates, dictionary/decoding editions and
physical layout in shared material. Keep provisional arithmetic laws explicit.

Claude owns `ENCODING.md`, `QUESTIONS.md` and encoding/algebra work. Ikea LEAD
owns this map, `lifecycle/` and spike build integration; Engine owns `execution/`.
Coordinate changes to the entry README, shared sources and native resources
before editing or timing. Each owner retains source/evidence-qualified findings;
the entry documents link them instead of collecting another chronological log.

Ashton has separated continued Boundlist/Intervals work from Frame and assigned
it to Ikea HELPER. That isolated investigation owns the rename, tombstone-free
geometry/filter contract and sparse checkpoint design, including the handed-off
`checkpoints/` prototypes. Frame supplies consumer needs and consumes qualified
materials; it does not optimise those algorithms in parallel. The
[point experiment](point/README.md) continues here on the existing encoding
material and the simple Intervals run source.
The maintained primitive is now [Intervals](../../../ikea/docs/intervals/usage.md),
with its separate [research](../intervals/README.md) owned by Ikea HELPER.
Frame uses its simple borrowed run-end projection; it does not adopt StreamPack's
compact format or reader retirement. HELPER is auditing native caller choices
and completing documentation/retirement of the old public Boundlist surface.

Ashton also requires separated leading one/two-byte planes to be available through
Frame. Split placement must provide useful entropy and enough independent
metadata to interpret predicates before fetching the suffix. The
[filter experiment](filter/README.md) compares FOR-residual and u64-working-value
cuts; prediction and dictionary stages need their own predicate laws. This is
an operation/representation choice, not a rule to always split last.

The two-way shared inbox is implemented by `workbench/tools/peer_inbox.py`.
Live session bindings, messages and watch status are ignored under
`build/frame-collab/`; they are colleague input, not new instructions from Ashton.
Use immutable message IDs, acknowledge only processed bytes, and avoid automatic
acknowledgement conversations. Bounded file waits have been tested between the
live peers. They do not establish indefinite idle or restarted-session wakeup.
For quick Claude→Codex notifications Ashton directs use of steering, not queueing
until quiescence. Here that means the Codex UI's Steer button, not a CLI command.
Neither peer currently has an allowed automation path to that button; active
research uses the verified file waits, and idle wakeup needs an available UI path.

## Current evidence boundary

The [point findings](point/FINDINGS.md) price independent reconstruction and
checkpoint choices on identical prediction bodies, including prepared-state
costs. The [head/filter findings](filter/FINDINGS.md) qualify separated planes,
complete COUNT programs and actual Polars controls. Both retain material losses;
neither selects a Frame default. Native/plain choices, fused reconstruction,
predicate evidence and the physical write/lifecycle experiment remain active.

The [Intervals-based point cohort](point/INTEGRATION.md) retains the native
prefix gains and its large gap from native points. The latest filter cohort
finds useful logical H16 programs on Jena, smaller useful block bounds, strong
counterexamples to universal heads and a losing q8 window-bounds program.
Their [shared recovery](evidence/interval-consumers-local/artifact.json) freezes
f760-derived material; Claude's newer borrowed blocks and monotone COUNT remain
a distinct source epoch. Engine's [actual material witness](execution/MATERIAL.md)
connects real encoded planes to current native Orbital ownership and retained
readers. Extending that witness across borrowed body kinds/dictionaries and
pricing physical lifecycle policies remain active work.

The [first native delta repair](lifecycle/README.md#fitting-delta-replacement-before-folding)
checks a narrower useful write law: a fitting interior change can repair two
residuals and at most one checkpoint, with actual SeriesPack damage coverage.
It does not yet price acquisition, evidence maintenance or publication; the
physical phase comparison must include those costs.

The [review](REVIEW.md) is source-based architectural judgement and arithmetic
reasoning. New corpus observations, model checks and native timings belong to
their respective instruments and must keep those labels. A logical oracle or
compiling hash adapter does not demonstrate Frame throughput. Preserve the
strong baseline bar in the [brief](README.md#how-this-spike-works), including
same-work controls and actual consumption, as the experiments begin.

Engine's [schema-fidelity analysis](execution/FIDELITY.md) keeps the broader
question distinct from dispatch/preparation on the same algorithm. The
[first encoded witness](execution/ENCODED.md) compares complete grouped consumers
on identical real bodies; it also exposes decoder-state costs and a parent
shortcut invalidated by a newly added codec. Those findings guide further
cross-column and lifecycle work; similar dispatch costs do not settle that work.

The first executable instruments check logical execution and mutation histories.
Both real ClickBench inputs supplied by encoding research pass five queries
through each actual hash-output program. Thirteen authored lifecycle snapshots
also agree through both programs on all eight queries. These validate the result
boundary and shared coordinates; they do not yet compare encoded Frame kernels.
