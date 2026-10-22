# Price the write program, NULL policy and instrumentation

Keep the masked owner operations and the native/immutable-base alternatives.
Inline repair plans remove most allocator churn, but give only a modest complete
write gain. Predictor fill does not earn its construction cost in these sparse
update histories. A larger pressure image can read faster because it changes
the useful operation. None of these results selects one Frame default.

The [batch comparison](BATCH.md) ruled out repeated batch scans as the principal
sparse-writer gap. Workbench located existing DHAT and software CPU-sampling
tools, so this investigation uses them instead of adding allocator interception.
The upstream [masked/NULL operations](../encoding/README.md#row-selected-aggregates-and-null-fill)
are now integrated into the real owner chronology, with construction charged.

## Separate measurement from the operation

The phase driver called full `allocation::statistics` after every update to
record peak Pool capacity. This scans allocation geometry; it is instrumentation,
not required Frame write work. Timing now disables that census. Independent
accounting passes retain its peak measurements. Cheap PMR high-water accounting
and one final Pool census remain available outside phase clocks.

The eight-arm accounting probe counterbalances native/heap-plan/inline-plan,
accounting on/off, and exact native/inline duplicates in the same process.
Across four inputs, 2,048 exact histories and 32 samples/cell, the native census
adds 12.8–20.2 us per eight-publication phase. Fitting pressure/EventTime cases
also show a clear added cost. Expensive run/exception rebuilding is noisier;
some small or contrary differences are unresolved. Probe duplicates span
.944–1.079. Do not subtract an estimate from earlier timings: use the directly
measured plain phases below. Previous instrumented cohorts remain historical
evidence with their source conditions.

`capacity_accounting=0` makes `peak_nonidle_bytes=0` **unavailable**, not zero
storage. The explicit accounting pass supplies that peak. Final Pool capacity
and the independently tracked PMR peak remain meaningful in plain records.

## What allocation removal buys

DHAT allocation stacks, restricted to repair planning inside `Machine::update`,
find the following successful ordinary heap allocations per eight-publication
dispersed phase:

| Input | Heap plans: blocks / requested bytes | Four-store inline plans: blocks / requested bytes |
| --- | ---: | ---: |
| Pressure | 486 / 18,560 | 32 / 4,224 |
| EventTime midpoint | 300 / 7,952 | 2 / 256 |

The private plan keeps four stores inline and spills larger lists into the
existing vector path. Dense replacements and anchor checkpoint repairs still
work. The shared planner/apply logic is templated over storage; there is no
second repair algorithm. All 64 widths/four checkpoint modes pass 22,528 plans
and 6,082,560 independently expected points/aggregates, including spill, move,
refusal, descriptor shifts and unaligned guards. Owner checks add 328,160
comparisons. Copied/rebuilt/examined work and persistent owner capacity match
the heap control. The new program also changes stack layout and initialization;
its measured gain is not a pure malloc-latency estimate.

Plain complete update phases, mixed NULL/presence, dispersed commands, all blocks,
no old reader, median us. Native is the strongest observed raw/FOR/coalesced arm:

| Input | Native | Heap repair/fallback | Inline repair/fallback | Batch overrides, debt |
| --- | ---: | ---: | ---: | ---: |
| Pressure | 73.4 | 145.7 | 138.1 | 139.9 |
| EventTime midpoint | 72.3 | 101.0 | 96.4 | 127.8 |
| UserID prefix | 97.0 | 865.2 | 877.5 | 122.6 |
| URLHash prefix | 92.5 | 1,468.8 | 1,449.6 | 142.7 |

Pressure's complete improvement is about 5%, despite 93% fewer plan allocations.
EventTime's smaller difference is near calibration noise. Repeated-hot pressure
is unchanged at 57.3 us. Keep inline storage as a bounded alternative, not a
claim that allocation removal closes the native gap. Run-heavy prefixes still
reject raw packed/delta fitting and pay rebuilding; that is a different program
choice. UserID repeated-hot batch overrides take 27.2 us versus native's 54.1 us,
before charging eventual folding.

## Reads still depend on the program

Dispersed read-rich complete phases have 64 queries after each publication and
512 final queries. Median ms, strongest observed native control:

| Input / Frame program | Mixed Frame / native | All-valid Frame / native |
| --- | ---: | ---: |
| Pressure / inline repair | 5.62 / 5.64 | 4.01 / 2.94 |
| EventTime midpoint / inline repair | 3.69 / 5.11 | 4.73 / 2.40 |
| UserID / retained base + overrides | 1.53 / 6.98 | 1.52 / 3.61 |
| URLHash / retained base + overrides | 5.67 / 7.10 | 5.56 / 3.64 |

Pressure mixed is parity, not a decisive win. EventTime mixed and run-heavy
UserID remain useful wins, while all-valid pressure/EventTime and URLHash retain
material losses. Overrides retain unpaid fold work. These are current same-source
comparisons, not a subtraction from an earlier codec or timing epoch.

## NULL fill: construction and later operations both count

The owner constructor consumes `fill_nulls` before the existing EncodedRegion
builder. Validity already has a separate Part, so no second validity plane is
retained. All-valid inputs bypass filling. Predictor/zero are optional controls;
the existing retained hidden payload remains a control. Both new modes pass
984,480 checks through raw/sorted/frequency dictionaries, all checkpoint modes,
old/folded readers and a genuine next publication. Construction with every
all-NULL input value page protected also passes; presence remains correct.

For mixed dispersed final folds, value-Part bytes and median conversion us:

| Input | Retain bytes / us | Predictor bytes / us | Zero bytes / us |
| --- | ---: | ---: | ---: |
| Pressure | 8,556 / 242.7 | 8,556 / 783.2 | 12,177 / 272.4 |
| EventTime midpoint | 17,424 / 264.2 | 17,424 / 800.3 | 19,337 / 288.2 |
| UserID prefix | 10,098 / 155.8 | 10,098 / 499.1 | 11,534 / 179.3 |
| URLHash prefix | 39,074 / 300.4 | 39,073 / 805.8 | 39,634 / 308.6 |

These histories start all-valid and introduce sparse NULLs while retaining old
physical payload. They do not test fresh zero/poison NULL construction, where
the upstream fill study has a different starting point. Here predictor's candidate
analysis buys essentially no bytes or read gain. Do not apply it to every write.

Pressure's zero-filled read-rich phase is 4.17 ms, versus retained 5.56 and
predictor 5.95. The actual final census explains the changed opportunity:
retained/predictor have 31 delta + one packed block; zero has 25 patched,
six delta and one packed. NULL outliers change the selected encoding and useful
masked reduction. EventTime instead changes 25 packed blocks to patched and
loses read time; run-heavy UserID/URLHash remain runs. This supports choosing
representation and operation together, not a zero-fill or minimum-byte rule.

The next useful discriminator is whether a packed-FOR program can earn its
additional bits directly, without inducing a different codec through NULL values.
The remaining raw delta point reconstruction also still materialises a prefix;
the existing catalogue's fused SUM deserves a separate owner-operation comparison.
Neither proposal is a measured improvement yet.

## Qualification and recovery

The core is exact encoding 3c3b5824/Ikea 2ce62539. The final upstream df8c262d
differs only in README/cost-driver code; its 417-run masked/NULL qualification
therefore covers these identical owner operations. The owner material check
passes 190,028 cases, including protected NULL inputs. The capacity-recording
source `1ac645e46ff37d3fa9450601a22eeb7962f3679f633249f26285d53a1ac28d71`
adds 159,907,840 original-coordinate point checks and 51,200 timed histories.

Plain timing source
`bc93a4abdb359339a22484c3d41c40f7a08478584067ef1f9cdb67d016ea8f23`
changes only census placement/labeling, retaining operation/repair/body code.
It has 51,200 exact aggregate histories plus 6,400 separate capacity histories.
Source `71b7b93d8b6881c414b3f043c2844ece8eba5eac99222db63ae0422a9563de8f`
adds the explicit balanced accounting probe. Do not pool the three epochs.
Plain bounded cells have 40 samples; full-surface cells have 20. Across rankable
nonempty cells, four exact duplicate pairs span .879–1.145, median 1.001.
The displayed update native/inline duplicates span .952–1.040; small differences
remain unresolved.

CPU-clock/task-clock sampling opened successfully but delivered too few update
samples for attribution. Callgrind's function-toggle run failed scope validation
(outside-query costs and inconsistent inclusive totals). Those diagnostics are
retained, with no CPU/instruction percentages claimed. DHAT counts ordinary heap
allocations and instruction lifetimes, not native time, Pool backing or traffic.
The whole-group copy halo, primary retention after adoption and earlier accounting
limits remain. Local pinned Clang 21.1.8/O3/native/generic AArch64 VM only.

[Selected timings, accounting, heap stacks/census and checks](evidence/program-costs-local/)
link to full frozen sources, exact binaries, raw repetitions, original-coordinate
verification and four small measured input slices. Masks, novelty and old readers
remain in the full chronology. No public Frame API/default/graduation or native
fleet/cold/Loom result is selected here.
