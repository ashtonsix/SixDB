# Admitted short reads and the repaired large-footprint pilot

The 10 September V2 and GNR runs show that retaining count, origin-alignment
and output-type facts can recover competitive short reads while preserving
runtime source placement. This is a measured caller-region result, not a new
Count16 public interface or completion of the ordinary range implementation.

[Capture and validation](evidence/admitted-20260910/capture.json) identifies both
workers, exact sources, library/caller binaries, checks and recovery bundles.
[V2 cases](evidence/admitted-20260910/v2/cases.csv) and
[GNR cases](evidence/admitted-20260910/gnr/cases.csv) retain every repetition,
control and footprint counter. Both use the coherent production source before
the selected-kernel and scalar-callback integrations. Each actual production
archive matches its historical coherent hash and remains unchanged within
the experiment. All 481 captured source files and declared output hashes were
rechecked. Admitted guards pass 400 placements and 6,744 queries per target.

## What was measured

Small runs request 4 KiB encoded and carry 64 KiB of query indices. They cover
Local1..7/56 and all twelve striped widths with five sequential repetitions of
at least 0.1 s. The ordinary endpoint materializes a runtime range; the admitted
region knows count16, origin alignment16 and u64 output, but retains actual
runtime base and stride. Raw additionally knows dense placement. Each query
uses one indirect call, original coordinates, sixteen u64 stores and the same
checksum. [The diagnostic](prototype/README.md) owns the
precise admissions and expression lowering. These arms cannot be subtracted
into universal additive ABI, placement and kernel costs.

GNR's full-feature profile registers AVX2 and AVX-512 endpoint families, both
with the profile's extra features available. Family names do not impose an
instruction ceiling. Immediate LocalPack/ScanPack predecessors remain primary
controls; Calico is an additional control where available.

| Small case | Ordinary | Admitted | Raw dense | Predecessor |
| --- | ---: | ---: | ---: | ---: |
| V2 Local1 | 4.902 | 3.873 | 4.156 | 4.290 |
| V2 Striped3 | 7.040 | 4.706 | 4.921 | 4.748 |
| V2 Striped7 | 7.190 | 4.425 | 4.458 | 5.154 |
| GNR AVX2 Local1 | 10.299 | 2.155 | 1.963 | 6.202 |
| GNR AVX-512 Local1 | 13.359 | 6.510 | 6.248 | 6.202 |
| GNR AVX2 Striped3 | 9.874 | 3.059 | 3.069 | 6.713 |
| GNR AVX-512 Striped3 | 13.514 | 6.604 | 6.642 | 6.713 |

Numbers are medians in ns/query. On V2 admitted beats ordinary on all twenty
shapes, with disjoint repetition ranges and reductions of 2.5–38.5%. It still
loses to the predecessor on Local2 (12.6%), Striped6 (14.0%) and Local56 (1.8%);
raw closes or reverses these losses. Raw is faster than admitted on only twelve
of twenty shapes. Placement metadata alone therefore does not explain the
remaining pattern. GNR's admitted AVX2 family also beats the predecessor by
large margins in some small cases; a slow control must not become the ceiling.

## One GiB pilot

The pilot covers Local1 and Local56, requesting 1 GiB encoded plus roughly
512 MiB of query indices. Three sequential repetitions use at least 0.03 s.
Construction uses bounded staging, with no accompanying full u64 value array.
These are footprint and stream descriptions, not claims that each access misses
a particular cache. The machines report different cache capacities.

| Local56 get16 | Ordinary | Admitted | Raw dense | Predecessor |
| --- | ---: | ---: | ---: | ---: |
| V2 | 43.044 | 37.605 | 36.097 | 33.942 |
| GNR AVX2 | 50.801 | 37.084 | 33.388 | 25.684 |
| GNR AVX-512 | 46.920 | 26.143 | 25.284 | 25.684 |

The small-footprint AVX2 winner is not automatically the large-footprint winner.
V2's materialized Local56 loss remains open. The admitted route recovers much
of GNR's ordinary-path loss, but the individual arms and sample spread matter.

The dependent walk now includes a persistent full-width Weyl nonce. Independent
coverage/replay checks at all 64 widths preceded these measurements; the former
index-only walk entered short cycles and its historical timings do not establish
broad random access. V2 dependent medians are about 163–169 ns/query, while its
independent point medians are about 14–16 ns/query. This is consistent with
different latency overlap, but includes address generation and lacks per-query
cache-miss evidence. Calibrated providers may consume different prefix lengths.

The verified helper and persistent state are now in the recurring benchmark.
Its point/get16 source loops remain byte-identical. A fresh pinned Linux build,
134 shortened all-width trace/replay checks and twelve two-repetition functional
cases pass; these local checks add no performance claim.
