# Exact selections as message enhancement

2026-09-26. A focused comparison within the broader dissemination investigation.
Message enhancement is general: an application can attach exact row selections,
partial aggregates, projections, decoded representations, references to reusable
derived state, or an execution plan. A plan hint is one example. This study
explores **which rows match a predicate**; it does not select a universal transform,
relay placement, encoding, publication rule or topology.

The [network-backed experiment](enhancement_study.py) keeps each invocation's
exact matching rows identical across four policies. It varies which recipients
already hold the input version, how much of the input matches, predicate cost,
and the price/capacity of destination edges. It charges missing row data, so a
small bitmap does not claim to replace bytes needed to materialize a result.

## What is compared

Each invocation filters a distinct immutable input chunk containing 4,096 rows
of 32 bytes. The authored truth predicate selects exactly `floor(rows × fraction)`
stable row IDs, either scattered reproducibly or in one contiguous run. All three
consumers require those matching rows at the same input version and cut. Chunk
identities differ between invocations, so the cold cases do not repeatedly fetch
one unchanged artifact while silently ignoring cross-query caching.

The origin, relay and one consumer are in an authored cheap local zone. Another
consumer is across an AZ edge; the third uses a public edge. This deployment is a
comparison fixture. Public propagation is 1 ms with 125 bytes/µs shared service;
local/cross-AZ propagation is 8/100 µs. The generic witness fixture's slow-witness
offset is disabled. Prices are illustrative coefficients from the shared model,
not actual provider quotes. All policies use the same topology and inputs.

| Policy | Work and bytes |
| --- | --- |
| `raw` | Each consumer receives a predicate request and evaluates it. A consumer with the correct resident base receives no base rows; otherwise the origin sends the full chunk. |
| `blocking` | The relay evaluates once and sends an exact selection. A consumer with the correct resident base uses it directly. Otherwise the message also carries the qualifying rows, priced on that edge. A cold relay first receives the full input chunk. |
| `optional` | Send the ordinary request/base path and a separate exact selection. Use whichever valid computation completes first after the needed base arrives. Work already admitted to a CPU queue is not cancelled; count it even if another result wins. The sidecar does not ship a second copy of selected rows. |
| `edge_selective` | An explicit static comparison rule shares selections on cheap edges, lets a public consumer with the right resident base filter locally, and sends selected rows to nonresident consumers. It observes declared residency and edge class, not future completion times. It is not an optimizer. |

Resident inputs mean the application declares the exact requested chunk/version
readable before the invocation. Prepositioning, its history retention and storage
cost remain outside this query comparison. A stale resident copy is unusable:
the raw policy fetches the correct base and the enhanced policy sends correct
selected rows. An optional selection cannot materialize absent rows by itself.

The default predicate service is 0.05 µs per row; a cheap-predicate case uses
0.001 µs per row. Encoding costs 0.0005 µs per input row at the relay; decoding
costs 0.2 µs plus 0.001 µs per selected row. These are assumptions, not timings of
the Python encoding routines. Predicate, encoding, decoding and packet work use
the shared modeled CPU queues, alongside NIC/fabric service and finite credits.

## Exact encodings and validity

The encoder compares a bitmap, uint32 stable-row-ID list, and uint32
`(start,length)` physical spans. It chooses the smallest payload and adds an
authored 112-byte binding/coverage record. This is a small representation
comparison, not a production wire format; delta coding, compressed bitmaps and
receiver decode complexity may change the winner.

| Declared fraction/pattern | Exact matches | Chosen form | Total selection bytes |
| --- | ---: | --- | ---: |
| 0.001, scattered | 4 | Row IDs | 128 |
| 0.1, scattered | 409 | Bitmap | 624 |
| 0.8, scattered | 3,276 | Bitmap | 624 |
| 0.8, contiguous | 3,276 | One span | 120 |
| Empty | 0 | Empty row-ID list plus complete coverage | 112 |

Bindings name the dataset/chunk, input version, cut, predicate definition, logical
row domain and physical mapping. Bitmap/spans require the named physical row
mapping. Stable row IDs can be applied to a different physical ordering of the
same logical domain. A content reference covers this metadata and coverage flag
as well as encoded selection bytes; a missing reference still owes a fetch.
Neither a cached reference nor a digest proves that an arbitrary predicate was
evaluated correctly.

