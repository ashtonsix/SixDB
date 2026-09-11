# Current SeriesPack composition review across seven profiles

The authored function remains a useful composition boundary, but a tile-sized native execution plan is often a poor complete operation on x86. Of 144 matched authored-versus-materialized cases, 48 favor the basic authored form and 96 favor materialization, with separated repetition ranges. Considering each case's fastest **registered** native grain/carrier alternative changes this to 110 native wins and 34 residual losses. This is post-measurement selection among available controls, not an automatic dispatch policy or a claim of an optimal plan.

Profile and execution family remain separate below. An AVX2-labelled endpoint in a full-feature build can use that build's additional ISA features. Each row has 16 cases: eight representative formats and full/75% incoming masks. Times cover the full compare-and-modulo-u64-sum consumer over 8,192 logical values; the materialized alternative includes decoding to a reused minimal-width scratch array and consumption. Allocation, binding and the scalar correctness oracle are outside timing.

| Profile | Execution family | Basic authored wins / losses | Authored / materialized range | Fastest registered native wins / losses | Residual maximum |
| --- | --- | ---: | ---: | ---: | ---: |
| gnr-avx2 | avx2 | 7 / 9 | 0.540–1.704 | 15 / 1 | 1.055 |
| gnr-avx512 | avx2 | 5 / 11 | 0.741–1.583 | 14 / 2 | 1.157 |
| gnr-avx512 | avx512 | 6 / 10 | 0.482–1.798 | 14 / 2 | 1.324 |
| gnr-avx512bw | avx2 | 3 / 13 | 0.739–1.867 | 12 / 4 | 1.149 |
| v2-neon | neon | 15 / 1 | 0.287–1.018 | 15 / 1 | 1.009 |
| zen-avx2 | avx2 | 6 / 10 | 0.555–1.584 | 14 / 2 | 1.109 |
| zen-avx512 | avx2 | 1 / 15 | 0.953–2.758 | 8 / 8 | 1.925 |
| zen-avx512 | avx512 | 4 / 12 | 0.641–3.719 | 12 / 4 | 1.791 |
| zen-avx512bw | avx2 | 1 / 15 | 0.957–3.926 | 6 / 10 | 1.671 |

`cases.csv` retains every basic comparison, the direct control, three repetitions per plan, and the selected measured alternative. `native-variants.csv` retains every registered native alternative separately, including its losses; `summary.json` has the complete profile/family counts. `provenance.json` hashes the audited input table and records the original per-profile raw input identities and contexts.

## What the remaining cases say

- The largest basic-form losses occur for narrow Local5/7 on Zen: roughly 3.6–3.9× materialization in selected profiles. Their admitted dense32/dense64 alternatives reduce that to approximately 0.56–0.97× in the corresponding full-mask cases. Physical tile size therefore does not establish an appropriate execution grain.
- Local12 remains a significant unresolved cell **within this registered set**: full-profile Zen AVX512 basic authored is 2.610× materialized, and its best registered deferred-carrier form is still 1.791×; the AVX2 family in the same profile remains 1.925×. Grouped-consumer studies retain additional alternatives elsewhere; those older linked measurements are not silently substituted for this current table.
- Wide and headed cases also need their own choices. Local31/full-mask in the full Zen profile remains 1.305× with the best registered AVX512 alternative and 1.512× with AVX2. Local60/H8/full-mask improves from basic 1.419× to deferred 0.805× for AVX512, but remains 1.126× for AVX2. Under the BW-only ceiling, Local60/H8 is still 1.248× full-mask and 1.350× prefiltered after the best registered alternative. V2's Local60/H8/full-mask residual is approximately 1.009×.
- Authored versus direct ratios span about 0.671–1.010. The observations do not show material authoring overhead in these cases, but the substantial authored wins in some contexts also mean the controls should not be described as universally identical. Wrapper/direct agreement cannot establish competitiveness against materialization.

The strongest supported design judgment is to preserve authoring, native grain and result-finalization choices as separate concerns. The current component supplies useful composition mechanisms, and the narrow dense controls plus BEC consumer give concrete recovery examples. It does not establish that composing the basic tile producer always compensates for a primitive gap. There is no need to prescribe a per-width exception list from this table; a real consumer should compare appropriate admitted regions and carrier/finalization choices.

This is a reading of one frozen baseline, not a new candidate experiment or promotion decision. Sequential case observations and their three-sample ranges are not confidence intervals. Dense/grouped grain admissions do not follow merely from equal vector types, and the benchmark-only u64 carrier control does not establish a general erased/CPS ABI. Prefilter75 is a specific incoming mask pattern, not evidence of arbitrary progressive-filter scheduling or sparse-source skipping.
