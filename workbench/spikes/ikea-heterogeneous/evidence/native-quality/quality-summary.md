# Whole-window BEC decision comparison

180,948 rows validated: 45,237 windows, exactly four frozen-model/scan variants per window. Every supplied nonempty source window includes all 256 tiles, including empty/full tiles. Absent all-zero source windows are not synthesized.

Each window occurrence has weight one. Archive/family weights in the CSV are fractions of their corpus; families are not balanced and the two natural corpora are not pooled. Structural cases are a fixed comparison suite, not an estimated natural frequency distribution. The full per-archive and per-family statistics are in [quality-summary.csv](quality-summary.csv).

Natural partitions retain the original model lineage: census-income is `test_comparison`, census1881 is `validation_comparison`, and the remaining families/MS MARCO are `train_family_comparison`. All these supplied families were previously inspected; this is not a fresh holdout and no model or decision margin is fitted here.

Each comparison has two explicit byte scopes. `readable_extent` totals metadata + body + 64, the owned readable extent before allocation rounding. `aligned_allocation` totals metadata + 64 × ceil((body + 64) / 64), matching the body owner’s 64-byte allocation rounding. Metadata is already a multiple of 64: 512 bytes for the two packed layouts or 1,024 bytes for direct32. Neither scope includes allocator bookkeeping or object/control storage; no per-tile 32-byte cap, raw escape or tag is applied.

In each scope, convert exactly when its predicted total is < 8,192, with the same fixed zero margin. The actual best choice is the smaller of the actual total in that scope and 8,192 plain bytes. False conversions include an actual tie (zero excess bytes); missed savings require an actually smaller total. Decision loss is chosen actual bytes minus that best actual choice, summed over occurrences. The CSV includes actual and predicted total-byte sums and means separately for each scope.

Signed error is predicted minus actual body bytes. P95 is the nearest-rank 95th percentile of absolute error; these body-error statistics are unchanged between byte scopes. The near-crossover subset satisfies |actual total in the stated byte scope − 8,192| ≤ 512, so its membership and errors can change with rounding. Empty subsets have blank error fields. The sample estimate is eight times the rounded per-tile predictions from the fixed 32 strata; it is not a bound.

Prepared census cross-check: real-roaring, msmarco-keyset.

| Corpus / family | Partition | Window occurrences |
|---|---|---:|
| msmarco-keyset / msmarco | train_family_comparison | 2,160 |
| real-roaring / census-income | test_comparison | 1,001 |
| real-roaring / census1881 | validation_comparison | 4,000 |
| real-roaring / dimension | train_family_comparison | 27,567 |
| real-roaring / uscensus2000 | train_family_comparison | 2,221 |
| real-roaring / weather_sept_85 | train_family_comparison | 3,848 |
| real-roaring / wikileaks-noquotes | train_family_comparison | 3,365 |
| structural / byte-structure | structural_comparison | 545 |
| structural / dense-mixture | structural_comparison | 514 |
| structural / sample-alias | structural_comparison | 16 |

