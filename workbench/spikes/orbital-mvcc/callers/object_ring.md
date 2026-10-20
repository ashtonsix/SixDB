# Selected Object Ring caller

This disposable composition uses an actual Loom Worker and the selected
`ObjectStore<ShapePhysical>`. [object_ring.h](object_ring.h) exposes a small
compiled caller for Tooling to consume; [object_ring_check.cpp](object_ring_check.cpp)
checks its meaning and ownership independently. It adds no module API or Engine
navigation rules, and contains no timing entry point.

## Work and interval

Every Request owns one stationary Scope, selects one actual Tuple Representation,
and follows its sorted-key dependent chain. A borrowed TupleProof admits the
exact fixed encoding; each selection must have that exact Shape. The Scope
protects cached bytes and interpretation through native admission, returned
quanta and retirement. The proof's caller-owned Shape remains live while the
Driver can use it. No Allocation/Hold substitutes for selected content authority.
Request/output storage is disjoint from protected source bytes and live caller
metadata, following the underlying native consumer's output requirement.

Cold `configure(width)` prepares one reusable Pool-backed Ring bank. `run()`
includes per-request Scope enrollment/destruction, Object selection, initial
hints, admission, replacement admission, useful reads and terminal Outcome
publication. Frame and Ring inventory construction is reusable cold work, with
its full `sizeof(Driver)` and Pool storage still owned by the caller. All three
controls use that same configured Driver. There is no prebound timing control.

Serial execution finishes each request ordinarily. Manual and actual Ring
execution split the same lower_bound into dependent key probes, candidate
confirmation and record consumption. Equality narrows left: duplicate keys
select the first logical row, as the complete selected consumer does. A payload
produces the next key only after decoding next/value/tag through the existing
Ikea exact-law body. A missing key contributes its absent digest once; zero
steps still validate Object/Shape selection but touch no payload.

Manual and Ring use identical live rotation and refill at returned visit-budget
boundaries. Vacancies remain vacant inside a quantum. New requests follow
survivors behind the current frontier; unbounded request spans reuse fixed
stationary frames. The whole-operation bank has no overflow. The separate
consequence history exercises both active and overflow borrowers. Changing
refill cadence or adding queued admission must change both compared controls.

Six explicit native values carry a Frame pointer, key, search bounds, remaining
steps and digest. No owning handle, Selection or callback-local View crosses
musttail. `yield` persists its argument tuple, so this is not wholly register
resident. Owning Frame state receives one terminal Outcome rather than per-hop
escaped results. The three stages compile independently for hints on/off.

Hints describe an irregular demand qualified against the minimum 15-byte window
left at every payload hint target. The first class can hint one occupied address;
key hints use row start, payload hints use row+8. This is a bounded bootstrap,
not a guarantee that an unaligned key or payload spanning two cache lines has
both lines covered. Serial, manual and Ring have identical hint opportunities.
Hardware/pressure policy is the Ring's immutable configured snapshot. No native
visit reads a clock or hardware profile. Default `run()` does not refresh policy
or poll. The optional [ready-Message control](../performance/controls/cooperative-polling/README.md)
services completions at returned boundaries in a separate disposable experiment.

## Consequential checks

The independent logical oracle scans for the first matching authored row and
uses an explicit digest recurrence. It reads no encoded bytes and calls no
native search, decoder or `consume_*` helper. Complete selected `read_proven`
also agrees with those expected Outcomes.

The check runs 756 matched panels: three controls, hints on/off, widths
1/2/4/8/16/32 and quanta 1/17/128 over seven fixtures, with 97,524 request
results. Cases include empty/singleton data, unequal duplicate payloads,
below/between/above keys, zero and maximum keys, missing tails, cycles and
step limits 0/1/2/3/15/16/17. A final tight row ends against a protected page;
empty storage forms no payload address. Source bytes remain unchanged.
Refused absent/mismatched-Shape zero-step requests leave their output untouched;
the Driver remains reusable after discarding all outstanding borrowers.
Earlier completed outputs may remain after a later admission refusal, so the
enclosing failed trial is abort-only.

A separate correctness-only three-visit history reads old A row 0, reads old A
row 1 through its saved Selection, then first-discovers old B after atomic A/B
replacement and Store reset. A new Scope verifies new A/B and the born Object;
old Scopes see born absent. Selective cleanup removes one active and one overflow
borrower before their frames are destroyed; survivors publish exactly once.
The independent totals are four accepted, seven visits, two completed, two
abandoned and one promoted. This one-Part Tuple history checks saved-selection
lifetime; it does not claim sibling-Part discovery.

An actual selected-copy Part survives all Scopes and Store teardown. Its old
binding and values remain correct until owner-lane release. PMR then reaches
zero; the empty configured Ring still blocks Worker shutdown until close.
Final owner service reaches zero Arena charge. All physical/control destruction
remains on the assigned owner Lane.

Pinned Clang 21.1.8 V2/Zen5 object inspection finds no ordinary calls or local
recurrence spills in the six native callbacks. V2 callbacks/yield use no stack;
Zen5 hint-enabled callbacks save/restore RBP. Continuing yield persists 48 bytes
in the slot and dispatch reloads them. The dynamic hint-switch alternatives
remain emitted (hinted Probe text: 1,360 B V2, 1,701 B Zen5); these static sites
are not executed hint counts. The generic completion path retains a conditional
ordinary overflow-promotion call, excluded by this Driver's zero-overflow
configuration. Entry ABI preservation is outside recurrence. No timing follows
from these observations.

Build `loom_object_ring_check` in an `orbital-mvcc` spike build and run its CTest
selection. The private release records native and full ASan/UBSan with
always-use-after-return/leak qualification and pinned V2/Zen5 compile-only
inspection separately. These checks establish correctness and machine-code
properties, not throughput, IPC, stalls, DRAM residency or a policy ranking.
The completed [wider operating comparisons](../performance/FEEDBACK.md#wider-request-interleaving-and-append-growth)
measure direct, manual and Ring reads, including cooperative Message service.
Those findings retain the workload, source and timing limits separately from
these correctness and code-generation checks.
