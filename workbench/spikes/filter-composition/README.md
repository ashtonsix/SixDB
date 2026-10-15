# Where to share filter composition

Ashton's October 15 steer distinguishes outlined stages (`A+B+C+D`), useful
fused islands joined at CPS seams (`A*B+C*D`), and the fully fused product
(`A*B*C*D`). Keep selected critical combinations fused without multiplying
every surrounding stage. This investigation tests that distinction with actual
SeriesPack decoding, unsigned comparisons and Ikea's existing native chain.

[Native runtime results and the adoption decision](RUNTIME.md) now cover AVX2,
AVX-512 and NEON: retain ordinary fused filters, share canonical consumers within
appropriate existing CPS operations, and keep cheap neighboring predicates fused.
The selected pattern is maintained in [Ikea's filter guide](../../../ikea/docs/filters.md)
and its [composition example](../../../ikea/examples/filters/composition.cpp).
The experimental catalogue policies below remain controls, not library dispatch.

## What failed

The first NEON filter job, `20261015T114535Z-cfbe9bb1`, was killed while compiling
the original single `ikea/test/filters/check.cpp` on a `c8g.medium`. Its script
receipt records 211.86 seconds and 1,509,668 KiB maximum process RSS. That is a
job-level resource receipt, not a kernel record naming the killed compiler PID.
There is no completed test executable from that failed build to size.

The test source expands 182 width/geometry/head combinations across five
predicates: 910 complete `series_case<Format,Op>` bodies. Each includes fixture
construction, writes, random generation, oracle traversal and ordinary/prepared/
wide filter checks. Later width and benchmark-operation shards bound individual
TUs; they do not remove the total expansion or establish that production kernels
should be outlined.

A first test-only control shared the cold fixture/oracle driver and outlined
typed verification endpoints, leaving production headers unchanged. All six
original-oracle executables passed. Compile wall time fell, but linked code and
read-only sections grew: 45,086→45,622 bytes at width 3, 70,070→88,470 at width 17,
and 55,326→73,118 at width 64. Peak compiler memory barely changed. This control
is rejected; sharing a driver can lose useful caller relationships and add more
endpoint code than it removes.

## The native composition control

[probe.cpp](probe.cpp) covers all 64 Local widths, five unsigned predicates and
16-row packets. A separate scalar oracle and Local wire encoder check boundary
queries, empty/partial activity and all returned positions: **30,720 windows per
variant**, all passing with the same checksum.

The shared seam normalizes a decoded packet by its **1/2/4/8-byte lane width**,
rather than retaining each source bit width as a different comparator type.
Source-width-specific decode stages then share twenty byte-width/predicate
stages. Each stage retains its useful native work inline. The existing
`native_chain` flattens vector arguments at the `preserve_none`/`musttail` seam;
this prototype adds no new carrier ABI or stored format.

The middle control keeps 47 concrete fused combinations: narrow packed paths
(equality through seven bits, ordered predicates through five) and standard
8/16/32/64-bit lanes. Remaining combinations use shared stages. These are
experimental choices following the current filter candidates, not a public
specialization registry or a new selected production dispatch.

| Control | Emitted kernels/stages | Executable `.text` | Peak compiler RSS | Compile wall |
| --- | --- | ---: | ---: | ---: |
| All fused | 320 width/predicate bodies | 179,604 B | 533.8 MiB | 11.65 s |
| Shared CPS | 64 decoders + 20 comparators | 58,468 B | 288.2 MiB | 5.64 s |
| Selective fusion | 55 decoders + 20 comparators + 47 fused bodies | 65,284 B | 316.7 MiB | 4.93 s |

All 47 retained fused object-text sections are byte-identical to their all-fused
counterparts. Decoding/comparison stages contain no ordinary calls or stack
accesses in the inspected NEON executable. The AAPCS entry shim still has its
required frame. Query/context/result traffic and tail transfers remain real
costs; stack-free stages do not establish free boundaries.

These are single serial local OrbStack compilations with pinned Clang 21.1.8,
`-O3 -g`, `armv8-a+simd`, Neoverse-V2 tuning, disabled automatic vectorizers and
time tracing. Compiler RSS and final section sizes support the build-cost
comparison. Compile timings are observations, not a native-host ranking; the
middle/shared timing difference does not select one over the other. The
prototype does not cover every physical layout, head arrangement or consumer.

## Consequence for the design

The useful seam removes a specialization axis from a substantial downstream
body. Merely moving the same Cartesian product into smaller TUs, wrapping each
pair in `noinline`, or passing a width-specific carrier to a templated consumer
does not achieve that sharing. Canonical carriers need compatible value meaning
as well as matching register shapes.

For filters, keep physical decoding and selected packed predicates close to
their layouts. Share ordinary traversal, preparation and suitably normalized
consumers where they are expensive to replicate. Preserve direct fused author
paths for measured critical combinations. A seam belongs around a useful packet
or operation, with parameters and borrowed lifetimes explicit; it need not be a
scheduler yield or a memory-materialized decoded array.

The selective control cuts `.text` **63.7%** and compiler peak RSS **40.7%** in
this matrix while preserving those fused bodies. The [runtime study](RUNTIME.md)
qualifies complete consumers separately: ordinary hot calls favor fusion; shared
stages can be useful after CPS entry, and a large mixed catalogue favors selective
sharing for Zen 5 throughput while losing dependent latency. The NEON catalogue
does not show that throughput win. These observations select an authoring pattern,
not a mandatory library dispatch, compiler change or global inlining policy.

## Evidence and reproduction

[The compact comparison](evidence/neon-20261015/summary.json) and its
[verified recovery reference](evidence/neon-20261015/artifact.json) retain the
complete comparison and negative harness control. The archive includes frozen
headers from Ikea's selected-source cut `ae6c9a99…a1a62`, source manifests,
commands, compiler resource receipts, traces, objects, executables and assembly.
Recover it with the ordinary artifact helper:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/filter-composition/evidence/neon-20261015 \
  build/recovered/filter-composition-20261015
```

`qualify.py` in the recovered study records the link/oracle/section audit. Its
compile databases preserve the original absolute paths; relocate source/include
and output paths before recompiling. To explore current headers, compile this
checkout's `probe.cpp` separately with `-DMODE=0`, `1`, or `2`, using the same
flags above and `-Iikea/include`. That creates a new observation rather than
reproducing the frozen source identity. [Compilation tooling](../../tools/compilation.md)
keeps outputs and resource measurements isolated from incremental builds.
