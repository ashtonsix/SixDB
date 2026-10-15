# Folding private owners on actual Workers

The next experiment reuses the bounded-intake Worker caller and folds one
immutable source into an existing private destination. It covers sets, opaque
maps and multimaps, with ordinary Abseil in the first executable comparison.
This addresses a shared ownership boundary; it is not a multimap-only optimization.

Keep the source's Worker, Pool, directory and interpretation alive. Copy bounded
chunks into caller-owned message storage; no Allocation or borrowed payload pointer
moves between Pools. A source cursor may advance once bytes have been copied, but
the message cannot be reused until the destination accepts its complete suffix.
Destination growth must not migrate or invalidate the immutable source. A failed
preparation leaves both owners and the pending suffix usable. The destination
retains its own original Pool authority.

Sets fold by union; maps use the caller's associative opaque combiner; multimaps
preserve every value and duplicate. Consecutive values of a source key can share
one destination lookup/reservation, for both contenders. The eight-request public
packet remains separate from bounded scan/mutation work and transport capacity.
Do not give either contender exact future union cardinality. The first control
uses decoded keys; bucket-wise same-seed transfer remains a distinct opportunity.

Time build, transfer including destination growth, source retirement and complete
owner lifetime separately. Charge bounded tickets, cursors, active bytes, backing,
Pool metadata and sampled source/destination coexistence; sampled coexistence is
not an exact allocation peak or RSS. Check the full independent expected result
after timing and allocator-sensitive phases, using preallocated inspection space.
Include compact-only sources, compact-to-hierarchy crossing, overlapping/disjoint
keys, narrow/wide values and long bags spanning many tickets. Force delayed source
and destination tasks, refusal/cancellation and exact accepted-prefix retry.

The retained-reader boundary remains separate. Whole-material private capture is
already supported. Orbital growth may retire old backing before returning, so
finer preservation needs a synchronous pre-growth/retirement hook covering old
initialized bytes and owner metadata, also before directory relocation. A Hold
alone is neither immutability nor retention of interpretation. Copied prefixes
are not issued writes. Do not invent a byte-COW guarantee from post-grow events.

## Completed evidence

Local frozen V1 completes 115 fold checks across all 40 key-domain/kind/W2/W64 cases, plus three original Worker regressions: 118 checks total. It also completes 51 benchmark processes/918 rows/306 cells. V2 passes 31 ASan/UBSan/always-stack-UAR checks and six TSan histories. V2 changes only CSV labels/report recovery; operation bodies match V1.

Native Zen5 AVX2, V2 NEON and Granite AVX-512 each complete 34 checks and 102 benchmark processes/3,060 rows/306 cells, ten samples/cell. Every process exits 0 with empty stderr. All three jobs then fail their final report because V1 emitted three accounting values without their CSV header labels. The repaired report accepts only that exact known 33-label/36-value schema, records each repair, and rejects any other width mismatch. Raw measurements are unchanged. Original job failures remain recorded; the recovered summaries reproduce byte-identically from retained measurements.

Source V1: `5ec330b4b4dfad3aa1e4f963b93d45e9e9c40de585a1b254414daa6ed27e3e47`. Header/report correction V2: `934aa78b8bdb56c235b7fe13db59ac580a37b5af588ba859a953d72e57096b45`. Jobs: Zen20261015T093934Z-17aca3c5, V2...-573591cd, Granite...-1db2314b. Full sources/binaries/disassembly/checks and process resources remain in each archive.

768 keys per source, 32 input updates per key. The table contains one reduced map value or membership per key; multimaps retain all 32 values. **Fold cost is ns per transferred source record/value**, including bounded Worker handoff and destination growth. Source retirement is separate. Disjoint sources produce 1,536 keys; full overlap produces 768. Logical bytes describe the final destination, not RSS or total runtime memory.

