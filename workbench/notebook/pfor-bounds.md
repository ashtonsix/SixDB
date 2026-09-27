# Choosing residual bounds

Notes from the discontinued bounds-estimation investigation, September 2026.
The spike and its artifacts were deleted at Ashton's request. These qualitative
observations identify directions to revisit; they are not retained benchmark
evidence or a recommendation. The fresh investigation did not establish a win
against Calico Keyset POST.

## What needs a clearer objective

Bounds estimation chooses an interval whose inliers can be represented as
residuals; an enclosing composition handles exceptions. Estimator time,
interval quality, physical byte cost and complete codec performance are
different measurements. Minimising width for a fixed exception budget is a
useful diagnostic, but does not by itself define a useful production objective.

On returning, the central question is what the caller needs: one interval
under a specified cost, several width/exception alternatives, or a cheap
initial choice that later analysis can improve. The parent composition and
workload should make that question concrete. No exception format, block size
or estimator interface was settled.

Calico remains reference material and a strong performance baseline. Its
encoding policy, patch limits, width rules, delta convention and layout do not
become SixDB constraints. Any adopted choice needs independent justification.

## Promising mechanisms and their failures

- **Chunk extrema are inexpensive proposals.** Masked first/second extrema
  avoid sorting each chunk. Distinguish second occurrences from second distinct
  values: repeated extrema and their multiplicities matter. Chunk grain is an
  execution choice, independent of the population being encoded.
- **Merging is harder than extracting features.** Removing one extreme per
  chunk leaves clusters of exceptional values behind. Median chunk envelopes
  recovered some such cases, but performed poorly on sorted inputs: the median
  chunk minimum can exclude roughly half the population. Equal tail masses
  arranged differently can produce very different proposals.
- **A valid guess can still be bad.** Counting exceptions proves an interval
  meets a budget; it does not reveal that the residuals waste many leading
  bits. Bad-guess recognition needs evidence about missed compression as well
  as excessive exceptions.
- **Refine width separately from the base.** For a proposed lower bound,
  comparisons count outliers and an OR of retained residuals exposes occupied
  bits. This can skip empty width bands without a full histogram. Membership
  is monotone as the interval narrows, allowing exact refinement for that base
  under a fixed exception budget. It does not establish that the base is good.
- **Cheap proofs can avoid more work.** If the minimum occurs more often than
  the entire exception budget, every feasible interval must retain it; exact
  refinement at that minimum then solves the fixed-budget problem. A feasible
  zero-width interval also settles that objective. Another possible proposal
  comes from sorting chunk minima: the zero-based q-th minimum has at most qK
  values below it, for chunks of at most K values. This bounds
  low exceptions, not the quality of the resulting interval. The latest
  shortcuts had correctness checks but incomplete performance investigation.

Previous-block priors remain unexplored in the fresh investigation. Base and
width priors may have different value; recognition and fallback costs belong
in their comparison, especially across distribution changes.

## What SixDB composition changes

[SeriesPack's physical geometry](../../ikea/docs/seriespack/representation.md)
means narrower is not always smaller. For 17 values, a headless `bulk_x86`
width-1 body occupies a 32-byte striped tile; width 8 occupies three 8-byte
Local tiles, or 24 bytes. This illustrates ragged slack, not a recommended
width or population. Physical cost need not be monotone in residual width.

The enclosing composition determines body, exception, metadata and reserved
capacity costs. SeriesPack supplies packed unsigned arrays;
[TuplePack](../../ikea/docs/tuplepack/reference.md) can supply manually described
metadata layouts. Neither prescribes a PFOR representation. Point access,
other transforms and ragged addressing also affect useful block sizes; the
record-segment bound is not a limit on every standalone array.

Low exceptions have a separate mutation benefit: a later value below the base
may consume exception capacity instead of forcing a rebase. This capability
does not require the initial estimator to raise the base, and depends on
available capacity. Posting-list edits may change neighbouring gaps, so they
need a different workload from independent array replacements. Mutation and
publication follow [Ikea's owner contracts](../../ikea/docs/integration.md).
Bounds over inliers must also remain distinct from summaries over all rows.

## Evidence worth seeking when the question becomes concrete

The exploratory runs covered native 8/16/32/64-bit domains, AVX-512 on Zen 5
and Granite Rapids, AVX2, and Neoverse V2. Benefits did not transfer uniformly
across domains or targets; large slow cases and poor proposals remained.
Real posting gaps and other integer columns exposed weaknesses that generated
cases alone did not settle. There is no supported speed/compression conclusion.

A sorted exact interval oracle is useful for diagnosing proposal quality.
Physical costs need a separate evaluator tied to the chosen SixDB composition.
Estimator timings should include feature extraction, validation, scratch work
and fallback, while keeping transformations, packing and decoding identifiable.
Calico comparisons must measure the same declared work. A substantial speed
gain for a small size penalty remains an interesting tradeoff, once those
quantities have a concrete meaning for the caller.
