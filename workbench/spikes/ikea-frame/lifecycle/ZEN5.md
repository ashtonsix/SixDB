# Warm owner phases on Zen 5

The current compressed programs lose most warm cases to the strongest native
form, especially with AVX-512. The mixed UserID AVX2 case retains a useful win
even after a final fold. The ARM VM rankings in [program costs](PROGRAM-COSTS.md)
are not a Zen 5 recommendation. Keep native forms and compare complete programs;
neither compression alone nor a faster isolated repair selects a Frame policy.

This repeats the existing owner chronology and its native raw/FOR/coalesced
controls. Two 4096-row groups, eight ordered 64-command batches, original block
coordinates, NULL/presence transitions, real publication, adoption and old
readers remain. The four 8192-row slices are forcing cases, not population weights.
Construction is outside the phase. Updates, queries, conversions and old-reader
release/drain are inside; final current-object disposal and Pool shutdown are
verified outside it. Plain clocks exclude the full Pool census; capacity uses
separate passes. This is warm complete-phase time, not independent point
throughput, a DRAM/cold result, or staged Loom performance.

## Read-rich complete work

Dispersed edits, mixed presence/NULL transitions, all blocks, no retained reader.
There are 64 SUM/COUNT/presence queries after each publication and 512 final
queries: 1024 complete queries plus the writes. Median milliseconds; native is
the fastest observed raw/FOR/coalesced control in that same cell.

| ISA | Input | Native | Inline repair/fallback | Batch overrides, debt | Batch overrides + final fold |
| --- | --- | ---: | ---: | ---: | ---: |
| AVX2 | Pressure | 2.141 | 4.857 | 4.007 | 4.724 |
| AVX2 | EventTime midpoint | 2.039 | 2.343 | 2.596 | 2.666 |
| AVX2 | UserID | 2.976 | 2.298 | 1.232 | 1.693 |
| AVX2 | URLHash | 3.078 | 9.491 | 4.021 | 6.895 |
| AVX-512 | Pressure | .944 | 3.080 | 2.427 | 3.014 |
| AVX-512 | EventTime midpoint | .904 | 1.900 | 1.987 | 2.147 |
| AVX-512 | UserID | 1.164 | 2.278 | 1.268 | 1.695 |
| AVX-512 | URLHash | 1.165 | 9.369 | 3.817 | 6.716 |

The AVX2 UserID final-fold program takes 43% less complete time than native.
The no-fold arm is faster still but retains maintenance debt. Neither wins this
AVX-512 cell. Pressure's larger zero-filled image improves its encoded program
but still loses to native: 3.619/2.340 ms on AVX2/AVX-512 respectively.

All-valid values-only histories are a necessary counterexample. AVX-512 native
versus inline repair / override debt / final fold, milliseconds:

| Input | Native | Inline repair/fallback | Batch overrides, debt | Batch overrides + final fold |
| --- | ---: | ---: | ---: | ---: |
| Pressure | .495 | 2.121 | 2.417 | 2.390 |
| EventTime midpoint | .421 | 1.675 | 2.009 | 2.010 |
| UserID | .518 | 1.800 | 1.257 | 1.371 |
| URLHash | .521 | 4.420 | 3.760 | 3.767 |

## Update-dominated complete work

Same mixed dispersed edits, one query per publication and no final queries.
Median microseconds for all eight batches, not nanoseconds per assignment:

| ISA | Input | Native | Inline repair/fallback | Batch overrides, debt | Batch overrides + final fold |
| --- | --- | ---: | ---: | ---: | ---: |
| AVX2 | Pressure | 42.51 | 119.34 | 119.14 | 251.27 |
| AVX2 | EventTime midpoint | 45.68 | 71.94 | 102.23 | 234.13 |
| AVX2 | UserID | 60.28 | 594.86 | 97.05 | 191.46 |
| AVX2 | URLHash | 60.55 | 890.89 | 130.61 | 287.60 |
| AVX-512 | Pressure | 33.63 | 105.95 | 108.31 | 245.99 |
| AVX-512 | EventTime midpoint | 37.12 | 68.21 | 94.09 | 255.22 |
| AVX-512 | UserID | 47.01 | 595.13 | 96.44 | 191.81 |
| AVX-512 | URLHash | 47.17 | 898.48 | 129.16 | 284.40 |

Run-heavy prefixes reject fitting raw packed/delta repair and rebuild. Override
merging avoids much of that work but does not beat native here. Longer histories
must price accumulating corrections, periodic folds and reader age; eight batches
cannot select a sustained policy.

## One concrete execution seam

The measured catalogue's SUM and masked SUM explicitly use SeriesPack's
16-row `composition::native_ops`, including in AVX-512 builds. In the exact
AVX-512 ELF, `masked_sum<10>` uses XMM/YMM work with no ZMM instructions;
the native dispatch contains ZMM scan loops. SeriesPack already offers
`wide_ops<N>`/`native_group` independently of physical Local tile geometry.
Those wrappers have not used it. A bounded substitution can test this unused
capability without changing bytes. It is not yet a measured improvement or an
attribution of the entire gap. Shared decode for SUM/COUNT and representation
choice remain separate opportunities.

## Qualification and recovery

One on-demand `c8a.xlarge`, AMD EPYC 9R45, CPU 0, Clang 21.1.8/O3/assertions.
AVX2 uses `-march=x86-64-v3`; AVX-512 uses `-march=znver5`; both tune `znver5`.
The recorded macros confirm AVX2 without AVX512F in the former and AVX512F in
the latter. Wider ISA availability and code generation change together; their
absolute difference is not a pure vector-width experiment.

Frozen source
`a525096b5b8f323e83439f08039c33c896ffe6801375f3e0264c856b0e131e48`
matches all 3470 worker source hashes. The operation cut is core `3c3b5824`/Ikea `2ce62539`,
identical to df8c262d for these operations. Later point-prefix4a265ceb, fused work,
sustained histories and the in-flight Ikea interface migration are absent.

Each ISA passes 51,200 timed histories, 6,400 separate accounting histories,
11,796,480 original-coordinate point comparisons, material/repair/batch/nullable
owner checks and the 11440 timing-order checks. Bounded cells have 40 samples,
full-surface cells 20. Four exact duplicate pairs also match their logical work
inventories. Nonempty cells of at least 5 us have duplicate time ratios
.956–1.051 on AVX2 (320 pairs) and .946–1.041 on AVX-512 (319), medians 1.000.
Close differences remain unresolved. Each exact executable was copied before
running and its hash rechecked afterwards; both recovered measurement ELFs
still match their timing identities.

[Selected evidence](evidence/native-zen5/) keeps every timing cell, controls,
ISA/compiler and check identities. Its verified worker archive contains raw
repetitions, separate capacity passes, original slices, frozen source and all
exact executables. [Recovery](evidence/native-zen5/REPRODUCE.md) names the runner.
The earlier missing-target attempt and Spot setup interruption produced no
timings in this cohort. No Frame API, universal representation or default is
selected.
