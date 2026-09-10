# LocalPack and ScanPack: format contract

[Back to the spike](README.md). **LocalPack / ScanPack** are the accepted names
for the two tail components and standalone 1–7-bit integer representations.
They express an access bias, not an ISA or an operation restriction.
This is the current wire contract; superseded Scan5/7 layouts remain in
[historical measurements](measurements.md#historical-experiments).

LocalPack stores an eight-value bit transpose in exactly `k` bytes: bit `b`
of value `i` is bit `i % 8` of byte `(i / 8) * k + b`.
ScanPack uses intact 32-byte stripes and the packing period described below.
[reference.cpp](reference.cpp) supplies an independent bit-at-a-time oracle;
[formats.h](formats.h) contains the data definitions and bit-address map.

## Data definitions and operation grains

[formats.h](formats.h) defines wire geometry without compute or storage ownership.
`LocalPack<K>` has eight values and `K` bytes. `ScanPack<K>` uses the smallest
whole-byte packing period across intact 32-byte stripes:

| k | ScanPack values | Packed bytes | Repetitions in the measured 256-value endpoint |
| ---: | ---: | ---: | ---: |
| 1 | 256 | 32 | 1 |
| 2 | 128 | 32 | 2 |
| 3 | 256 | 96 | 1 |
| 4 | 64 | 32 | 4 |
| 5 | 256 | 160 | 1 |
| 6 | 128 | 96 | 2 |
| 7 | 256 | 224 | 1 |

`Tiles<Child, N>` repeats a data definition. Native bulk kernels independently
take their operation extent as a template argument; they do not create a TU
for each repetition count. An eight-value data primitive does not imply an
eight-lane vector or a separate call boundary. The current materializing
comparison chooses 256 logical values so that every prior and selected endpoint
performs the same work. Immediate `local_pair`/`local_read16`/`scan_read16`
results can instead feed a native consumer directly.

Widths 5 and 7 currently use a continuous high-fragment-first wire: a crossing
value finishes its high bits in the current 32-byte stripe and begins its low
bits in the next stripe. Every value needs at most two adjacent stripes. For
width 7, each line below describes corresponding byte lanes of successive
stripes, with `Xg` a group of 32 input values:

```
X0[6:0] | X1[6:6]<<7
X1[5:0] | X2[6:5]<<6
X2[4:0] | X3[6:4]<<5
X3[3:0] | X4[6:3]<<4
X4[2:0] | X5[6:2]<<3
X5[1:0] | X6[6:1]<<2
X6[0:0] | X7[6:0]<<1
```

Widths 1/2/3/4/6 retain the prior field assignments. The independent
bit-at-a-time oracle specifies all wires without using the production map.
Earlier whole-stripe permutations are retained as measured alternatives in
the evidence, not silently treated as the same representation.

The continuous 7-bit wire admits a 15-bit body/tail placement in the surveyed
family of intact 32-byte chunks; the permuted 7-bit wire cannot. Replacing a
child can therefore change which enclosing placements are admissible, even
when width, density and mean required bytes agree. That feasibility result is
separate from which implementation wins a particular access regime.

`ScanReader::fragment_classes` and `ScanReader::constant_offsets` are two native
reader lowerings over identical bytes. The first is much faster in the small
resident cases; the second recovers large-extent independent-read performance.
The native template choice is explicit, and the benchmark's `--scan-reader`
option binds one choice before execution. There is no hot cache classifier or
automatic selection policy. [Measurements](measurements.md) record the tradeoff.

## Locality and admission

An integer has a head of 0/8/16 leading bits (no wider than the integer), a body
of whole bytes and a tail of `k % 8` least significant bits. The body and tail
stay near one another. A selected nonzero head is a separate child/resource.
The default locality comparison uses head zero. Body/tail placement belongs to
the enclosing block; individually local children do not prove a local parent.

The user requires a point read to touch at most two **adjacent** 64-byte payload
lines, and aligned sixteen-value reads to do likewise wherever physically
possible. The array allocation starts at a 64-byte boundary; every tile phase
reachable from its stride matters. Deliberately separated filter heads are the
stated exception, but still count toward full reconstruction work and storage.
Required payload bytes, actual kernel accesses and whole-lookup metadata traffic
are separate quantities. An address proof does not establish cache residence.

All hot kernels require valid indices, aligned logical groups, sufficient exact
extents, width-valid unsigned inputs and disjoint encode/decode buffers. They
carry no validation, error tag or size header. Immediate native reads can feed
a consumer directly; materialization is an explicit endpoint. Preparation checks
resource and placement contracts outside execution. The [12-bit exercise](composition/README.md)
shows this with actual body/tail children and three enclosing placements.

Exact packed size is the baseline. The [locality audit](locality/README.md)
proves the selected narrow bounds and examines wider bodies and optional
padding. The [instruction audit](access-audit.md) checks actual narrow accesses.
The [wider-body proposal](wider-bodies.md) and [width-56 experiment](wide56/README.md)
separate wider geometry from implemented native coverage. No PFoR, metadata
meaning, universal carrier or progressive-filter interface is implied here.
