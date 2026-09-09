# Real Roaring bitmap collections

```sh
python3 workbench/tools/datasets.py get real-roaring
python3 workbench/tools/datasets.py get real-roaring --param min_cardinality=16
```

The [descriptor](dataset.json) pins all 12 original ZIPs already retained at
`s3://calico-fleet-artifacts/corpora/real-roaring/` (125.9 MB total), using
Calico's `MANIFEST.sha256`. Nothing is copied to a second source prefix.
[Upstream](https://github.com/RoaringBitmap/real-roaring-datasets) distributes
the benchmark archives. Calico's retained `PROVENANCE.md`, manifest, and
`convert.py` describe its preparation; SixDB's [recipe](prepare.py) is fresh.

The output contains one `.kw16` file and `.windows.jsonl` index per archive,
plus `bitmaps.jsonl` recording archive member names, raw-content SHA-256 IDs,
and duplicate aliases. See the [format and training notes](../keyset-windows.md).
Members are visited by filename; byte-identical files within an archive are
emitted once, matching Calico. No cross-archive deduplication or sampling occurs.
The default keeps all nonempty windows; absent all-zero windows are not
synthesized because the full bitmap universe is unspecified.

`min_cardinality=16` reproduces Calico's floor and verifies all ten retained
historical `.kw16` hashes during preparation. `dimension_003` and
`dimension_008` had source ZIPs but no retained `.kw16`; SixDB prepares them too.
Calico's `keyset/bench/peers.cpp` used nine variants (the four original/sorted
pairs and `dimension_033`), then evenly sampled their window pools. This
catalog keeps the pools complete. Point `KS_REALROARING` at the floor-16
directory to supply the old peer loader with its expected files.

For evaluation across source families, group `_srt` with its original variant
and all three `dimension_*` projections together. The recipe records that
conservative family assignment in `prepared.json` and `bitmaps.jsonl`; it is
a grouping recommendation, not a recovered join between sorted and unsorted
bitmap identities.
