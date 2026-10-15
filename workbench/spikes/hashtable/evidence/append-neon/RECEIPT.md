# Native checked/admitted StreamPack bag append

Group `20261013T190634Z-5ba9f183`, source manifest
`26dbdfeda68319b681c6fcfdc0ac4e28afa23d6713b2b66faf69ac1588156c04`.
All three jobs completed successfully; native owner checks, physical recipe
checks and link reservation/event-parity checks passed before timing.

| Target | Job | Native flags | Raw rows |
| --- | --- | --- | --- |
| Neoverse V2, c8g.large | `20261013T190644Z-b12efe41` | `armv8-a+simd`, `neoverse-v2` | [formats.csv](formats.csv) |
| Zen5, c8a.large | `20261013T190653Z-15ac212d` | `x86-64-v3`, `znver5` | [formats.csv](../append-zen5/formats.csv) |
| Granite Rapids, c8i.large | `20261013T190701Z-6860deee` | `graniterapids`, `graniterapids` | [formats.csv](../append-granite/formats.csv) |

Clang 21.1.8, Release `-O3`, explicit ISA, both auto-vectorizers disabled and
`-inline-threshold=1000`. All 25 append translation units have
`HT_FORMAT_APPEND=1`. The runner pins each single-threaded process to the first
allowed CPU (CPU 0); Granite SMT is disabled. There is one native host per target.
The manifest hash is SHA-256 of `json.dumps(manifest, sort_keys=True).encode()`,
not the physical manifest bytes. Captured `formats.cpp` SHA-256 is
`e1bf86828caff86a6e9486bacd41e753dc2fcb0c9ed30d02732cf21d1e1f5116`;
`links.h` is `a6cb7383aabfba52b81bf8700b992f3f13d63f9e68b39120fa5525639ff8689a`.
The captured driver, report, runner, owner/layout headers and reservation check
match this manifest on all targets. Full sources and compile commands are in
each verified archive.

## Coverage and interpretation

Six arms are checked/composed, checked/direct, admitted/composed,
admitted/direct, ordinary Abseil and exact-key Abseil. Each target has 270
processes, 45 width/kind/geometry cases, nine phases and nine repetitions:
2,430 cells / 21,870 rows. K8 has A6 only; all other widths have A6/A10. Each
process runs one contender/case; process order is shuffled reproducibly, with
all repetitions inside that process and a fresh Pool each time. Repetitions
are not separate hosts or process restarts. Static first-use format admission
can affect the first repetition. Phases have untimed oracles between them.

Every candidate arm has identical requested bytes, backing capacity, metadata,
slot counts and build primary/secondary/overflow populations in every phase and
repetition. The phase-summed lifecycle equals construct + build + update + erase
+ release; it excludes reads, inventory, oracles and Pool setup/drain. The
[comparison script](compare.py) independently checks full cell coverage, these
per-row equalities and lifecycle sums before generating ratios. The frozen
`formats-report.py --append --complete` also reproduced each archived summary.

Only bag slot acquisition changes. The private fixed/full/immortal owner admits
the existing `author::append_unchecked` leaf after validating its format,
capacity, disjoint input and relocation/lifetime conditions. Key overflow,
layout, owner count, growth, blank input and copy source are unchanged. Direct
arms substitute bucket fields, not the bag representation. Set/map arms have
unchanged owner types and serve as negative controls.

The timing history covers singleton-to-bag promotion and short bags: build
promotes one third of keys to two values; update promotes the other two thirds
and adds a third value to existing bags. It does not time long-bag extension
across the larger link families. Native checks cover those families but cannot
extend the performance claim. Inventory equality is not a measurement of RSS,
cache residency, transient growth peak or dynamic memory traffic.

## Actual timed boundaries

The [clock audit](clock_audit.py) walks the final disassembly between the actual
start/end clock calls of K40/A10 multimap update, then follows its native callees.
In each candidate arm the per-element call is specifically
`try_set(...)::lambda()::operator()`, not a claim that the full mutation has
inlined into the driver. The allocation/proposal path and whole-owner operation
remain charged. Both ordinary and exact-key Abseil search in the timed loop,
retaining missing-key preparation and `InlinedVector::EmplaceBackSlow` helpers.

| Target | Checked/admitted bag acquire addresses | Stack evidence |
| --- | --- | --- |
| V2 | `0x61280` / `0x833e0` | 16,624 / 176 bytes, including saved registers |
| Zen5 | `0x6b2c0` / `0x92260` | `sub rsp,0x40b8` / `sub rsp,0x78`, both with six preceding pushes |
| Granite | `0x68110` / `0x8dd70` | `sub rsp,0x40b8` / `sub rsp,0x78`, both with six preceding pushes |

These are `links<64,false,0,...>` bag acquisitions shared across key widths.
The admitted body removes checked input-alias, lengths and dispatch calls and
folds the zeroed stack input into destination zeroing; it still contains
runtime layout branches, control writes, allocation/growth and memset calls.
The checked path retains its stack input and checked append dispatch. These are
static differences, not a claim that the full large frame is touched, or a
cycle/cost attribution. The source has not changed the blank-input contract.

One unchanged V2 control requires particular caution: K16 set A10 composed arm 2
has median update 24.60 ns versus arm 0's 8.75, with disjoint raw ranges. Its
44-instruction timed region matches arm 0 modulo relocation and calls the exact
same mutation helper at `0xae540`. This rules out a different inlining boundary
there; it does not diagnose placement or runtime effects. Other control outliers
are retained too. Near-unity aggregate negative controls do not validate every
individual timing cell.

## Result

Median admitted/checked ratios across the 15 multimap cases, shown as
composed / direct (lower is faster):

| Target | Build | Update | Phase-summed lifecycle |
| --- | ---: | ---: | ---: |
| V2 | 0.964 / 0.980 | 0.933 / 0.948 | 0.969 / 0.970 |
| Zen5 | 0.930 / 0.950 | 0.927 / 0.913 | 0.933 / 0.940 |
| Granite | 0.934 / 0.950 | 0.938 / 0.941 | 0.936 / 0.946 |

All 30 multimap update medians improve on each x86 host; 26/30 improve on V2.
Raw nine-repetition ranges are nonoverlapping in the faster direction for
14/30 Zen5, 4/30 Granite and 3/30 V2 comparisons. Those descriptive counts and
case medians are not significance tests or workload-weighted averages.
Set/map phase-ratio medians generally stay near 1.00, with the outliers above.
The substitution gives a modest complete-operation improvement for this
short-bag history; it does not close the gap to ordinary Abseil. Admitted
multimap update remains about 5.0–5.9 times ordinary Abseil at the median across
cases; lifecycle remains about 2.3–3.5 times. No layout or production policy is
selected from this cohort.

## Reproduction and recovery

All raw rows, process order, check outputs, compiler/host context and resource
reports are retained. Each `artifact.json` recovers its complete verified worker
archive, including the measured executable, disassembly, compiler commands,
logs and source. No second source capture is implied by these authored notes.

Run the retained artifact report recipe to rebuild all cell summaries, or run
`compare.py` on the three evidence directories to reproduce the equality checks
and cross-case ratios. To repeat the clock audit, recover `assembly.gz` into
ignored storage with `artifacts.py fetch ... --file assembly.gz`, then pass that
results directory and a new ignored output directory to `clock_audit.py`.
