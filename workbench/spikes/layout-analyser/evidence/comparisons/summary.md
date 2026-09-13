# Selected paired consumer contrasts

Candidate / baseline elapsed time. Below 1 is faster. Ranges are the two matched seed ratios of three-repetition medians, not confidence intervals. All rows use 262,144 logical records/keys. The CSV also retains the 4,096-row controls and other operations.

| Change / operation | Zen 5 ratio range | Granite Rapids ratio range |
| --- | ---: | ---: |
| 7-byte prefix / equality_mismatch7 | 2.686–2.718 | 2.905–3.044 |
| 9-byte prefix / equality_mismatch8 | 0.366–0.396 | 0.410–0.430 |
| 7-byte prefix / equality_mismatch17 | 1.244–1.491 | 1.496–1.515 |
| 63 to 64 padding / all_fields | 0.956–0.983 | 0.940–0.957 |
| separate prefix / equality_mismatch7 | 0.743–0.791 | 0.659–0.665 |
| separate prefix / full_string_rare | 1.274–1.349 | 1.300–1.319 |
| N8 padding / load90_uniform/mixed/rawfp_tuplepack | 0.966–0.968 | 0.850–0.851 |
| N8 fingerprint 7 to 8 / load90_uniform/mixed/rawfp_tuplepack | 0.805–0.808 | 0.744–0.748 |
| N9 padding / load90_uniform/mixed/rawfp_tuplepack | 1.005–1.007 | 1.074–1.090 |
| N9 split / load90_uniform/mixed/rawfp_tuplepack | 1.102–1.103 | 0.949–0.964 |
| N16 padding / load90_uniform/mixed/rawfp_tuplepack | 1.003–1.016 | 1.094–1.096 |
| N16 fingerprint 8 to 12 / load90_uniform/mixed/rawfp_tuplepack | 1.280–1.291 | 1.299–1.303 |
| N16 split / load90_uniform/mixed/rawfp_tuplepack | 1.045–1.077 | 0.971–0.973 |
| N16 split fingerprint 8 to 12 / load90_uniform/mixed/seriespack_tuplepack | 1.043–1.060 | 1.124–1.131 |

Candidate definitions and recipe differences are in `prefix/README.md` and `bucket/README.md`. This selection illustrates contrasts; complete retained samples include every measured candidate and repetition.

## Authored space/time frontier

Same 262,144-key, 90%-uniform mixed read trace; both recipes retain ordinary TuplePack. Median of seed medians, encoded buffer footprint only. This is the observed finite-menu frontier, not statistical dominance. `bucket-frontier.csv` includes every compared point.

| Host | Candidate / recipe | Encoded bytes | ns/op |
| --- | --- | ---: | ---: |
| zen5 | n16b8stride104_split / seriespack_tuplepack | 2,799,744 | 36.060 |
| zen5 | n8b8stride64_combined / rawfp_tuplepack | 3,028,992 | 33.249 |
| gnr | n16b8stride104_split / seriespack_tuplepack | 2,799,744 | 44.130 |
| gnr | n8b8stride64_combined / rawfp_tuplepack | 3,028,992 | 43.226 |
