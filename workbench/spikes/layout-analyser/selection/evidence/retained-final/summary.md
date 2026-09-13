# Retained-plan selection study

Each cell is observed subset regret after complete-plan probes. Median / maximum across the nine machine/mixture cases; no statistical winner claim.

| Method | 1 layout | 2 | 4 | 8 | 16 | 28 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| coaccess_first | 1.27% / 25.88% | 1.20% / 25.88% | 0.22% / 14.81% | 0.02% / 0.53% | 0.00% / 0.22% | 0.00% / 0.00% |
| density_first | 0.90% / 5.99% | 0.70% / 5.99% | 0.53% / 5.95% | 0.32% / 5.34% | 0.00% / 1.80% | 0.00% / 0.00% |
| diverse | 0.90% / 5.99% | 0.60% / 1.53% | 0.39% / 1.53% | 0.39% / 1.53% | 0.00% / 1.16% | 0.00% / 0.00% |
| edge_first | 0.90% / 5.99% | 0.70% / 5.99% | 0.53% / 5.95% | 0.32% / 5.34% | 0.00% / 0.22% | 0.00% / 0.00% |
| isolated | 0.64% / 9.93% | 0.53% / 9.93% | 0.36% / 8.81% | 0.00% / 0.74% | 0.00% / 0.02% | 0.00% / 0.00% |
| outer_order_first | 0.90% / 17.62% | 0.90% / 3.89% | 0.57% / 3.89% | 0.46% / 1.80% | 0.02% / 0.53% | 0.00% / 0.00% |

Held-out mixture selection: two other mixtures train each palette. Fast uses nearest training mixture; fitted probes every palette plan.

| Palette plans | Fast median / max regret | Fitted median / max regret |
| ---: | ---: | ---: |
| 1 | 2.17% / 13.55% | 2.17% / 13.55% |
| 2 | 2.17% / 44.92% | 1.03% / 7.10% |
| 4 | 2.17% / 44.92% | 0.75% / 3.89% |
| 8 | 2.17% / 44.92% | 0.62% / 3.89% |

## Limits

- 28-layout measured subset only; structural diversity restricted to that subset
- finalist and local-fit choices charge plan-probe counts, not estimated experiment time
- held-out workload classes share a capture and data set; not collection/key-region/temporal holdouts
- three observed repetitions summarized by medians; no statistical winner claim
- no independent container generalization or migration performance established
