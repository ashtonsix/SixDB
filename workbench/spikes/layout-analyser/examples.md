# Worked candidate comparisons

These are concrete questions and exact geometry calculations, not measured
performance winners. All examples assume 64B cache lines unless stated otherwise.
For the connected read/update/adaptation argument, start with the
[worked investigation](case-study.md); this page retains the original geometry
and standalone fixtures.

## Geometry baseline

For dense, non-overlapping `w`-byte slots beginning at a line-aligned base, slot
`i` starts at phase `(i*w) mod 64`. Let `d = gcd(w,64)`. Uniform selection over
one complete phase cycle visits `0,d,...,64-d`, giving

```text
lines(i) = floor(((i*w) mod 64 + w - 1) / 64) + 1
E[lines] = 1 + (w - d) / 64
score = useful_slot_bytes / E[lines]
```

This assumes every byte of the slot is requested. For an arbitrary finite array,
average its actual slot phases; uniform random choice from an infinite array is
not a distribution. The steady-state cycle has `64/d` slots. Within widths
1..128, full-payload widths 64 and 128 tie at the maximum score of 64B/line.
Selected boundary cases are:

| Dense width / stride | Distribution of full-slot line demand | Mean lines | Width / mean |
| --- | --- | ---: | ---: |
| 32B | always 1 | 1 | 32 |
| 48B | 1 or 2, equally | 1.5 | 32 |
| 63B | 1 with probability 1/32; otherwise 2 | 1.96875 | 32 |
| 64B | always 1 | 1 | 64 |
| 65B | always 2 | 2 | 32.5 |
| 96B | always 2 | 2 | 48 |
| 112B | 2 or 3, equally | 2.5 | 44.8 |
| 127B | 2 with probability 1/32; otherwise 3 | 2.96875 | 42.7789 |
| 128B | always 2 | 2 | 64 |

The full original 1..128 ranking is reproducible with standard Python:

```python
from fractions import Fraction
from math import gcd

ranked = []
for width in range(1, 129):
    period = 64 // gcd(width, 64)
    touches = Fraction(sum(((i * width) % 64 + width - 1) // 64 + 1
                           for i in range(period)), period)
    ranked.append((Fraction(width) / touches, width, touches))
for score, width, touches in sorted(ranked, key=lambda r: (-r[0], r[1])):
    print(width, float(touches), float(score))
```

Padding changes stride independently of requested extent. A 63B extent at stride
64 touches one line; 96B at stride 128 still touches two. The latter increases
array footprint by one third while leaving full-record line demand unchanged.
Use useful bytes in the numerator, never padding. Alignment is an actual base
constraint: a 64B stride alone does not ensure one-line 64B accesses.
For a projection or multiple planes, enumerate the **union** of touched line
addresses for the operation, including shared lines; do not sum full-slot means.

## A string prefix near the 64B boundary

Consider an exact record with required scalar/control bytes, a variable string,
and an ideally 8B inline prefix. String length and the information needed to
find its remainder are included in the fixed control bytes below. A “prefix
comparison” uses those bytes as evidence and refines against the tail whenever
that evidence cannot decide; a full string read reconstructs exactly the same
value under every layout. Prefix length is a soft choice in this example.

| Candidate | Independently addressable contents | Full fixed-portion line demand at aligned dense placement | Change to the operation |
| --- | --- | ---: | --- |
| A: 63B | 55B other fixed bytes + 8B prefix; separate remainder | 1.96875 | Initial baseline |
| B: useful 64B | 55B + 9B prefix; remainder begins one byte later | 1 | More evidence inline; full string still reconstructs identically |
| C: padded 64B | 55B + 8B prefix + 1B padding; same remainder as A | 1 for the 63B fixed extent | No extra inline evidence; footprint/stride changes |
| D: 65B | 57B other fixed bytes + 8B prefix; separate remainder | 2 | Second baseline, with two more required fixed bytes than A |
| E: useful 64B | 57B + 7B prefix; the displaced byte begins the remainder | 1 | May need more tail refinement than D |
| F: useful 64B + side plane | 56B + 8B prefix; one relocatable fixed byte in a side plane | 1 for the core alone | Access to that field needs its side-plane mapping and load |

