# Intervals native search

Ashton asked to optimise `ikea::intervals` beyond its narrowly tuned cells. Every
source finishes in one native step: Absolute Ends search (`direct_source`),
checkpoint routing in Sizes and Relative Ends, Relative Ends' local fields,
`numeric_binding` and the interleaved `author::lower_bounds`. This study measured
that step across element widths, counts, count variability, request dependency
and ISA, replaced it in Ikea, and checked a Frame run-block consumer. The
[native study](../intervals/RAISE-BAR.md) had tuned sparse G64 prefix routing; the
[Frame run audit](../intervals/AUDIT.md) kept a tiny projection below 16 runs
because the old native search lost there.

## Result

The maintained search now bisects branchlessly until the window fits one 64-byte
span, then counts the span's elements below the query at a base clamped inside
the array: over ordered endpoints that count is the rank. Arrays under 64 bytes
count themselves with no control that depends on the count beyond a few size
classes: one fault-suppressing masked load on AVX-512, overlapping 32/16-byte
spans and register-assembled 8/4-byte pieces on AVX2/NEON, and scalar compares
for one to three elements. Reads stay inside the array. An owner that admits a
readable window, `author::with_readable_window<32>(source)` (Ikea LEAD's
prefix-window law: `max(n*size, 32)` initialized, read-isolated bytes from
`data`), lets AVX2/NEON count arrays under 32 bytes with one load.

Same binary, frozen old search (`control/search_before.h`, ace41712) against the
new default, medians of 9 rotated trials, 168 cells per stream and ISA:

| Stream | Zen 5 AVX2 | Zen 5 AVX-512 | Graviton 4 NEON |
| --- | --- | --- | --- |
| Independent random | 1.21× min, 1.93× median, 4.7× max | 1.22×, 2.27×, 16.0× | 1.13×, 1.87×, 4.8× |
| Ascending runs per block | 1.34×, 2.01×, 3.5× | 1.32×, 2.26×, 4.8× | 1.23×, 1.93×, 3.2× |
| Dependent chain | 1.05×, 1.14×, 1.6× | **0.90×**, 1.04×, 2.0× | **0.72×**, 1.10×, 1.4× |

Selected cells, ns per request, old → new → with a 32-byte window:

| Cell | AVX2 | AVX-512 | NEON |
| --- | --- | --- | --- |
| u8, counts vary 1–34, random | 15.4 → 6.6 → 1.4 | 15.0 → 1.05 → 0.97 | 11.5 → 7.3 → 3.0 |
| u8, 34 ends, random | 3.6 → 1.3 → 1.2 | 3.4 → 1.05 → 0.96 | 4.8 → 3.0 → 3.0 |
| u16, counts vary 1–16, random | 13.2 → 5.7 → 1.2 | 11.7 → 1.04 → 0.95 | 8.8 → 6.6 → 2.2 |
| u32, 256 ends, random | 8.9 → 4.3 | 7.4 → 3.8 | 14.5 → 5.7 |
| u32, 16,384 ends, random | 16.1 → 10.1 | 13.9 → 9.3 | 24.7 → 12.3 |
| u64, 4,096 ends, random | 16.6 → 8.4 | 13.6 → 8.0 | 29.8 → 11.2 |

**Losses, retained.** Dependent chains over 1–8 elements: up to 10% on AVX-512
(masked-load latency) and up to 1.4× on NEON at 4–8 u8–u32 elements
(14.9 → 20.8 ns), where a vector compare, reduction and transfer to a general
register is longer than the old predicted scalar loop. Independent requests lose
in no cell. Validation passes on all three profiles: Intervals search (with new
front-guard and window-flush page tests and LEAD's resolved/mixed-domain batch
witness), sparse, seams and cuts checks, StreamPack check and boundaries, and
`engine_directory_check`.

## What the experiments showed

1. **One vector of convergence was too narrow.** Branchless bisection to one
   64-byte span and one count beat the old search about 2× at every width,
   including where the old search had a full native vector (u32 n = 256:
   7.4 → 3.7 ns AVX-512, 14.5 → 5.6 NEON). Spans of 128–512 bytes lost to 64 on
   NEON (more registers to reduce) and gained nothing on x86.
2. **Varying counts were the largest gap.** With each block's count uniform in
   [1, n], the old loop exits mispredicted: 10–18 ns at 8–64 u8 ends against
   0.7–1.3 ns for one fixed-control count. AVX-512 masked loads do this exactly.
   AVX2/NEON need the readable window below 32 bytes; their exact pieces still
   branch on size class (5.7–7.3 ns in those cells).
