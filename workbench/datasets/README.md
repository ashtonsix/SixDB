# Datasets

Reusable source records and preparation recipes:

- [Ua-parser](uap-core/README.md): regex rules and fixture strings.
- [Accident descriptions](accidents/README.md): text records.
- [Real Roaring](real-roaring/README.md): bitmap collections with source-list lineage.
- [MS MARCO keyset sample](msmarco-keyset/README.md): Boolean term postings.

These are available inputs, not a prescribed workload matrix. The
[keyset input notes](keyset-windows.md) cover window format, grouping for model
training/evaluation, and use on small workers.

From Linux (prefix with `orb -m ubuntu` from the Mac):

```sh
python3 workbench/tools/datasets.py list
python3 workbench/tools/datasets.py get uap-core
python3 workbench/tools/datasets.py get accidents --param blocks=8 --param block_rows=256
```

`get` prints a local directory. It reuses checked prepared data, restores a
published variant when available, or prepares it once from pinned sources.
An unavailable S3 mirror falls back to source preparation; AWS access is not
required for ua-parser or accidents. The retained Calico keyset sources use S3
and require read access to the existing bucket. Corrupt downloaded bytes remain an error.
Ua-parser does not fetch accidents. The first accident preparation can download
a 1.15 GB CSV; existing checked downloads from the regexp spike are reused.
Repeated calls skip parsing. Concurrent callers share a lock and atomically
installed result. Interrupted preparations leave no partial cache entry.

Each catalog entry has a small source descriptor and a plain Python recipe.
The helper keys preparation by source hashes, recipe bytes, and parameters;
`prepared.json` records those and the output hashes. Output bytes and
preparation metadata both contribute to the prepared identity. Source downloads and
prepared variants have separate caches under ignored `build/datasets/`.
When using multiple worktrees, point them at one cache, for example:

```sh
export SIXDB_DATA_CACHE="$HOME/sixdb/build/datasets"
```

The same setting can move new preparations to another volume. Reuse a checked
cache while iterating; an inactive variant can be recovered from its available
pinned inputs and recipe, or a published reference kept outside the cache.
Recipe changes create new variants; old recorded inputs retain their identity.
Git keeps recipes, provenance notes, and selected S3 references. Large source
bytes remain at their existing locations; use existing Calico S3 objects where
available. The helper supports pinned HTTP and S3 sources.

Publication is separate from running an experiment:

```sh
python3 workbench/tools/datasets.py publish uap-core
```

This uploads and download-verifies the prepared variant, then writes a small
reference under the dataset's `prepared/` directory. Another checkout can reuse
that reference. `datasets.py fetch REFERENCE.json` restores a particular older
variant. The existing content-addressed S3 store also deduplicates prepared
inputs shared by retained runs.

## Using inputs in a spike

Python callers can import `datasets` from `workbench/tools` and call
`datasets.get('uap-core')`. A spike-specific binary adapter can use
`datasets.cached(name, recipe_path, input_identities, parameters, build_function)`;
the build function writes files into the supplied directory. It needs no
catalog registration. The [regexp adapter](../spikes/regexp-lowering/prepare.py)
is an example and reproduces its original six prepared files byte-for-byte.

`run.input('inputs', prepared_directory)` records the dependency and returns
its local path. Normal runs need no S3 access when the input is cached. On
retention, the input is published once and the run bundle stores its reference.
Fetch restores the dependency into the recorded subdirectory automatically.
Existing runners can still accept ordinary custom input directories.

Dataset identity does not prescribe the experiment. Sampling parameters belong
to the prepared variant; query pairing, access order, weighting, and execution
semantics belong to the workload using it. Keep those choices in the spike
until another study benefits from sharing them. No dataset registration or
retention step is required for a one-off experiment.