A/B/C form one equivalent-record comparison; D/E/F form another. D is not the
same record as A. No byte is lost. For strings shorter than the inline capacity,
the length defines valid content; moving a boundary need not save allocated tail
bytes if the arena rounds allocations. For B, nine bytes may cost more to compare
than a native eight-byte prefix. E may lose precisely the evidence that avoided
a dependent tail access. F is legal only when that fixed byte may move. For this
fixture its dense side plane uses the same original row coordinate and a shared
base, so it adds no per-record locator to the 64B core. Other mappings must
account for their locator/presence/rank storage and work. If the supplied contract
requires at least 8B inline, E is outside the candidate set even though it
preserves the exact string.

For a first workload, use exact known-row projections of the scalar fields, exact
string equality with prefix rejection, full string reads and partial updates.
Supply the mix, length/prefix-collision distribution and row order. Under each
candidate record the conditional tail/side-plane request rate and nonempty
groups, not just the fixed width. A scan filtering only on the prefix should
also consider a dense prefix plane plus scalar plane, even if its full-record
point lookup needs more addresses. The table prices full fixed-portion **line
demand**; it does not price the narrower projection used for prefix rejection.

The trade-off in the 65B case is visible without asserting a winner:

```text
Same logical record and requested exact result

D  65B [ fixed 56 | byte f | prefix 7 | byte p ] --> [ remaining string ]
E  64B [ fixed 56 | byte f | prefix 7          ] --> [ byte p | remainder ]
F  64B [ fixed 56 |          prefix 7 | byte p ] --> [ remaining string ]
        |
        +--when needed, side_base[row]--> [ byte f ]

Rightward arrows are taken when string evidence is unresolved (or the remaining
string is requested). Boxes name information at addresses, not actual issued
load/store spans. Unit composition and placement remain candidate choices.
```

One TuplePack unit can contain at most 64B. D therefore requires multiple units
or a different composition; E/F may fit the core in one unit if their code maps
are admitted. This is a **primitive capability boundary**, distinct from the
64B cache-line geometry. A whole record, an operation and a plane may each span
multiple units. Crossing the primitive boundary establishes no timing cliff.

A bounded micro fit could compare D/E/F for the same record and objective while
reusing applicable cost observations collected earlier. If the supplied choices
forbid another plane, that excludes F; calling the fit “micro” does not. This is
a possible use of the proposed analyser, not an implemented fitter.

Rare fields, patches and optionals offer more boundary-moving choices: inline
presence bits plus an out-of-line sparse value stream; an exception byte plane;
or a shared cold record reached through one locator. Charge presence lookup,
rank/offset decoding and updates. Eight independently patched fields with per-field
probability `p` need some patch with probability `1-(1-p)^8`: about 7.7% at 1%,
56.95% at 10%. Real clustering/correlation can differ. “Usually absent per field”
does not imply “the cold record is almost never needed.”

## A hash bucket: capacity, fingerprints and slack

The user's proposed composition has `N` SeriesPack fingerprints of width `b`, a
TuplePack key/value array and a metadata tuple including the first overflow-rope
block locator. Treat that composition as a worked consumer, not a permanent
Engine hash-table format. Lookup follows conditional dependencies:

```text
bucket/control + fingerprint candidates
    -> exact candidate keys -> requested value on a match
    -> next overflow block when the table's lookup rules require it
```

Insertion, erase and update also change occupancy, fingerprints, entries and
possibly overflow metadata. A narrow key projection must be allowed to avoid
loading values when the recipe/placement supports it. Separate key/value planes
compete with the proposed tuple array for precisely this reason.

