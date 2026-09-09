# Ikea bitset primitives

Headless plain bitsets and Bec256 (Bisection–Enumerative Code) cover the same
256 positions. This spike implements the codec afresh and compares trusted
native endpoints against Calico's fast arms. External cardinality is part of
interpreting the bytes; validation is a separate cold operation. Compressed
set algebra is outside this experiment.

## BEC256 byte contract

This format is compatible with Calico's partial-cell enum body, implemented
afresh. We additionally define zero-byte empty/full bodies, where Calico's
outer kind normally handles those cases. BEC is expanded here as
**Bisection–Enumerative Code**; the acronym also has unrelated uses, including
binary erasure channel. The accepted name makes no originality claim.

- Position `i` is bit `i % 8` in byte `i / 8`. All fields are emitted least
  significant bit first, breadth-first through five equal bisections.
- A parent population `P` covering `2H` positions encodes its left population
  `L` as `L - max(0, P-H)`, in `bit_width(min(P, 2H-P))` bits. The right
  population follows by subtraction. The root population is external.
- The resulting 32 byte populations select enumerative ranks of byte values
  in ascending numeric order. Population 0..8 has rank width
  `[0,3,5,6,7,6,5,3,0]`. The final partial byte has zero padding bits.
- The maximum is 150 tree bits plus 224 rank bits: **374 bits / 47 bytes**.
  There is no embedded population, byte length, or raw 32-byte escape.

Trusted encode requires 32 readable input bytes, the correct population and
64 writable output bytes. Its scalar return is the logical byte count; stores
may extend beyond it. Trusted decode requires **64 readable bytes** even for
a short or empty body, a valid body/population association, and 32 writable
output bytes. The two-stream body takes two independent such inputs. Input
and output ranges must be disjoint. Allocation slack is a caller/admission
contract, not a header in the format.

The cold validator accepts an exact-length span without slack, validates
split/rank bounds, canonical padding and exact framing, and reports a scalar
error plus bit count. It cannot authenticate a root population against the
intended original data. A containing interface must retain the admitted bytes,
root association and readable extent for the entire trusted use; this spike
does not supply a production storage owner for compressed bodies.

## Physical definition, native width and repetition

[blocks.h](blocks.h) contains data definitions only: `PlainBits<Positions>`,
`Tiles<Child, Count>` and a `Bec256` format identity. A 512-position native AND
or OR accepts two native carriers regardless of the physical type from which
they were loaded. No Pair TU or codec per storage size is required.

[tiling.h](tiling.h) demonstrates a contiguous placement adapter over the same
ordered positions, using native grains and a short word remainder. Repetitions
up to four expand statically; larger extents use one loop. Checks cover 64,
256, 512, 4096 and 65536 positions, flat and tiled storage, union/intersection
and exact in-place output. Arbitrary partial overlap is unsupported. This is
an execution/traversal example, not a decision that every physical composition
must be contiguous or word-aligned.

The AVX-512 decoder also operates on two **independent** compressed streams
and produces one native 512-position carrier. It does not require adjacency,
invent a Pair storage type, or combine their independent root populations.
Compressed decode–operate–encode union/intersection has been removed from
scope; the implemented set algebra is plain bitwise algebra only.

## Validation and reproduction

The native correctness runner checks canonical bytes, round trips, framing,
malformed inputs and guard-page readable bounds. Use the pinned Linux toolchain:

```sh
orb -m ubuntu python3 workbench/spikes/ikea-blocks/run.py --sanitize --check-only
```

Hardware runs use one pinned CPU and five sequential repetitions. These are
resident microbenchmarks in nanoseconds per cell, not concurrent or cache-cold
workloads. Natural inputs sample up to 64 windows per named corpus and 16 tiles
per window. Partial-only views exclude empty/full cells; synthetic cases are
separate. The comparison uses matched padded input/output contracts.

## Hardware findings, 2026-09-09

The selected resident matrix **meets or beats the matched Calico baseline in
every tested partial/synthetic case on all three targets**. This statement is
about these calls and inputs, not all workloads. Each ratio below is the
median of the 30 per-case ratios (28 named partial-corpus views plus two
synthetic shapes); the maximum exposes the worst case rather than hiding it
inside a combined throughput score. Each individual time first takes the
median of five sequential repetitions. Lower is better.

| Operation / matched prior | Zen 5 median / max | Granite Rapids median / max | Neoverse V2 median / max |
| --- | --- | --- | --- |
| Encode / Calico P2 AVX-512 on x86, NEON on ARM | 0.596 / 0.790 | 0.686 / 0.711 | 0.769 / 0.795 |
| Single decode / Calico AVX2 on x86, NEON on ARM | 0.548 / 0.551 | 0.616 / 0.620 | 0.984 / 0.986 |
| Two-stream decode / Calico AVX-512, per cell | 0.929 / 0.935 | 0.693 / 0.695 | — |

The uniform-population synthetic case gives the following absolute costs.
These include opaque call and materializing endpoint costs, with identical
loop/checksum machinery for each matched pair. Our single decoder also returns
the consumed bit extent; Calico's comparison endpoint returns only the payload
through its destination. The x86 P2 encoder computes its own root population;
our headless encoder receives the externally associated population.

| Nanoseconds per cell, Bec / Calico | Zen 5 | Granite Rapids | Neoverse V2 |
| --- | --- | --- | --- |
| Encode | 13.49 / 23.39 | 24.48 / 35.57 | 25.11 / 32.65 |
| Single decode | 43.70 / 79.97 | 48.91 / 79.47 | 83.60 / 84.92 |
| Two-stream decode | 34.16 / 36.78 | 29.80 / 43.00 | — |

The initial restarted two-stream decoder lost on Zen 5. Restricting tree reads
to their proven 150-bit extent allows a single 64-byte permutation domain for
the two trees; prefix computation now visits only 2/4/8/16 active nodes per
domain at successive levels. NEON tree gathers similarly need only the first
32 input bytes, while enumerative decoding retains the full readable extent.
The x86 encoder also directly emits singleton/single-hole codes. These are
algorithmic specializations under the trusted contract, not validation branches.
The final kernel was measured against the fastest pinned prior arms after
these changes; passing round trips alone was not used to establish speed.

## Retained evidence

The [Zen5](evidence/20260909-zen5/), [Granite Rapids](evidence/20260909-granite-rapids/)
and [Neoverse V2](evidence/20260909-neoverse-v2/) exports retain each timing
repetition. [Native ASan/UBSan](evidence/20260909-native-sanitize/) is correctness
evidence only. The original bundles also contain the related composition
measurements. Sources, binaries, disassembly and commands are recoverable through
each export's artifact.json. Calico input identity is pinned in
[prior-input.json](prior-input.json).

```sh
orb -m ubuntu python3 workbench/spikes/ikea-blocks/report.py \
  workbench/spikes/ikea-blocks/evidence/20260909-zen5 \
  workbench/spikes/ikea-blocks/evidence/20260909-granite-rapids \
  workbench/spikes/ikea-blocks/evidence/20260909-neoverse-v2
```
