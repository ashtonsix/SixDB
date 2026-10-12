# Design review

Ashton's brief remains the objective: packed composition with broad competitive
performance and memory, with correctness and cost considered together. The
additional AI critique is advice, not new requirements or a different
project. The useful questions overlap with the three independent reviews already
commissioned before implementation.

## Accepted and integrated

- Define the small set of operation semantics needed to compare useful work:
  duplicate batch lanes, multimap multiplicity, copied/visited results, merge
  conflicts and physical-location invalidation.
- Treat value width and chain-reference tags as geometry inputs. Count all
  metadata, retained storage and rebuild peaks on both sides of comparisons.
- Audit exact downstream counts and key reconstruction through deletion,
  secondary placement, chains, growth and merge using an independent reference.
- Challenge simple hash families with structured keys, not only uniform random
  inputs. Test conditional secondary distribution and account for inverse cost.
- Use strong native-policy and well-matched control baselines, including
  partitioned Abseil, concurrent bulk visitation and flat-map/small-vector bags.
- Keep heat prediction falsifiable and outside the mandatory hot lookup path;
  explicitly compare forced direct/Ring operation first.

## Corrections and limits

- The spatial probe is not absent: its source and fleet observations are in
  memory-characterisation. Subsequent inspection found a speculative-stimulus
  flaw in the old helper and runtime calibrator; Loom corrected both and delivered
  controlled native captures and runtime exposure within the requested hour.
  Old observations are historical, not qualified tuning facts. ARM qualification
  remains open. Hidden-cache attribution can remain unknown; it is not a pass/fail
  gate preventing bucket experiments.
- Leaving displaced entries in place does not mean an overflow check forever:
  exact descendant counts return to zero when those entries disappear. Measure
  backfill against that actual policy, not a permanently sticky overflow flag.
- Low-bit odd multiplication distributes ordinary sequential keys regularly;
  power-of-two strides supply the stronger failure example. Fixed multipliers
  can also correlate high bucket bits despite secret XOR seeding. Random odd
  multiplication has different guarantees from a cryptographic permutation.
- Shared caller-owned entropy can produce reproducible private seeds across
  replicas. Independent random draws are not required by the brief. Hashtable
  takes seed material and does not invent the owning replication protocol.
- Require bucket bits `a <= w`. At equality the bucket address identifies the
  whole key; a zero occupancy is an exact negative even without fingerprints.
  A missing fingerprint does not universally mean no early negative is possible.
- A 64-byte-aligned dense 96-byte array touches at most two 64-byte lines per bucket;
  arbitrary shifted bases can touch three. Spatial-fetch observations do not
  change this byte-address accounting or justify out-of-window SIMD loads.
- “Multimap not comparable to Abseil” does not remove its memory measurement.
  Compare meaningful compositions and report their memory as well as throughput.
- A 16-bit heat indicator's shared non-atomic races are not benign in C++.
  Prefer owner-local updates or explicitly synchronized schemes with costs shown.
- Do not preselect SVE2 as merely wider NEON, or fix bucket/batch choices from
  an interface aesthetic. Select a small useful family from measured evidence.

The review should sharpen the experiment, not add STL coverage, formal process
gates, a distributed-seed subsystem or an automatic cache oracle to the critical
path before they earn their cost.
Operation semantics are provisional enough to keep comparisons honest; layout,
kernel cost and ownership evidence can still change the design. They are not a
separate specification phase to finish before considering performance.
