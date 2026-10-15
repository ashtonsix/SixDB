# Capacity families across growth levels

> Lifetime comparisons below use an allocating inter-phase oracle and need corrected reruns; see the [measurement correction](BENCHMARK-VALIDATION.md). Logical-byte, slot and overflow observations are unaffected.

The consolidation comparison overreserves its narrow maps, while earlier wide
map controls trade excess spare capacity against linked-overflow cliffs. A
physical byte unit alone cannot choose the organization. This comparison couples
primary bucket count, per-bucket capacity, secondary count/capacity and padding.
It reuses the complete-operation driver, including ordinary and exact-key Abseil,
from its first executable form.

The actual layout solver computes extents from the key fingerprint/suffix widths,
value bytes, container kind, tag and platform. `record_table::unit_for_slots` is
cold spike admission using the same field-placement body as the fitting recipe;
there is no second byte-plane formula. Final padding can admit additional slots,
which the compiled recipe and result columns report. This helper is not a chosen
production API or a replacement for the broader solver grammar.

The experiment starts with three capacity families. At the reference population,
their average primary populations are 6, 12 and 24 keys/bucket; requested primary
capacities are 7, 14 and 28. Secondary bucket count is one quarter of primary, with
requested capacities 3, 5 and 8. Each has paired 8-byte and 64-byte extent rounding.
The old 128/128-byte and 192/192-byte, 1:16 secondary controls remain. All slot counts are
arbitrary; only the number of buckets must be a power of two. These finite
candidates are experiments, not final load thresholds or a statistical guarantee
against overflow. Timing and observed tier populations decide their usefulness.

Every layout is measured at 50%, 100% and 125% of the reference population, at two
bucket-count levels and three caller seeds. This exposes a capacity band rather
than choosing a single favorable exact cardinality. It does not implement a new
dynamic growth policy: the existing migration/worker caller still needs to use
the resulting choices through actual transitions. Both baselines reserve the
actual input population and then undergo the same update/delete/reuse history.

Cases cover every hierarchy key domain K16 through K64, sets, opaque narrow/wide
maps, and narrow/wide multimaps. K8 retains the independently qualified compact
whole-table list. Multimaps use the current shared linked owner and a16-byte
inline budget when it can hold values; W32/W64 use the8-byte external descriptor.
The wide values cannot fit either 8 or 16 bytes, so the extra 8 bytes would buy no inline
capacity. This panel holds that owner policy fixed; it does not select it over
larger inline budgets, contiguous vectors or buffers.

Construction, build, independent hit/miss throughput, dependent map/bag reads,
mutation, deletion, aged reads, reuse and release remain separate. Sets retain
the honest `ordered` label: their existing Boolean-dependent loop does not prove
strict serial lookup latency. Full-byte/multiplicity oracles run for every timed
history. Logical bytes, allocation backing and owner metadata are separate;
this warmed-read comparison establishes neither a named cache tier nor isolated RSS.

Larger buckets can improve occupancy while wasting cold prefetch bandwidth. The
current staged reader hints whole bucket units; any large-unit candidate must
subsequently face that complete Loom consumer and selective-window alternatives.
An attractive hot/memory result is not a cold-format selection. Suffix/selector
packing variants and actual Worker/retained-version costs also remain in scope.

## Current qualification

The local pilot completes 420 histories with independent full-byte/multiplicity
oracles and offline checks of field fit, operation counts and matrix coverage.
It includes all eleven cases at three populations, but only one seed/repetition
and the smaller bucket-count level. It is a diagnostic screen, not native format
selection. Smaller solver-sized families sometimes use fewer logical bytes than
exact-key Abseil; wide singleton multimaps still carry substantial external-owner
cost. Large wide-map buckets can require dozens of whole-bucket prefetch lines.
The report labels these hint costs as modeled, not measured memory traffic.

