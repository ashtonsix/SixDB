# Orbital and Loom hot paths, October 2026

Where do the current owner paths spend their time, and what would cheaper
versions of them look like? This survey times the implemented allocation, Hold,
foreign-return, Lane, Object catalog and Loom Task paths. It prototypes five
alternatives as patches against a copy of Orbital; production sources are
unchanged. [AHEAD.md](AHEAD.md) carries the consequences forward to the planned
Object, Allocation, Journal, Port and Worker submodules, with Loom above them.

## Starting point

- [Live-tail](../allocation-live-tail/README.md) and
  [palette](../allocation-palettes/README.md) comparisons measured prepared
  finite allocate/release at about 56 ns (Zen 5) and 106 ns (Graviton). The
  [descriptor tuning study](../memory-descriptors/TUNING.md) measured
  mimalloc, snmalloc and a local free list at 3.3–4 ns per pair, and its own
  prototype descriptor pairs at 16–18 ns. That gap had not been attributed.
- [Reuse selection](../allocation-reuse-temperature/FINDINGS.md) rejected
  ranked tier queues and an extra *candidate* cache: more ways to pick a Group,
  still followed by the full claim and bind. The `bound` variant below is a
  different mechanism. It skips the claim/bind transaction for an identity whose
  Home is still bound.
- [Stage 2 outcomes](../orbital-mvcc/OUTCOMES.md) priced short coherent reads
  against LMDB, but not catalog lookup at 64K–1M Objects, Lane fan-in or cold
  page growth.
- Ashton's [workspace](../../notebook/orbital/ASHTON-SCRATCH.md) asks whether
  mapping pages ahead could bring "~1 µs" allocations to 100–200 ns.

## Results

Each value is the median, across fresh processes, of that process's median
block mean (9 blocks). Variant columns are speedups over production
(`baseline ns / variant ns`). The hosts were Spot **c8a.2xlarge (EPYC 9R45,
Zen 5)** and **c8g.2xlarge (Graviton4, Neoverse V2)**, with Clang 21.1.8, the
`release` preset (`-O3`, assertions on) and `SIXDB_TUNE` set for the host. Each
host ran 3 processes per build for the variant round and 5 for the fill/Lane
round. Helper threads were pinned to separate cores.
[Evidence](evidence/) holds the per-case summaries; the job IDs below recover
raw CSVs and captured sources.

Variants that do not touch a path give its noise floor. For example, `tagged`
leaves allocation alone, and its allocation rows stay within ±1% of baseline,
except Graviton `window/65536` and `hold/window`. There the baseline processes
were slow (the variants agree with each other, not with baseline). Lane rows
move up to ±20% between binaries that share Lane code, so treat Lane ratios
under about 1.2× as layout effects.

### Allocation

| Case | Zen 5 base | lean | bound | Graviton base | lean | bound |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| pair 64 B (allocate, touch, release) | 61.9 ns | 1.21× | 3.45× | 113.7 ns | 1.34× | 3.06× |
| batch256 FIFO 64 B | 67.0 | 1.52× | 1.51× | 114.5 | 1.55× | 1.56× |
| churn256 64 B (random victim) | 92.6 | 1.62× | 4.85× | 143.0 | 1.79× | 4.17× |
| churn4096 1 KiB | 96.8 | 1.57× | 4.15× | 150.5 | 1.77× | 4.06× |
| Window pair 64 KiB | 34.4 | 1.20× | 1.23× | 59–73 | 1.48× | 1.46× |
| Hold + release, finite | 18.5 | 1.02× | 1.03× | 38.3 | 1.04× | 1.04× |
| foreign final release, 64 B | 82.2 | 1.37× | 1.41× | 176.5 | 1.11× | 1.16× |
| glibc malloc/free pair 64 B | 4.8 | — | — | 9.6 | — | — |

- **Production warm reuse costs 13× (Zen 5) to 12× (Graviton) a glibc pair.**
  In OrbStack profiles, `release_home` (re-deriving the Home and scanning 16
  sibling bindings) took 28% of samples and `on_lane` 11%. The rest went to
  candidate claim, bind's sibling scan and ready-cache upkeep.
