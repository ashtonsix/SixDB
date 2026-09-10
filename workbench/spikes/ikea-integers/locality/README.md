# Packed integer locality: exact byte footprints

2026-09-09. The audit covers widths `k=1..64`, with a deliberately separate
filter head `h ∈ {0,8,16}`, `h<=k`. Write `w=k-h=8q+r` for the remaining body.
Heads are excluded from the locality bound, not from storage accounting. No
kernel, code-generation or performance claim follows from this audit.

**A local eight-value body gives the required point locality for every width.
Dense sixteen-value locality has a real packing limit above 32 remaining bits.
The prior's 5- and 7-bit interleave violates even point locality; repairing the
tail does not by itself keep a separate whole-byte body nearby.**

[Back to the spike](../README.md). Start with the [contract](#what-is-being-proved),
[dense-packing limit](#why-some-wider-dense-groups-cannot-be-repaired-by-permutation)
or [interleave repairs](#repairs-for-intact-32-byte-interleaving). The
[reproduction section](#reproduce-and-use) links the complete current audit.
Names such as `local8` are audit-script labels; the accepted narrow block name
is LocalPack. Wider `local8` body/tail layouts remain proposals except for the
implemented 12-bit composition and width-56 body experiment.

## What is being proved

A point's footprint is every stored byte containing one of its data bits. A
group's footprint is the union for sixteen logical indices starting at a
multiple of sixteen. The requirement is
`max(byte/64)-min(byte/64)<=1`: two touched lines must also be adjacent. Counting
only the number of touched lines would incorrectly admit the prior's 5-bit code.

The array starts at a 64-byte boundary. For a uniform packed primitive with
stride `S`, all reachable primitive residues are multiples of `gcd(S,64)`.
The script exhausts the entire residue period, every point, and every aligned
group. Eight-value primitives are paired when a group crosses their boundary.
Each format's bit map is checked to store every input bit exactly once and fill
its exact advertised extent. This checks physical bit dependencies, not an
assumed scalar/SIMD load width. A kernel that overreads, widens a load, or reads
unneeded chunks must separately satisfy the physical-access bound.

**Uniform-run phase is part of the contract.** Arbitrarily concatenating
different widths can enlarge the residue set. A 128-byte group of 64-bit values
after a two-byte group of 1-bit values starts at residue 2 and spans three lines.
`local8` points remain valid at every arbitrary byte residue, but its special wider group
guarantees need the admitted run phase or a new aligned run. Arrays whose
primitive strides are all multiples of 32 retain the `{0,32}` residue lattice
even when those primitives vary. A composition must carry this placement fact.

## The prior and its exact violations

The prior [full-width planes](../../../../../calico/workbench/prototypes/bytepack/planes.h)
have 256 values: up to two leading byte planes, then ascending 1/2/4-byte
groups, then a `32r`-byte residual. The
[bitplane tail](../../../../../calico/workbench/prototypes/bytepack/kernels/bitplanes.h)
stores `r` adjacent bytes for each eight-value transpose. The
[interleaved tail](../../../../../calico/workbench/prototypes/bytepack/kernels/interleaved.h)
uses 32-byte lanes and explicit per-width fragment assignments. The audit
transcribes those assignments from the
[frozen format oracle](../../../../../calico/frame/test/ref/bytepack_ref.h);
it does not import prior kernel implementations.

| Prior residual | Exact counterexample, relative to tail base | Consequence |
| --- | --- | --- |
| 5 bits, base residue 0, value 32 | bytes 0 and 128 | lines 0 and 2: only two lines, but not adjacent |
| 5 bits, same phase, aligned rows 32–47 | bytes 0–15 and 128–143 | the same forbidden gap |
| 7 bits, base residue 0, value 192 | bytes 96, 160, 192 | lines 1, 2, 3 |
| 7 bits, same phase, aligned rows 192–207 | bytes 96–111, 160–175, 192–207 | three lines |

In the 7-bit code, input group `x2` uses chunks 1, 2 and 3 and **passes at both
residues**. The failing input group is `x6`, whose low nibble is in chunk 3 and
whose upper fragments are in chunks 5 and 6. At residue 32, that same `x6` footprint
uses only two adjacent lines. Padding each 224-byte primitive to 256 bytes would
hold it at the *failing* phase; alignment alone is not this repair.

Across both reachable phases, the prior 5-bit residual fails 96/512 points and
6/32 groups; the prior 7-bit residual fails 32/512 points and 2/32 groups. The
prior residuals 1/2/3/4/6 and every `bitslice8` residual pass both bounds. These
are exact case counts, not sampling or timing results.

The full-width global plane arrangement is a separate problem. Removing
deliberate heads does not remove gaps between the remaining planes and tail.
The exhaustive passing remaining widths are:

| Excluded head | Prior full planes + `bitslice8` tail | Prior full planes + original interleaved tail |
| --- | --- | --- |
| 0 bits | `w=1..8` | `w=1/2/3/4/6/8` |
| 8 bits | `w=0..8` | `w=0/1/2/3/4/6/8` |
| 16 bits | `w=0..8,16,32` | `w=0/1/2/3/4/6/8,16,32` |

For example, after removing two heads, width 40 leaves independent 8- and
16-bit planes over 256 values. Value 0 needs body bytes 0 and 256–257: this is
already invalid before considering a tail. The wider `w16` and `w32` successes
in the last row each have a single contiguous native-width plane.

## A complete narrow baseline: `local8`

For eight values, place their `8q` whole-byte body bytes in per-value AoS order,
followed by the `r` bytes of an eight-value transpose. The exact primitive size
is `8q+r=w` bytes. No full block size is fixed: repetitions compose it.

For one value, the entire dependency is inside this primitive of at most 64 bytes.
That proves the point bound at **every** byte residue, including arbitrary
mixtures of widths. An aligned group of sixteen consumes exactly two adjacent
primitives, hence a contiguous `L=2w` bytes. Its uniform-run start residues are
multiples of `gcd(L,64)`. Therefore its worst line span is at most two exactly
when

```
L + 64 - gcd(L,64) <= 128
w <= 32 + gcd(w,32)
```

Thus dense aligned groups of 16 pass for `w=0..34,36,40,48,64`. Map back to the requested
integer width with `k=w+h`; the audit enumerates all 170 legal `(k,h)` cases.
A failed width remains point-local. Width 35 illustrates why
checking only the first primitive is insufficient: a 70-byte group reaches
start residue 60, so it ends at relative byte 129 and needs three lines.

The result allows a 16-value physical primitive too: `16q` body bytes followed
by `2r` transpose bytes has the same exact `2w`-byte group footprint. That is a
placement choice with different point footprints and loop structure. The audit
also enumerates body-first primitive counts 16/32/64/128/256; larger counts
need checking, not inheritance of the eight-value guarantee.

## Why some wider dense groups cannot be repaired by permutation

This bound assumes independent fixed-width group data stored once in an exactly
dense body array, with no extra excluded stream, padding, redundant data or
cross-group coding. Let one group own `W=2w>64` bytes' worth of bits.

Each group must occupy an adjacent pair of lines. Two groups cannot occupy the
same pair: together they own more than 128 bytes. Regard a group as an edge
between its two lines. A connected component containing `m` groups uses `m+1`
lines and therefore requires

```
m*W <= 64*(m+1)
m*(W-64) <= 64.
```

Exact density requires equality for each completed component. Consequently
`W-64` must divide 64. Above 32 bits, the only exactly dense successes are
`w=33,34,36,40,48,64`, agreeing with `local8`'s constructive layout. Arbitrary
bit permutations within or between those lines do not change this capacity
argument. It is a bound on this storage contract, not a claim about arbitrary
compression schemes. Finite array ends can leave partial lines; they cannot
make an arbitrarily long dense run satisfy a missing component capacity.

If padding is allowed, the optimal density under the same two-line group
contract is constructive. Set

```
delta = 2*w - 64
m = floor(64/delta)
P = 64 - m*delta
```

Store `m` contiguous sixteen-value groups in a 64-byte-aligned supertile, then
append `P` padding bytes. Its stride is `64*(m+1)`. Group `i` starts at residue
`i*delta`; `(i+1)*delta<=64` proves every group touches at most two lines. No
component can contain more than `m` groups, so this achieves the maximum
payload per line under the stated contract. This is a proposal, not a selected
format or authorization to add padding.

| w | Groups / values per supertile | Payload + padding | Padding per 16 values |
| ---: | ---: | ---: | ---: |
| 35 | 10 /160 | 700 +4 bytes | 0.4 bytes |
| 37 | 6 /96 | 444 +4 bytes | 2/3 bytes |
| 43 | 2 /32 | 172 +20 bytes | 10 bytes |
| 49 | 1 /16 | 98 +30 bytes | 30 bytes |
| 63 | 1 /16 | 126 +2 bytes | 2 bytes |

The audit emits `padding.csv` with the complete `w=33..64` calculation.
Rounding every group up to a 64-byte boundary wastes much more at widths such
as 35; grouping a variable number of primitives matters here.

## The 12-bit example: a 64-byte body and 32-byte tail

With `q=1,r=4,N=64`, store body bytes 0–63, then tail bytes 64–95. The packed
stride is 96, so starts alternate between residues 0 and 32. Both tail twins pass
every point and group at both phases.

For `bitslice8`, aligned group `g=0..3` requires body bytes `16g..16g+15` and tail
bytes `64+8g..71+8g`. For `interleaved4`, its tail bytes are
`64+(16g mod32)..79+(16g mod32)`. The worst interleaved envelope has 80 bytes;
its allowed starts never push its last byte beyond the second line. The
16-value group envelopes are 80/80/48/48 bytes for the four groups, respectively.

Keeping both children in the same enclosing 96-byte primitive is the useful
placement guarantee. Merely giving a parent arbitrary body and tail pointers
would not establish it. A separate high 8-bit filter head makes a 20-bit logical
value with this same 12-bit body; the head's placement is deliberately outside
the bound.

## Repairs for intact 32-byte interleaving

A sequential lane bitstream for 5- and 7-bit residuals keeps every value in one or two
adjacent residual chunks. Finishing a high fragment in the old byte and
starting the low fragment in the next has the same chunk dependencies as the
audit's LSB-first stream. The audit now also checks the exact high-fragment-first
wire as `continuous32`, including its bit bijection and dependency equivalence.
Keep
the already legal 1/2/3/4/6 patterns as independent controls.

There is also a smaller repair to the prior wire: permute only its intact
32-byte chunks. For the 5-bit residual, the physical order of **old** chunk numbers
is `[0,1,4,2,3]`; for the 7-bit residual it is `[0,1,2,3,6,5,4]`. These are the
`permuted_prior32` candidates in the audit. Lane order and every intra-byte
field stay exactly as in the prior. They are different representations from
the first continuous, high-fragment-first repair, even though both satisfy
the residual-only locality bound.

The permutation check tests the bit map against the old map under only the
declared chunk permutation, verifies exact storage bijection, and enumerates
all 256 points and 16 aligned groups at **each** of residues 0 and 32. Neither
candidate has a failure. Their tail chunk dependencies, with new physical
chunk numbers, are:

| Input group of 32 | Permuted 5-bit | Permuted 7-bit |
| ---: | --- | --- |
| 0 | 0 | 0 |
| 1 | 0,2 | 0,1 |
| 2 | 1 | 1,2,3 |
| 3 | 1,2 | 2 |
| 4 | 3 | 6 |
| 5 | 2,3 | 5,6 |
| 6 | 4 | 3,4,5 |
| 7 | 2,4 | 4 |

Every set has chunk-index span at most 2. Within a group, all required bytes
have the same lane, and an aligned 16-value read stays within one half of each
32-byte chunk. At both allowed base phases this proves at most two adjacent
lines for both access grains. Three residual chunks can therefore satisfy
the bound; the bound is about lines, not a limit of two chunks.

The incidence still matters even when the worst case passes. Counts below
hold separately at **each** phase, with uniform logical indices; they count
required bytes, not measured cache misses.

| Residual wire | One-line / two-line points | One-line / two-line aligned groups of 16 | Maximum required tail chunks |
| --- | ---: | ---: | ---: |
| Continuous 5-bit | 192 / 64 | 12 / 4 | 2 |
| Permuted prior 5-bit | 160 / 96 | 10 / 6 | 2 |
| Continuous 7-bit | 160 / 96 | 10 / 6 | 2 |
| Permuted prior 7-bit | 160 / 96 | 10 / 6 | 3 |

The permuted 5-bit wire increases two-line incidence from 25% to 37.5%. The two
7-bit repairs have equal line incidence and the same mean of 1.75 required bytes
per point, but the permuted 7-bit wire needs three bytes for input groups 2 and 6;
the continuous 7-bit wire never does.
This difference becomes decisive when adding an intact whole-byte body.

Combining tails with body chunks is stricter. Let `Xi` be 32 consecutive
whole-byte body values and `Yj` be one intact 32-byte residual chunk. With
`q=1`, each value and aligned group of 16 requires `Xi` plus its tail chunks.
Across both reachable base phases, the bound is equivalent to the maximum
required chunk index minus the minimum being at most 2.

The small [CSP search](audit.py) turns each required chunk
set into a clique. A legal ordering has graph bandwidth at most 2, so a vertex
can have at most four neighbours. This gives short impossibility certificates:

| Residual r | Result for q=1 | Certificate or example ordering |
| ---: | --- | --- |
| 1 | impossible | `Y0` needs `X0..X7`: degree 8 |
| 2 | possible | `X0 X1 Y0 X2 X3` |
| 3, retained prior | impossible | `Y1` needs `X2 X3 X4 X5 Y0 Y2`: degree 6 |
| 4 | possible | `X0 Y0 X1`; body-first `X0 X1 Y0` also works |
| 5, sequential repair | impossible | `Y1` needs `X1 X2 X3 Y0 Y2`: degree 5 |
| 5, permuted prior | impossible | `Y2` needs `X1 X3 X5 X7 Y0 Y1 Y3 Y4`: degree 8 |
| 6, retained prior | possible | `X0 Y0 X1 Y1 X2 Y2 X3` |
| 7, sequential repair | possible | `X0 Y0 X1 Y1 … X6 Y6 X7` |
| 7, permuted prior | impossible | `X2 Y1 Y2 Y3` are jointly required: four distinct chunks cannot have span ≤ 2 |

These are exhaustive existence answers for `q=1`, intact 32-byte chunks and the
specified tail dependencies, not for all possible lane counts, smaller chunks
or different residual encodings. The degree failures require no factorial
search; the generic remaining search finds and verifies witness orders.

For the permuted 7-bit wire, the second four-chunk witness is `X6 Y3 Y4 Y5`.
Reordering all body and tail chunks cannot remove either obstruction: four distinct slots
span at least 3, which fails at one of the two phases even when the slots are
consecutive. Thus this tail cannot inherit the continuous 7-bit wire's `w15`
construction under the same dense, intact 32-byte-chunk contract. Changing phase
constraints, splitting chunks, adding padding or changing the fields would be a different
storage contract. The negative result is stronger than failure of one proposed
body-first ordering.

The prior's bulk merge expressions can be retained for these permutations by
changing chunk load/store addresses; that algebraic reuse is not a timing
result. Keep wire identity separate from implementation identity when measuring
this tradeoff. Compare kernels on the same wire where possible, and label
cross-wire comparisons explicitly, with identical logical values, operation
grain and placement phases. A fast kernel for the permuted 7-bit wire would not
establish the continuous 7-bit wire's composition eligibility, and the continuous
wire's stronger eligibility would not establish competitive kernel speed. Both are explicit
candidate data definitions; ISA selection must not silently change their wire.

A higher-byte positive example is `q=2,r=4,N=64` (`w20`): put the AoS body for
the first 32 values in bytes 0–63, the residual chunk in bytes 64–95, and the
AoS body for the last 32 values in bytes 96–159. Each aligned 16-value body is 32 bytes;
its body/tail chunk indices span at most 2. This 160-byte primitive passes both
phases and both access grains. Body-first `body128 + tail32` fails point
locality; moving the shared tail between the two bodies is substantive.

## Reproduce and use

```sh
orb -m ubuntu python3 workbench/spikes/ikea-integers/locality/audit.py \
  --output build/experiments/ikea-integers-locality/recheck
```

The self-contained Python script requires no compiler or external data. The
current run checks 862 layout/width cases, 170 head choices, 311,888 points and
19,493 aligned groups. Its analytic assertions, bit bijections, CSP orders and
positive repairs all pass. The retained [cases.csv](continuous-evidence/cases.csv) selects
39 decisive cases; [audit-checks.json](continuous-evidence/audit-checks.json) and
[provenance.json](continuous-evidence/provenance.json) identify the complete current audit.
Full tables, per-layout witnesses, head/padding calculations and the captured
script are in the [recoverable bundle](continuous-evidence/artifact.json). They also
regenerate under the ignored output directory. This audit measures no hardware
traffic.

For a composition exercise, `local8` and `body64+tail32` are sufficient to test
actual tail-child substitution, shared native compute and a checked enclosing
placement guarantee. Head selection, larger-body chunk orders and optional
padding are separate choices. They must not be silently inferred from ISA or
from a successful residual-only test.
