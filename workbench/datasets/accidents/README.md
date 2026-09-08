# Accident descriptions and published extraction queries

The December 2021 US Accidents CSV and four BLARE queries, pinned through BLARE
revision `b3c2a344307aed76e70ad1c40fd772c160d89425`. The [source descriptor](dataset.json)
records exact hashes, including the published LFS hash for the 1.15 GB CSV.
The [original workload note](../../spikes/regexp-lowering/workloads.md) records
publisher provenance and interpretation limits.

`datasets.py get accidents` produces `descriptions.jsonl` with source row IDs,
the unchanged `queries.txt`, and preparation metadata. Defaults retain 64 evenly
spaced blocks of 1,024 descriptions, preserving order and duplicates. Parameters
`blocks` and `block_rows` choose other samples. Block offsets and the checked
source count of 2,845,342 rows accompany each variant.

Preparation parses the source once, checks the record count, and caches the
result. Reuse skips parsing. The source CSV stays externally referenced and
locally cached; publishing a prepared variant uploads only the selected records
and queries. Four queries provide limited syntax coverage, and source order is
not asserted to be timestamp order. Boolean matching, captures, and scan order
remain choices for the consuming study.