- **`lean` (1.2–1.8×) removes redundant work without changing behavior.** It
  inlines `on_lane` against a trivially constructible TLS token, replacing ~5
  out-of-line calls through a TLS-init guard per pair. It releases a bound
  identity directly (`release_bound`), drops bind's O(16) sibling scan (the
  occupied bit already excludes a second binding), and uses a plain store for a
  sole local user. Every production allocation/worker/Object check passes
  except one: a `finite.cpp` misuse check that binds an occupied Home to a
  second identity, which the allocator never does.
- **`bound` (3–5×) is a per-class LIFO of 32 released identities that keep
  their binding, Home and Group pin.** Allocate pops one when class and
  alignment match. Trim and close flush it first. This reaches 18 ns (Zen 5)
  and 37 ns (Graviton) per pair, still 3.8× glibc. Five production checks fail
  on purpose: cached slots count as live in statistics, are invisible to
  prepared reuse and cross-class reuse, and keep the Arena pin until a flush. A
  production version needs those semantics settled, not just the fast path.
- Holds are already cheap; foreign release gains only from the shared
  recycle work.

### Cold growth: the "~1 µs" allocation

| Case (fresh Pool per block, includes final release/close) | Zen 5 base | premap | Graviton base | premap |
| --- | ---: | ---: | ---: | ---: |
| fill 1 KiB (4 Homes per 4 KiB page) | 460.5 ns | 1.96× | 732.5 ns | 1.74× |
| fill 64 B (64 Homes per page) | 103.5 | 0.93× | 180.4 | 0.86× |

At 1 KiB, each new page costs one `mprotect` plus one first-touch fault.
`premap` maps the Home plus up to 15 following absent pages with one
`mprotect` and prefaults the run with `MADV_POPULATE_WRITE`. That halves cold
allocation, to 235 ns on Zen 5 and 422 ns on Graviton. At 64 B it loses
because the run spills into other classes' pages that this trace never uses;
the run should stop at the class's own page range. Four checks fail because
they pin exact mapped/resident footprints. The policy question is who pays:
foreground batching, or the existing [Preparation](../../../orbital/include/orbital/preparation.h)
service Lane.

### Lane and Loom

| Case | Zen 5 | Graviton |
| --- | ---: | ---: |
| Lane local post+poll (per message, 64 per poll) | 10.8–13.7 ns | 15.9 ns |
| Lane ping-pong round trip, two cores | 191–202 ns | 724–800 ns |
| 1 producer → 1 consumer, per message | 22.9–28.7 ns | 77–91 ns |
| 3 producers → 1 consumer, per message | 29–32 ns | 40–53 ns |
| same, `post_chain` of 64 linked Messages (`chain`) | 5.5 ns | 7.0–7.5 ns |
| Loom Task start → done (256 per poll) | 7.2 ns | 10.5 ns |
| Loom Task yield → resume | 3.3 ns | 3.3 ns |

Loom's Task path is already lean. Lane fan-in is dominated by one `exchange`
per post on a shared line, and on Graviton by the producer/consumer handoff of
every node's line. A chained publication (one exchange for a pre-linked run)
gives 4–10× consumer throughput and passes the worker and foreign-return
checks. Journal admission and io_uring completion drains both naturally
produce such runs.

### Object catalog (test material, one Scope)

| Case | Zen 5 1K / 64K / 1M | tagged at 1M | Graviton 1K / 64K / 1M | tagged at 1M |
| --- | --- | ---: | --- | ---: |
| independent bind hit | 7.2 / 21.5 / 108 ns | 1.07× | 9.4 / 44 / 163 ns | 1.19× |
| dependent bind chain | 19 / 55 / 333 | 0.99× | 19 / 175 / 433 | 1.10× |
| bind miss | 5.6 / 21 / 49 | 2.13× | 8.4 / 23 / 90 | 1.39× |
| Scope open + close | 3.1 | — | 7.1 | — |
| publish 1 change (+bounded drain) | 164 / 243 / 594 | 1.07× | 222 / 413 / 578 | 1.15× |
| publish 64, per change | 100 / 150 / 383 | 1.03× | 152 / 254 / 380 | 1.11× |

