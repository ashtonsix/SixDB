# Dirty-buffer validation

The [verified bundle reference](artifact.json) preserves four original files:
`sanitize-build.log`, `sanitize.csv`, `sanitize.stderr`, and `rejected-analysis.json`.
Fetch it with `artifacts.py fetch REFERENCE build/recovered/NAME`.

ASan/UBSan passed the final probe's small scenario, covering all methods with
one and four writers. Queries and final summaries matched the afterimage
oracle. This does not establish race freedom or concurrent reader publication.

The failed intermediate receipt records the analyzer's original omission of
Google Benchmark's `/manual_time` suffix when matching selections. Matching
was fixed and the affected measurements were rerun; failed output is not used
as successful evidence.