The native comparison and odd-population sanitizer checks use capture
`capacity-families-source-20261015-v3`, digest
`2ca93759e425687da8c00acc3f13873ee55df54de69764dbe21fc34406bda22a`.
Native runs are complete and verified: Zen 5 AVX2 `20261015T051903Z-29a0885a`,
Neoverse V2 NEON `20261015T051903Z-13345e6d`, and Granite Rapids AVX-512
`20261015T051903Z-9e1eba07`. Each completes 9,180 histories / 119,340 phase rows at
two bucket-count levels, three populations, three seeds and three repetitions;
multimaps also vary fanout. ASan/UBSan/always-stack-UAR completes 280 histories
on five boundary cases at odd and crowded populations, plus the all-domain
migration check. Source captures remain unchanged.

Retained [Zen 5](evidence/capacity-families-zen5/qualified/summary.csv),
[V2](evidence/capacity-families-neon/qualified/summary.csv) and
[Granite](evidence/capacity-families-granite/qualified/summary.csv) summaries keep
all arms, seeds, populations, fanouts and phases. The raw operations, process
receipts, captured source, binaries and assembly remain in the referenced worker
archives. Report recipes recover those raw inputs and repeat the independent
fit/count/coverage checks. Retaining every summary row preserves the rejected
families and memory/throughput trade-offs without duplicating the larger raw run.

## What the comparison changes

The larger families remove the old wide-map overflow cliff and can save memory
against exact-width Abseil. They do not close the independent-hit gap. The table
below uses the **same experimental arm 4** throughout: 28 requested primary
slots, eight secondary slots, 8-byte rounding, and 6,144 keys. This is a useful
control, not a selected preset or a per-row search for the fastest layout.
Cells show **ours / ordinary Abseil**, in absolute nanoseconds. Each number is
the median of the three per-seed medians. Lifecycle is construction, build,
update, half deletion, reuse and release, divided by initial keys; it excludes
the separately measured reads and untimed oracles.

| Case | Platform | Independent hit ns | Dependent read ns | Lifecycle ns/initial key |
| --- | --- | ---: | ---: | ---: |
| K40 / W2 map | Zen 5 | 3.10 / 2.07 | 9.70 / 8.66 | 21.48 / 26.44 |
| K40 / W2 map | V2 | 6.32 / 3.20 | 15.73 / 13.14 | 35.03 / 41.58 |
| K40 / W2 map | Granite | 4.06 / 2.77 | 13.99 / 12.12 | 26.13 / 32.25 |
| K64 / W16 map | Zen 5 | 3.50 / 1.94 | 11.99 / 8.85 | 26.40 / 25.46 |
| K64 / W16 map | V2 | 7.73 / 2.98 | 18.67 / 13.47 | 47.95 / 42.94 |
| K64 / W16 map | Granite | 4.49 / 2.42 | 16.24 / 12.66 | 34.25 / 32.69 |
| K64 / W64 map | Zen 5 | 5.92 / 2.84 | 15.09 / 11.35 | 42.68 / 37.74 |
| K64 / W64 map | V2 | 9.75 / 4.50 | 23.20 / 16.91 | 92.52 / 96.41 |
| K64 / W64 map | Granite | 6.56 / 3.44 | 20.73 / 16.11 | 66.17 / 56.00 |

Logical bytes at the same population are separate from those timings:

| Case | Ours | Ordinary Abseil, x86 | Exact-key Abseil, x86 |
| --- | ---: | ---: | ---: |
| K40 / W2 map | 49,960 | 139,208 | 73,720 |
| K64 / W16 map | 181,544 | 204,696 | 204,695 |
| K64 / W64 map | 550,168 | 597,624 | 597,623 |

V2 baseline owner metadata is eight bytes smaller in these cases; candidate
logical bytes are unchanged. Allocation backing is retained separately in the
summary; baseline backing is unmeasured, not zero. These are not RSS numbers.

