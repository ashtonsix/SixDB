# Parked follow-ups

These four controls preserve useful findings from private Stage 2 investigations.
They are **unselected and untimed**, with no further implementation or measurement
queued. They do not change the selected runtime or establish new APIs. Future
Orbital work should reconsider their questions in real Ikea/Engine compositions.

The small patches, consequential probes and receipts below replace dependence
on otherwise unpublished `build/reviews/` work. Each receipt records its exact
Git base, patch/source hashes and original qualification scope. Curation checked
patch hunks against Git blobs in memory and matched the recorded source hashes;
it ran no compiler, tests or clocks. Binaries, full logs, warm build dependencies
and toolchains are not bundled. Reconstructible sources do not imply an exact
binary replay or qualification against today's headers.

| Control | Lesson | Retained material |
| --- | --- | --- |
| Manual local state | A local state copy changes actual generated code, but also adds recurrent loads/stores; smaller text is not proof of a faster caller. | [Patch](manual-local/control.patch), [common frozen-header context](manual-local/source-context.patch), [receipt](manual-local/receipt.json) |
| Cold alternative choice | Explicit discard or quiescent promotion can release one catalog image without adding read-path state. External owners and reusable Pool capacity still matter. | [Module patch](cold-alternative/module.patch), [fixture patch](cold-alternative/checks.patch), [probes](cold-alternative/probe/), [receipt](cold-alternative/receipt.json) |
| Empty-history census shortcut | Existing node occupancy can avoid some census work, but cached work can remain necessary after the last node disappears. | [Frozen masked parent](empty-history/masked-parent.patch), [common observation](empty-history/observation.patch), [refinement](empty-history/refinement.patch), [receipt](empty-history/receipt.json) |
| Canonical tail load | One bounded eight-byte transfer replaces three payload loads, with mixed code size and register costs. | [Patch](tail-load/control.patch), [receipt and codegen summary](tail-load/receipt.json) |

## Manual local state

Base `f80e8be9e76fe7109870eaa40687d6d0a32800b8`. Only the manual Ring driver
copies `Frame.state` into a local before its stage switch and writes it back
afterward. Selection, Scopes, refill, hints, native Ring callbacks and actual
body reads are unchanged. The common context patch restores the frozen
`representation.h` comment; it makes no runtime change.

The candidate TU was privately linked with verified warm dependencies and passed
756 matched panels / 97,524 requests plus the retained-lifetime history. No
sanitizer or timing comparison was run. Pinned Clang 21.1.8 cross-codegen for
x86-64-v3/Zen5 removed three intermediate digest stores and changed search
direction handling to conditional moves. Driver text fell 12,232→12,070 bytes;
the 1,064-byte local frame and 48 bytes of saved registers did not change. All
six native Ring callback disassemblies were identical.

The copy adds five recurrence loads and five stores on every manual visit,
40 bytes each way. Dispatch, rotation and an existing cursor spill remain.
That tradeoff is the lead. The wider measured manual/Ring results do not measure
this control, and the cross sysroot is not the worker binary's build environment.

## Cold alternative choice

Base `7ae1726c5e9edf2ba130aeee49bd3e234064595c`. The two experimental cold
operations address the current tip only:

- Discard releases its catalog alternative. Already selected alternatives retain
  their exact owner; later alternative discovery returns absence.
- Promotion refuses while any Scope, accepted packet or target-entry pin exists.
  Otherwise it transfers the alternative's catalog use into primary, then
  releases the former primary. Logical identity and cut do not change.

Both finish map/pointer changes before physical-owner callbacks and keep Core
alive through disposal. Conversion compatibility uses the target's retained
source. No format inference, automatic retirement policy, per-read tracking or
new instance field is introduced. This is a frozen proposal, not a supported
operation on the selected ObjectStore.

Five native checks passed, including new logical and real composed histories;
the two new probes also passed ASan/UBSan with always-on use-after-return and
leak checking. They cover accepted work, delayed targets, aliases, reentrant
publication/reset, old selections and escaped Parts. Six 8,192-row cases made
427,664 independent comparisons. The native Scope/Borrowed read assembly was
identical after normalizing assertion filenames.

After external owners end, keep retains two images; discard or promotion keeps
one. Tuple live capacity was 376,832→188,416 bytes; Series was
368,640→184,320 bytes. Reusable mapped capacity did not necessarily fall:
Tuple remained 561,152 bytes. Discard's subsequent conversion starts from a
different format than keep/promotion, so the full trajectories are not matched
work. PMR observations include verification and observation Scopes. Global
quiescence can prevent promotion indefinitely. None of this establishes latency,
an immediate-promotion reclamation mechanism or a general representation policy.