Rows are sequential IDs. Random 128-bit IDs give the same picture (see
evidence). A dependent bind at 1M touches bucket, page pointer, Entry and the
material's Value. Its 333 ns on Zen 5 suggests about three DRAM misses
(inferred, not counted). `tagged` keeps 32 more hash
bits beside each bucket handle, so probes compare Entries only on a tag match.
That roughly halves misses at 1M and passes all 20 checks, including the
directory regression. Inline Entries, or no page indirection, would remove one
miss from every hit.

The fold `high ^ low·φ` collapses IDs whose words are correlated through that
same multiplier. Locally, `{i·φ + 1, i}` gave ~240 ns per bind at 1K and ~30 µs
at 64K (OrbStack timing, so direction only; the effect is probe-chain length).
Hashing each word independently before combining removes it.

## Overhaul: single-class Groups

The survey's variants pointed at the claim/bind transaction, not the Group or
Hold design. The overhaul (branch `claude/project-thread-891kd8`) replaces the
finite backend in [Orbital](../../../orbital/README.md#allocation):

- Each Group holds 256 equal Homes of one class; identity *i* is Home *i*. The
  one-byte bindings, mixed-class palettes and ready-candidate cache are gone.
- Free, ready and pending Homes are 256-bit bitmaps. Allocation takes the lowest
  ready Home of the class's head Group (inline); release sets two bits.
- 26 classes from 16 to 3072 bytes, at most 25% apart. A class must be a
  multiple of the alignment and fill a native page with 256 Homes (16 B at
  4 KiB, 64 B at 16 KiB, 256 B at 64 KiB). Equal requests get equal capacity.
- Mapping a Home maps following absent pages of its Group with one `mprotect`,
  up to as many as the Group already maps and at most 15; budget refusal maps
  only the Home, and trim returns idle pages.
- One Arena pin per Pool instead of per Group; `on_lane` is an inline compare
  against a trivially constructible TLS token; a sole local user releases
  with a plain store.

Holds, `Storage*` use tokens, foreign-return Messages, the shared budget, trim,
Preparation, Windows and the public API are unchanged. All 21 Orbital, 21 Loom
and 9 Object integration checks pass, and the Engine directory check. The finite
and palette checks were rewritten for the new geometry (class oracle over every
size/alignment/page, readiness, preparation, trim and exact charges); four
preparation checks that depended on grid placement were replaced by geometry
checks or dropped.

| Case (median of 5 processes) | Zen 5 main → overhaul | speedup | Graviton main → overhaul | speedup |
| --- | ---: | ---: | ---: | ---: |
| pair 64 B | 58.7 → 24.1 ns | 2.4× | 114.7 → 32.3 ns | 3.5× |
| batch256 FIFO 64 B | 63.5 → 16.5 | 3.9× | 114.8 → 27.6 | 4.2× |
| churn256 64 B | 87.1 → 25.9 | 3.4× | 143.3 → 46.4 | 3.1× |
| churn4096 1 KiB | 91.0 → 29.1 | 3.1× | 150.0 → 50.1 | 3.0× |
| cold fill 64 B | 103.5 → 48.3 | 2.1× | 192.7 → 76.6 | 2.5× |
| cold fill 1 KiB | 467.3 → 204.8 | 2.3× | 750.5 → 364.8 | 2.1× |
| Window pair 64 KiB | 32.4 → 27.5 | 1.2× | 60.7 → 47.4 | 1.3× |
| foreign final release 64 B | 77.6 → 32.8 | 2.4× | 180.7 → 108.3 | 1.7× |
| glibc malloc/free pair 64 B (control) | 4.5 | — | 9.6 | — |
| glibc batch256 FIFO 64 B (control) | 15.0 | — | 28.6 | — |

Batched allocate/release now matches glibc (16.5 vs 15.0 ns on Zen 5, 27.6 vs
28.6 ns on Graviton). An isolated pair is still 5× (Zen 5) and 3.4× (Graviton)
glibc: the remaining cost is API overhead (validation, Pool state, `Result`
moves, pulse, use-count bookkeeping), not geometry.
[Evidence](evidence/zen5-overhaul.csv), [Graviton](evidence/v2-overhaul.csv);
jobs `20261023T023711Z-99ee07bd` and `20261023T023711Z-abf544b7`.

The [footprint census](census.cpp) counts mapped, idle, metadata and reserved
bytes for fixed live populations ([census.csv](evidence/census.csv), 4 KiB
OrbStack host; counters, not timings):

| Population (after trim) | main mapped / metadata / reserved | overhaul mapped / metadata / reserved |
| --- | ---: | ---: |
| one per class (15) | 60 KiB / 8.3 KiB / 6.8 MiB | 60 KiB / 25 KiB / 2.5 MiB |
| 64 × 64 B | 4 KiB / 3.8 KiB / 2.5 MiB | 4 KiB / 1.6 KiB / 16 KiB |
| 100k uniform 16–3072 B | 280 MiB / 1.7 MiB / 1.6 GiB | 161 MiB / 0.7 MiB / 164 MiB |
| 100k log-uniform 16–3072 B | 165 MiB / 1.6 MiB / 1.4 GiB | 60 MiB / 0.7 MiB / 62 MiB |
| 20k × 200 B, 64-aligned | 4.9 MiB / 357 KiB / 338 MiB | 4.9 MiB / 129 KiB / 4.9 MiB |

Mixed-class Groups left pages partly used by several classes, so large mixed
populations mapped 1.7–2.8× their capacity. Single-class Groups map about their
capacity. The cost moves to Groups per class: a Pool touching every class pays
one Group's metadata per class (25 KiB for 15 classes versus 8.3 KiB).

## Variants and checks

| Patch | Change | Production checks |
| --- | --- | --- |
| [lean](variants/lean.patch) | inline `on_lane`, direct bound release, no sibling scans, plain sole-user store | 19/20 (misuse refusal removed) |
| [bound](variants/bound.patch) | lean + per-class bound-slot LIFO, flushed by trim/close | 15/20 (accounting/reuse semantics) |
| [premap](variants/premap.patch) | one `mprotect` + `MADV_POPULATE_WRITE` over a 16-page run | 16/20 (exact footprints) |
| [chain](variants/chain.patch) | `worker::post_chain(first, last)` | 20/20 |
| [tagged](variants/tagged.patch) | 32-bit tag per directory bucket | 20/20 |
| [main](variants/main.patch) | reverse diff from the overhaul back to production `main`, for paired runs | — |

On the overhaul branch only `chain`, `tagged` and `main` apply; configure skips
the others with a warning. Regenerate `main.patch` with
`git diff -R HEAD -- orbital/src orbital/include` after changing the allocator.

CMake copies `orbital/src` and `orbital/include` per patch at configure time,
applies it, and rebuilds 20 Orbital checks against the copy.
`orbital_hot_<variant>_validate` runs them. Patches are made against the
working tree at `da1941b3`, and configure fails if one no longer applies.

## Run

```sh
orb -m ubuntu cmake --preset release -B build/orbital-hot-paths -DSIXDB_SPIKES=orbital-hot-paths
orb -m ubuntu cmake --build build/orbital-hot-paths --target orbital_hot_paths orbital_hot_paths_lean
orb -m ubuntu build/orbital-hot-paths/workbench/spikes/orbital-hot-paths/orbital_hot_paths \
  --cpu 2 --peers 3,4,5 --filter alloc/pair,lane
python3 workbench/tools/worker.py run workbench/spikes/orbital-hot-paths/cloud.sh \
  --machine zen5 --instance-type c8a.2xlarge --env SIXDB_TUNE=zen5 \
  --env "HOT_VARIANTS=lean bound" --env SIXDB_BUILD_JOBS=8
python3 workbench/spikes/orbital-hot-paths/summarize.py build/workers/JOB/results --markdown
```

`--filter` takes comma-separated substrings and skips unselected sections'
setup. The OrbStack VM exposes no PMU, so the instructions column is -1 there.
Local timings swung up to 2× between runs and are for direction only.

Jobs: variants `20261023T010808Z-7cb0b793` (Zen 5) and
`20261023T010808Z-aab5e1d6` (Graviton); fill/Lane `20261023T011205Z-ea123aa1`
and `20261023T011205Z-a875ff60`.

## Limits

These are whole-loop means of mechanism loops, not request latencies or
database operations. The allocation traces reuse a warm Pool, apart from
`fill`. Catalog rows use the integer test material, whose Values are separate
heap records. No case measures contention on the catalog, which is
single-owner, or I/O. The `bound` and `premap` changes alter observable
accounting, so they need a design decision before any production form.
