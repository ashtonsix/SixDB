# MS MARCO Boolean keyset sample

```sh
python3 workbench/tools/datasets.py get msmarco-keyset
python3 workbench/tools/datasets.py get msmarco-keyset --param min_cardinality=16
```

The [descriptor](dataset.json) pins the 16 existing `.u32` posting lists at
`s3://calico-fleet-artifacts/corpora/msmarco-keyset/` (200.7 MB total), plus
the original roster, provenance, and checksum manifest. These are the retained
sample, not all the terms named in the roster. No source objects are republished.
The roster describes an 8,841,823-document collection; lists contain distinct,
increasing document IDs with Boolean membership only. There are no term
frequencies, document lengths, or query-pair frequencies in this sample.

SixDB's [recipe](prepare.py) produces one `.kw16` file and `.windows.jsonl`
index per term, preserving its name and original high 16 bits. The default
keeps every nonempty window, so the original `.u32` bytes can be reconstructed
exactly; the optional floor discards sparse windows. See the
[format and training notes](../keyset-windows.md).

Calico's `keyset/bench/peers.cpp::load_msmarco` used `the.u32`, `services.u32`,
and `humble.u32`, dropped windows below cardinality 16, and evenly sampled the
survivors. It reads `.u32`, so the prepared `.kw16` directory is not a direct
`KS_MSMARCO` replacement. To supply that loader, resolve the pinned originals
with `datasets.source(spec['sources']['the.u32'])` and stage them under their
original names outside the results directory. They already reside in the
shared download cache after `get`; no second download is needed.

The retained provenance identifies archived prototype-2 commit
`9a6acea729d942487440eb95da31d448380dbad6`. Historical extraction code is available
locally under `~/calico-archive/calico-prototype/tools/datasets/msmarco.sh` and
`msmarco_postings.cpp`: lowercase alphanumeric tokenization, distinct document
membership, and a union of frequency-rank and query-selected terms. These
explain the earlier extraction approach; the exact selection step that reduced
it to the retained 16 files is not recorded. Use the pinned objects to reproduce
this sample; the archive is provenance, not a build dependency.
