# Historical Zen 4 / Zen 5 recipe comparison — 17 October 2026

The compact-table advantage carries back to Zen 4. With AVX-512, K14 all-hit
independent throughput is 32% ahead for sets and 12% ahead for eight-byte maps on
Zen 4, compared with 43% and 35% on Zen 5 in this same experiment. K30 all-hit is
approximately tied for sets and 8% behind for maps on Zen 4; its Zen 5 leads are
only 7% and 4%. All-miss throughput wins strongly on both generations.

The K14 and all-miss margins survive this generation change. K30 hot hits are
much closer and sometimes lose; this does not establish an AMD-wide hot-hit
advantage. These measurements belong to their captured sources. The
[maintained scorecard](ACCUMULATION.md) contains the later integration result.

## Independent hot reads

Each timing is **ns/query**, with separate columns for Ikea and Abseil; lower
is better. Hot means b10, zero cache conditioning, long independent randomized requests.
Eight-byte maps consume the complete value. These are measured complete loops,
not modeled point costs.

| K | Value bytes | Misses | Zen 4 Ikea | Zen 4 Abseil | Zen 5 Ikea | Zen 5 Abseil |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 14 | 0 | 0% | 2.171 | 2.875 | 1.497 | 2.143 |
| 14 | 0 | 50% | 11.960 | 12.767 | 9.355 | 10.967 |
| 14 | 0 | 100% | 4.868 | 11.740 | 3.305 | 9.351 |
| 14 | 8 | 0% | 3.144 | 3.532 | 1.953 | 2.628 |
| 14 | 8 | 50% | 13.677 | 13.585 | 9.634 | 11.495 |
| 14 | 8 | 100% | 5.211 | 12.100 | 3.392 | 10.399 |
| 30 | 0 | 0% | 2.869 | 2.879 | 1.957 | 2.104 |
| 30 | 0 | 50% | 12.548 | 12.667 | 10.083 | 10.867 |
| 30 | 0 | 100% | 6.516 | 11.463 | 4.410 | 9.444 |
| 30 | 8 | 0% | 3.712 | 3.420 | 2.428 | 2.525 |
| 30 | 8 | 50% | 13.749 | 13.289 | 10.434 | 11.247 |
| 30 | 8 | 100% | 6.942 | 12.027 | 4.715 | 9.759 |

These are independent throughput measurements, not dependent serial latency.
At b10, K14/K30 correspond to W24/W40 key domains. Memory and multi-writer
behavior are not measured by this table.

## ISA and attribution

AVX2+GFNI retains the K14 all-hit advantage: 1.31x/1.16x for Zen 4 set/map8
and 1.37x/1.33x for Zen 5. K30 all-hit loses: about 0.71x for Zen 4 and
0.80x/0.79x for Zen 5. Bare AVX2 all-hit also trails on both generations.
All 64-byte-map, prefetch and losing cells remain in the
[full summary](evidence/zen-generations.json).

Both the candidate and actual pinned Abseil are compiled with the same native
march and generation-specific mtune; Abseil is not a generic-build control.
The pinned Abseil x86 control implementation uses a 16-slot SSE2 group
([pinned source](https://github.com/abseil/abseil-cpp/blob/2065f4ded0558c6f89fee67c8e5228feb4eb960e/absl/container/internal/hashtable_control_bytes.h)).
That is a description of this control, not evidence of a particular tuning defect.

AMD documents Zen 4's 256-bit execution datapath for AVX-512 and Zen 5's full
512-bit datapath ([Zen 4 architecture](https://www.amd.com/content/dam/amd/en/documents/products/epyc/4th-gen-epyc-processor-architecture-white-paper.pdf),
[Zen 5 architecture](https://www.amd.com/en/blogs/2025/leadership-hpc-performance-with-5th-generation-amd.html)).
The generation difference is consistent with different packed-kernel costs.
It does not isolate that mechanism from load/store, scheduling or code placement.
Native recompilation changes all six complete binaries; their
[code hashes](evidence/generation-codegen.json) do not establish identical-code comparisons.

## Reproduction and limits

[Runner](generation-native.sh), [auditor](generation-results.py).
EPYC 9R14, family 25/model 17 (c7a.xlarge), versus EPYC 9R45, family 26/model 2
(c8a.xlarge), pinned Linux Clang 21.1.8. Three seeds × five repetitions,
1,048,576 requests, 85% nominal primary occupancy, K14/K30, b10,
sets/map8/map64, misses 0/50/100%, direct and prefetch32.
The auditor validates **6,480 timing rows**, membership/complete payload checks,
compile flags on both sides and actual CPU family.

The existing **Zen 5 K→E/kernel recipe is transferred unchanged** to Zen 4;
this is not a new encoding search or a production Zen 4 profile. L1/L2 and
generation differences remain part of the hardware comparison. Small b10 tables
and these two K values do not establish behavior for every key width or workload.

Raw rows, compiler, compile commands, topology, functional checks and full
disassembly are recoverable from the [Zen 4](evidence/generation-zen4-artifact.json)
and [Zen 5](evidence/generation-zen5-artifact.json) archive references.
Source capture identity is in [followup jobs](evidence/followup-jobs.json).
