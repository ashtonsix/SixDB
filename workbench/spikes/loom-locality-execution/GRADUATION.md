# Historical execution-policy graduation

Generic execution feedback lives in
[`loom/execution.h`](../../../loom/include/loom/execution.h), with checked
construction and observation in Loom's compiled library. The
[execution guide](../../../loom/docs/execution.md) owns its caller contract.
The current API accepts epochs, paths and samples. Callers qualify their legal
procedures and evidence cohort, then call `reset()` when that qualification
changes. Loom defines no container identity schema.

This page preserves the initial graduation's frozen, identity-bearing interface
and native results. Its layout, codegen, checks and timings do not automatically
qualify the revised API or the caller's reset adapter. The original private
implementations remain matched experimental controls. The general unique-line
contiguous selector remains rejected.

## Measured interface

In the measured interface, one owner-held Policy combined qualified serial
evidence and bounded retry cadence. Options required an explicit
consumer-qualified cutoff and maximum age. Its historical `Identity` covered
the complete compared procedure pair,
distribution, owner incarnation, generation and assigned worker/cache domain.
Identity changes, expiry and backward time discarded history permanently.
Invalid observations did not change history. A saturating countdown replaced
the private sampler's wrapping batch/deadline arithmetic.

The caller retains its legal complete kernels, representative bounded sample
work, head rotation, content/version stability and physical protection. Only
completed serial useful work enters the policy. Clocks, observations and
direct/Ring decisions stay at ordinary operation boundaries. No Worker fields,
Ring fields, native callback arguments, per-bucket observer or hint procedure
were added. Tasks and suspension remain separate caller choices.

The measured public API contained no experiment instrumentation switches, hint
counters or fixed-path controls. As with the current API, callers could execute
known legal paths directly or supply an explicit direct prior without inventing
evidence. Changing options required a new Policy. A kernel, width or hint
snapshot change that altered the qualified crossover also required
requalification of the cohort/options.

## Measured boundary cost

An initial native Zen run passed all 20 correctness checks, then the strict
ordinary-wrapper guard found two unnecessary callee-save register spills.
Qualification of an unchanged, already-qualified identity kept excess temporary
values live. Validating identity on cohort change removed that work from the
common path while preserving invalidation on every boundary. Both pinned ARM
and x86 compilation then produced call-free, stack-free ordinary wrappers.
Those native callbacks contained no policy work.

At that cut, public Policy was 88 bytes on the measured ABIs, including its
options. The private wrapper was 104 bytes: its 80-byte CSV counter counted only
the record and cadence, excluding 24 bytes of options. Those counters are not
an apples-to-apples memory ranking. Independent compiled layout diagnostics are
retained with the checks.

## Qualification of the frozen cut

All 18 Loom runtime checks passed after that flow repair. O3 ASan/UBSan with
always-UAR passed the policy and complete direct/Ring example. Those public
tests covered identity fields, inclusive age/cutoff boundaries, rollback/wrap,
exact cadence, rejected observations, legality, numeric limits, confirmation
saturation and a heterogeneous-sample counterexample. The report/codegen checker
has 23 positive and refusal controls in normal Python and optimized mode.
[Checks and source identity](evidence/graduation-checks-20261015/artifact.json)
retain logs, compilation settings and layout diagnostics.

The final native campaigns used Ikea's verified V4 caller, explicitly selecting
`HT_RECORD_READ_WINDOWS=2` in every measured reference/private/product consumer.
All source, allocator and format bytes were matched. Five passes alternated
private/product process order and rotated internal mode/cutoff order. Each host
covered 186 original full-graph cells and 2,016 execution-policy cells, including
all-hit/mixed, hot/large, set/map/multimap, all seven phases and six controls at
cutoffs 32 and 128. Actual serial samples are fully charged without increasing
the useful request denominator. Actual execution callbacks, serial probes and
ordinary loops receive assembly checks; private/product callback instruction
streams must match after address normalization.

Final source: 3,416 files,
`434d3ade5845700b22c37f62a0cd1a16824b388c873fdfd739e077094adff201`.
Both hosts qualify all cells and five repetitions, all 90 scheduled processes,
20 CTest identities, 22 binary hashes/compilation targets and 20 assembly
replays. All six private/product execution callback pairs have identical
normalized instruction streams on each host. Complete source and launch hashes
match the frozen input.

| Host | Worker job | Complete controls and recovery |
| --- | --- | --- |
| V2 | `20261015T073943Z-26b98f30` | [artifact](evidence/graduation-v2-20261015/artifact.json) |
| Zen 5 | `20261015T073837Z-1f3125b1` | [artifact](evidence/graduation-zen5-20261015/artifact.json) |

Fresh-cheap ordinary selection costs 2.825 ns on V2 versus the private
control's 2.773 ns, and 1.679 ns on Zen versus 1.993 ns. These synthetic-seeded
noinline-wrapper diagnostics include ordinary argument transfer and return,
not observation work or native visit cost. Actual operations charge their
clocks, samples and selection separately below. Caller-known direct costs
2.238 / 1.723 ns on V2 / Zen versus the private control's 0.716 / 0.991 ns;
the measured public prior still maintains context/epoch invalidation. Callers
already choosing a legal path can execute it directly.

## Complete-operation results