Sets and bags also shape the decision. With the same arm 4/reference population,
the K16 set uses 12,584 logical bytes versus ordinary Abseil's 24,604 on x86.
Zen 5 independent hits are 1.41 / 1.41 ns and lifecycle 12.12 / 16.85 ns. V2
instead favors the medium arm 3: 2.56 ns/hit and 18.87 ns/lifecycle, against
arm 4's 3.28 and 24.16 ns and Abseil's 2.34 and 27.01 ns. Its 15,656 logical
bytes remain below Abseil's 24,596. The set `ordered` result is retained, but
neither it nor these hit loops establishes strict serial set latency.

K40/W2 singleton bags in arm 4 use 166,192 versus Abseil's 270,184 logical bytes
on x86; Zen 5 hits are 4.41 / 3.53 ns and lifecycle 29.94 / 33.56 ns. At eight
values per key, hits are 7.58 / 4.16 ns and lifecycle 112.04 / 115.49 ns; appending
the ninth value crosses the 16-byte inline budget and remains part of that
lifecycle. Wide K64/W64 singleton bags still use 1,737,936 versus 663,112 bytes,
with hits 8.45 / 4.71 ns and lifecycle 126.68 / 105.58 ns. Geometry does not fix
that external-value ownership deficit. At fanout eight, Granite also preserves
a useful exception: arm 7 reads all values in 38.20 ns versus Abseil's 42.02 ns,
but uses 4,938,752 versus 3,808,840 logical bytes. Keep both sides visible.

## Capacity bands, padding and execution

The finite bands rule out selecting only the densest favorable snapshot. With
3,072 keys in the same K64/W16 arm 4, ours still reserves 181,544 logical bytes
while ordinary Abseil uses 102,296 on x86. With 7,680 keys, ours uses 195,534
versus 409,496, but has a median 319 linked-overflow keys. Zen 5 hits rise to
4.30 ns and lifecycle to 42.27 ns. Those are different trade-offs at different
points in a growth cycle, not evidence for one universal load threshold.

Padding can buy capacity rather than merely waste space. K40/W2 map arm 7 rounds
the primary extent from 176 to 192 bytes, admitting 31 rather than 28 slots;
its secondary also grows. At 7,680 keys, it has no linked-overflow keys for the
median seed, uses 57,640 bytes and takes 3.29 ns/hit / 23.66 ns/lifecycle on Zen 5.
Arm 4 uses 54,426 bytes but takes 3.86 / 39.20 ns with 288 linked keys. The wider
record cases do not necessarily admit another slot from the same rounding.
Count actual fitted slots and measured overflow, not just alignment labels.

V2's reference-population K40/W2 map favors the medium arm 3 over arm 4:
5.39 versus 6.32 ns/hit and 27.93 versus 35.03 ns/lifecycle, at 55,592 versus
49,960 logical bytes. Larger buckets are therefore not a portable default.
The native map disassembly was inspected for K40/W2 and K64/W64, candidate arms
2–7 and both baselines. Timed read control-flow regions are resolved from the
captured source's DWARF callsites. x86 has no outlined lookup forwarding calls;
some V2 arms outline the whole query loop once, with no calls inside it. This
does not assign the timing gap to an instruction count or prove that the same
holds for every container. All eleven targets on each host disable both
compiler autovectorizers; the existing explicit native selectors remain in use.

Cold cost can reverse a hot choice. In K64/W64 arm 4, the primary unit is 2,000
bytes. The current whole-bucket hint covers a modeled 32.26 cache lines per hit
including the observed secondary fraction, versus 16.82 for medium arm 3.
This is a stride/alignment and tier-population model, not measured traffic.
The complete Loom consumer must compare whole-bucket hints with selective
selector/suffix and located-value windows, including the cost of any extra
dependent stage. The previous cold wins do not automatically transfer to these
larger formats.

The immediate decisions are to reject equal-sized 128/192-byte tiers as a
universal geometry and to carry medium and large capacity families into actual
growth and cold-consumer comparisons. Favoring a family by platform is supported
as an experiment; final formats still need complete lifetime/residence evidence.
The smallest requested-capacity family remains a control for cold traffic and
lower density, not an equal-status public option. Coupled layout/inline/owner
selection, rather than another isolated multimap append patch, remains the next
architectural task.
