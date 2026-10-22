# Native phase baselines

The first physical chronology compares actual native-owned, published images,
not a Python model or an isolated setter. It supports a useful direction:
preserve cheap algebra over an immutable encoded base, correct it with coalesced
logical overrides, and make folding earn its cost. It does not select a Frame
mutation format or transition policy.

Prior work shapes this comparison. [StreamPack](../../streampack/RECOMMENDATION.md)
showed why complete phases and eventual cleaning matter; the
[aggregate-maintenance conclusions](../../aggregate-maintenance/CONCLUSIONS.md)
warned that repeated-hot and dispersed edits reverse buffering's benefit. The
[Orbital conversion census](../../orbital-mvcc/performance/controls/conversion-capacity/README.md)
already established primary-plus-alternative retention after reader release.
These mechanisms are reused and exposed here, not rediscovered as allocator bugs.

## Same useful work, actual ownership

Two 4096-row groups contain the first 8192 values of each original Jena-pressure
and UserID input. Eight ordered batches each contain 64 edits. Patterns are
repeated-hot, dispersed, moving-hot and full-width novelty. All, none,
alternating-block and first-group masks retain original coordinates. Presence
and NULL validity are independent; edits include deletion and NULL transitions.
An old reader either spans all eight publications or is absent.

Six policies share the public Orbital allocation/capture/Prepared/Scope protocol:

- Native raw64 and adaptive native FOR clone only affected group value/flag
  Parts. Fitting writes preserve native lanes; novelty widens/rebases when needed.
  Bounds widen conservatively instead of scanning to tighten dead extrema.
- Whole-group re-encode reconstructs an affected group and encodes it again.
- Immutable base plus ordered, coalesced overrides preserves original logical
  contributions and latest contributions at each distinct dirty position.
- The same override program folds before the final read phase, charging the
  fold and retaining its actual primary/alternative backing.
- Thaw/reseal converts selected groups to native FOR during writes and encodes
  them again before the final read phase.

The update phase demands one complete SUM/COUNT/presence query after each
publication. The read-rich phase demands 64 per batch plus 512 after the final
transition. A retained old reader demands another query after every batch.
These are forcing ratios, not workload-frequency estimates. Starting-image
construction is reported separately; phase totals include updates, publication,
demanded reads, conversion, old-reader release and drain. Final disposal of the
still-live current object and Pool shutdown are verified, outside the phase.

Native controls use exact u128 SUM with separate 32-bit halves, not per-row u128
addition. The linked native dispatcher uses vector mask shifts, comparisons and
reductions; its actual code is retained. Plain all-valid groups retain the flat
scan path. Coarse-empty operations return before source interpretation.

Values, presence/validity and overrides are separate Parts. NULL-only changes
can retain the value Part. An exported logical view retains its entire dependency
bundle after catalog teardown. Folding uses real conversion/adopt at the same
logical version; mixing a folded base with the old correction bundle is refused
using physical base identity, even when Object/version is equal.

## What the baselines establish

Selected all-block cases without a retained old reader; median µs per complete phase.

| Input / mix / edits | Native FOR | Whole-group re-encode | Overrides, no fold | Overrides + fold | Thaw/reseal |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pressure / update / repeated-hot | 66.3 | 876.4 | 73.1 | 307.6 | 351.3 |
| Pressure / update / dispersed | 83.7 | 1,623.2 | 166.3 | 373.1 | 349.9 |
| Pressure / update / novelty | 199.2 | 1,997.3 | 186.2 | 435.0 | 502.9 |
| Pressure / read-rich / repeated-hot | 3,113.3 | 4,857.2 | 3,919.5 | 4,250.5 | 3,777.0 |
| Pressure / read-rich / dispersed | 5,685.0 | 13,577.2 | 4,526.8 | 9,204.4 | 8,559.7 |
| Pressure / read-rich / novelty | 7,607.1 | 12,939.1 | 5,106.2 | 8,451.6 | 10,483.5 |
| UserID / update / repeated-hot | 85.0 | 494.2 | 48.3 | 186.2 | 283.2 |
| UserID / update / dispersed | 130.0 | 1,210.3 | 176.5 | 318.5 | 334.0 |
| UserID / update / novelty | 161.5 | 1,372.4 | 181.8 | 345.2 | 379.7 |
| UserID / read-rich / repeated-hot | 4,116.1 | 1,841.1 | 1,101.5 | 1,403.8 | 2,938.2 |
| UserID / read-rich / dispersed | 7,860.1 | 10,619.9 | 1,641.2 | 6,262.0 | 9,306.3 |
| UserID / read-rich / novelty | 7,497.3 | 10,102.0 | 1,843.2 | 6,423.6 | 8,975.5 |