## Empty-history census shortcut

Base `d9fff3eaf54aed5202b642cb6069b4bfb9e373ac`, followed by the retained
masked-maintenance and observation patches, then the refinement. The
[measured parent](../../FEEDBACK.md#masked-maintenance-retained-reader-gain-fresh-reader-regression)
remains unselected: long-reader gains accompanied roughly 10% scalar age1
regression. Its [worker archive](../../evidence/zen5-masked-maintenance-screen-20261010/artifact.json)
recovers that measured composition, **not this later refinement**.

The refinement prefers one queued Object before the first census word and skips
an unstarted/empty-mask census when the existing history-node live count is zero.
It adds no metadata. Prepared node reservations conservatively permit extra work.
It must never discard a cached census mask: a disposal callback can leave an
unfinished tombstone even with no older nodes. Removing that guard fails the
retained `last_empty_history_callback_tombstone` test in the patch.

Focused native histories passed reader changes, partial walks, accepted work,
refusal/retry, abort and callbacks. Age1 work for the small counter fixture fell
1,920→1,856, still above baseline 1,664; word queries fell 256→192. The ordinary
caller already drains the changed Object before reader closure, so queue-first
does not remove its first word query. Generic AArch64 drain text grew by 40 bytes;
Core remained 696 bytes and the 64-entry directory 3,104 bytes. No sanitizer or
timing run qualifies the refinement. It does not establish recovery of the
measured regression.

## Canonical tail load

Base `768e033584bc83e097de34e0f59ee96ba0e491cc`. The shared canonical Tuple
consumer used by Inline fusion and mapped LMDB reads replaces three value/tag
loads with Ikea's existing `read_gpr_transfer<1,8>` at byte 15. Bytes 15–22 lie
inside the exact 23-byte row, including the last row; the irrelevant byte and
spare value bits are discarded. No padding promise or retained state is added.

Four existing native checks passed independent fields/wire bits, nonzero spare
bits, unaligned rows, exact-extent refusal, old-cut reads and intact refusal
output. No sanitizer, timing, PMU or cycle model was run. Exact-size native
buffers alone are not an overread detector; the source bounds and emitted
eight-byte load support the narrower within-row claim.

Separate-TU Clang 21.1.8 x86-64-v3/Zen5 codegen changed coherent/current-borrow
text from 922/930 to 925/939 bytes and mapped text from 480 to 468 bytes. Mapped
saved one additional register; fusion frames and hot-loop spill counts were
unchanged. Wider unaligned loads can split. The static load reduction therefore
does not establish a complete-operation improvement.

## Recover the authored source

Use a fresh disposable Git root at the exact base from the chosen receipt.
Apply its listed patches in order, using absolute patch paths. Initialize that
root before `git apply`; applying inside an uninitialized directory nested in
another repository can silently skip paths. Check the receipt's postimage hashes
afterward. These are historical-source instructions, not patches for current HEAD.

For **manual-local**, the normal target is `loom_object_ring_check`. The package
reconstructs all 140 recorded source inputs; original warm archive hashes remain
in the receipt, but their bytes are not supplied.

For **cold-alternative**, recover the Git tree into `RECOVERY/source/`, apply
`module.patch` and `checks.patch` there, and copy the retained `probe/` directory
to `RECOVERY/probe/`. This preserves the qualified fixture's `../probe/` paths.
Configure with `SIXDB_SPIKES=orbital-mvcc`; the two added targets are
`orbital_primary_choice_history_check` and
`orbital_primary_choice_composed_check`. `probe/read.cpp` is the separate
assembly comparison input. Its original command used C++23, Clang 21.1.8,
`-O3 -g0 -ffp-contract=off` and a macro-prefix map for assertion filenames.

For **empty-history**, all fourteen changed/additional source files, including
the consequential check, are reconstructed by the three patches. The target is
`orbital_object_changed_queue_check`; the patch wires it into normal Orbital
validation. Observation fields use the same schema on both arms, with explicit
zero census for the baseline. The retained observation patch is for this old
header layout, not the later standalone statistics header.

For **tail-load**, apply `tail-load/control.patch` at its named base. The receipt
lists the four existing CMake check targets and exact header postimage. Its
original projected-build record does not contain a complete dependency/binary
manifest, so this preserves the authored change and observed summary without
establishing exact historical binary replay.

Use the pinned Linux toolchain and an independent incremental build. No fresh
rebuild/replay of these recovery recipes was performed during curation. The
receipts distinguish earlier execution from this source-preservation check.
