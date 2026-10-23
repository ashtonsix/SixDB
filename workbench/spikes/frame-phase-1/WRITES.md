# How Frame writes meet Orbital

Research notes for the [Ikea Frame spike](README.md): what the existing Orbital,
Ikea and Engine work already establishes about writes, versions and readers,
and what that means for the [encoding](ENCODING.md). Ashton's recollection
(2026-10-22) set the starting shape: once an epoch's transactions are admitted
and persisted, the affected part gets a new virtual allocation, with many
optimisation wrinkles. At minimum Ikea must honour locks and publish byte
damage, and `engine::directory` must support coherent reads. These notes trace
where each piece is documented; they are a reading, not a contract, and the
area will be revisited to drive performance.

**Implementation status:** the table mixes current mechanisms, experiments and
future protocol proposals. Current Orbital supplies coherent local
`Scope`/`Prepared` publication and exact-version conversion/`adopt`; durable
epoch admission, a generic writable `Draft` and distributed lock projection
remain proposed. See the [review](REVIEW.md#arithmetic-and-ownership-cautions-for-the-prototypes)
and [current runtime](../../../orbital/README.md#embedded-objects) before using
these notes as an implementation dependency.

## What is established, and where

| Topic | Finding | Source |
| --- | --- | --- |
| Object hierarchy | Object → Representation → Part → View. Objects hold logical state, lock set and version history (version IDs from journal epochs); representations are replica-local physical state. | [Ashton's scratch](../../notebook/orbital/ASHTON-SCRATCH.md), [Orbital README](../../../orbital/README.md#embedded-objects) |
| Write sequence | Store input → admit batch → edit Draft → publish State. A Draft retains admitted input and the complete previous State; published readers keep seeing the previous State; publication is atomic and cannot fail or suspend. | [Design: admitted-input path](../../design/orbital/DESIGN.md#future-admitted-input-path) |
| Epoch completion | An epoch completes when its version has a valid recovery basis. If admitted input is reproducible, derived material writeback can trail. | [Views findings §1](../orbital-views/FINDINGS.md#recommendations) |
| Draft acquisition | Views versioned with "latest" and "draft" upper bounds. Taking a latest view with no readers into a draft and writing in place is safe if the old version is recoverable; with readers, make a new allocation and copy on write (or copy on read and write touch). Orbital decides what is safe; Loom chooses among safe methods; UFFD/TLB costs may favour plain copying. | [Ashton's scratch, view notes](../../notebook/orbital/ASHTON-SCRATCH.md) |
| Unique-reference reuse | Existing storage is reused without COW only when no reader, checker, hash, send or write still needs its bytes and retained state can recover required versions. Late old readers get a separately reconstructed view (deterministic replay from a snapshot is the worst case). | [Architecture](../../notebook/orbital/reference/ARCHITECTURE.md), [Brief](../../notebook/orbital/reference/BRIEF.md), [Physical brief](../../notebook/orbital/reference/PHYSICAL.md) |
| Measured binding costs | 16 KiB binding, no old reader: per-page copy/prepare 29.0 µs, retained mapped copy 1.15 µs, eligible take 0.82 µs (Zen5). Two-level shared pages publish a 64 B change in 1 MiB in 1.45 µs versus 79.4 µs for a full snapshot. | [Views findings](../orbital-views/FINDINGS.md#evidence-and-decision-boundaries), [residency](../orbital-views/residency/README.md) |
| UFFD | Write-protect COW, missing-page supply and late old readers were exercised by standalone probes; current production and embedded views do not use UFFD. | [Object history](../orbital-mvcc/history/objects.md#standalone-linux-fault-path-probes) |
| Afterimages vs in-place | Both were prototyped. Afterimage authority (synced replayable afterimages, shared-page material) and semantic-intent replay (admitted input re-executed through the same Ikea/owner law) both recovered correctly; the recommendation is to let Ikea data units define change and reconstruction rather than one universal delta format. | [Publication](../orbital-views/publication/README.md), [systems](../orbital-views/systems/README.md), [publication review](../orbital-views/prior-publication.md) |
| Byte damage | Ikea journals issued stores (including preserved neighbours) per source, plane and span. Coverage is not authority, not a beforeimage, and not sufficient alone: Bec256 empty and full bodies are both zero bytes, so interpretation metadata must be captured with the data. | [Ikea integration](../../../ikea/docs/integration.md#consume-effects-and-publish), [unit-boundary probe](../orbital-views/FINDINGS.md#composition-evidence) |
| Locks | Lock sets project to view masks; Ikea supplies the projection and its interpretation. Disjoint logical fields do not make overlapping packed stores safe: the physical conflict envelope must be known before native stores. | [Ashton's scratch](../../notebook/orbital/ASHTON-SCRATCH.md), [Views findings §4](../orbital-views/FINDINGS.md#recommendations) |
| Summaries | Readers need matching data, summary cut and corrections, or a valid bypass. Count/sum corrections and min/max repair have different laws; removing the last extremum witness has no scalar inverse. | [Publication review: summaries](../orbital-views/prior-publication.md#summaries-publish-validity-with-data) |
| Coherent reads | `engine::directory` publishes one packet per `apply`, reuses unchanged Parts from the basis version and lets readers borrow Parts under their own `Store::Scope`. | [Engine README](../../../engine/README.md#segment-directory) |
| Conversion | `adopt` replaces a version's physical alternative without changing logical time; the converter owes exact logical equivalence. | [Orbital README](../../../orbital/README.md#embedded-objects) |

## What this means for Frame's encoding

1. **"In place" means in place within a Draft.** The write rule in
   [ENCODING.md](ENCODING.md#writes) runs against the epoch's writable view.
   Whether that view was taken (no readers, recoverable) or copied is Orbital's
   and Loom's choice. Frame's lever is cost: a write should dirty as few pages
   as possible, so COW, shared-page publication and capture stay proportional.
2. **Group-sized Parts fit the existing pattern.** If each group's planes are
   Parts (or page-aligned ranges within one), an epoch that touches one group
   republishes only that group and shares the rest, as `engine::directory`
   already does with its leaves. Per-Part metadata cost decides how fine this
   can go; that needs measurement.
3. **Each write outcome has a different physical envelope.** An in-place write
   dirties tile bytes (with preserved neighbours), the block's statistics and
   the parent's sum. An override dirties the group's override store, the
   block's bitmap and possibly a reservation repack. A compaction or streaming
   re-encode rewrites the whole group. Frame must report the envelope before
   stores so locks can be projected onto it; concurrent writers in one group
   conflict on the override store even when their rows differ.
4. **Damage must include interpretation.** Block descriptors, override bitmaps,
   counts, checkpoints and dictionaries are interpretation metadata. They are
   captured and published in the same cut as the payload they describe.
5. **Recovery can replay instead of capturing.** If admitted input is the
   recovery basis, Frame's encoder and write rule must be deterministic
   functions of (representation, input, priors) so replay reproduces a
   logically identical part. A group re-encode is then reproducible from its
   inputs rather than shipped as bytes, and which representation a replica
   rebuilds need not match another's.
6. **Statistics publish with data.** Block min/max widen on write and become
   dirty on possible shrink; the parent's block sums receive `sum_change` in
   the same epoch, so readers never see data without matching summaries.
7. **Streaming re-encode is a conversion.** Re-encoding a plain tail group
   changes no logical content, so it can use `adopt` instead of a new version.

## Research questions this raises

Collected in [QUESTIONS.md](QUESTIONS.md#writes-overrides-and-compaction): Part
grain for groups, write envelopes and lock projection, replay versus captured
material for group re-encodes, and the cost of taking versus copying hot groups.
