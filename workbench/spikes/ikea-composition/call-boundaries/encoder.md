# Scalar arguments at the trusted encoder boundary

Retain the scalar callback as a shared simplification. The public
`bound_encoder::encode(input_values, effects)` facade is unchanged; its trusted
callback receives the source pointer and carrier width without carrying unused
capacity through the ABI. This removes argument copying and 2,701 library text
bytes with no physical variants, new headers or per-case policies. The change
is +10/−9 source lines across three files.

The exposed `encode_function` signature changes, so custom callbacks and callers
must rebuild together. Capacity remains a checked-front-end and trusted-caller
obligation. Full construction reads exactly destination `n` input values;
capacity does not grant extra lookahead. Checked capacity/domain/alias rejection,
encoder object layout, canonical final-tile slack and effects ordering remain
unchanged. This is the same principle as the retained
[reader callback simplification](decoder.md).

## Ordinary calls on Zen

[Paired samples](evidence/encoder-callback-20260911/20260911T013319Z-45302832/paired.csv)
and [summary](evidence/encoder-callback-20260911/20260911T013319Z-45302832/summary.json)
retain the individual case outcomes. A separated win/loss means the six samples
on each side do not overlap. It is descriptive, not a statistical confidence
interval. Ratios are after/before; each case has two ABBA ordering edges.

| Ordinary scope | Cases | Separated wins / losses / overlaps | Median ratio |
| --- | ---: | ---: | ---: |
| 256 values, registered minimum-fit and u64 carriers | 630 | 614 / 9 / 7 | 0.6851 |
| 8,192 values, H0/H16 and representative H8, u64 | 282 | 116 / 15 / 151 | 0.9944 |
| 1,048,576 values, representative encoders | 16 | 3 / 1 / 12 | 0.9974 |
| Checked/bound casing, with/without effects | 96 | 21 / 22 / 53 | 1.0003 |

Small ordinary cases cover all 206 registered descriptions for both explicit
x86 families, with minimum-fit and u64 source carriers deduplicated. The API
also admits narrower carriers when actual values fit; casing/public checks
cover such admissions, but the 630 timing cases are not every admitted carrier.
The 625/630 small ordering pairs agree in direction. Bulk and large timings are
ns/value; casing is ns/call. Casing includes scalar, AVX2 and AVX512 endpoints.
The AVX2-labelled endpoint was compiled in the same full-feature x86 build.

The small bulk caller benefits substantially; this is **not** a universal 31%
gain at the boundary. The differently compiled casing caller is approximately
flat overall. Visible small losses include AVX512 Local25/H0 at u32 and u64
(+12.1% and +11.4%), Local41/H16/u64 (+7.4%), and Striped17/H16/u32 (+16.7%).
Bulk Striped2/H0/AVX512 rises 12.5%, with its candidate block medians moving
0.04992→0.05406 ns/value against approximately 0.04624 before. Casing
Striped28/H8/AVX512 bound with effects rises 31.86→35.42 ns/call (+11.2%).
The remaining losses and ordering edges stay in the paired table.

Prior and decode controls also move: small prior ratios range 0.735–1.317,
bulk prior ratios 0.829–1.093, and the large decode-control maximum is 1.105.
These observations limit attribution; they are not subtracted from candidate
results. Calico controls represent alternative encodings and do not exhaust
the strongest prior inventory at every width. No primitive parity or compound
recovery claim follows from this ABI comparison.

Under the [performance and maintenance guidance](../design.md#performance-and-maintenance),
the broad small-call gain and simpler implementation justify retaining the
change despite local losses. Those losses do not justify callback exceptions.
Bulk, large and casing results remain separately reported; a cross-machine
runtime gain is not established by this Zen experiment.

## Code, build and verification evidence

The [actual caller review](evidence/encoder-callback-20260911/20260911T013319Z-45302832/cost-review/review.md)
finds the bulk caller loses the input-count load and two aggregate copies;
the bound casing argument setup shrinks from eight to five instructions without
effects and nine to six with effects. Checked casing caller instruction bodies
are unchanged. These are instruction observations, not cycle attribution.
Native text shrinks 1,504 bytes, scalar/operations 1,197; data/rodata sizes are
unchanged. The whole bulk caller object grows 77 bytes, casing shrinks 144.
Common non-encoder instruction bodies match, excluding relocation targets,
constant contents and final linked placement. Retained Ninja edge durations
describe build costs from different jobs/cache states, not a build-time speedup.

Job `20260911T013319Z-45302832` used capture
`db2a89bea6494808a54541a6b6e4ad888d5808a12689d68fe51e30b0768275fc`.
The [audit](evidence/encoder-callback-20260911/20260911T013319Z-45302832/audit.json)
checks exactly three source changes, unchanged normalized compiler arguments,
matching rebuilt callers/callbacks, retained link inputs, before-binary identity,
logical counters, 15,648 timing samples and 2,608 preflights. CPU0 cases and
repetitions ran sequentially, ABBA with three 30 ms repetitions per block,
without background result uploads. All scalar/AVX2/AVX512 public wire and exact
guarded-range checks passed before measurement. Preparation also passed scalar
and NEON public checks and ARM compilation; this is correctness evidence, not
ARM timing evidence.

The sealed runner omitted an explicit pre-build rehash of seven external Calico
source files and the copied baseline compile database. No mismatch was found.
The main audit checks the original database pin; separate read-only job
`20260911T015455Z-1ce96478` checks the seven external files, all 46 compiled source
entries and normalized arguments. Its
[verification](evidence/encoder-callback-20260911/20260911T013319Z-45302832/post-verification.json)
occurred **after** timing and does not retroactively establish a pre-build check.
The original receipt is preserved. Recovery locations and compact hashes are in
[provenance](evidence/encoder-callback-20260911/20260911T013319Z-45302832/provenance.json).
