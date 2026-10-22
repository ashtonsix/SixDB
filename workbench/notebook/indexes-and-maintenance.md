# Indexes and maintenance

The directory structure is implemented in [`engine::directory`](../../engine/README.md#segment-directory);
its evidence and rejected alternatives are in the
[segment-directory study](../spikes/segment-directory/README.md). Links below
into the earlier complete-consumer directory study now lead to its
[archive notice](retired-spikes.md#index-directory-spikes--october-2026); the
paths they name are inside that archive. The routes recover related work that
is easy to miss when starting from a directory data structure.
The [Engine guide](../../engine/README.md) owns what is actually implemented.

## Logical order, physical placement and duplicates

| Question | Read together |
| --- | --- |
| Can natural regions stay cheap while sparse regions remap? | [Trie-remapping findings](../spikes/trie-remapping/FINDINGS.md), then [prefix addressing](prefix-addressing.md) and the current directory brief |
| What does spare key space save if packed columns still move? | [Trie-remapping design](../spikes/trie-remapping/design.md) and [findings](../spikes/trie-remapping/FINDINGS.md): locator repair, column movement and conversion are different costs |
| What ordering does a prefix name actually establish? | [Prefix addressing](prefix-addressing.md): routing name, stable identity, mapping domain and payload order |
| How should bounds locate an ordered source? | [Maintained Intervals usage](../../ikea/docs/intervals/usage.md), [ownership study](../spikes/boundlist/OWNERSHIP.md) and [consumer portfolio](../spikes/boundlist/PORTFOLIO.md) |
| What changes when equal logical keys cross storage boundaries? | [Directory query semantics](retired-spikes.md#index-directory-spikes--october-2026), [owning multimap frontier](../spikes/hashtable/README.md) and [K-way merge](../spikes/hashtable-merge/README.md) |
| Do wider natural partitions repay their copy-on-write edit cost? | [Native partition comparison](retired-spikes.md#index-directory-spikes--october-2026) and [population counterexamples](retired-spikes.md#index-directory-spikes--october-2026): natural24's small edit-copy result covers spread L4; spread L3 and concentrated L4 expose large copies (body counters, not timings) |
| What sparse-scan cost remains after preparing boundaries? | [Native scan follow-up](retired-spikes.md#index-directory-spikes--october-2026), including one-row-Segment losses and independently fused controls |

The remapping probe kept both fully ordered gaps and order between blocks in
play. Finding the remapped suffix is itself an index operation. Spare physical
positions do not create spare rank positions in a packed column. Those are
useful warnings, not a selected SixDB topology. [Calico's map](calico.md) locates
its earlier trie and Engine work for comparison.

## Avoiding per-write ancestor work

Read [aggregate-maintenance conclusions](../spikes/aggregate-maintenance/CONCLUSIONS.md)
before designing eager folding, append contributions or dirty summaries. Its
[shared-buffer proposal](../spikes/aggregate-maintenance/shared-dirty-buffer.md)
and [dirty-buffer results](../spikes/aggregate-maintenance/dirty-buffer/FINDINGS.md)
distinguish idempotent dirtiness from exact numerical maintenance.

The original [count/sum experiment](../spikes/aggregate-maintenance/FINDINGS.md)
removed 71% of higher-stratum adjustments under uniform writes, yet its measured
cycle became about 24 times as costly. Construction, sorting, allocation and
query correction must repay the logical work saved. The summary-only fixture
does not establish physical write amplification or an MVCC policy.

The later [directory buffered consumer](retired-spikes.md#index-directory-spikes--october-2026) and
[native pending-aggregate comparisons](retired-spikes.md#index-directory-spikes--october-2026)
revisit this in a complete caller. Preserve the earlier no-filter win and
dispersed-write losses beside later useful filtering regions. Read-only pending
queries and a full mutation/drain cycle answer different cost questions.

Exact aggregates also need a numeric contract. The
[aggregate semantics discussion](../spikes/aggregate-maintenance/design.md#separate-the-aggregate-semantics)
keeps overflow, NULLs and floating-point regrouping separate from storage policy.
[Sketches, filters and histograms](secondary-summaries.md) have different jobs:
proving absence, estimating selectivity and answering approximately cannot be
treated as interchangeable summaries.

## Selecting a table or consuming packed rows

[Hashtable benchmarks](../benchmarks/hashtable/README.md#find-the-result-that-answers-your-question)
route to construction, writes, reads, memory and Loom consumers. Read the
[memory-matched comparison](../spikes/hashtable-boost-adoption/MEMORY.md) beside
native-reserve rankings. The
[predictability study](../spikes/hashtable-primary-branchless/GRADUATION.md)
and [borrowed-inline promotion](../spikes/hashtable-wide-values/GRADUATION.md)
are later changes; the Boost-adoption study is not the current lookup API.

For several fields from one row, start with
[Ikea consumers](../../ikea/docs/consumers.md) and
[native payload transfers](../../ikea/docs/tuplepack/extending.md#keep-native-payloads-native).
The [ordered directory consumer](retired-spikes.md#index-directory-spikes--october-2026)
shows why that existing seam matters. The
[representation route](representations-and-planning.md) connects it to physical
layout and conversion rather than assuming a wrapper automatically fuses decoding.

## Publication and complete costs

The [selected directory evidence](retired-spikes.md#index-directory-spikes--october-2026)
separates complete ownership cohorts, point storage, sparse scans, duplicates,
allocation and causal drain. Follow the relevant panel rather than promoting
one fast point result into an index-wide conclusion.

The [native follow-up](retired-spikes.md#index-directory-spikes--october-2026)
also compares prepared predicates and complete three-row placement cycles,
retaining structural-edit losses beside qualified numeric updates. Earlier
[Ring cohorts](retired-spikes.md#index-directory-spikes--october-2026) and
[large point-storage/memory comparisons](retired-spikes.md#index-directory-spikes--october-2026)
retain their distinct scopes. The later
[explicit-context API qualification](retired-spikes.md#index-directory-spikes--october-2026)
checks numerical-state admission and paired complete pending reads on local
NEON. The frozen native campaign qualifies its own source, rather than that
subsequent API cleanup.

[Object material](../../orbital/docs/material.md) owns current publication and
physical-source composition. [Memory and lifetimes](memory-and-lifetimes.md)
connects the consumer to retained-reader, allocation and reclamation findings;
[transaction workloads](transactions/README.md) supplies cases beyond point lookup.