| Corpus | Model / scan | Byte scope | Metadata B | Body MAE B | Body P95 B | Body max B | False conversions | Missed savings | Excess actual B | Near windows / body MAE B |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| msmarco-keyset | cheap / full | aligned_allocation | 512 | 16.63 | 51 | 123 | 4 | 0 | 0 | 373 / 28.61 |
| msmarco-keyset | cheap / full | readable_extent | 512 | 16.63 | 51 | 123 | 5 | 0 | 84 | 354 / 28.29 |
| msmarco-keyset | cheap / full | aligned_allocation | 1024 | 16.63 | 51 | 123 | 11 | 0 | 0 | 233 / 37.12 |
| msmarco-keyset | cheap / full | readable_extent | 1024 | 16.63 | 51 | 123 | 13 | 0 | 197 | 232 / 36.94 |
| msmarco-keyset | cheap / sample32 | aligned_allocation | 512 | 76.48 | 198 | 430 | 11 | 21 | 2,624 | 373 / 94.20 |
| msmarco-keyset | cheap / sample32 | readable_extent | 512 | 76.48 | 198 | 430 | 13 | 10 | 2,507 | 354 / 94.37 |
| msmarco-keyset | cheap / sample32 | aligned_allocation | 1024 | 76.48 | 198 | 430 | 21 | 5 | 1,088 | 233 / 99.82 |
| msmarco-keyset | cheap / sample32 | readable_extent | 1024 | 76.48 | 198 | 430 | 13 | 7 | 1,161 | 232 / 99.94 |
| msmarco-keyset | quadrants / full | aligned_allocation | 512 | 15.69 | 42 | 67 | 6 | 0 | 0 | 373 / 31.25 |
| msmarco-keyset | quadrants / full | readable_extent | 512 | 15.69 | 42 | 67 | 4 | 0 | 50 | 354 / 31.29 |
| msmarco-keyset | quadrants / full | aligned_allocation | 1024 | 15.69 | 42 | 67 | 11 | 0 | 0 | 233 / 36.91 |
| msmarco-keyset | quadrants / full | readable_extent | 1024 | 15.69 | 42 | 67 | 13 | 0 | 176 | 232 / 36.91 |
| msmarco-keyset | quadrants / sample32 | aligned_allocation | 512 | 74.92 | 197 | 414 | 9 | 24 | 2,688 | 373 / 92.64 |
| msmarco-keyset | quadrants / sample32 | readable_extent | 512 | 74.92 | 197 | 414 | 12 | 10 | 2,208 | 354 / 93.09 |
| msmarco-keyset | quadrants / sample32 | aligned_allocation | 1024 | 74.92 | 197 | 414 | 20 | 5 | 1,024 | 233 / 99.41 |
| msmarco-keyset | quadrants / sample32 | readable_extent | 1024 | 74.92 | 197 | 414 | 13 | 9 | 1,232 | 232 / 99.53 |
| real-roaring | cheap / full | aligned_allocation | 512 | 1.69 | 6 | 281 | 0 | 5 | 320 | 66 / 85.09 |
| real-roaring | cheap / full | readable_extent | 512 | 1.69 | 6 | 281 | 0 | 5 | 159 | 61 / 85.75 |
| real-roaring | cheap / full | aligned_allocation | 1024 | 1.69 | 6 | 281 | 0 | 2 | 192 | 54 / 77.94 |
| real-roaring | cheap / full | readable_extent | 1024 | 1.69 | 6 | 281 | 0 | 5 | 163 | 54 / 77.94 |
| real-roaring | cheap / sample32 | aligned_allocation | 512 | 13.77 | 56 | 770 | 2 | 9 | 2,176 | 66 / 185.86 |
| real-roaring | cheap / sample32 | readable_extent | 512 | 13.77 | 56 | 770 | 0 | 9 | 1,616 | 61 / 194.33 |
| real-roaring | cheap / sample32 | aligned_allocation | 1024 | 13.77 | 56 | 770 | 5 | 6 | 1,472 | 54 / 221.13 |
| real-roaring | cheap / sample32 | readable_extent | 1024 | 13.77 | 56 | 770 | 4 | 5 | 873 | 54 / 221.13 |
| real-roaring | quadrants / full | aligned_allocation | 512 | 1.34 | 6 | 130 | 0 | 1 | 64 | 66 / 20.77 |
| real-roaring | quadrants / full | readable_extent | 512 | 1.34 | 6 | 130 | 0 | 2 | 2 | 61 / 20.43 |
| real-roaring | quadrants / full | aligned_allocation | 1024 | 1.34 | 6 | 130 | 0 | 0 | 0 | 54 / 19.52 |
| real-roaring | quadrants / full | readable_extent | 1024 | 1.34 | 6 | 130 | 0 | 2 | 64 | 54 / 19.52 |
| real-roaring | quadrants / sample32 | aligned_allocation | 512 | 13.75 | 57 | 786 | 3 | 7 | 1,920 | 66 / 177.77 |
| real-roaring | quadrants / sample32 | readable_extent | 512 | 13.75 | 57 | 786 | 1 | 5 | 1,048 | 61 / 185.18 |
| real-roaring | quadrants / sample32 | aligned_allocation | 1024 | 13.75 | 57 | 786 | 7 | 3 | 832 | 54 / 227.20 |
| real-roaring | quadrants / sample32 | readable_extent | 1024 | 13.75 | 57 | 786 | 12 | 4 | 2,643 | 54 / 227.20 |
| structural | cheap / full | aligned_allocation | 512 | 104.26 | 512 | 768 | 0 | 6 | 640 | 79 / 90.63 |
| structural | cheap / full | readable_extent | 512 | 104.26 | 512 | 768 | 0 | 6 | 338 | 73 / 91.53 |
| structural | cheap / full | aligned_allocation | 1024 | 104.26 | 512 | 768 | 0 | 6 | 640 | 80 / 82.85 |
| structural | cheap / full | readable_extent | 1024 | 104.26 | 512 | 768 | 0 | 6 | 336 | 74 / 83.81 |
| structural | cheap / sample32 | aligned_allocation | 512 | 223.34 | 512 | 10528 | 21 | 3 | 12,736 | 79 / 184.09 |
| structural | cheap / sample32 | readable_extent | 512 | 223.34 | 512 | 10528 | 17 | 5 | 12,394 | 73 / 181.48 |
| structural | cheap / sample32 | aligned_allocation | 1024 | 223.34 | 512 | 10528 | 1 | 4 | 9,280 | 80 / 168.70 |
| structural | cheap / sample32 | readable_extent | 1024 | 223.34 | 512 | 10528 | 1 | 10 | 9,554 | 74 / 164.77 |
| structural | quadrants / full | aligned_allocation | 512 | 186.57 | 768 | 768 | 7 | 4 | 1,216 | 79 / 148.94 |
| structural | quadrants / full | readable_extent | 512 | 186.57 | 768 | 768 | 7 | 4 | 1,448 | 73 / 149.07 |
| structural | quadrants / full | aligned_allocation | 1024 | 186.57 | 768 | 768 | 7 | 2 | 960 | 80 / 139.85 |
| structural | quadrants / full | readable_extent | 1024 | 186.57 | 768 | 768 | 6 | 4 | 1,091 | 74 / 140.24 |
| structural | quadrants / sample32 | aligned_allocation | 512 | 285.29 | 768 | 10528 | 21 | 3 | 12,736 | 79 / 244.92 |
| structural | quadrants / sample32 | readable_extent | 512 | 285.29 | 768 | 10528 | 23 | 1 | 13,980 | 73 / 245.26 |
| structural | quadrants / sample32 | aligned_allocation | 1024 | 285.29 | 768 | 10528 | 6 | 4 | 9,664 | 80 / 211.75 |
| structural | quadrants / sample32 | readable_extent | 1024 | 285.29 | 768 | 10528 | 4 | 8 | 9,754 | 74 / 214.20 |

Structural construction is fixed in quality.cpp: all 257 dense-tile counts with random prefix/permuted placement and repeated 0x55 bytes; eight sample-mask phases and their inverses; all 256 homogeneous byte values; and 32 rotations of a contiguous half-full tile. Dense random bytes use SplitMix64 seed `0x6b77696e646f7731`. The prefix/permutation pair has identical full-scan targets and estimates, while sample selection can change. This suite includes both sample underestimation and overestimation; its count sweep was not narrowed using an observed crossover.
