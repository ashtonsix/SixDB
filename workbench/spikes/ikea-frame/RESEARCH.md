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

Ashton asks Ikea LEAD to drive the effort collaboratively with Claude- and
Codex-based peers. Responsibilities below are current working ownership, not a
ranking of capability or a partition of future public APIs. Challenge framing,
bring counterexamples, and revise the division as the work reveals strengths.

| Frontier | Current lead | Active contribution and connection |
| --- | --- | --- |
| Real-column encoding and compression | Claude, Ikea frame design | [Encoding inventory](encoding/README.md): existing corpora and controls, useful stages/kinds, complete bytes and analysis cost; supply representative images to Engine early |
| Representation algebra and structured intermediates | Claude, with Ikea/Engine | Masked and overridden reductions, weighted delta/checkpoint alternatives and dictionary/run results; use the same images and consumer witness as schema-fidelity work |
| Mutation and representation lifecycle | Ikea LEAD | [Lifecycle investigation](lifecycle/README.md): repeated versus dispersed writes, novelty, selective folding, thaw/reseal and retained-reader economics |
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

The [review](REVIEW.md) is source-based architectural judgement and arithmetic
reasoning. New corpus observations, model checks and native timings belong to
their respective instruments and must keep those labels. A logical oracle or
compiling hash adapter does not demonstrate Frame throughput. Preserve the
strong baseline bar in the [brief](README.md#how-this-spike-works), including
same-work controls and actual consumption, as the experiments begin.

The first executable instruments check logical execution and mutation histories.
Both real ClickBench inputs supplied by encoding research pass five queries
through each actual hash-output program. Thirteen authored lifecycle snapshots
also agree through both programs on all eight queries. These validate the result
boundary and shared coordinates; they do not yet compare encoded Frame kernels.
