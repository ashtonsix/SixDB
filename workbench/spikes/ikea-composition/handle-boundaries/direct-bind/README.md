# Direct predicate binding from an admitted law

This finishes the cold-preparation question left open by the
[copied-projection comparison](../PROJECTION-NATIVE.md). That study qualified
the plan-independent predicate and measured complete block commands, but built
all generic plans before timing. The
[transient-schema negative](../../../orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase)
also makes repeated preparation a consequential cost. The new comparison starts
from an already validated TuplePack layout and charges admission, operation
preparation and every query. It measures warm resident data, not cold caches.

`bind_predicate<Rows>(law, code_index, named_view)` selects an existing validated
code without preparing packet ordering or a general shuffle. It produces the
same predicate as plan-based binding: copied code, borrowed named view, no plan
or layout borrow. The supplied law must be authoritative for that view's bytes;
the view retains unit extent rather than encoding identity. Extent mismatch
rejects before invalid index; failure reads no payload. A temporary law is safe,
while a temporary view is refused.

The owning predicate check covers the original eleven packet shapes, all valid
width/shift pairs, guarded exact tails, real protected expired plans and layouts,
rank 127, full-size invalid indices and admission before inaccessible payload.
The hot filter body and existing binding paths are unchanged by this overload.

## Complete preparation commands

[The fixture](commands.cpp) preserves the earlier block study's 64-block
population: eight widths, exact 13-row tails, full/sparse activity, tight/strided
and mixed placement, clustered/bursty/shuffled order, and 1/8/64 queries per
block. An independent physical-bit oracle exhausts every timed RHS/activity
domain before timing and checks ordered complete traversals.

Every route attaches the same exact view once per block command:

- `construct_plan` makes a generic single-code packet plan, binds and queries.
- `shared_plan` selects an already prepared shared plan, binds and queries.
- `direct` copies the validated code, binds and queries.

Layout validation and fixture allocation precede timing in all routes. A generic
plan bank remains resident in the common fixture, including where unused; its
actual fixture bytes and the control bytes required by each route are separate
counters. This fixture demonstrates no allocator-class or resident-memory win.
Reported CPU ns/command divides the timed 64-block traversal by 64; ns/query
divides again by query count. Preparation is included rather than subtracted.

[The native runner](run.py) builds all profiles and runs independent checks
before any timed process. Routes and an identical shared-plan duplicate use one
linked executable, the same argv0, working directory and output path. Five
explicit orders retain every sample; the duplicate is never a correction factor.
The source capture and worker archive own raw binaries, actual linked assembly,
compile commands and outputs. The existing old-to-new filter losses remain a
different comparison.

```sh
orb -m ubuntu python3 workbench/spikes/ikea-composition/handle-boundaries/direct-bind/run.py \
  --output build/ikea-direct-predicate-local --check-only --profile neon
```

## Native findings

The [source-qualified result](../../evidence/interface-cohesion-v2-20261023/predicate-direct-native.json)
retains [all 81 cases and five samples per route](../../evidence/interface-cohesion-v2-20261023/predicate-direct-native-cases.csv).
All 81 direct/construct paired medians improve by at least 5%. Direct binding
removes a real `prepare_packet_read` call per block, rather than merely reducing
the nominal plan size. The common fixture still retains its plan bank.

Against an already prepared shared plan the result is different:

| Profile | Direct/shared paired-median range | Cases gaining ≥5% | Cases losing ≥5% |
| --- | --- | --- | --- |
| NEON / Neoverse V2 | 0.658–1.121 | 9 | 12 |
| AVX2 / Zen 5 | 0.915–1.006 | 6 | 0 |
| AVX-512 / Zen 5 | 0.912–1.002 | 7 | 0 |

Every ARM one-query case improves, but all nine 64-query cases and the three
tight eight-query cases lose over 5%. For clustered tight blocks, CPU ns per
complete command are:

| Profile | Queries | Construct plan | Direct | Shared plan |
| --- | --- | --- | --- | --- |
| NEON | 1 | 259.58 | 11.26 | 14.55 |
| NEON | 8 | 300.81 | 59.27 | 55.44 |
| NEON | 64 | 659.81 | 465.66 | 416.25 |
| AVX2 | 1 | 162.75 | 9.17 | 9.23 |
| AVX2 | 64 | 312.95 | 153.58 | 167.42 |
| AVX-512 | 1 | 121.03 | 5.43 | 5.86 |
| AVX-512 | 64 | 285.90 | 168.85 | 171.72 |

These absolute medians and the paired-median ratios are separate statistics.
The identical shared-plan duplicate's paired medians range from 0.983 to 1.007
across the three profiles. Every sample remains in the evidence; the duplicate
does not correct another route's timing.

Actual linked inspection covers all nine complete traversal functions. ARM and
AVX-512 use the same out-of-line predicate filter symbol for all three routes;
AVX2 inlines filtering and hoists mask and strided-address work. Direct and
shared bindings do not call generic preparation. Their local frame reservations
differ by 16 bytes. Direct's larger complete function also contains a separate
zero-query traversal that no timed case uses.

The ARM shared/direct query loops have the same 25-mnemonic sequence, with
different registers, operands and stack placements. Their predicate and view
addresses differ; function and loop placement also differ. The emitted frame
and preparation reductions do not explain the reused-query loss. No PMU or
causal discriminator qualifies that attribution here.

The [common cohort receipt](../../evidence/interface-cohesion-v2-20261023/execution-controls-native.json)
owns protocol, source capture, failure recovery and complete worker artifacts.
The [functional receipt](../../evidence/interface-cohesion-v2-20261023/predicate-direct-binding.json)
qualifies the binder's admission and lifetime boundaries; the separate
[representative codegen bridge](../../evidence/interface-cohesion-v2-20261023/predicate-direct-codegen.json)
retains existing caller records exactly. These are different proofs. The native
comparison does not resolve earlier cohesion/projection losses or establish
performance for a Directory or Frame predicate owner.
