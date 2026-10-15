# Primary and secondary capacity together

> Lifetime comparisons below need corrected reruns after removing inter-phase validation allocation; see the [measurement correction](BENCHMARK-VALIDATION.md). Capacity and logical-byte observations remain useful.

The earlier geometry sweep fixed secondary bucket count at 1/16 of primary and
used the same byte unit in both tiers. Its K64/W16 map had a cliff: the crowded
128-byte control saved memory but put 1,271 of 6,144 keys into linked overflow;
192-byte units avoided congestion but slightly exceeded Abseil's logical bytes.

`HT_COUPLED_GEOMETRY` in `value_width_bench.cpp` retains those controls and adds
asymmetric 160/168/176/192-byte primary and 64/96/128-byte secondary units with
secondary ratios from 1/8 to 1/2. The actual solver still chooses arbitrary slot
counts and verifies field placement. These are experimental controls, not ten
public options. Growth thresholds, header widths and the solver's other encodings
remain open; this does not declare a final load policy.

The same complete histories measure construct/build, independent hit/miss, serial
value-dependent reads, update, delete, aged reads, reuse, release and lifecycle.
Content and mutation oracles use an independent map and compare full opaque bytes.
Ordinary and exact-width Abseil are both present. Requested, backing and owner
metadata remain distinct. These are scratch histories; no MVCC publication cost
is silently assigned to either side.

The first local K64/W16 map panel has 72 fresh-process histories: 384 and 6,144
keys, 12 arms and three repetitions. All complete-byte/mutation checks passed.
Selected 6,144-key results on the OrbStack ARM VM (not native qualification):

| Primary / secondary | Secondary buckets | Linked keys | Logical bytes | Backing bytes | Hit ns | Serial ns | Lifecycle ns/initial key |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 128 / 128 | 64 | 1,271 | 185,934 | 198,656 | 10.56 | 23.44 | 85.93 |
| 192 / 192 | 64 | 11 | 210,170 | 209,920 | 4.94 | 20.67 | 42.68 |
| 168 / 96 | 256 | 66 | 201,966 | 202,752 | 5.00 | 20.43 | 43.90 |
| 168 / 128 | 128 | 125 | 196,362 | 197,632 | 5.23 | 21.25 | 49.44 |
| Ordinary Abseil | — | — | 204,688 | unmeasured | 2.47 | 16.26 | 34.60 |

The middle ground was hidden by the old restriction. This improves the trade-off;
it does not establish broad parity or choose the 168/96 format. Non-64-byte units
can increase line crossings, and different key/value widths change capacity. The
additional local panel includes K16/W2 and K40/W16 maps and K40/W8 multimaps with two inline
values and the existing larger shared bag blocks. The multimap geometry controls
use uniform fanouts 1/2/3/8; the separate staged/lifetime studies cover mixed fanout.
All 432 additional complete histories pass. K40/W16 also admits denser choices;
these particular units do not solve the narrow-map or eight-value-bag memory
deficit. Native comparisons now cover all three requested baseline platforms;
these sparse control points still do not select a universal format.

The first native V2 panel completes 504 histories and 6,552 phase rows, three
repetitions each. The capacity mechanism survives native measurement:

| K64/W16 primary / secondary | Logical B | Hit ns | Serial ns | Lifecycle ns/initial key |
| --- | ---: | ---: | ---: | ---: |
| 128 / 128, 64 secondary buckets | 185,934 | 13.45 | 24.01 | 93.23 |
| 192 / 192, 64 secondary buckets | 210,170 | 6.80 | 20.64 | 47.56 |
| 168 / 96, 256 secondary buckets | 201,966 | 7.36 | 20.65 | 50.61 |
| 168 / 128, 128 secondary buckets | 196,362 | 7.18 | 20.70 | 53.05 |
| Ordinary Abseil | 204,688 | 3.16 | 14.41 | 40.79 |
| Exact-key Abseil | 204,687 | 3.28 | 15.06 | 40.73 |

This removes the crowded control's exceptional overflow cost, while preserving
the common-hit deficit. K40/W16's 168/128 control uses 189,594 B versus ordinary
Abseil 204,688 B, but **exact-key Abseil uses 180,129 B**. It has not met the
stronger memory comparison. The narrow-map cases are intentionally useful
negative controls: these units are much too large; the existing smaller layouts
must remain candidates. Multimap singleton/two-value storage is denser, while
eight-value bags still exceed Abseil and read much slower. The full table includes
those results and phases after churn. No preset is selected from this panel.

[Native V2 rows](evidence/coupled-neon/panel/summary.csv) retain every arm, baseline,
size, fanout and phase; the recovery bundle holds binaries/disassembly. These are
warmed histories, with no per-request cache-service measurement.

The [matched Zen 5 panel](evidence/coupled-zen5/panel/summary.csv) also completes
504 histories. K64/W16 retains the same byte counts: 128/128 costs 7.20 ns/hit;
168/96 costs 3.97 ns/hit and 13.76 ns/serial read; ordinary Abseil costs
2.04 and 9.98 ns respectively, with 204,696 logical bytes. The roomier 192/192
control is 3.67 ns/hit at 210,170 B. On this host the denser middle ground costs
about two ns per hit rather than the crowded control's five ns penalty. It still
has a throughput cost and does not replace the separate cold-execution evidence.

For two-inline W8 multimaps, the roomy control's complete lifecycle is
31.70/82.36/73.50 ns per initial key at fanout 1/2/3, versus Abseil
32.12/85.37/78.64 ns. At fanout eight it is 147.79 versus 167.92 ns, but uses
876,074 versus 663,400 B and reads in 13.51 versus 4.48 ns. Keep the lifetime
wins alongside those substantial storage/read losses; changing the external
owner must improve that exchange without losing the short-bag regimes.

[All local rows and recovery context](evidence/coupled-local/summary.csv) accompany
these selections. `coupled-run.py` preserves fresh-process order and verifies all
phase rows; `coupled-native.sh` runs the same histories on the target machines.


The [Granite AVX-512 panel](evidence/coupled-granite/panel/summary.csv), job
`20261015T014002Z-b3abc1a8`, completes the same 504 histories and 6,552 phase
rows with all complete-operation oracles passing. For K64/W16 at 6,144 keys:

| Primary / secondary / secondary buckets | Logical B | Hit ns | Serial ns | Lifecycle ns/initial key |
| --- | ---: | ---: | ---: | ---: |
| 128 / 128 / 64 | 185,934 | 10.52 | 21.61 | 87.77 |
| 192 / 192 / 64 | 210,170 | 4.64 | 18.71 | 40.87 |
| 168 / 96 / 256 | 201,966 | 5.15 | 20.22 | 46.81 |
| 168 / 128 / 128 | 196,362 | 5.21 | 20.09 | 47.01 |
| Ordinary Abseil | 204,696 | 2.47 | 14.27 | 32.96 |
| Exact-key Abseil | 204,695 | 2.64 | 15.02 | 34.53 |

Granite confirms the same capacity trade-off, with a larger hot-throughput cost
than Zen5. The 168/96 choice removes most overflow while saving a little logical
memory; it does not establish an overall winner. At W8/fanout8, the roomy
multimap has 222.87 ns/initial-key lifecycle versus Abseil 267.35, but reads in
14.23 versus 5.90 ns and retains 876,074 versus 663,400 logical bytes. These
remain useful mutation/read/memory trade-offs, separate from the cold Loom path.
