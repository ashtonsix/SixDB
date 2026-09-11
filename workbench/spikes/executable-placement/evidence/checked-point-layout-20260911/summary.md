# Checked-get improvement survives restoration of linked code placement

This bounded V2 experiment preserves the candidate checked getter's executed instructions and restores the baseline addresses and bytes of the surrounding executable code. The checked calls remain faster. Restoring placement removes much of the regression in an unchanged bound Striped5 control, while leaving a smaller residual. Keep the simpler checked path as a Workbench candidate; there is no production padding policy or universal performance claim.

| Witness, ns/query | Baseline | Candidate | Candidate with restored placement |
| --- | ---: | ---: | ---: |
| Local1 checked | 10.474 | 5.020 | 5.027 |
| Local1 bound control | 2.176 | 2.176 | 2.176 |
| Striped5 checked | 11.233 | 7.347 | 7.262 |
| Striped5 bound control | 2.380 | 3.283 | 2.532 |

The bound Striped5 candidate/baseline ratio is 1.380; restoration lowers it to 1.064. The restored control remains slower with separated sample ranges and ratios 1.064/1.063 on the two ordering edges. The checked benefit survives on both edges. The Local1 bound control remains approximately flat. These are observed repetition ranges, not confidence intervals. The remaining movement does not identify a particular predictor, cache, allocator or store-forwarding mechanism.

The original worker pair had shown six checked-call improvements and the bound Striped5 loss. Independent inspection found only checked get changed among 1,244 common O3 operations-object instruction/relocation bodies. The bound timed caller and selected Striped5 point function were byte-identical; the point function's linked address moved by 64 bytes. The original whole `.rodata` contents were also identical. That observation motivated this counterfactual rather than a new kernel implementation.

## Identity and measurement

Job `20260911T070108Z-f9a5ebd2` used derivative capture `2b63964ee5bf6289ab689bfdbd2ef6ee8ce90440e6e6eeab58765e5bd4d900b6`: all 601 original sources are unchanged, with three Workbench recovery/measurement files added. The original worker had expired. Reconstruction therefore compiled only its 12 caller/check objects and the Google Benchmark dependency, verified every original object/archive hash, restored the measured SeriesPack/Calico archives, and required byte-identical baseline and candidate benchmark relinks. It did not rebuild physical kernels or rerun the broad baseline.

The padded archive changes only the candidate checked-get text section: 48 bytes of unreachable AArch64 NOPs follow its 96-byte function. All other object section contents, including the candidate's unwind information, remain identical. The final executable's complete `.text` address/size/bytes match baseline except the checked-get slot; the executed getter bytes equal the unpadded candidate. Baseline `.rodata` address and contents are restored. Existing operations, scalar/NEON public-wire and static-point checkers pass after relinking against the padded archive. The alternative preserves the bounds check and value domain.

On Neoverse V2, Clang 21.1.8 `-O3 -march=armv8-a -mtune=neoverse-v2`, CPU0, the four existing gapped-placement witnesses run in base/candidate/padded/padded/candidate/base order. Each block has three sequential 50 ms minimum repetitions. There are 72 raw timing samples, with equal logical/query/placement counters and unchanged original query coordinates. `samples.csv` retains every repetition; `summary.csv` includes both ordering edges. `audit.json` identifies raw inputs and contexts; `layout-receipt.json` records reconstruction, hashes, links, checks and commands.

The first preparation job, `20260911T065653Z-868d01b9`, stopped at the recovery helper's output-path check before compiling or measuring. The retry corrected only that destination. Its failed status is retained rather than counted as a measurement. The successful measured binaries, archives, sources and full logs are recoverable through `layout-worker-artifact.json`; the original pair through `original-worker-artifact.json`.

The compact original assembly/code review is included as `original-review.md` and adjacent selected extracts. `current-v2-composition.json` is a separate review of the original frozen baseline: authored compare-and-sum beats reused-scratch materialization in 15 of 16 current V2 cases, with Local60/H8/full selection about 2% slower. That useful compound-operation result neither belongs to the layout intervention nor offsets unrelated primitive losses by assumption.
