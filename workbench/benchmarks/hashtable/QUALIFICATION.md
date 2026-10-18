# Set/map promotion qualification — 17 October 2026

This records the promotion cut before the RMW follow-up. Later measurements and
source changes are recorded in [RMW](RMW.md), [Zen generations](ZEN-GENERATIONS.md)
and the [maintained results](ACCUMULATION.md). Its original timing and source
receipts remain unchanged. Read times here are independent-loop reciprocal
throughput, not dependent serial latency; prefetched reads are ordinary software
prefetch loops, not Loom. The [suite guide](README.md#what-a-number-means)
defines dimensions and current timer/accounting boundaries.

At this cut, the selected set/map core moved into Ikea with its concrete owner
and descriptor, reader representation and measured encoding selection.
Mixed/existing writers shared the reader's backing, and same-seed growth/merge
built independent owners while preserving sources. The later mutable-accumulation
API has different destination ownership; use its current guide rather than
inferring that contract from this historical report. Owning multimap remains
research.

## Preservation against the frozen candidate

Five native CPU/ISA screens compare the promoted owner with the exact selected
research implementation and pinned Abseil in the same executable. Each screen
contains 3,360 complete measurement files / 17,700 timing rows. The auditor
checks expected coverage, source identity, check receipts and full operation
oracles. Final growth confirmation adds 600 complete operations per CPU/ISA.

Table entries are medians of paired **promoted / research time ratios** across
all measured cells in that phase. Below 1 is faster. All per-cell medians, MADs,
sample counts, memory estimates and Abseil comparisons remain in the JSON files.

| CPU / ISA | Independent reads | Prefetched reads | Default upsert | Erase | Final growth |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen 5 AVX-512 | 1.005 | 0.999 | 1.000 | 0.998 | 1.005 |
| Granite Rapids AVX-512 | 0.998 | 1.001 | 1.001 | 1.007 | 1.002 |
| Neoverse V2 NEON | 1.001 | 1.001 | 1.000 | 1.002 | 0.995 |
| Zen 5 AVX2 | 1.005 | 1.000 | 0.999 | 0.998 | 1.014 |
| Granite Rapids AVX2 | 0.999 | 1.001 | 1.003 | 1.000 | 1.017 |

The initial promotion exposed a K30 set-growth regression of about 12–14% on
x86 AVX-512. Disassembly showed flag-byte reloads after packed stores. Keeping
partition masks local through those stores removed it. Final AVX-512/NEON
concrete growth is within 3.3% of the reference in every measured cell;
descriptor growth is within 2.5%. AVX2 concrete growth has a 6.6% worst loss
(K30 set), with every growth surface still ahead of the faster Abseil control.

One Zen AVX2 K30 set / 50% miss direct loop was 8% slower in the full screen.
The two complete loops have the same 142 normalized instructions. Repeating
with independently placed instantiations gives 10.38 ns for the promoted owner,
9.71 ns for the frozen owner, 9.69 ns for the promoted split-owner instantiation
and 10.33 ns for its frozen counterpart; Abseil is 10.58 ns. The fast/slow
ordering follows the instantiation, despite unchanged readers. Code placement
is the supported explanation, not an encoding change. These observations are
retained; the slower published cell is not replaced with its fastest clone.

Granite AVX2 also has 7–10% losses when deliberately using the existing-key
writer for K14/map64 mixed/new traffic. The intended default mixed writer
preserves those same cells (about 0.99–1.00× the research times). Choose the
writer for the traffic; neither strategy is an automatic classifier.

## Actual Abseil comparisons

The following ranges are **Ikea / Abseil time**, across the tested K/value/load
or miss cells. Below 1 favors Ikea. Hot means b10; conditioned reads mean K30,
b18 after 512 MiB conditioning. Conditioning does not prove every access is DRAM.

| CPU / ISA | Hot direct reads | Conditioned prefetch | Mixed upserts | Final growth |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX-512 | 0.70–1.17 | 0.52–0.94 | 0.53–0.88 | 0.17–0.81 |
| Granite Rapids AVX-512 | 0.79–1.21 | 0.60–0.82 | 0.53–1.02 | 0.19–0.84 |
| Neoverse V2 NEON | 0.82–1.33 | 0.59–1.22 | 0.53–0.95 | 0.19–0.86 |
| Zen 5 AVX2 | 0.84–1.83 | 0.65–1.41 | 0.69–1.08 | 0.18–0.89 |
| Granite Rapids AVX2 | 0.90–1.94 | 0.73–1.13 | 0.77–1.24 | 0.20–0.87 |

On the native AVX-512/NEON profiles, growth wins all 18 surfaces, by 1.16–5.90×.
Mixed upserts win all 12 Zen cells, 11/12 Granite cells and all 12 NEON cells.
At 85% occupancy, eight-byte map mixed upserts are 8.15/9.13 ns (K14/K30) on
Zen against Abseil 15.29/10.80 ns. This is the same owner used for reads.

The compactness survives: at b10/85% occupancy, set allocations use 43–49% of
Abseil’s bytes and eight-byte maps 76–80%; 64-byte maps are about 99%. These
are owner estimates, not RSS. Wide values have little footprint advantage.

The measured losses and remaining qualification limits are:

- Initial construction with the default mixed writer is 1.13–1.58× Abseil on
  AVX-512/NEON. Existing-only split updates are still as much as 1.79× on NEON.
- Hot direct reads lose selected cells by up to 33% on NEON and 21% on Granite
  AVX-512. Conditioned ARM map64 hits can lose by 22%. Prefetch is not a blanket win.
- Bare AVX2 is supported and preservation-tested but has fewer throughput wins.
  Use the measured native ISA profile when the deployment supports it. AVX2+GFNI
  profile data is retained; this promotion did not run a new GFNI-only screen.
- Same-geometry merge is useful and source-preserving, but disjoint/wide-value
  merge is not uniformly faster. Every merge cell and both Abseil capacity controls
  are retained in the full reports.
- Owning multimap, append/churn/pool lifetime, and further Loom execution work
  remain in research. No consumer object directory or join design is included.

The promoted core is useful for compact integer sets and narrow maps with
independent reads, mixed mutations and growth. It is not the default choice
for a workload dominated by constructing tiny tables or repeatedly overwriting
existing ARM map entries. Those losses do not block the bounded core’s use.

## Correctness, scope and recovery

Final local ASan/UBSan checks pass **19/19**, including all W=1..64, both writer
strategies, direct/staged reads, growth across bitmap/96-byte/encoding boundaries,
Swiss duplicates behind holes, sticky counts, byte damage, aliased writes,
allocation/reducer refusal, source preservation and escaped old-reader lifetime.
All 11 public/author headers compile independently. Each full native profile
passes the public suite and five focused sanitizer histories plus 12 sanitized
candidate/reference/Abseil complete operations. Local OrbStack timings are not
used as native performance evidence.

Native promotion timings cover K14 and K30, sets/map8/map64. This is preservation
verification, not a new all-K encoding search. The retained per-K encoding
provenance remains in `encoding-profiles.h`. Concrete owner reads/writes are
measured; descriptor growth is measured separately. Erased descriptor point-call
overhead is not included in the concrete throughput numbers.

[Source identity](evidence/final-source-identity.json) records the only changes
after the full paired capture: growth mask lifetime and byte-identical internal
header relocation. At this promotion cut, executable Ikea source files matched the final growth
capture. The later [RMW additions and native checks](RMW.md) extend the table and
descriptor; the prior read/write/growth measurements remain this dated record.

[Evidence and recovery](evidence/README.md) contains the complete cell summaries,
raw archive references, source capture identities and local check receipts.
[Consumer guide](../../../ikea/docs/hashtable/usage.md) and executable ordinary/composed
examples describe the supported operations and ownership obligations.
