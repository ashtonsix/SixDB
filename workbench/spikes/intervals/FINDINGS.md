# Intervals: shared geometry without owner policy

The useful seam is a borrowed ordered source and typed ready geometry, with the
consumer's complete work kept fused. StreamPack, Directory and Frame share those
capabilities; they do not share a payload, a liveness interpretation or one layout.
The candidate namespace/target is `ikea::intervals`. Tombstones are external
operation filters. No owning layout/retirement API is exported by this candidate.

## Keep these capabilities

- Ordered endpoint search remains independent of interval arithmetic. Directory
  needs u32 physical prefixes and arbitrary K-byte lexical domains, including
  duplicate fences, partial 128-row leaves and 512-row interiors. It needs no
  compact geometry header or mandatory bitmap.
- Bind representation once around useful ready work. Preserve StreamPack's fused
  typed value path and start+U addressing; avoid a mandatory flattened descriptor
  or per-rank generic reconstruction. `with_geometry` permits owner specializations.
- Keep storage/checkpoint spacing separate from native G16 work. G16 and sparse
  G64 are useful options to qualify by consumer; G32/128/256 remain measured
  controls. Nothing here changes the default or requires sparse checkpoints on
  already direct/fixed sources.
- A readable-checkpoint-capacity grant enables the same native directory masking
  as the strong maintained compact path. It does not turn padded lanes into ranks.
  Keep exact-readable fallback sources too. Sparse phases expose all possible
  local native windows, including the spans between strided geometry groups.
- Stored checkpoint width is not the working coordinate type. At least u32
  arithmetic avoids repeated u16 carry narrowing; u64 origins stay u64. This
  distinction preserves bytes while avoiding a representation-driven arithmetic tax.
- Rebuild, allocation, publication and value movement stay with owners. The new
  checked Sizes editor reports original-source byte effects for append/resize;
  moving one boundary remains different work from translating the suffix.

## Rejected replacements and counterexamples

A generic StreamPack start/length reconstruction was slower than its existing
fused get in several cells. The proposed seam retains the fused path. The diagnostic
arm remains in the evidence; namespace cleanup is not a reason to accept that cost.

Translating every tiny Frame run query to a native-byte search was slower than the
existing comparison projection. A typed source can bind the small projection
before its whole query batch, while larger lists use native-byte search. Per-call
selection remains a diagnostic control. Frame's immutable u8 end-minus-one plane
needs no checkpoints/control/tombstones and still represents endpoint 256 exactly.

Sparse Relative Ends can be much larger: M15, N256, G16 fits u8 local fields;
G64 requires u16. Fields plus checkpoints rise from 288 to 520 bytes. Sizes instead
falls from 288 to 264 bytes. These are initialized geometry/checkpoint bytes, not
an Orbital footprint or the native source object's size.

Sparse Sizes saves checkpoint writes and directory bytes while point/short-range
reads can lose. Carried full ranges must not recompute each prefix; preserving a
wide running coordinate matters too. A universal sparse default is not supported.

## Qualification and limits

Independent scalar endpoints/intersections check 1,200 fixture cases for Sizes and
Relative Ends, both exact and full-capacity checkpoint grants, G16/32/64/128/256,
u8/u16 fields, u16/u32/u64 checkpoint carriers, near-limit origins, duplicate/zero
groups, partial tails, signed/wider queries, selected ranges, prepared completion
and ordered replacements. The seam check also covers Directory lexical keys up to
1,024 rows, every Frame run count 0…256, strided geometry with poisoned companions,
exact 9-byte inverted filters, no geometry access for an empty filter, protected
native tails, 4,536 checked append/resize operations, refusal without byte/effect
changes and a retained independent old image.

The actual Directory implementation is compiled twice: the candidate transform
changes only the include and namespace binding. Both the existing and candidate
40-shape tests pass through actual Objects, old Scopes, edits and no-op publication.
Their complete implementation object disassemblies match. Namespace isolation has
not changed Directory's executed algorithms; noisy timing differences are not wins.
StreamPack's maintained history/reverse/transfer/effects and exact-boundary checks
pass. All candidate headers compile independently. ASan/UBSan checks are separate
from timing.