Here is a small byte-accounted fixture, with deliberately authored assumptions:
20-bit exact keys, 24-bit values, a dense 6B TuplePack entry (44 useful bits and
4 spare bits), and an 8B metadata tuple. The metadata budget admits a 32-bit
overflow locator, up to 16 occupancy bits and 16 other control bits; no per-entry
tombstone is assumed. This is an illustrative locator limit, not an Engine
address choice. Fingerprints use compact Local SeriesPack with no separated
heads. Each eight-row tile occupies `b` bytes, including its final partial tile.
Use one gap-free bucket containing these three regions, then choose array stride.
The schema/representation description is shared by a bucket class in this
fixture; the table excludes that once-per-class overhead and allocator metadata,
which must be added to collection and peak-memory costs. A format storing a full
descriptor per bucket would need to add it to each row of the table.

```text
fingerprint_bytes(N,b) = ceil(N/8) * b
occupied_bucket_bytes = 8 + 6*N + fingerprint_bytes(N,b)
```

| N | Fingerprint bits | Fingerprint bytes | Occupied bytes | Candidate stride | Deliberate stride padding |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 7 | 7 | 63 | 63 or 64 | 0 or 1 |
| 8 | 8 | 8 | 64 | 64 | 0 |
| 9 | 7 | 14 | 76 | 76 or 96 | 0 or 20 |
| 9 | 8 | 16 | 78 | 78 or 96 | 0 or 18 |
| 16 | 7 | 14 | 118 | 118 or 128 | 0 or 10 |
| 16 | 8 | 16 | 120 | 120 or 128 | 0 or 8 |
| 16 | 12 | 24 | 128 | 128 | 0 |

An eight-entry bucket can turn one byte of padding into an extra fingerprint
bit per entry. A sixteen-entry bucket at stride 128 can turn eight bytes of
padding into four extra bits per entry. Neither is automatically free execution:
SeriesPack's recipe changes with width, and wider fingerprints may require more
loads/instructions. N=9 exposes the cost of a second physical tile; the ideal
`ceil(N*b/8)` is not its occupied storage. Separated heads or striped residuals
need their own exact geometry. Entry spare bits and stride padding are distinct
resources; consuming the former requires another admitted layout/composition.

With `m` occupied nonmatching entries and independent uniform `b`-bit fingerprints
conditional on bucket assignment, an unsuccessful search expects `m/2^b` false
candidate keys, and the probability of any is `1-(1-2^-b)^m`. A successful lookup
also has a true candidate; termination order matters. Fingerprint entropy must
remain after the bucket-index bits are conditioned on. Neither independence nor
uniformity is guaranteed by naming a hash, and fingerprint collisions are not
the table's load/overflow distribution.

Choosing N also changes buckets per collection, metadata per entry, occupancy
variance, overflow depth, scan work, SIMD utilization and mutation work. Compare
at fixed logical key count, specified hash distribution and load-factor policy;
report the actual total allocation and overflow population. Hold requested
operations fixed, not all these consequences. Include missing-key probes,
successful lookups, skew, expensive key checks and mixed updates. The optimum
cannot be inferred from bucket bytes divided by line touches.

## Same 96B record: separate, dense or padded

Use an identical 64B core plus 32B extension under three organizations:

| Candidate | Organization | Primary trade-off |
| --- | --- | --- |
| Split | Dense 64B cores and separately located dense 32B extensions | Core-only scans omit the extension stream; dependent address/load when needed |
| Dense | 96B contiguous records | Smaller full array; alternating start phase 0/32 and shared neighboring lines |
| Padded | 96B payload in 128B-aligned, 128B-stride records | Predictable placement with 32B padding per record |

Even the dense candidate's **core-only** 64B extent touches 1.5 lines on average,
versus one for the aligned split/padded core. Full dense and padded records both
touch two lines; the split case depends on actual plane bases and the union of
requested addresses. These demand counts cannot establish which is fastest.

Retained Zen adjacency observations make full/conditional extension consumption
interesting: one load can sometimes make the following line cheaper by the
time it is needed. They do not imply a 128B coherence line, free bandwidth, or
that 96/128B objects win. Their fenced target-load diagnostic includes specific
PC history and uncalibrated source-loop slack. Measure the ordinary consumer
with controlled useful work and lookahead; keep the latency diagnostic separate.
The [experiment design](experiments.md#add-mechanisms-only-to-discriminate-a-live-explanation)
preserves these distinctions.
