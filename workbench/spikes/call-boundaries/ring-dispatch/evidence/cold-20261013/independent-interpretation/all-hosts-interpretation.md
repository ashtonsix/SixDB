All three frozen-source hosts qualify 16,128 rows in 24 cells. The same 8d640b9e/e1294dfb capture passed 48 native Loom checks, 18 ASan/UBSan always-UAR checks, and 30 release plus 30 sanitized forced caller controls before timing. Twelve executables per host, both forward/reverse outer orders, seven balanced inner repetitions and eight arms are present. The complete independent recomputation is all-hosts-cells.json; old x86 reports remain intact.

For the target large C32/grain4 hinted workload, per-pass ns/lookup:

| Host | Original reference | Pointer tail | Prepared vector | Its actual manual | Snapshot | Its actual manual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Granite AVX512 | 15.693/15.807 | 16.252/16.116 | 13.880/13.462 | 13.089/12.791 | 14.012/13.643 | 13.457/12.985 |
| Zen5 AVX512 | 13.945/13.956 | 14.255/14.255 | 12.478/12.554 | 12.068/12.117 | 12.457/12.561 | 11.867/12.022 |
| V2 NEON c8g.large | 17.254/17.225 | 17.189/16.975 | 14.716/14.662 | 13.890/13.664 | 14.911/14.538 | 13.902/13.579 |

Prepared common facts close most of the target gap in both orders on all three hosts. V2 prepared vector remains 5.94–7.30% above its own manual; snapshot remains 7.07–7.26% above its own manual. The prepared path changes fact hoisting and stage kernels together with argument representation, so this is not isolated proof of an ABI-only improvement. Cold tail by itself moves possible ordinary calls but does not close the original smaller Cohort path's gap. Frame16B versus40B/48B costs remain included.

Hot work retains a negative. Every V2 hot C1 factor/grain loses to its own manual; prepared vector is still about 15–16% slower. V2 hot C32/grain4 vector 7.657/7.653ns versus own manual 6.702/6.712ns (about 14% slower); grain1 retains about 77% overhead. Large C1 is near manual and order-sensitive. Nine of 120 factor comparisons exceed the descriptive 5% ratio-drift threshold, all large C1; none hot/C32. The new V2 flag is large C1/grain4 snapshot/vector. No serial-win claim follows from pooled medians.

The V2 retry is c8g.large, 4GiB, 2vCPU, CPU0, versus the failed medium's 2GiB/1vCPU. Its source archive's 3,170 files and digest are independently verified. Use within-host ratios and actual per-executable manual curves. Older c8g.medium 17.93/16.32ns results are excluded from matched comparisons.

Actual V2 ready callbacks in tail/vector/snapshot/slim have no ordinary calls, no SP references and no memory references through an established x29 frame base. The prepared g4 metadata is 216 instructions versus tail 272, but payload is 328 versus 160; speed does not track one static body count. V2 g4 cold frames are 48B tail and 144B vector. Snapshot is 16B wrapper plus 160B ordinary finish_cold; all cold helper work remains timed. The worker's SP-only list omitted 4 x29 memory instructions in tail cold, 16 in vector cold, and 3 in snapshot's helper. The independent audit adds those. Counts are static across branch paths and exclude nested helper stack.

On x86 the call-free g4 payload still has stack traffic: vector/snapshot 36 negative RSP memory instructions versus tail 24. Thus the positive whole-operation result is not proof that register restore/spill traffic is lower everywhere.

Normal timing has n128 all-primary and n4M 4,190,897 primary/3,407 secondary/0 overflow: only 0.0812% downstream. Forced semantic controls establish 32 real secondary and 80 overflow plus budgets/ragged cancellation; they do not time an overflow-heavy trace. The separately verified Ikea tiered hierarchy owns the genuine aged-chain throughput result. These workloads, value shapes and hardware/ISA cuts must remain distinct.

No further probe or runtime/peer edit was made. All three native result sets are now complete. There is no PMU, IPC, stall or cache-tier claim and no generic packing ABI promotion.
