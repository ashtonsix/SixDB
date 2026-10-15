# Hierarchy geometry at fixed cardinality

A bucket layout's fitting capacity and the table's chosen bucket count jointly
determine memory and overflow. The earlier `n = 6 * primary_buckets` controls
kept input size independent of fitting slot capacity; they could substantially
underfill narrow-value layouts. This study instead fixes the key/value history
while changing the physical hierarchy. It asks where reduced spare capacity
pays off and where downstream traffic makes it expensive.

`HT_GEOMETRY_POLICIES` uses the complete byte-operation driver with sets and
W2/W8/W16 maps. CMake targets admit K16/24/32/40/48/56/64 separately. K8 stays in
the existing whole-table-list study: its domain fits that distinct regime.
Cardinalities are 384 and 6144, with the same keys, randomized insertion order,
value bytes, query history and ordinary/exact-key Abseil reservation for every
arm. Each arm has a fresh process and fresh Pool for each repetition; a shuffled
schedule and reverse pass retain order sensitivity.

The executable controls are the full cross product of 64/96/128/192-byte units
and primary bucket counts one quarter, one half, equal to, or twice the reference
count. The reference has one primary bucket per six input keys. Secondary count
remains one sixteenth of primary and uses the same byte unit. These are whole
organizations, not independent measurements of primary size and secondary
budget. The solver admits arbitrary fitting slot counts up to the current
32-slot lowering limit; that limit is not a production choice. Reports distinguish
fitting capacity, occupied tiers, unused unit bytes and total owner storage.

All arms measure construction, build, independent hits/misses, ordered or
dependent reads, existing-key mutation, erase and release. Full independent
byte-content and hit/miss oracles run outside timing. Maps use their actual
value digest to choose the next key in a proved full cycle. Sets have no value
payload: their first prototype's Boolean-dependent rank-array traversal became
only control-dependent in local AArch64 machine code, in both candidate and
Abseil. Those rows are now called `ordered`, not `serial`, and cannot qualify
strict serial lookup latency. A located-entry consumer with a matched external
successor array is a possible useful diagnostic; it still needs implementation
and machine-code validation and must charge the extra array and mapping work.

The first local screen retained 2,592 phase rows and 96 fresh processes under
`build/hashtable-design/geometry-local`, with its exact source and ELFs. It used
ten geometry controls; peer review identified useful missing compact-unit
pairings, now included in the full 4×4 panel. At K40/W2, halving the reference
bucket count changed initial logical memory from 1.003 to 0.502 times ordinary
Abseil, with similar or slightly better local read timings. Quartering it at
the same unit caused substantial key overflow and much slower hits. This is
OrbStack diagnostic evidence, not a native, cache-residence or preset claim.
The original set `serial` labels in that frozen raw output are disqualified
by the branch-synthesized hit results described above.

The expanded local 4×4 screen passes all independent oracles and report checks:
23,328 phase rows in 864 fresh processes across K16/K40/K64 and all four payload
cases. The independent fit calculation agrees with every compiled recipe's
capacity, used bytes and padding. Its preserved inputs and timed ELFs live under
`build/hashtable-design/geometry-local-expanded`. Narrow-key sets expose substantial
unit padding at the 32-slot ceiling; wide-key/value cases can instead move
thousands of keys into overflow when capacity is reduced. These outcomes rule
out a universal density adjustment and motivate native comparison of the whole
trade-off. They do not select a layout or repair the set-latency limitation.
The K16 set and K64/W16 map extremes also pass all 18 arms at both cardinalities
under ASan/UBSan: 72 complete histories, including the same independent oracles.

`geometry-native.sh` runs K16/K40/K64 and all four payload cases, preserving the
timed binaries, compiler commands, disassembly and two reversed process-order
passes. Native results must qualify those exact inputs before selecting policy.
The separate [selector experiment](selectors.md) holds those physical recipes
fixed while comparing two 16-byte windows with one 32-byte x86 comparison.

The [native cohort](evidence/geometry-native/README.md) is complete on Zen5,
Granite Rapids and Neoverse V2: 116,640 phase rows in 2,592 fresh processes.
All 36 timed binaries match their captures, every report replays, and all 5,184
read/update regions pass the call-boundary audit. This does not repair the set
latency limitation or prove every map's machine-code dependency. All three
workers have terminated.

Representative A10-reference cases below keep the same 6,144-key history.
Ratios divide candidate time/storage by ordinary Abseil; lower is better.
These are observed trade-offs, not selected policies or a Pareto frontier:

| Case | Actual primary A / unit | Initial logical memory | Independent hit time across hosts | Mutation lifecycle time |
| --- | --- | ---: | ---: | ---: |
| K16 set | 8 / 64 B | 0.720 | 1.23–1.38 | 1.24–1.29 |
| K16, W2 map | 9 / 64 B | 0.857 | 1.26–1.45 | 0.97–1.19 |
| K40 set | 8 / 128 B | 0.476 | 1.67–1.96 | 0.44–0.50 |
| K40, W2 map | 9 / 128 B | 0.502 | 1.43 V2; 4.16–4.42 x86 | 0.37–0.42 |
| K64, W2 map | 9 / 192 B | 0.752 | 1.61–1.99 | 0.40–0.47 |
| K64, W16 map | 10 / 192 B | 1.027 | 1.85–2.15 | 0.61–0.65 |

Memory savings are achievable for several previously underfilled layouts, but
they do not close the read gap. Crowding wide records can add thousands of
linked-overflow entries and severe read/mutation losses. The listed W16 case
has eleven linked-overflow entries; the other listed cases have none, though
they still use secondary buckets. Logical memory includes owner metadata and
does not establish an RSS or retained-version saving. Lifecycle sums construction,
build, update, erase and release; it excludes queries and Pool setup/drain.

The x86 K40/W2 outlier has a separate code-generation lead. Its independent-hit
loop copies the previous payload checksum into the count register, overwrites
only the low byte with 40, then uses BZHI for the next hash. A10's corresponding
loop freshly defines that register from a constant. Primary-hit paths otherwise
have equal representative instruction counts and no calls. A matched count-only
intervention is needed to assign the slowdown; selector changes can alter this
register allocation too. This issue prevents treating the outlier as an inherent
cost of halving the bucket count.

`geometry-run.py` records every process, timed binary identity and phase.
`geometry-report.py` validates the entire schedule, operation counts, tier
populations and an independent byte-plane fit calculation before retaining all
comparisons. This first finite study does not establish growth/migration policy,
aged reads after churn, retained-reader cost, arbitrary-value performance,
LLC/DRAM residence or platform selection. Pressure-sensitive choices need
matched caller-seed repeats and histories through the growth transition.
