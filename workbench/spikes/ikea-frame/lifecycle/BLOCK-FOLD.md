# Fold only changed blocks

Changed-block folding removes most codec work for sparse edits, but is still
far behind native arrays when dispersed edits touch most blocks. Keep it as a
fallback/control while testing fitting replacements. It does not replace the
immutable-base program or select a Frame write policy.

The [first native phase cohort](NATIVE.md) found whole-group reconstruction and
analysis much too expensive, and showed why folding must earn both its CPU and
its effect on later reads. This experiment preserves those native, override and
thaw controls and their original chronology. It changes the amount of codec work,
not the source data, query demand or public ownership protocol.

## Mechanism and qualification

Only dirty 256-row blocks are decoded. Ordered edits retain physical values for
NULL/deleted outputs, updating presence/validity separately. A flags-only edit
reuses the value Part. Value changes re-encode that block through the existing
EncodedRegion builder. Neighbouring blocks retain their exact bytes. A changed
coded block becomes raw; other coded blocks retain their original dictionary
bytes and identity. The dictionary disappears only when no block needs it.

Shared block summarisation maintains counts/bounds without scanning the group.
The value Part still owns an entire group: repacking copies unchanged blocks
and dictionary bytes into new backing. This is an explicit copy halo, not
completion of independent block ownership or coarse-masked movement.

The new focused check passes 295,344 original-value comparisons and aggregate
checks across all four checkpoint modes, raw/sorted/frequency dictionaries,
repeated/novel edits and old readers. It additionally proves NULL-only value-Part
reuse, exactly one selected block re-encoded, byte-identical neighbours and
retained dictionary interpretation. The existing 90,642 material checks pass.

The real-slice comparison retains every previous policy and adds changed-block
folding. Separate original-coordinate verification checks 61,341,696 points;
20,736 timed histories check every demanded aggregate after publication.
Nine arms include two identical-policy controls; complete odd-population balanced
rounds give 18 samples/cell. Local ARM VM only. Selected-mask duplicate ratios
range .776–1.237, median .997: large separations are useful; close rankings are not.
No results are pooled with the earlier eight-arm cohort.

## Complete update phase

All blocks selected, no retained old reader, median µs for eight batches of
64 edits with one demanded aggregate after every publication:

| Input / edits | Native FOR | Whole-group encode | Changed-block fold | Overrides, no fold |
| --- | ---: | ---: | ---: | ---: |
| Pressure / repeated-hot | 60.4 | 665.7 | 112.8 | 64.9 |
| Pressure / dispersed | 87.1 | 1,525.5 | 1,194.8 | 168.5 |
| Pressure / novelty | 207.7 | 1,970.1 | 1,774.7 | 188.7 |
| UserID / repeated-hot | 88.8 | 544.5 | 99.2 | 54.7 |
| UserID / dispersed | 131.8 | 1,411.8 | 1,197.0 | 207.0 |
| UserID / novelty | 160.7 | 1,439.9 | 1,361.6 | 185.6 |

Sparse edits encode 2,048 rather than 32,768 rows: a 16× work reduction and
about 5–6× lower complete time versus whole-group encode. Copied pressure bytes
fall only 42,304→35,136, because group ownership still forces neighbour copies.
The median of each trace's largest update call falls 93.7→16.5µs on pressure
and 88.1→17.0µs on UserID. These calls include publication and local destruction;
18 traces are not production tail-latency evidence.

Dispersed writes touch most blocks, exposing the encoding cost again. Block
folding also does not recover the immutable-base program's read-rich advantage:
UserID dispersed read-rich phases take 9.10ms with block folding, 1.68ms with
unfolded overrides and 7.40ms with native FOR. Unfolded overrides still owe future
folding. The remaining lever is fitting packed/delta repair before re-encoding,
plus finer ownership and the pending NULL-aware owner programs.

## The next fitting-write leaf

The private `replace_private_plane` primitive uses the existing scalar SeriesPack
setter over a wholly fresh, initialized, isolated Local plane. Placement, row and
value proofs belong to its caller. It emits no effects/summaries and cannot
justify modifying retained/shared bytes. Operation-level in-place mutation still
requires the source-qualified writer and actual issued-store envelope.

The first adapter used `view::attach` on every plane: linked AArch64 code had a
160-byte frame and an out-of-line admission call. Existing public
`view::assume_valid` reuses the admitted source/exact-copy placement proof.
The resulting K13/24/32/64 leaf loops have no stack frame or calls. No maintained
API or packing algorithm changed. All 33,856 point comparisons pass over all
64 widths, partial/full tiles, unaligned prefix/suffix guards, repeated edits,
protected-empty and active-fault controls. This is a qualified primitive, not
a measured fitting Frame writer in this cohort. The width matrix compiles once
in its own TU. The later [sparse-write study](REPAIR.md) prices fitting writers
with the newer coherent owner cut.

## Recovery

[All candidate cells, pauses and controls](evidence/block-fold-local/) retain the
full comparison, including read-rich phases, masks and old readers. Raw results,
original-coordinate checks, binaries and full frozen sources remain in recovery.
Block-fold source capture:
`42928aacd8d48340cdb45864cf3993343b0a79604299aba71a5e5e6b73c03797`.
The separately checked private-plane capture is
`ba24880169c4e3f636aac6942b7427d1f9fc4097ab7b441b9645d14bd72737aa`.
The owner core stays D2; new masked operations, NULL fills and caching are not
substituted into this cohort. Existing first-cohort accounting limits still apply.