The original large mixed C32 full-graph controls retain their gains. Values
are median ns per complete useful request across five passes.

| Host | Kind | Bare direct | Hinted Ring |
| --- | --- | ---: | ---: |
| V2 | Set | 37.943 | 20.417 |
| V2 | Map | 39.905 | 22.795 |
| V2 | Multimap | 59.759 | 31.895 |
| Zen 5 | Set | 43.520 | 12.100 |
| Zen 5 | Map | 45.525 | 15.698 |
| Zen 5 | Multimap | 73.394 | 25.770 |

For locality policy, each phase completes 64 batches of 256 useful requests.
The large mixed map at cutoff 32 shows the path choice and fully charged
observation cost; fixed controls are from the product process.

| Host / phase | Private adaptive | Loom adaptive | Fixed direct | Fixed Ring |
| --- | ---: | ---: | ---: | ---: |
| V2 / reused | 5.186 | 5.194 | 4.875 | 9.802 |
| V2 / interference | 6.186 | 6.148 | 5.947 | 10.333 |
| V2 / changing | 34.998 | 34.402 | 45.740 | 33.229 |
| V2 / return | 6.871 | 6.724 | 6.103 | 10.536 |
| Zen / reused | 3.974 | 3.980 | 3.661 | 6.079 |
| Zen / interference | 4.528 | 4.608 | 4.448 | 6.555 |
| Zen / changing | 23.646 | 26.409 | 55.626 | 27.178 |
| Zen / return | 4.836 | 4.928 | 4.549 | 6.449 |

Both policies use 63 direct / one Ring batch for reused work, zero direct /
64 Ring batches for changing work and 62 direct / two Ring batches on return,
in every pass of these cells. Corresponding actual sampled request counts are
96 / 96 / 128, additional to the 16,384 useful requests. Loom's interference phase usually
uses 64 direct batches; one pass per host needs two Ring batches and 128
samples rather than 64. The complete per-pass counts and sample times remain
in the retained panels.

Zen's changing-map median is worse than the private control. The median paired
product/private ratio is 1.032, spanning 0.951–1.161; its unchanged fixed-Ring
control spans 0.907–1.132 with median ratio 1.012. Both policies select Ring
throughout. Sampling also conditions current work. These cells limit claims
about small implementation differences. Across the 84 adaptive cutoff-32
cells, the median of casewise paired-ratio medians is 1.0008 on V2 and 1.0017
on Zen. This is an unweighted diagnostic, not a workload score. No such cell
loses more than 5% in all five pairs.

The higher cutoff remains a consequential negative. For large **all-hit**
changing maps at cutoff 128, Loom adaptive costs 32.465 versus fixed Ring's
22.862 ns on V2, and 31.238 versus 19.365 ns on Zen. Zen chooses 30 direct /
34 Ring batches in every pass; V2 does so in four passes and 28 / 36 in one.
The policy therefore requires a consumer-qualified cutoff. It establishes no
universal serial-cheapness rule.

The initial capture omitted older spike prerequisites declared by CMake and
stopped before compilation. The next Zen attempt stopped at the ordinary
wrapper guard before timings. The completed pre-repair V2 run is auxiliary;
its timings are not pooled with the final policy. Worker archives preserve
those outcomes.

## Replay reports or rebuild consumers

The selected evidence retains every used timing cell/repetition, sample and
decision count, unchanged competitor and compact control receipt. Full source,
executables, actual execution instruction-stream receipts, assembly, compilation
databases and mapping details stay in the verified worker archives. Both
archive-backed recipes successfully replay qualification with current tools,
without executing downloaded scripts or binaries:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/loom-locality-execution/evidence/graduation-v2-20261015 \
  build/reports/loom-execution-v2
```

Use the Zen evidence directory for its comparison. To rebuild consumers, recover
the full verified source through `replay.py --graduation --source-output NEWDIR`,
then configure with `SIXDB_LOOM_CONSUMER_SOURCE_ROOT=NEWDIR` and
`SIXDB_LOOM_GRADUATION_ONLY=ON`. Primitive targets remain independent of Ikea's
private header graph. The current `graduation.cpp` adapter keeps its own
experimental `Tag` and resets the public Policy when that cohort changes.
Rebuilding it with current libraries creates a new composition requiring
qualification; it does not reproduce the measured identity-bearing interface.
Exact measured sources and binaries remain archived.

## Comparison limits

Samples finish each dependent consumer chain before starting the next head.
They measure software-serial complete-consumer cost with normal CPU overlap,
not one-outstanding-miss or isolated load latency.

Small probes extrapolate from eight of 32 heads after two cheap samples. They
can miss heterogeneous costs. Still-fresh evidence can cover a first operation
after unreported interference. Expiry before every operation can request a
probe every time: the bound is work per probe, not elapsed time or a universal
sampling rate. A serial-cost cutoff does not establish a universal direct/Ring
crossover; the 128-ns counterexamples remain in the comparison.

The interference traversal conditions memory history without proving cache
service tiers. Generation/domain/expiry transitions are synthetic within one
pinned owner; this does not establish publication or physical migration.
Sampling also conditions the work it precedes. Rotated order and unchanged
controls expose drift but do not make every small gap causal. No PMU, IPC,
stall or miss-occupancy attribution is claimed.
