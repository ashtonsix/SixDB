# Group projected slots across rows

TuplePack codes are pieces of application values. A blanket byte transpose can
separate pieces that a consumer needs adjacent. This probe lets the caller group
**ordered decoded slots**. With map `{A,B,C}` and groups `{2,1}`, four rows begin:

```text
A0 B0 A1 B1 A2 B2 A3 B3 | C0 C1 C2 C3
```

The caller may interpret A/B as a 12-bit value and C as a 4-bit value. Groups
partition consecutive map slots, independently of physical ranks and application
types. This directory retains the experiments behind Ikea's
[reader/writer interface](../../../../../ikea/docs/tuplepack/usage.md).

## Packet rule

For a group of length L starting at map slot b, `(row, b+s)` occupies packet
byte `Rows*b + row*L + s`. The default is one group `{map.size()}`; unused capacity
trails all mapped bytes. Explicit holes reproduce fixed row slices. The
[packet contract](../../../../../ikea/docs/tuplepack/reference.md#packet-shape-and-coordinates)
defines this for ordinary, native and CPS reads and writes.

## Original experiment

The physical unit has two bytes: A occupies byte 0; B/C share the low/high nibble
of byte 1. Sixteen rows project to a 64-byte packet. The grouped consumer uses
A/B in u16 lanes and C in byte lanes, sums AB where `AB<2048 && C<8`, and optionally
increments AB modulo 4096 while preserving C and stride gaps. The row-major
control performs the same operation directly in u32 lanes. Both are explicitly
authored native consumers, not scalar or materializing straw controls.

Three implementations share physical transfer helpers:

- **Row-major:** existing TuplePack read/write routes and a direct consumer.
- **Explicit transpose:** existing read, a register permutation into groups,
  the grouped consumer, and the inverse permutation before existing writes.
- **Route-folded:** grouping is composed into decode/encode shuffle controls;
  no separate transpose is issued. The required C preservation still happens.

Each case traverses 8,192 rows at strides 2, 3, 16 and 64 with all or alternating
rows active. Timing includes native reading, consumption and preserving writes
for updates. It excludes preparation, checked admission, journals, owner hooks
and CPS boundaries. The production comparison below adds admission and journals.

Checks compare exact packet bytes and final storage to scalar expectations,
including empty/sparse masks, a partial final packet, padding and neighboring C
bits. Address callbacks assert that inactive rows are never requested. Separate
coordinate checks reproduce the four-row example above and an 8-byte/two-row
packet. The measured kernels remain specific to the 64-byte/sixteen-row case.

## Reproduction

```sh
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/tuple-layout/batching/groups/run.sh --machine zen5
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/tuple-layout/batching/groups/run.sh --machine zen5 --env GROUP_PROFILE=avx2
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/tuple-layout/batching/groups/run.sh --machine neoverse-v2 --env GROUP_PROFILE=neon
```

The original runs used five sequential repetitions and captured source
`765da760c15ce1ef4c134ecd3b98b3c8c88773fad4d9bf0b77a017f79d7a82a6`.
Measurements and limits are summarized below; each archive includes source,
binary, toolchain/build receipts and hardware context.

## Original results and decision

**Grouped packets merit an Ikea interface extension.** They preserve adjacency
within a caller-defined value while offering a useful choice of native lane
width to its consumer. Simply transposing every code byte is too restrictive.
Folding groups into prepared routes usually pays; it is not a universal fastest
lowering, and the caller should not need to select individual routing algorithms.

These are median route-folded/direct-row-major runtime ratios over the eight
stride/activity cases per operation; lower is better. Each case has the same
logical result, stored layout and access permissions across its three controls.

| Target | Read/consume range | Winning read cases | Read/compute/preserving-write range | Winning update cases |
| --- | ---: | ---: | ---: | ---: |
| Zen5 AVX-512 | 0.698–1.096 | 5/8 | 0.572–0.929 | 8/8 |
| Zen5 AVX2 | 0.848–0.950 | 8/8 | 0.623–0.910 | 8/8 |
| Neoverse V2 NEON | 0.661–0.896 | 8/8 | 0.787–0.946 | 8/8 |

For example, with a 16-byte stride and every row active, Zen AVX-512's preserving
update takes 10.46 ns per 16-row packet versus 18.29 ns for the direct row-major
control; AVX2 takes 11.98 versus 19.23 ns. V2's grouped read at tight two-byte stride
takes 4.14 ns versus 6.26 ns for row-major and 9.33 ns with an explicit transpose.

Tight Zen AVX-512 reads favor row-major by about 4–8%,
and the all-active 64-byte-stride case favors the explicit transpose (4.81 ns)
over direct row-major (5.66 ns) and route-folded grouping (6.20 ns). The cause of
that last lowering reversal remains unisolated.

[AVX-512](evidence/zen5-avx512/artifact.json),
[AVX2](evidence/zen5-avx2/artifact.json) and
[NEON](evidence/neoverse-v2/artifact.json) retain every one of the 48 cases and
all repetitions for each profile. These native loop timings establish neither a
cache level nor database throughput.

## Production comparison

`probe.cpp` retains the three original controls, using an explicit fourth hole
for their four-byte row slices. Additional cases exercise prepared Ikea plans:

- `read/public-native` and `read/public-buffered` include the real bound reader.
- `update/public-native-body` measures the grouped native read/compute/write body.
- `update/public-{native,buffered}-checked` include range, width and capacity
  admission and qualified byte journals. Matching `row-major-…-checked` controls
  include the same obligations. Journals reset per packet; publication and
  persistent summary maintenance are outside these timings.
- `word/…` compares default `{3}` and grouped `{2,1}` projections in an 8B packet
  of two rows, with direct GPR consumers and ordinary checked updates. The active
  masks are both rows and only row zero; strides are 2/3/16/64 bytes. Each timed
  operation is checked against independent physical-bit expectations first.

The final measured source is
`83b16b076f7f91846e71bb1200835cb4ee037c68389db8f2abeb66bd216502aa`.
All 176 cases completed with five repetitions on each profile, after the
TuplePack wire, operation, execution, packet, GPR and ownership suites and both
grouped examples passed. Each ratio divides a case's median CPU time by its
control's median CPU time. Tables give the median and range of those ratios
over eight stride/activity cases; lower is better.

| Target | 64B native read/consume | 64B checked native update | 64B checked buffered update |
| --- | ---: | ---: | ---: |
| Zen5 AVX-512 | 1.032 (0.800–1.099) | 1.006 (0.997–1.061) | 0.870 (0.785–0.891) |
| Zen5 AVX2 | 0.895 (0.873–0.991) | 1.030 (0.982–1.069) | 1.020 (0.954–1.096) |
| Neoverse V2 | 0.862 (0.735–0.958) | 0.961 (0.869–0.984) | 0.903 (0.836–0.993) |

Buffered reads also favor grouping in 23/24 cases; median ratios are 0.962,
0.975 and 0.943 respectively. The retained four-byte row control deliberately
pads its map so its consumer can use u32 lanes; the grouped consumer uses u16
AB lanes and separate C bytes. Both produce the same logical result and preserve
the same physical data. These results compare complete concrete consumers, not
layout-independent codec speed or database throughput.

For GPR packets, both readers project the same three-code map over two rows.
The ratios compare groups `{2,1}` against default `{3}`. Each read timing
includes its consumer (sum AB where `AB<2048 && C<8`); ordinary reads use the
bound `get_unchecked` endpoint. Native update bodies exclude admission/effects;
ordinary updates include checked writes and journals.

| Target | GPR native read + consume | GPR ordinary read + consume | GPR native update body | GPR checked ordinary update |
| --- | ---: | ---: | ---: | ---: |
| Zen5 AVX-512 | 1.092 (1.069–1.108) | 1.168 (1.111–1.227) | 1.264 (1.211–1.324) | 1.069 (1.050–1.127) |
| Zen5 AVX2 | 0.974 (0.962–1.004) | 1.183 (1.152–1.213) | 1.218 (1.176–1.288) | 1.054 (1.048–1.087) |
| Neoverse V2 | 1.103 (1.096–1.115) | 1.122 (1.111–1.131) | 1.026 (0.966–1.039) | 1.023 (0.995–1.076) |

Across the 24 ordinary GPR read-and-consume cases, grouping takes 11.1–22.7%
longer than the default: median increases of 16.8%, 18.3% and 12.2% by profile.
This measures the complete operation, not isolated call overhead. For this
consumer, grouping does not pay for itself through the ordinary interface.
Prefer the default when it suits the computation; choose groups when
adjacency or a packet-width bridge benefits the complete consumer.

An earlier general GPR implementation first assembled rows and then rearranged
them; native reads took 1.53–1.83× as long as the default. Direct output placement and a
shared write body parameterized by byte extraction removed that intermediate
work. The final native-read range is 0.962–1.115×. Bound native operations also
compute their base address and stride once before entering the packet body.

[AVX-512](evidence/public-avx512/artifact.json),
[AVX2](evidence/public-avx2/artifact.json) and
[NEON](evidence/public-neon/artifact.json) retain samples, checks and build
resources. Their selected-target builds took 77/87/109 seconds with peak compiler
RSS 1.09/1.15/1.41 GiB, with toolchain and repository already present. Full compiler
output, source and executables remain in the archives.
[Baseline validation](evidence/public-baseline/artifact.json) uses
`check-baseline.sh` to check the x86 buffered fallback and standalone public
headers without AVX2. Local checks also passed the retained GPR/crossover controls
and 174,960 Bec256 heterogeneous-composition cases after their fixed row slices
were expressed with explicit map holes.
