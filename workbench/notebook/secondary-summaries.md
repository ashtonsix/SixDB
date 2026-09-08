# Sketches, filters, and histograms

Secondary open research question, opened with Ashton on 2026-09-07. Consider
non-hierarchical summaries such as min-sketches, Bloom filters, and histograms
separately from the [essential descendant aggregates](../spikes/aggregate-maintenance/README.md).
This is a starting scope and reading list, not a selected representation.

**Which compact summaries help SixDB, what evidence do they supply to a
query, and how should they be built, refreshed, merged, or retired under
changing data?** Placement need not mirror the data structure's strata.

The term **min-sketches** is retained from the question. The intended family
has not been specified: MinHash/bottom-k style set samples and Count-Min
frequency sketches answer different questions. The examples below distinguish
them without selecting either interpretation.

## Different contracts

| Family | Possible use | Maintenance character and open issue |
| --- | --- | --- |
| Bloom-style membership filter | Prove absence, avoid probes or reads | Insertion sets bits; ordinary deletion cannot safely clear shared bits. Retaining deleted items wastes probes, while omitting visible additions can invalidate pruning. |
| MinHash / bottom-k style sketch, if intended | Set overlap / similarity estimates | Compatible sketches can support merging, but removing sampled minima may require replacement candidates or reconstruction. Hash/seed and set/multiset semantics matter. |
| Count-Min sketch, if intended | Frequency / heavy-hitter estimates | Counters combine additively, but guarantees depend on the update model; arbitrary signed intermediate deltas must not inherit non-negative-frequency guarantees. |
| Histogram and most-common-value statistics | Distribution and selectivity estimates | Fixed-bin counts can use signed updates; adaptive boundaries and sampled distributions need different refresh/merge policies. Correlation can dominate single-field accuracy. |

These are not all approximate in the same sense. A filter can provide a sound
negative test for exact execution. A histogram may only guide a plan. A sketch
may produce an explicitly approximate answer. Fixed-bin counts can be exact
while interpolation within their bins is an estimate. Define the intended
consumer before comparing representations.

Merging is not deletion support. Bloom OR requires compatible dimensions and
hash functions and can saturate; Count-Min addition requires compatible
counters/hashes; histograms with different boundaries need reconciliation.
Even where merge exists, acceptable error and useful selectivity may degrade.

## Questions to develop independently

- Which query decision benefits: membership rejection, join estimation,
  distribution estimation, or approximate result production? What would an
  error do to execution correctness versus planning quality?
- What is summarised: a field, tuple of fields, value domain, physical run,
  logical partition, or a workload-selected subset? How are predicates and
  deletions represented?
- Is the summary rebuilt with immutable runs, incrementally maintained in a
  mutable region, or refreshed on demand? Which merges preserve its contract?
- How are pending additions accounted for before a negative test can prune?
  When is a stale estimate acceptable, and how is its age/coverage exposed?
- What error/space/probe-cost trade-off remains useful under skew, distribution
  drift, deletes, repeated merging, and long snapshots?
- What is the cost of acquiring training samples or a replacement population,
  including cold reads? Shared scan/build work may be useful without sharing
  the essential aggregates' maintenance policy or API.

Potential probes: membership-filter saturation and probe savings through run
merges; histogram plan-quality drift under moving hot values and correlation;
and deletion/replacement cost for whichever min-sketch family is intended.
These are separate from the first study's count/sum write-amplification probe.

The [row-signature study](../spikes/row-filter-signatures/FINDINGS.md) now supplies
one concrete filter comparison: retaining joint byte-code presence can reject
blocks that marginal presence or Bloom-style OR cannot. Repeated values also
make one hash collision persist across many rows and blocks. Row-level error
rate, same-row correlation, Boolean projection onto planes, and physical
grouping therefore need separate treatment when estimating pruning value.
This is evidence for that filter family; the sketch and histogram questions
above remain open.

## Starting references

- [Bloom, “Space/Time Trade-offs in Hash Coding with Allowable Errors” (1970)](https://www.cs.princeton.edu/courses/archive/spr05/cos598E/bib/p422-bloom.pdf):
  membership rejection with a space/error trade-off.
- [Broder et al., “Min-Wise Independent Permutations”](https://www.sciencedirect.com/science/article/pii/S0022000099916902):
  foundations for min-wise sampling. Relevant if this is the intended family.
- [Cormode and Muthukrishnan, Count-Min sketch](https://dimacs.rutgers.edu/~graham/pubs/html/CormodeMuthukrishnan04CMLatin.html):
  frequency estimation; inspect the precise update model before choosing
  subtraction or merging rules.
- [PostgreSQL planner statistics](https://www.postgresql.org/docs/current/planner-stats.html):
  a concrete example of histograms, common values, sampling, and multivariate
  statistics serving estimates rather than exact query answers.

These sources remain a starting list. The row-signature study supplies the
filter evidence linked above; a broader comparative review and experiments
for the sketch and histogram questions remain open.