An exact empty selection proves completed coverage of its bound domain. Silence,
a lost message, a truncated join or an uncompleted partition does not. The checks
include a partition that drops the public recipient's empty selection: two
consumers finish, the third remains owed, and whole-invocation completion is zero.
An approximate filter with false positives may help prune candidates, but cannot
substitute for this exact predicate result without the application's remaining
verification. No Bloom-filter output is treated as an exact selection here.

The application supplies predicate meaning, exact coverage and transform
validity. The relay still needs the applicable sandbox, deterministic execution
or required verification. Those protocols are not implemented by an integer
bitmap or a binding check. A tentative enhanced message grants no publication
authority; the measured endpoint is **all requested exact result rows available
at the consumers**, not a new durable-write or external-effect guarantee.

## What the bounded comparison shows

The selected run uses eight invocations for each of ten workload configurations,
and compares all four policies. Every retained healthy case completes all 24
required recipient results. The 40 comparisons retain queue/resource work,
offered/completed counts and unfinished work alongside byte and latency results.
Eight invocations are insufficient for any p99/p99.9 confidence claim; the
reported p99 here is effectively the slowest modeled invocation.

With all bases resident and 10% scattered matches, moving predicate evaluation
to the relay reduces total predicate service from **4,915.2 to 1,638.4 µs**.
It adds 16.384 µs encoding and 14.616 µs decode work, and wire traffic rises from
**6,720 to 22,400 bytes**. Local completion becomes **213.63 → 225.60 µs**;
public completion becomes **1,207.27 → 1,224.51 µs**. Repeated compute was saved,
but no base-data network traffic existed to remove, and the extra relay/encoding
dependency makes those completions later.

With no bases resident and 0.1% scattered matches, relay selection plus matching
rows reduces wire traffic **3,342,528 → 13,568 bytes** and public completion
**3,341.77 → 1,220.26 µs**. The enhanced messages include **3,072 selected row
bytes** across all 24 consumer results. At 80% matches, selected-row shipping is
still about 2.52 MB and total wire traffic 2.69 MB; a 624-byte bitmap is not the
complete network bill. The relative gains depend on actual input residency and
output size.

Optional selection is not guaranteed to save CPU. In the resident cases it raises
predicate service to **6,553.6 µs** because consumers finish their local filters
before the sidecars arrive, while relay filtering still ran. In nonresident
cases a selection can sometimes win while a consumer's already admitted filter
continues running; the model counts both. Queue cancellation or delayed local
start would be different policies with their own delay and work costs.

The stale-base/cold-relay case is a useful price counterexample. Blocking
enhancement moves about 1.05 MB of full input to the relay along a cheap edge,
then sends only selected rows over the public edge. Total wire bytes slightly
increase (**1,118,656 → 1,123,456**) while the illustrative price falls sharply.
Fewer total bytes, lower priced bytes, less CPU and lower latency are different
objectives. Mixed-residency cases also reveal that removing a large base transfer
from the origin's NIC can advance an unrelated public control message; that
latency change is shared-resource interference, not faster bitmap decoding.

## Reproduce, vary, and interpret

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/check_enhancement.py \
  --output build/orbital-dissemination/enhancement-checks.json
orb -m ubuntu python3 workbench/spikes/orbital-dissemination/enhancement_study.py \
  --count 8 --output build/orbital-dissemination/enhancement-selected.json
```

`--config PATH` compares four policies for one supplied JSON workload.
`run_enhancement(config, policy)` supports programmatic scenarios. Controls include
rows/row width, selectivity/pattern, predicate/encode/decode cost, residency,
stale recipients, relay residency, rate/count, and shared network capacities and
faults. The source hashes are captured before and after the campaign; output is
refused if they change. Core transport and write/read modules remain unchanged.

The **ten checks** cover exact round-trip encoding and choices, input/predicate/
cut/domain/mapping rejection, row-ID remapping, complete empty coverage, content
reference binding, resident compute accounting, nonresident selected-row bytes,
optional stale-sidecar fallback, cold/stale input acquisition, explicit edge
selection and loss of an empty selection. Related checks share one test function
where they exercise the same boundary.

Output includes per-recipient completion, all-required completion, useful result
rows/bytes, full-base/relay-input/selection/selected-row payload bytes, wire bytes
and price, predicate/encode/decode work, total CPU service, retries, resource
occupancy and cutoff backlog. The exact row-set oracle checks semantic equality;
the model has no real table operator, memory-bandwidth scan kernel, selector
signature protocol, changing-cut recovery, cross-query artifact cache, or native
verification implementation. Cold input acquisition, placement changes and
different matching patterns can reverse these comparisons. No policy is selected
on the strength of these favorable synthetic constants.
