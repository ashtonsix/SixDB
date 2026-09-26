# Selected dissemination evidence

All times, service rates, loss distributions and economic coefficients here are
**authored simulation inputs or modeled outcomes**. They are not hardware
measurements, cloud-price estimates or a p99.9 confidence claim. The selected
evidence contains 281 scenario/policy comparisons plus 44 separate exact ideal-
code calculations. Every selected failed, refused, late and unfinished case is
retained; smaller exploratory smoke runs are not.

| Artifact | Cases and cohort | Interpretation |
| --- | --- | --- |
| [comparison.json](comparison.json) | 95: 76 write/message ×1,000 offers, 14 read ×100 queries, five joins ×80 offers | [Main findings](../../FINDINGS.md), supplied payload/history/coverage facts with finite physical resources. |
| [prefix.json](prefix.json) | Four ×100 offers | Separate-producer effects under 300/600µs payload-path interruption. |
| [relays.json](relays.json) | Six ×1,000 offers | Enhancement sizes versus an unsuitable/suitable unloaded-RTT retry interval. |
| [adaptation.json](adaptation.json) | 34 ×600 offers | Static route families, selection granularity, equivalent sender release and notices. |
| [mixed.json](mixed.json) | 12 ×600 foreground offers; up to30 background reads per case | Foreground durable-notification proxy, shared CPU/outputs/cancellation; no witness admission. |
| [enhancement.json](enhancement.json) | Ten workloads ×four policies ×eight invocations, three required recipients each | Exact selection with real required result rows, declared residency and encoding/binding checks. |
| [edge.json](edge.json) | 40 ×100 offers; separate economic formulas | Directed illustrative tariffs, origin/relay placement, hashes, returns, overload and crash. |
| [proposal.json](proposal.json) | 26 ×300 offers, two producers | Actor-observed future proposal timing; existing prefix positions preserved. |
| [transport.json](transport.json) | 24 ×400 offers; 44 separate ideal-code calculations | MTU/CPU/loss/ACK sensitivity; exact erasure arithmetic is not an implemented FEC transport. |

Each artifact binds its executable source hashes. For enhancement, before and
after source hashes are retained explicitly; other drivers use `sources`.
The retention metadata also hashes the full input JSON and the retention script.
The full edge/proposal resource tables are reduced to service totals, peak/
outstanding bytes and the three largest mean waits; their complete configuration,
counters, per-producer/end-point outcomes, diagnostic traces and every case remain.
The identical edge tariff matrix is stored once. Full reports are reproducible
into ignored `build/` storage rather than stored repeatedly in Git.

The conceptual/source audit is separate: [catalog](../../CATALOG.md), exact
inspection inventories in its owning survey files, and
[source identity manifest](../../survey-source-manifest.json). That manifest
records source identities, not a claim that every referenced file was read in
full. Historical Calico/Consurgent measurements are not copied into synthetic
results or treated as inherited contracts.

## Reproduction

From the SixDB repository root, use the pinned Linux environment. No C++ build,
cloud infrastructure or third-party Python dependency is required.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/run_study.py --count 1000 --output build/workbench/orbital-dissemination/selected.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/prefix_study.py --output build/workbench/orbital-dissemination/prefix.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/relay_study.py --output build/workbench/orbital-dissemination/relays.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/adaptation_study.py --count 600 --output build/workbench/orbital-dissemination/adaptation.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/mixed_study.py --count 600 --output build/workbench/orbital-dissemination/mixed.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/enhancement_study.py --count 8 --output build/orbital-dissemination/enhancement-selected.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/edge_study.py --count 100 --output build/workbench/orbital-dissemination/edge.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/proposal_study.py --count 300 --output build/workbench/orbital-dissemination/proposal.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/transport_study.py --output build/workbench/orbital-dissemination/transport.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/retain_evidence.py
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/retain_evidence.py --verify-only
```

`check.py` runs each check script, including the custom ledger/graph checks that
ordinary unittest discovery would omit. The numerical comparisons are finite
cohorts: a completed percentile is always paired with its offered denominator
and unfinished work. Traffic includes ACKs, retries and many operations after
logical completion; the completed-TX byte counter does not yet include a packet
still transmitting at the drain boundary. Elapsed physical service is tracked
separately. Static RTT/MTU/price inputs, supplied cut/authority facts and trusted
transformation semantics remain limits described in [MODEL](../../MODEL.md) and
the individual studies. No successful run establishes a production SLO.

Final verification passed on 2026-09-26: 113 focused checks, 6,384 exact small-
graph oracle cases, 100 distance oracles and 10,000 sender rankings, plus source
hash verification of all nine retained artifacts. The catalog has 223 unique
entry identifiers; all 383 local Markdown links checked at handoff resolve.
These finite checks do not prove unrestricted protocol safety or survey coverage
outside the named corpus.