The runner retains complete-operation raw samples, source/binary hashes, every arm
and losing cell. Local measurements use Clang 21.1.8, release O3, ARM NEON in the
Apple/OrbStack Linux VM, vCPU8, warm fixtures. Primitive consumer files disable
loop/SLP auto-vectorization; explicit native kernels remain. Two processes reverse
geometry case order and Directory before/after order; primitive arms rotate over
11 trials. Directory's reused driver retains medians/minima of seven inner
repetitions with fixed metric order, not raw inner samples. No result establishes
DRAM residence, fleet rankings or a Zen 5 recommendation. Concurrent activity on
other vCPUs and VM scheduling can affect results. Small differences do not prove
an improvement or regression.

Production rename/ownership migration, the full portable descriptor/byte law,
cold dependency service, complete owner edit throughput and x86/native fleet
qualification remain unpromoted. This spike implements and tests the candidate
seam and exposes choices; it does not claim those remaining production changes
or Frame's separate full-width prediction/exception algebra are complete.

## Selected local results — 22 samples per cell

The [retained summary](evidence/local-arm-20261022/summary.csv),
[byte accounting](evidence/local-arm-20261022/bytes.json) and
[identity](evidence/local-arm-20261022/identity.json) cover all 327 cells and
7,194 raw complete-operation samples. The archive referenced by
[artifact.json](evidence/local-arm-20261022/artifact.json) retains raw samples and
commands. The Directory [process summaries](evidence/local-arm-20261022/directory-summary.csv)
remain separate from those raw primitive samples.

M255/N256 hot Sizes consumers, ns per complete operation:

| Operation | Maintained typed G16 | Candidate G16 | Candidate G64 |
| --- | ---: | ---: | ---: |
| Rank → payload byte/length | 1.61 | 1.33 | 2.00 |
| Coordinate → child value | 3.60 | 3.53 | 4.83 |
| Eight intervals → payload reduction | 6.58 | 5.95 | 9.66 |
| Full 256-interval payload reduction | 194.38 | 195.11 | 192.30 |

These cells support keeping a G16 point path and treating sparse storage as a
consumer choice. They do not support promoting G64 for point-heavy workloads.
G256 inverse is 14.27 ns and rank access 7.42 ns here: the smallest directory
is a clear losing point-read choice. Full-range units are whole traversals.

The checked candidate Sizes resize pair costs 18.36 ns at G16, 14.25 ns at G64,
and 8.89 ns at G256. Issued effect bytes per pair fall 36 → 12 → 6. This isolates
spacing within one candidate editor. The maintained Sizes pair is 23.99 ns and
54 effect bytes; it has different control widths and preserved native stores,
so that difference is not attributable to sparse spacing alone. Allocation,
payload movement, publication and retained-reader copying are outside these
primitive timings. It is not complete StreamPack edit throughput.

For Frame run→value reads, maintained projection versus bound source is 3.80 →
1.07 ns at 16 runs, 5.25 → 3.54 at 64, and 6.97 → 5.36 at 256. At four runs,
the bound path **loses**, 2.54 → 3.26 ns. The per-call diagnostic has a different
result, and earlier process layouts also moved this tiny cell materially.
Retain the existing tiny projection rather than promoting the wrapper on these
measurements. The shared source seam admits that existing projection directly.

StreamPack's fused-owner candidate/maintained ratios span 0.87–1.08 across the
nine codec/full/placement cells. Most are within a few percent, but full Relative
Ends loses about 8% in this cohort. The generic companion reconstruction loses
more in multiple cells. This is evidence to preserve the established fused value
body and qualify actual migration/code placement, not a claim of uniform wrapper
parity. No production caller is switched to either losing path.

Directory's candidate has the same compiled implementation instructions and
passes its complete integration test. Ready-plane and real-Object timings are
retained as sanity checks; namespace changes cannot explain their fluctuations.
The incoming [Directory evidence](../segment-directory/README.md) remains the
owner's native complete-consumer account. No new tree policy or timing promise
is inferred here.

## Native performance follow-through

The [bar-raiser study](RAISE-BAR.md) adds frozen controls, true C1/C4/C8 useful
request chains, linked disassembly/MCA audits and three native qualification
cohorts. It retains the serial default and a measured G64 prefix throughput
option over the same bytes. Final M255/N256 random C8 rank→payload improves
6.112→1.970 ns on Zen5 AVX2 and 6.148→3.316 on Neoverse V2. C1, clustered and
several range cells lose; the inverse candidates were rejected. Read that study's
comparison limits and full losing cells before choosing the binding. This extends
the initial local evidence above and establishes no production migration.