| Host / case | Disjoint ns ours / Abseil | Full overlap ns ours / Abseil | Disjoint logical B ours / Abseil | Full overlap logical B ours / Abseil |
| --- | ---: | ---: | ---: | ---: |
| Zen5 / K40 set | 43.22 / 25.92 | 19.87 / 16.23 | 20,776 / 18,472 | 10,536 / 9,256 |
| Zen5 / K40/W2 map | 27.14 / 27.43 | 21.30 / 18.80 | 14,862 / 34,848 | 14,632 / 17,440 |
| Zen5 / K64/W64 map | 77.66 / 43.05 | 26.13 / 19.45 | 143,698 / 149,480 | 71,776 / 74,728 |
| Zen5 / K40/W2 multimap | 24.95 / 15.56 | 19.99 / 14.82 | 251,120 / 165,904 | 242,480 / 132,112 |
| Zen5 / K64/W64 multimap | 65.57 / 37.02 | 62.01 / 26.48 | 5,944,690 / 3,311,584 | 6,226,098 / 3,228,640 |
| V2 / K40 set | 94.45 / 56.78 | 45.89 / 42.01 | 20,776 / 18,464 | 10,536 / 9,248 |
| V2 / K40/W2 map | 61.35 / 63.39 | 50.06 / 45.88 | 14,862 / 34,840 | 14,632 / 17,432 |
| V2 / K64/W64 map | 136.82 / 126.01 | 67.37 / 59.18 | 143,698 / 149,472 | 71,776 / 74,720 |
| V2 / K40/W2 multimap | 55.80 / 42.17 | 47.36 / 41.79 | 251,120 / 165,896 | 242,480 / 132,104 |
| V2 / K64/W64 multimap | 126.18 / 80.97 | 118.47 / 65.72 | 5,944,690 / 3,311,576 | 6,226,098 / 3,228,632 |
| Granite / K40 set | 82.51 / 56.38 | 50.51 / 44.73 | 20,776 / 18,472 | 10,536 / 9,256 |
| Granite / K40/W2 map | 60.86 / 56.78 | 51.16 / 47.89 | 14,862 / 34,848 | 14,632 / 17,440 |
| Granite / K64/W64 map | 148.87 / 103.76 | 66.73 / 68.57 | 143,698 / 149,480 | 71,776 / 74,728 |
| Granite / K40/W2 multimap | 57.44 / 43.85 | 50.16 / 43.22 | 251,120 / 165,904 | 242,480 / 132,112 |
| Granite / K64/W64 multimap | 127.05 / 88.06 | 111.71 / 69.65 | 5,944,690 / 3,311,584 | 6,226,098 / 3,228,640 |

Narrow-map disjoint transfer is near parity with substantial storage savings. Sets and wide maps expose growth/copy costs; multimaps remain materially behind. Faster source retirement does not offset those merge losses: for Zen5 wide MM/full overlap, retirement is 4,790 ns versus 19,205.5 ns, but transfer is 62.01 versus 26.48 ns per value. Keep both phases rather than selecting the favorable one.

The serialized one-chunk protocol limits overlap and copies message bytes through the coordinator. Those are explicit opportunities, not proof that transport explains the container-specific gap. Same-seed bucket-wise union, evolving occupancy balance, retained-reader destination publication and exact-key fold baselines remain unmeasured. No further campaign is queued during research consolidation.

Loom LEAD independently audited final-release credit, Task retirement, owned chunk copying, source cursor lifetime and destination-only cancellation/growth. No bug was found in this frozen single-credit protocol. Future failed/detached/cancelled Task completion needs terminal credit handling: the fixture currently recognizes only done. This is a scope limit, not a generalized scheduler guarantee.

Evidence: [Zen5](evidence/worker-fold-zen5/artifact.json), [V2](evidence/worker-fold-neon/artifact.json), [Granite](evidence/worker-fold-granite/artifact.json). Each retained set includes all raw repetitions and a recovery recipe. [Local checks/timings](evidence/worker-fold-validation/worker-fold-local-v1.json), [ASan/UBSan](evidence/worker-fold-validation/worker-fold-v2-asan.json), [TSan](evidence/worker-fold-validation/worker-fold-v2-tsan.json) recover validation sources and receipts.