The whole-group writer is an intentionally expensive control, not a competitive
Frame writer. Sorted-vector override coalescing remains too expensive for dispersed
writes. Repeated-hot edits are a more promising case; close native/override
rankings do not establish a winner. Read-rich UserID work benefits strongly from
keeping the base's cheap run/prediction programs intact. Folding novelty into the
base can destroy those programs even though the folded byte count looks tidy.

The no-fold arm carries unpaid future folding work. Its read advantage does not
justify never cleaning. Conversely, folding at this artificial phase boundary
is not automatically useful: price the future operations it actually improves.
This cohort exposes both, rather than hiding conversion in setup.

`adopt` retains a version's primary and latest alternative. Reads here explicitly
choose that alternative; the next genuine logical update clones the chosen
image, matching the existing composed-caller convention. Old Scopes, exported
Parts and conversion handles can independently retain more. Unchanged Parts are
shared across realizations; physical nonidle accounting does not count repeated
references twice. Same-version folding alone does not reclaim primary backing.
Engine records this contract consequence in its material guide.

## Limits and next discriminators

Current mixed-validity encoded queries decode the affected block before reducing.
Claude's fine-mask owner algebra and predictor-consistent NULL fill are the next
coherent integration. That seam penalises folded forms and must be removed before
selecting a lifecycle policy. The frozen D2 baseline deliberately excludes those
new operations, optional dictionaries/arrival codes and later split-half changes.

Group-wide controls copy/reconstruct unchanged blocks inside an affected group;
this exposes their dependency halo, not completion of coarse-masked movement.
Empty groups are skipped. Block folding with retained dictionary dependencies,
fitting packed/delta repair with actual issued-store coverage, and independent
versus shared backing still need comparison. The fixed-basis delta repair's
32,640 correctness cases do not qualify a hot writer.

Reported copied bytes are explicit destination-copy/construction bytes, not
cache traffic or a complete store census. Native nonidle capacity, Pool mapped/
idle bytes and owner metadata are separate. Codec temporaries, vector scratch,
query stack/cache and process RSS are not included in native nonidle bytes.
The fixed two-group inline metadata is a prototype cost, not a product minimum.
Largest individual cleaning pauses, more reader ages, append/other operation
families and staged cold/Loom work remain open.

## Evidence

Pinned Clang 21.1.8, O3/native/generic tuning, local OrbStack AArch64 VM/CPU7.
Two exact-policy duplicates and balanced position/predecessor rounds accompany
16 samples/cell. Selected-mask duplicate/original median-time ratios range
0.865–1.153 (median 1.001). Keep the large separations; observed duplicate variation does
not support close rankings or fleet/cold conclusions.

A separate verification run compares 54,525,952 points against original logical
snapshots, including unselected rows and retained readers. Every publication's
demanded exact SUM/COUNT/presence answer is checked in the timing runs too.
The material check adds 90,642 authored checks over native lane widths, exact
large sums, dictionary/RHS changes, separate flags, overlay correction, protected
none access, three actual owner histories, real adoption and escaping bundles.
All releases finish with zero owner metadata and Arena charge.

[Selected cells, calibration and recovery](evidence/phase-baselines-local/)
retain every policy/mask/reader/pattern/mix cell. Raw repeated timings, complete
frozen source, binaries, native assembly and the original-value verification
runs remain in recovery. Earlier zero-query and pilot runs are diagnostics;
they are not pooled with this cohort. No public Frame API, default or graduation
is selected.