3. **Read authority rules out the fastest exact-looking tricks.** A page-local
   over-read reaches window speed with no grant, but reads bytes the owner never
   admitted ([leaf read-authority review](../frame-phase-2/instruments/LEAF-CONTRACTS.md));
   it was dropped. Clamped fixed-trip counts with per-lane masks stayed exact but
   cost 5–7 ns on NEON in instructions alone.
4. **Branching bisection does not win dependent chains.** An early cohort showed
   it 2× faster; its index walk repeated and the predictors learned it. With a
   salted walk that never repeats within a pass, branching is 0.73–0.97× of the
   old branchless search except at four or fewer elements. No branching binding
   was added.
5. **Interpolation pays only beyond the caches.** One lerp stage from the end
   values with two checked bracket probes loses 1.2–2× while resident (to 16,384
   elements, uniform values) and wins 1.1–1.3× over the new search at 2^18–2^22
   u32 ([large-array medians](evidence/large-20261023.csv)); at 2^24 it loses on
   Zen 5 and wins on Graviton 4. `author::bind_search` already offers prepared
   interpolation as an opt-in; nothing changed there.
6. **Per-block run ends beat group buffers.** Over Frame-shaped 256-row run
   blocks, every run end of a 16–256 block group in one u16 buffer loses 1.3–5×
   to each block's own u8 ends, and searching only the block's slice still loses
   1.2–2×. Group Relative Ends and Sizes sources are slower again.
7. **A run-start bitmap wins above about 50 runs.** Against the leaves thread's
   32-byte popcount rank on the same blocks, the windowed search is faster at
   ~6 runs per block (1.0 vs 1.5 ns AVX-512, 1.0 vs 4.9 AVX2, 2.1 vs 5.2 NEON) and
   the bitmap is faster at 50 and 145 runs (1.5 vs 3.5, 1.6 vs 4.8 ns AVX-512).
   Frame's maintained `run_end_source` is still 11–13 ns at ~6 runs because it
   keeps the projection below 16 runs; the plain direct source is now 1.1–7.4 ns
   there. That consumer choice belongs to Frame.

## Method

`bench.cpp` times one boundary per request over ~256 KiB of resident blocks,
placed at random sub-line offsets with random bytes after each array. Cells:
u8/u16/u32/u64; n from 1 to 16,384 (2^18–2^24 for a few arms with
`SEARCH_LARGE=1`); fixed counts, or each block's count uniform in [1, n]; streams
`dependent` (the next request's index is a per-step salt plus this answer),
`random`, `sorted` and `runs` (256 ascending queries per block). Every arm's
checksum matches an oracle before timing. `check` compares every arm with
branching bisection for n = 0…300 and larger, both edges, arrays flush against
`PROT_NONE` pages on both sides; arms that need a readable tail get one.
`groups.cpp` reads random rows → run values over Frame-shaped run blocks with
per-block density varied 0.25–4× around 4, 36 and 128 runs. Workers: EPYC 9R45
(c8a, `x86-64-v3` and `znver5` builds) and Graviton 4 (c8g), Clang 21.1.8, `-O3`
with function and block alignment controls from cohort 4 on. Timings are warm
and resident unless stated; no cold-cache or PMU claim is made.

[`candidates.h`](candidates.h) keeps every measured alternative, including the
rejected ones. Evidence:
[final medians](evidence/summary-20261024.csv) (`summarize.py`), Zen 5 and
Graviton 4 [checks, validation and group results](evidence/zen5-20261024/) with
[recovery](evidence/graviton4-20261024/artifact.json) of every raw sample. Final
jobs `20261024T001337Z-667131f5` and `20261024T001405Z-c68faf58`; large arrays
`20261023T234453Z-5b25a44c` and `20261023T234531Z-806182e7`. Earlier cohorts
guided iteration and are superseded.

## Reproduce

```sh
python3 workbench/tools/worker.py run workbench/spikes/intervals-search/native.sh \
  --machine zen5 --instance-type c8a.large --capacity spot-or-on-demand \
  --env SEARCH_VALIDATE=1 [--env SEARCH_LARGE=1]
python3 workbench/spikes/intervals-search/report.py build/workers/JOB/results \
  --bytes 1 --stream random --arms before,current,proposed_halo
```

## Still open

- NEON dependent chains over 4–8 elements (finding above).
- Sparse Sizes local prefix: its 16-width owner kernel was not revisited.
- Frame's `run_end_source` below 16 runs, and whether its images grant the
  readable window (the [read envelope seam](../frame-phase-2/SEAMS.md)).
- The bitmap versus search crossover under DRAM residence.
