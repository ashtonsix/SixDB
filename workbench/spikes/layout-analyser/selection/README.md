# Small shortlists and learned palettes

This offline experiment consumes the retained `final-v2` and `final-zen5`
captures: three ISA/machine contexts, each with 5%, 50% and 90% write mixtures.
Each context has 28 layouts × two uniform recipe families. The source has three
timing repetitions summarized by medians. No new kernel timings are generated.

```sh
python3 workbench/spikes/layout-analyser/selection/check.py
python3 workbench/spikes/layout-analyser/selection/study.py \
  --output workbench/spikes/layout-analyser/selection/evidence/retained-final
```

Six heuristics rank the **28 already measured layouts**: four inherited
structural keys, farthest-first feature diversity, and isolated timing. For a
budget B, the experiment considers both uniform recipes for those B layouts
and selects the observed best complete mixed plan. It charges `2*B` complete-plan
probes. It is not the original isolated recipe chooser, nor an exhaustive search
over 2,816 layouts. Lexicographic candidate identity resolves structural ties;
those ties do not establish equal cost.

A probe here replays a retained complete-plan observation and charges a
hypothetical probe count; there is no fresh online fitting run. Observed regret
is `100 × (chosen time / best time among the 56 measured plans − 1)` for the
same context.

The palette experiment holds out one mixture class, using the other two in the
same machine/ISA capture for training. A greedy finite-menu palette minimizes
mean normalized training cost with the best palette plan per training class.
Sizes 1/2/4/8 count **plans**, unlike shortlist layout budgets. Fast selection
uses the nearest training mixture's measured ordering; equal distance chooses
the lower write fraction. Local empirical fitting probes every palette member
on the target mixture. Acquisition uses 112 complete-plan observations; the
report exposes probe counts, without estimating measurement time from them.

These holdouts are workload classes on the same rows and captured binary, not
independent containers, key regions, future telemetry or new physical hosts.
This comparison can expose a selector choosing poorly from a useful palette;
it cannot validate a collection training strategy. The check deliberately changes
all held-out costs and verifies that the trained palette and fast selection do
not change. No confidence interval or near-tie winner is inferred.

[Results](evidence/retained-final/summary.md) retain every shortlisted candidate,
palette and observation identity, including rejected alternatives. Source data
remain with TuplePack. Imported reference/report code and input hashes are
recorded; Python analysis wall time is diagnostic, not production selector cost.
