# Whole-window analyser findings

Full scans are the stronger accuracy baseline in this comparison. The quadrant
model improves the difficult natural cases, but does not dominate the cheap
model on structured inputs. The fixed 32-tile sample can miss whole-window
structure severely; its smaller input scan is not evidence of a safe decision
bound. Runtime tradeoffs belong to the separate timing experiment.

The [captured summary](../evidence/native-quality/quality-summary.md) and
[archive/family statistics](../evidence/native-quality/quality-summary.csv) come
from `20260910T013141.374050Z-native-quality`: 42,002 Real Roaring windows,
2,160 MS MARCO windows and 1,075 separately labelled structural windows,
producing 180,948 checked rows. Every source window retains all 256 tiles,
including empty/full tiles. Counts match both prepared manifests; absent
all-zero source windows are not synthesized. Each prepared window occurrence
has weight one, with no balancing of families or pooling of the two corpora.
The [provenance](../evidence/native-quality/provenance.json) identifies the capture.

The [frozen models](../../ikea-blocks/predictor/model.h) and zero decision margin
were unchanged. Census-income remains `test_comparison`, census1881 remains
`validation_comparison`, and the other families plus all MS MARCO terms remain
`train_family_comparison`. All supplied family holdouts had already been
inspected: this is a larger occurrence census, not an untouched holdout.

Body-size MAE, in bytes per whole window, is shown below rounded to two decimals.
Full scans sum 256 individually rounded tile predictions; sampling multiplies
the sum of 32 fixed tile predictions by eight.

| Corpus / family | Windows | Cheap full | Quadrants full | Cheap sample32 | Quadrants sample32 |
|---|---:|---:|---:|---:|---:|
| Real Roaring, all | 42,002 | 1.69 | 1.34 | 13.77 | 13.75 |
| census-income | 1,001 | 11.13 | 7.46 | 47.15 | 45.28 |
| census1881 | 4,000 | 3.59 | 3.58 | 19.08 | 19.06 |
| dimension | 27,567 | 0.50 | 0.55 | 6.31 | 6.40 |
| uscensus2000 | 2,221 | 0.02 | 0.02 | 3.25 | 3.25 |
| weather_sept_85 | 3,848 | 7.22 | 3.94 | 51.75 | 51.25 |
| wikileaks-noquotes | 3,365 | 1.12 | 1.21 | 22.15 | 22.23 |
| MS MARCO | 2,160 | 16.63 | 15.69 | 76.48 | 74.92 |

Dimension contributes 65.63% of Roaring occurrences and has no windows near
the packed-layout allocation crossover. The small overall MAE therefore hides
the more relevant weather cases. Roaring full-scan maximum errors are 281 bytes
for cheap and 130 for quadrants; MS MARCO maxima are 123 and 67 bytes.

The [storage decision helper](../storage_decision.h) defaults to
`aligned_allocation`: metadata plus `64 × ceil((body + 64) / 64)`.
`readable_extent` instead counts metadata plus body plus the 64-byte suffix.
Metadata is 512 bytes for local16/scan128 or 1,024 for direct32, already a
multiple of 64. Neither scope includes allocator bookkeeping or object/control
storage. Both compare their predicted total strictly against 8,192 plain bytes;
there is no per-tile raw escape, 32-byte cap or fitted margin. For example, a
7,553-byte body with packed metadata has an 8,129-byte readable extent but an
8,192-byte allocation, so only the readable-extent predicate chooses BEC.

For **512-byte metadata and aligned allocation**, decision regret is the summed
excess actual bytes over choosing the smaller actual BEC/plain representation.
A false conversion includes an allocation tie, which costs zero extra bytes.

| Corpus | Model / scan | False conversions | Missed-saving windows | Regret, bytes |
|---|---|---:|---:|---:|
| Real Roaring | cheap / full | 0 | 5 | 320 |
| Real Roaring | quadrants / full | 0 | 1 | 64 |
| Real Roaring | cheap / sample32 | 2 | 9 | 2,176 |
| Real Roaring | quadrants / sample32 | 3 | 7 | 1,920 |
| MS MARCO | cheap / full | 4 | 0 | 0 |
| MS MARCO | quadrants / full | 6 | 0 | 0 |
| MS MARCO | cheap / sample32 | 11 | 21 | 2,624 |
| MS MARCO | quadrants / sample32 | 9 | 24 | 2,688 |

Only 66 Roaring and 373 MS MARCO windows are within 512 bytes of this allocation
crossover; all natural regret in the table occurs there. Roaring full-scan
regret comes entirely from `weather_sept_85`. MS MARCO's full-scan false
conversions are all allocation ties, not evidence of perfect classification.
With direct32's 1,024-byte metadata, aligned regret is 192/0 bytes for Roaring
cheap/quadrants full and zero for both MS MARCO full scans. Scope changes are
material: packed-layout readable-extent regret is instead 159/2 bytes for
Roaring full and 84/50 bytes for MS MARCO full. Body-error statistics stay the
same; rounding changes decisions, loss units and near-crossover membership.

The structural suite reverses the full-model aggregate ranking: cheap versus
quadrants MAE is 104.26 versus 186.57 bytes, with packed-layout aligned regret
640 versus 1,216 bytes. Quadrants helps the random dense-mixture family
(84.58 → 52.03 bytes MAE), but worsens the byte-structure family
(125.89 → 311.43). These curated occurrence weights are not natural frequencies.

Sampling's largest error is 10,528 bytes. When only the 32 sampled tiles contain
repeated `0x55` bytes, actual body size is 1,504 bytes and cheap sample32 predicts
12,032; when those tiles are empty and the other 224 are dense, actual size is
10,528 and both sampled models predict zero. The 16 sample-alias cases alone
produce 9,024 bytes of aligned regret for either sampled model, outside the
near-crossover subset. A confirmation policy restricted to predictions near
the threshold would not catch these failures.

Use full scans as the accuracy reference while comparing their measured cost.
Keep cheap full as a useful control: the extra quadrant features help natural
threshold cases here, but do not justify a universal winner. Sample32 remains
an explicitly heuristic candidate for callers willing to accept missed savings
or confirm actual encoded size. None of these predictions proves capacity or
actual savings; that obligation remains with the storage owner and actual
encoded byte count. This capture does not establish a confidence band or tune
a confirmation margin.
