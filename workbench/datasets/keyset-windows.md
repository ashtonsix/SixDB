# Reusing keyset windows

[Real Roaring](real-roaring/README.md) and
[MS MARCO](msmarco-keyset/README.md) use the same prepared binary format:
repeated `[little-endian u32 cardinality][cardinality × little-endian u16 position]`.
Each record covers 65,536 positions. Its sidecar line has `ordinal`, byte
`offset`, `cardinality`, and original `high16`; it also carries `bitmap_id`
(Roaring) or `term` (MS MARCO). Reconstruct an original position as
`(high16 << 16) | position`. Ordinals are local to the named `.kw16` file.

For 256-bit examples, divide a window into its 256 cells and retain the parent
identity and cell ordinal. Within a bitmap byte, position `p` is bit `p % 8`
of byte `p / 8`. Include empty cells when the workload counts them; silently
dropping them changes the distribution. The recipes preserve nonempty source
windows and do not invent wholly absent windows. Source manifests copied into
the output describe the original objects; `prepared.json` hashes the actual
prepared files.

```python
import datasets  # workbench/tools on sys.path

inputs = datasets.get('real-roaring')
# In a recorded run:
inputs = run.input('inputs/real-roaring', inputs)
```

## Training and evaluation

Choose the split to match the generalization claim. For a cheap compressed-size
predictor, a useful first comparison is a held-out source family, with model
choices made on other families. This is a recommendation to reveal failure
modes, not a required fold matrix or training framework.

- Split source groups before generating cells, complements, mutations, or
  binary-operation examples. Keep a source bitmap's descendant windows/cells
  together. Generate paired operations within a split so both parents belong
  to it. A seeded random split of cells mostly tests nearby variations of
  already-seen lists.
- Real Roaring's six conservative families are `census-income`, `census1881`,
  `dimension`, `uscensus2000`, `weather_sept_85`, and `wikileaks-noquotes`.
  Keep original and `_srt` variants together for a family holdout. A source-list
  split using `bitmap_id` answers a narrower within-family question; that ID
  hashes source text and does not identify near duplicates or reordered rows.
- Keep each MS MARCO term together for an unseen-term test. Its terms share
  the document universe, so this is not an unseen-collection test. Holding
  out MS MARCO as a whole gives a useful separate distribution check.
- Choose features, LUT values, coefficients, quantization, and piecewise
  boundaries using training/validation data. Keep a final comparison out of
  those choices; if it changes the model, describe it as development evidence.
  Save the group assignments/seed and model bytes with the run so reuse does
  not depend on remembering a split.
- Report error by family and cardinality/shape as well as an overall number.
  Keep occurrence-weighted and deliberately balanced samples distinguishable.
  Identical small bitmaps naturally recur even across independent groups;
  inspecting exact-pattern overlap or a novel-pattern subset helps explain
  generalization without deleting common zero/full cases from the workload.

This applies the usual [grouped-validation principle](https://scikit-learn.org/stable/modules/cross_validation.html#cross-validation-iterators-for-grouped-data)
to these particular sources. Scikit-learn is not required by the data tooling.
Keep label definition and the cost of wrong predictions in the spike: raw
codec bytes, capped storage bytes, and a downstream allocation decision are
different targets. If labels are deterministic byte sizes, the same fitted
model can be evaluated on each ISA; predictor runtime is measured separately.

## Small workers and retention

Both recipes use Python 3.11+ standard library and bounded streaming buffers.
They work with the [worker](../tools/workers.md)'s Ubuntu 24.04 Python and S3
read role; no Calico checkout, compiler, NumPy, or training package is needed
to prepare them. Source downloads total about 327 MB for both collections;
keep the default data cache outside `$SIXDB_RESULTS`. Preparation and download
belong before timing. The complete 256-bit expansion can be much larger than
the window files: stream or sample it when using a 2 GiB worker.

The existing `Run.input()` and artifact helpers retain derived inputs once
and restore them on fetch. Original Calico S3 objects remain in place. A cold
worker can always prepare from those pins without a published prepared mirror.
Do not copy source archives or whole caches into result bundles. Trial feature
tables and fitted models belong in the spike's ignored output/selected evidence;
the shared catalog does not fix features, labels, or splits for every study.

Run `python3 workbench/tools/check_keyset_datasets.py` for offline adapter checks.
Add `--full` to reconstruct the pinned original lists and check historical
compatibility; this resolves the catalogs and can download on a cold cache.
