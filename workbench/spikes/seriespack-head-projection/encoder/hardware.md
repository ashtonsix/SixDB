# Head projection: shared input and compact stores

The H16/u64 focus passes normally optimized hardware guards on both Zen 5 and
Granite Rapids: 145,152 cases in AVX2-only and 290,304 in the full profile,
plus the independent masked-dword boundary check. The earlier QEMU masked-load
fault therefore does not reproduce in these native checks. [All 432 case records](hardware.csv)
retain sixteen balanced repetitions, buffer offsets and function placement;
[capture receipts](hardware.json) identify both recoverable worker artifacts.

The compact shared projection is the useful next public-operation candidate.
It shifts before narrowing, carries sixteen projected words in AVX2 or
thirty-two in AVX-512, then writes the two independent byte heads. It retains
their separate placements. Merely sharing the existing small register loads
does not explain the full benefit, and fully unrolling a tile is not a general
substitute for compact stores.

At 8,192 values, the following ratios compare payload-plus-head encoding with
the separate-head-pass control. These are shared-buffer raw operations; public
binding, validation and effect handling are not timed.

| Case | Zen AVX2 compact16 | GNR AVX2 compact16 | Zen AVX-512 compact32 | GNR AVX-512 compact32 |
| --- | ---: | ---: | ---: | ---: |
| Striped K17/H16 | 0.678× | 0.624× | 0.617× | 0.911× |
| Striped K23/H16 | 0.682× | 0.636× | 0.630× | 0.911× |
| Local K56/H16 | 0.817× | 0.771× | 0.755× | 0.820× |

The compact arm beats its separate-pass control at all three extents on both
hosts and profiles. The simpler shared-tile AVX-512 arm is sometimes faster
than compact32 on Zen, including the three 8,192-value cases, but does not
win consistently across extents and GNR. That is not sufficient evidence for
a width/count/host selection table. Compact16/32 is the bounded next candidate;
the simpler shared-tile arm remains an informative alternative.

The full-profile AVX2-labelled compact16 arm also improves its matched
AVX2-labelled control. Its compiler may use the additional permitted features;
it is not an AVX2-only result. No ratios here compare different target families.

These results cover three H16/u64 shapes. They do not establish a policy for
H8, narrower input carriers, every shift or the actual public endpoint. The
next implementation should separate the shared projection computation from
head-plane traversal, preserve partial extents and independent strides, and
compare the affected public H16/u64 surface across widths before selection.
Calico already stores two byte head planes in the relevant shapes; a different
number of planes is not the explanation for its whole-operation advantage.
